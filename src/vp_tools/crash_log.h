// vp_tools/crash_log.h - name the guest code behind a guest memory fault
//
// The SDK reports a bad guest access only as
//   "Unhandled guest access violation: read of guest 0x00010000 ..."
// without saying which code made it. This vectored handler runs before the
// SDK's for access violations inside the guest window below 0x40000000 (the
// 4 KB-page virtual heap and the null area; GPU write-watch faults live in
// the physical ranges and are not touched), unwinds the host stack from the
// faulting context and writes the frames, named through DbgHelp and the PDB
// (__imp__sub_XXXXXXXX = guest function), to the log and to
// logs/vp_crash.txt. It then lets the SDK handle the fault as before.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <fmt/format.h>

#include <rex/logging.h>
#include <rex/runtime.h>

#include "vp_tools/write_watch.h"

namespace vp_tools
{
#if defined(_WIN32)
  class CrashLog
  {
  public:
    static void Install()
    {
      static bool installed = false;
      if (!installed)
      {
        installed = true;
        AddVectoredExceptionHandler(1, &CrashLog::Handler);
      }
    }

  private:
    static constexpr int kMaxReports = 3;
    static constexpr int kMaxFrames = 24;
    static constexpr uint64_t kGuestLowEnd = 0x40000000;

    static LONG CALLBACK Handler(EXCEPTION_POINTERS *ep)
    {
      if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
          ep->ExceptionRecord->NumberParameters < 2)
      {
        return EXCEPTION_CONTINUE_SEARCH;
      }
      auto *rt = rex::Runtime::instance();
      const uint64_t base = rt ? reinterpret_cast<uint64_t>(rt->virtual_membase()) : 0;
      const uint64_t host = ep->ExceptionRecord->ExceptionInformation[1];
      if (!base || host < base || host - base >= kGuestLowEnd)
      {
        return EXCEPTION_CONTINUE_SEARCH;
      }
      static std::atomic<int> reports{0};
      if (reports.fetch_add(1) >= kMaxReports)
      {
        return EXCEPTION_CONTINUE_SEARCH;
      }

      const bool write = ep->ExceptionRecord->ExceptionInformation[0] == 1;
      std::string out = fmt::format("[vp_tools] guest {} of 0x{:08X} on thread {}; host stack:\n",
                                    write ? "write" : "read", host - base, GetCurrentThreadId());
      // Unwind from the faulting context (x64 unwind data of the exe and DLLs).
      CONTEXT c = *ep->ContextRecord;
      for (int i = 0; i < kMaxFrames && c.Rip; ++i)
      {
        out += fmt::format("  #{:<2} {}\n", i, WriteWatch::Describe(c.Rip));
        DWORD64 image_base = 0;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &image_base, nullptr);
        if (!fn)
        {
          // Leaf function: the return address is on top of the stack.
          c.Rip = *reinterpret_cast<DWORD64 *>(c.Rsp);
          c.Rsp += 8;
          continue;
        }
        PVOID handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, c.Rip, fn, &c, &handler_data, &establisher, nullptr);
      }
      REXLOG_ERROR("{}", out);
      if (FILE *f = std::fopen("logs/vp_crash.txt", "a"))
      {
        std::fputs(out.c_str(), f);
        std::fclose(f);
      }
      return EXCEPTION_CONTINUE_SEARCH;
    }
  };
#else
  class CrashLog
  {
  public:
    static void Install() {}
  };
#endif
}  // namespace vp_tools
