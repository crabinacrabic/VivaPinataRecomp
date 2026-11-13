// inflate.h - a small zlib (RFC 1950 / RFC 1951) decoder
//
// Rare keeps the game data in zlib streams inside Beta\bundles_packages\*.pkg.
// The shader seed (shader_seed.h) needs the beginning of some of those
// streams, so Inflate() stops as soon as it has produced `want` bytes. The
// Adler-32 trailer is not checked: every shader cut from the output is
// compared with its own hash anyway.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vp_inflate
{
  namespace detail
  {
    constexpr int kMaxBits = 15;  // the longest Huffman code in deflate
    constexpr int kFastBits = 10; // codes up to this length decode with one table lookup

    // Deflate is a stream of bits; in each byte the least significant bit comes first.
    struct BitReader
    {
      const uint8_t *next;
      const uint8_t *end;
      uint64_t hold = 0;  // bits not consumed yet; the next one is bit 0
      uint32_t count = 0; // how many bits of `hold` are valid

      void Refill()
      {
        while (count <= 56 && next < end)
        {
          hold |= uint64_t(*next++) << count;
          count += 8;
        }
      }

      // Reads n bits (n <= 32). False when the stream ends first.
      bool Take(uint32_t n, uint32_t &value)
      {
        if (count < n)
        {
          Refill();
          if (count < n)
          {
            return false;
          }
        }
        value = uint32_t(hold & ((uint64_t(1) << n) - 1));
        hold >>= n;
        count -= n;
        return true;
      }
    };

    // A canonical Huffman code, built from the code length of every symbol.
    struct Huffman
    {
      uint16_t fast[1 << kFastBits]; // (symbol << 4) | length; 0: the code is longer than kFastBits
      uint16_t count[kMaxBits + 1];  // how many codes have each length
      uint16_t symbol[288];          // the symbols in the order of their codes

      // False when the lengths ask for more codes than can exist.
      bool Build(const uint8_t *lengths, int n)
      {
        std::memset(count, 0, sizeof(count));
        for (int i = 0; i < n; ++i)
        {
          ++count[lengths[i]];
        }
        count[0] = 0;
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len)
        {
          left = (left << 1) - count[len];
          if (left < 0)
          {
            return false;
          }
        }
        uint16_t offset[kMaxBits + 2];
        offset[1] = 0;
        for (int len = 1; len <= kMaxBits; ++len)
        {
          offset[len + 1] = uint16_t(offset[len] + count[len]);
        }
        for (int i = 0; i < n; ++i)
        {
          if (lengths[i])
          {
            symbol[offset[lengths[i]]++] = uint16_t(i);
          }
        }
        std::memset(fast, 0, sizeof(fast));
        uint32_t code = 0;
        int index = 0;
        for (int len = 1; len <= kFastBits; ++len)
        {
          for (int k = 0; k < count[len]; ++k, ++code, ++index)
          {
            // The stream holds a code starting from its most significant bit,
            // so the table is indexed by the code with its bits reversed.
            uint32_t reversed = 0;
            for (int bit = 0; bit < len; ++bit)
            {
              reversed |= ((code >> bit) & 1) << (len - 1 - bit);
            }
            for (uint32_t i = reversed; i < (1u << kFastBits); i += 1u << len)
            {
              fast[i] = uint16_t((symbol[index] << 4) | len);
            }
          }
          code <<= 1;
        }
        return true;
      }

      // The next symbol, or -1 on a code that does not exist or the end of the input.
      int Decode(BitReader &in) const
      {
        if (in.count < uint32_t(kMaxBits))
        {
          in.Refill();
        }
        if (const uint32_t entry = fast[in.hold & ((1u << kFastBits) - 1)])
        {
          const uint32_t len = entry & 15;
          if (len > in.count)
          {
            return -1;
          }
          in.hold >>= len;
          in.count -= len;
          return int(entry >> 4);
        }
        // A long code: walk it bit by bit, one code length at a time.
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len)
        {
          if (uint32_t(len) > in.count)
          {
            return -1;
          }
          code |= int((in.hold >> (len - 1)) & 1);
          const int codes = count[len];
          if (code - codes < first)
          {
            in.hold >>= len;
            in.count -= uint32_t(len);
            return symbol[index + (code - first)];
          }
          index += codes;
          first = (first + codes) << 1;
          code <<= 1;
        }
        return -1;
      }
    };
  } // namespace detail

  // Decodes the zlib stream in [data, data + size) until it ends or `want`
  // bytes are out. False on a damaged stream. `out` is shorter than `want`
  // only when the stream itself is.
  inline bool Inflate(const uint8_t *data, size_t size, size_t want, std::vector<uint8_t> &out)
  {
    using namespace detail;
    static constexpr uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                                 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static constexpr uint16_t kDistanceBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                                   33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static constexpr uint8_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    static constexpr uint8_t kLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    out.clear();
    // Two header bytes: method 8 (deflate), no preset dictionary, a multiple of 31 together.
    if (size < 2 || (data[0] & 0x0F) != 8 || (data[1] & 0x20) || ((uint32_t(data[0]) << 8) | data[1]) % 31)
    {
      return false;
    }
    out.resize(want);
    size_t n = 0;
    BitReader in{data + 2, data + size};
    Huffman literals, distances;
    uint8_t lengths[320];
    for (bool last = false; !last && n < want;)
    {
      uint32_t final_block, type;
      if (!in.Take(1, final_block) || !in.Take(2, type))
      {
        return false;
      }
      last = final_block != 0;
      if (type == 0)
      {
        // Stored: skip to the next whole byte, then LEN, ~LEN and LEN bytes as they are.
        uint32_t skipped, len, not_len, byte;
        if (!in.Take(in.count & 7, skipped) || !in.Take(16, len) || !in.Take(16, not_len) || (len ^ 0xFFFF) != not_len)
        {
          return false;
        }
        for (; len; --len)
        {
          if (!in.Take(8, byte))
          {
            return false;
          }
          if (n < want)
          {
            out[n++] = uint8_t(byte);
          }
        }
        continue;
      }
      if (type == 1)
      {
        // Fixed codes.
        std::memset(lengths, 8, 144);
        std::memset(lengths + 144, 9, 112);
        std::memset(lengths + 256, 7, 24);
        std::memset(lengths + 280, 8, 8);
        literals.Build(lengths, 288);
        std::memset(lengths, 5, 30);
        distances.Build(lengths, 30);
      }
      else if (type == 2)
      {
        // Dynamic codes: the code lengths are themselves Huffman coded.
        uint32_t literal_count, distance_count, order_count, value;
        if (!in.Take(5, literal_count) || !in.Take(5, distance_count) || !in.Take(4, order_count))
        {
          return false;
        }
        literal_count += 257;
        distance_count += 1;
        order_count += 4;
        if (literal_count > 286 || distance_count > 30)
        {
          return false;
        }
        std::memset(lengths, 0, 19);
        for (uint32_t i = 0; i < order_count; ++i)
        {
          if (!in.Take(3, value))
          {
            return false;
          }
          lengths[kLengthOrder[i]] = uint8_t(value);
        }
        if (!literals.Build(lengths, 19)) // borrowed for the code of the code lengths
        {
          return false;
        }
        const uint32_t total = literal_count + distance_count;
        for (uint32_t i = 0; i < total;)
        {
          const int sym = literals.Decode(in);
          if (sym < 0)
          {
            return false;
          }
          if (sym < 16)
          {
            lengths[i++] = uint8_t(sym);
            continue;
          }
          // 16: repeat the previous length; 17 and 18: a run of zeros.
          uint32_t repeat;
          uint8_t repeated = 0;
          if (sym == 16)
          {
            if (i == 0 || !in.Take(2, repeat))
            {
              return false;
            }
            repeated = lengths[i - 1];
            repeat += 3;
          }
          else
          {
            if (!in.Take(sym == 17 ? 3 : 7, repeat))
            {
              return false;
            }
            repeat += sym == 17 ? 3 : 11;
          }
          if (i + repeat > total)
          {
            return false;
          }
          std::memset(lengths + i, repeated, repeat);
          i += repeat;
        }
        if (!lengths[256] || !literals.Build(lengths, int(literal_count)) ||
            !distances.Build(lengths + literal_count, int(distance_count)))
        {
          return false;
        }
      }
      else
      {
        return false;
      }
      while (n < want)
      {
        int sym = literals.Decode(in);
        if (sym < 0)
        {
          return false;
        }
        if (sym < 256)
        {
          out[n++] = uint8_t(sym);
          continue;
        }
        if (sym == 256)
        {
          break;
        }
        // A copy of `length` bytes from `distance` bytes back; the two may overlap.
        sym -= 257;
        uint32_t extra;
        if (sym >= 29 || !in.Take(kLengthExtra[sym], extra))
        {
          return false;
        }
        size_t length = kLengthBase[sym] + extra;
        const int distance_sym = distances.Decode(in);
        if (distance_sym < 0 || distance_sym >= 30 || !in.Take(kDistanceExtra[distance_sym], extra))
        {
          return false;
        }
        const size_t distance = kDistanceBase[distance_sym] + extra;
        if (distance > n)
        {
          return false;
        }
        length = (std::min)(length, want - n); // (std::min): windows.h may define a min macro
        for (size_t i = 0; i < length; ++i, ++n)
        {
          out[n] = out[n - distance];
        }
      }
    }
    out.resize(n);
    return true;
  }
} // namespace vp_inflate
