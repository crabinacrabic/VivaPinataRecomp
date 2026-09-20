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
