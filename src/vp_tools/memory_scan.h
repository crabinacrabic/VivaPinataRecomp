// vp_tools/memory_scan.h - Cheat Engine-style value scanner over guest memory
//
// Finds a 32-bit big-endian integer (coins, experience...) in guest memory:
// first scan for the current value, change it in the game, next scan for the
// new value, repeat until a few addresses are left. Used from the menu's
// Memory tab on the UI thread while the game keeps running; reads race with
// the guest harmlessly, writes are single aligned 32-bit stores.
//
// Only committed, readable pages are touched (VirtualQuery). 0xC0000000 and
// 0xE0000000 are skipped: they alias the same physical memory as 0xA0000000.
#pragma once

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <rex/runtime.h>

namespace vp_tools
{
  class MemoryScanner
  {
  public:
    static constexpr size_t kMaxResults = 200000;

    // Guest ranges, end exclusive: virtual 4 KB pages, virtual 64 KB pages,
    // XEX image + 0x90000000 heap, physical memory (64 KB view).
    static constexpr std::pair<uint32_t, uint32_t> kRanges[] = {
        {0x00010000u, 0x40000000u},
        {0x40000000u, 0x7F000000u},
        {0x80000000u, 0xA0000000u},
        {0xA0000000u, 0xC0000000u},
    };

    static uint8_t *Base()
    {
      auto *rt = rex::Runtime::instance();
      return rt ? rt->virtual_membase() : nullptr;
    }

    // Number of addresses left; 0 before the first scan.
    size_t count() const { return results_.size(); }
    bool scanned() const { return scanned_; }
    bool truncated() const { return truncated_; }
    const std::vector<uint32_t> &results() const { return results_; }

    void Reset()
    {
      results_.clear();
      results_.shrink_to_fit();
      scanned_ = false;
      truncated_ = false;
    }

    // Every 4-byte aligned address that holds `value` now.
    void FirstScan(int32_t value)
    {
      Reset();
      scanned_ = true;
      uint8_t *base = Base();
      if (!base)
      {
        return;
      }
      const uint32_t needle = __builtin_bswap32(static_cast<uint32_t>(value));
      for (const auto &[start, end] : kRanges)
      {
        ForEachReadableRegion(base, start, end, [&](uint32_t from, uint32_t to) {
          for (uint32_t a = from; a < to; a += 4)
          {
            uint32_t v;
            std::memcpy(&v, base + a, 4);
            if (v == needle)
            {
              if (results_.size() >= kMaxResults)
              {
                truncated_ = true;
                return false;
              }
              results_.push_back(a);
            }
          }
          return true;
        });
        if (truncated_)
        {
          break;
        }
      }
    }

    // Keeps the addresses that hold `value` now.
    void NextScan(int32_t value)
    {
      uint8_t *base = Base();
      if (!base)
      {
        return;
      }
      std::vector<uint32_t> kept;
      for (uint32_t a : results_)
      {
        int32_t v;
        if (Read32(a, v) && v == value)
        {
          kept.push_back(a);
        }
      }
      results_ = std::move(kept);
    }

    static bool Read32(uint32_t addr, int32_t &out)
    {
      uint8_t *base = Base();
      if (!base || (addr & 3) || !IsAccessible(base, addr, false))
      {
        return false;
      }
      uint32_t v;
      std::memcpy(&v, base + addr, 4);
      out = static_cast<int32_t>(__builtin_bswap32(v));
      return true;
    }

    static bool Write32(uint32_t addr, int32_t value)
    {
      uint8_t *base = Base();
      if (!base || (addr & 3) || !IsAccessible(base, addr, true))
      {
        return false;
      }
      const uint32_t v = __builtin_bswap32(static_cast<uint32_t>(value));
      std::memcpy(base + addr, &v, 4);
      return true;
    }

  private:
#if defined(_WIN32)
    static bool Readable(DWORD protect)
    {
      if (protect & (PAGE_GUARD | PAGE_NOACCESS))
      {
        return false;
      }
      return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    static bool Writable(DWORD protect)
    {
      if (protect & (PAGE_GUARD | PAGE_NOACCESS))
      {
        return false;
      }
      return (protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                         PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    static bool IsAccessible(uint8_t *base, uint32_t addr, bool write)
    {
      MEMORY_BASIC_INFORMATION mbi;
      if (VirtualQuery(base + addr, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT)
      {
        return false;
      }
      return write ? Writable(mbi.Protect) : Readable(mbi.Protect);
    }

    // Calls fn(from, to) for each committed readable guest sub-range of
    // [start, end); fn returns false to stop.
    template <typename Fn>
    static void ForEachReadableRegion(uint8_t *base, uint32_t start, uint32_t end, Fn &&fn)
    {
      uint64_t a = start;
      while (a < end)
      {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(base + a, &mbi, sizeof(mbi)) != sizeof(mbi))
        {
          return;
        }
        const uint64_t region_end =
            static_cast<uint64_t>(static_cast<uint8_t *>(mbi.BaseAddress) - base) + mbi.RegionSize;
        const uint64_t to = region_end < end ? region_end : end;
        if (mbi.State == MEM_COMMIT && Readable(mbi.Protect) && to > a)
        {
          if (!fn(static_cast<uint32_t>(a), static_cast<uint32_t>(to)))
          {
            return;
          }
        }
        if (region_end <= a)
        {
          return;
        }
        a = region_end;
      }
    }
#else
    static bool IsAccessible(uint8_t *, uint32_t, bool) { return false; }
    template <typename Fn>
    static void ForEachReadableRegion(uint8_t *, uint32_t, uint32_t, Fn &&)
    {
    }
#endif

    std::vector<uint32_t> results_;
    bool scanned_ = false;
    bool truncated_ = false;
  };
}  // namespace vp_tools
