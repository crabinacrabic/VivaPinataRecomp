// game_constants.h - Viva Pinata (2006) image layout and fixed addresses
#pragma once

#include <cstdint>

namespace GameConstants
{
  // From the XEX2 header of game_files/default.xex (see docs/XEX_ANALYSIS.md).
  constexpr uint32_t kTitleId = 0x4D5307F2;
  constexpr uint32_t kMediaId = 0x690B3287;
  constexpr uint32_t kImageBase = 0x82000000;
  constexpr uint32_t kImageSize = 0x00B90000;
  constexpr uint32_t kImageEnd = kImageBase + kImageSize;  // 0x82B90000
  constexpr uint32_t kEntryPoint = 0x826B8B48;
  constexpr uint32_t kTlsRawAddress = 0x82B09000;
  constexpr uint32_t kDefaultStackSize = 0x00040000;

  // Code range: codegen determines it and emits REX_CODE_BASE / REX_CODE_SIZE
  // into generated/default/vivapinata_pch.h. Until the first codegen has run
  // fall back to the whole image so debug_tools still compile.
#if defined(REX_CODE_BASE) && defined(REX_CODE_SIZE)
  constexpr uint32_t kCodeBase = static_cast<uint32_t>(REX_CODE_BASE);
  constexpr uint32_t kCodeEnd = static_cast<uint32_t>(REX_CODE_BASE + REX_CODE_SIZE);
#else
  constexpr uint32_t kCodeBase = kImageBase;
  constexpr uint32_t kCodeEnd = kImageEnd;
#endif
}

namespace GameConstants::PatchConstants
{
  // Byte patches applied to the loaded image (OnPostLoadXexImage). Address is
  // a guest VA, value is written big-endian. None yet: every entry needs an
  // address derived from THIS XEX, not from xenia-canary's 4D5307F2 patch
  // file blindly (verify the bytes at the address first).
  struct Patch
  {
    std::uint32_t address;
    std::uint32_t value;
    std::uint8_t width;  // 1 or 4 bytes
  };
}
