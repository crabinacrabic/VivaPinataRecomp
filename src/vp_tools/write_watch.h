// vp_tools/write_watch.h - "what writes to this address" (hardware breakpoint)
//
// Arms debug register DR0 of every thread in the process (except the UI
// thread) as a 4-byte write breakpoint on one guest address. Each write raises
// a single-step exception after the store; a vectored handler counts the host
// instruction (RIP) and lets the thread go on. The menu turns RIPs into names
// with DbgHelp and the PDB next to the exe: a recompiled guest function shows
// up as __imp__sub_XXXXXXXX, i.e. its guest address.
//
// Limits: threads started after arming are not covered (arm again); writes
// through another alias of physical memory (0xA/0xC/0xE...) are not seen; under
// the Visual Studio debugger (F5) the debugger takes the exceptions first, so
// use it with Ctrl+F5 / run_game.bat.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#pragma comment(lib, "dbghelp.lib")
#endif

#include <fmt/format.h>

#include <rex/runtime.h>

namespace vp_tools
{
  class WriteWatch
  {
  public:
    struct Hit
    {
      uint64_t rip;
      uint64_t count;
    };

    static WriteWatch &Get()
    {
      static WriteWatch watch;
      return watch;
    }

    bool armed() const { return armed_; }
    uint32_t address() const { return address_; }
    int threads() const { return threads_; }

    // Returns the number of threads armed (0 = failed).
    int Arm(uint32_t guest_addr)
    {
#if defined(_WIN32)
      auto *rt = rex::Runtime::instance();
      uint8_t *base = rt ? rt->virtual_membase() : nullptr;
      if (!base || (guest_addr & 3))
      {
        return 0;
      }
      Disarm();
      if (!handler_)
      {
        handler_ = AddVectoredExceptionHandler(1, &WriteWatch::Handler);
      }
      for (auto &slot : hits_)
      {
        slot.count.store(0, std::memory_order_relaxed);
        slot.rip.store(0, std::memory_order_relaxed);
      }
      address_ = guest_addr;
      host_address_.store(reinterpret_cast<uint64_t>(base + guest_addr));
      armed_ = true;
      threads_ = SetAllThreads(host_address_.load(), true);
      return threads_;
#else
      (void)guest_addr;
      return 0;
#endif
    }

    void Disarm()
    {
#if defined(_WIN32)
      if (!armed_)
      {
        return;
      }
      SetAllThreads(0, false);
      armed_ = false;
      host_address_.store(0);
#endif
    }

    std::vector<Hit> Hits() const
    {
      std::vector<Hit> out;
      for (const auto &slot : hits_)
      {
        const uint64_t rip = slot.rip.load(std::memory_order_relaxed);
        const uint64_t count = slot.count.load(std::memory_order_relaxed);
        if (rip && count)
        {
          out.push_back({rip, count});
        }
      }
      return out;
    }

    // UI thread only (DbgHelp is single-threaded). The first call loads the
    // PDB and can take a few seconds.
    static std::string Describe(uint64_t rip)
    {
#if defined(_WIN32)
      static const bool sym_ready = [] {
        char exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string dir(exe);
        dir = dir.substr(0, dir.find_last_of("\\/"));
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        return SymInitialize(GetCurrentProcess(), dir.c_str(), TRUE) != FALSE;
      }();
      alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
      auto *sym = reinterpret_cast<SYMBOL_INFO *>(buffer);
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = 255;
      DWORD64 displacement = 0;
      if (sym_ready && SymFromAddr(GetCurrentProcess(), rip, &displacement, sym))
      {
        return fmt::format("{}+0x{:X}", sym->Name, displacement);
      }
      const uint64_t exe = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
      return fmt::format("vivapinata.exe+0x{:X}", rip - exe);
#else
      return fmt::format("0x{:X}", rip);
#endif
    }

  private:
    struct Slot
    {
      std::atomic<uint64_t> rip{0};
      std::atomic<uint64_t> count{0};
    };

#if defined(_WIN32)
    // Runs on the writing thread, inside the exception dispatch: lock-free.
    static LONG CALLBACK Handler(EXCEPTION_POINTERS *ep)
    {
      if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(ep->ContextRecord->Dr6 & 1))
      {
        return EXCEPTION_CONTINUE_SEARCH;
      }
      WriteWatch &w = Get();
      ep->ContextRecord->Dr6 = 0;
      if (!w.host_address_.load(std::memory_order_relaxed))
      {
        return EXCEPTION_CONTINUE_EXECUTION;
      }
      const uint64_t rip = ep->ContextRecord->Rip;
      for (auto &slot : w.hits_)
      {
        uint64_t current = slot.rip.load(std::memory_order_relaxed);
        if (current == 0)
        {
          uint64_t expected = 0;
          if (slot.rip.compare_exchange_strong(expected, rip))
          {
            current = rip;
          }
          else
          {
            current = expected;
          }
        }
        if (current == rip)
        {
          slot.count.fetch_add(1, std::memory_order_relaxed);
          break;
        }
      }
      return EXCEPTION_CONTINUE_EXECUTION;
    }

    // DR0 = address, DR7: L0 enable, RW0 = 01 (write), LEN0 = 11 (4 bytes).
    static int SetAllThreads(uint64_t host_addr, bool enable)
    {
      const DWORD pid = GetCurrentProcessId();
      const DWORD self = GetCurrentThreadId();
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      if (snap == INVALID_HANDLE_VALUE)
      {
        return 0;
      }
      int armed = 0;
      THREADENTRY32 te{};
      te.dwSize = sizeof(te);
      for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
      {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self)
        {
          continue;
        }
        HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE,
                                   te.th32ThreadID);
        if (!thread)
        {
          continue;
        }
        if (SuspendThread(thread) != static_cast<DWORD>(-1))
        {
          CONTEXT c{};
          c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
          if (GetThreadContext(thread, &c))
          {
            c.Dr7 &= ~static_cast<DWORD64>(0xF0003);
            if (enable)
            {
              c.Dr0 = host_addr;
              c.Dr7 |= 1 | (1ull << 16) | (3ull << 18);
            }
            else
            {
              c.Dr0 = 0;
            }
            c.Dr6 = 0;
            if (SetThreadContext(thread, &c))
            {
              ++armed;
            }
          }
          ResumeThread(thread);
        }
        CloseHandle(thread);
      }
      CloseHandle(snap);
      return armed;
    }

    PVOID handler_ = nullptr;
#endif

    bool armed_ = false;
    uint32_t address_ = 0;
    int threads_ = 0;
    std::atomic<uint64_t> host_address_{0};
    std::array<Slot, 64> hits_{};
  };
}  // namespace vp_tools
