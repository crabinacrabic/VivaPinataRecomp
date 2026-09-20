// debug_tools.h - runtime diagnostics (enabled by dev_debug_runtime = true)
//
// Both tools write next to the executable: <exe_dir>/logs/stub_sweep.txt and
// <exe_dir>/logs/missed_functions.txt (same folder the SDK uses for its own
// vivapinata_NNN.log). Feed stub_sweep.txt to tools/stub_sweep_to_toml.py to
// turn the hits into config/vivapinata_functions.toml entries.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

#include "game_constants.h"

namespace debug_tools
{
  inline std::filesystem::path LogsDir()
  {
    std::filesystem::path dir = rex::filesystem::GetExecutableFolder() / "logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
  }

  inline FILE *OpenLog(const char *name)
  {
    const std::string path = (LogsDir() / name).string();
    FILE *f = std::fopen(path.c_str(), "w");
    if (!f)
    {
      REXLOG_WARN("debug_tools: cannot open {}", path);
    }
    else
    {
      REXLOG_INFO("debug_tools: writing {}", path);
    }
    return f;
  }

  // Stub sweep: every 4-byte-aligned code address without a registered
  // function gets a logging stub. Instead of the SDK's fatal "Call to invalid
  // or unregistered function" the call is logged once per address (with LR
  // and r3..r6) and returns with registers untouched, so one run collects
  // many missing functions. Returned r3 is garbage, so behaviour after the
  // first hit is not trustworthy - this is a discovery mode, not a fix.
  static void PerformStubSweep()
  {
    auto *rt = rex::Runtime::instance();
    auto *fd = rt ? rt->function_dispatcher() : nullptr;
    uint8_t *base = rt ? rt->virtual_membase() : nullptr;
    if (!fd || !base)
    {
      return;
    }

    static FILE *stub_log = OpenLog("stub_sweep.txt");
    static std::mutex stub_mutex;
    static std::unordered_map<uint32_t, uint32_t> stub_hits;

    static PPCFunc *stub = [](PPCContext &ctx, uint8_t *) noexcept
    {
      const uint32_t addr = ctx.ctr.u32;
      const uint32_t lr = ctx.lr;
      std::lock_guard<std::mutex> lock(stub_mutex);
      uint32_t &count = stub_hits[addr];
      if (count == 0)
      {
        REXLOG_WARN("[stub] unregistered function 0x{:08X} called from LR=0x{:08X}", addr, lr);
        if (stub_log)
        {
          std::fprintf(stub_log,
                       "[stub] addr=0x%08X LR=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X\n",
                       addr, lr, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);
          std::fflush(stub_log);
        }
      }
      ++count;
    };

    uint32_t stubbed = 0;
    for (uint32_t addr = GameConstants::kCodeBase; addr < GameConstants::kCodeEnd; addr += 4)
    {
      if (!fd->GetFunction(addr))
      {
        fd->SetFunction(addr, stub);
        ++stubbed;
      }
    }
    REXLOG_INFO("debug_tools: stub sweep armed on {} addresses in 0x{:08X}..0x{:08X}",
                stubbed, GameConstants::kCodeBase, GameConstants::kCodeEnd);
    if (stub_log)
    {
      std::fprintf(stub_log, "=== stub sweep: scanned %u addresses, stubbed %u ===\n",
                   (GameConstants::kCodeEnd - GameConstants::kCodeBase) / 4, stubbed);
      std::fflush(stub_log);
    }
  }

  // Data-pointer scan (runtime equivalent of the community "scan_data_pointers"
  // codegen flag): walk the loaded, decrypted image outside the code range and
  // collect big-endian words that point into the code range. Vtables are runs
  // of such words, so every entry is reported with the length of the run it
  // belongs to and whether the dispatcher already knows the target. Targets
  // with no registered function are the methods of vtables the codegen
  // scanner never found (the source of every "unregistered function" so far).
  // Must run BEFORE PerformStubSweep, which registers a stub everywhere.
  // Post-process with tools/data_pointers_to_toml.py (it drops interiors of
  // known functions using the generated code, so this stays simple).
  static void PerformDataPointerScan()
  {
    auto *rt = rex::Runtime::instance();
    auto *fd = rt ? rt->function_dispatcher() : nullptr;
    uint8_t *base = rt ? rt->virtual_membase() : nullptr;
    if (!fd || !base)
    {
      return;
    }

    FILE *log = OpenLog("data_pointers.txt");
    if (!log)
    {
      return;
    }

    auto in_code = [](uint32_t v)
    {
      return v >= GameConstants::kCodeBase && v < GameConstants::kCodeEnd && (v & 3) == 0;
    };
    auto load_be32 = [base](uint32_t ea)
    {
      const uint8_t *p = base + ea;
      return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    };

    const uint32_t ranges[2][2] = {
        {GameConstants::kImageBase, GameConstants::kCodeBase},
        {GameConstants::kCodeEnd & ~3u, GameConstants::kImageEnd},
    };

    uint32_t words = 0, runs = 0, unknown = 0;
    for (const auto &r : ranges)
    {
      uint32_t ea = r[0];
      while (ea + 4 <= r[1])
      {
        if (!in_code(load_be32(ea)))
        {
          ea += 4;
          continue;
        }
        // Measure the run of consecutive code pointers starting here.
        uint32_t end = ea;
        while (end + 4 <= r[1] && in_code(load_be32(end)))
        {
          end += 4;
        }
        const uint32_t run = (end - ea) / 4;
        ++runs;
        for (uint32_t p = ea; p < end; p += 4)
        {
          const uint32_t v = load_be32(p);
          const bool known = fd->GetFunction(v) != nullptr;
          ++words;
          if (!known)
          {
            ++unknown;
          }
          std::fprintf(log, "[ptr] at=0x%08X value=0x%08X run=%u idx=%u %s\n", p, v, run,
                       (p - ea) / 4, known ? "known" : "UNKNOWN");
        }
        ea = end;
      }
    }
    std::fprintf(log, "=== data pointer scan: %u code pointers in %u runs, %u unknown targets ===\n",
                 words, runs, unknown);
    std::fclose(log);
    REXLOG_INFO("debug_tools: data pointer scan: {} code pointers in {} runs, {} unknown targets",
                words, runs, unknown);
  }

  // Static view: every code address without a registered function. Large
  // (most addresses are function interiors); mainly useful for diffing runs.
  static void PerformMissingFunctionScan()
  {
    auto *rt = rex::Runtime::instance();
    auto *fd = rt ? rt->function_dispatcher() : nullptr;
    if (!fd)
    {
      return;
    }

    FILE *log = OpenLog("missed_functions.txt");
    if (!log)
    {
      return;
    }

    uint32_t missing = 0;
    uint32_t registered = 0;
    for (uint32_t addr = GameConstants::kCodeBase; addr < GameConstants::kCodeEnd; addr += 4)
    {
      if (fd->GetFunction(addr))
      {
        ++registered;
      }
      else
      {
        ++missing;
        std::fprintf(log, "[missed] addr=0x%08X\n", addr);
      }
    }
    std::fprintf(log, "=== scan: scanned=%u registered=%u missed=%u ===\n",
                 (GameConstants::kCodeEnd - GameConstants::kCodeBase) / 4, registered, missing);
    std::fclose(log);
  }
} // namespace debug_tools
