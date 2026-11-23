// shader_seed.h - a smooth first run: fill the shader storage from the
// player's own game files
//
// The SDK compiles a shader the first time a draw needs it; that is the
// stutter of a first run. Later runs are smooth because the SDK keeps two
// kinds of files in <cache>\shaders\shareable and replays them at startup:
//   4D5307F2.xsh                the microcode of every shader the game used
//   4D5307F2.<path>.<api>.xpso  the pipelines that were built from them
// The microcode is game code, so it cannot be shipped. But it lies unchanged
// in the game files: in zlib streams inside Beta\bundles_packages\*.pkg and
// in the loaded default.xex. assets/shader_seed/4D5307F2.seed.txt says where
// (hashes, sizes and offsets only), and the .xpso files next to it list the
// pipelines seen while testing. Apply() runs right before the SDK opens its
// storage: it cuts every shader out of the game files, checks its hash and
// appends what the storage does not hold yet.
//
// Some vertex shaders reach the GPU with a few words changed: the D3D runtime
// patches their vertex fetches when it binds them. For those the seed holds
// the place of the original and an XOR mask for each changed word, like a
// ROM patch: nothing usable without the game files.
//
// A copy of the game that differs from the one the seed was made from just
// gets fewer shaders: nothing is written without a matching hash.
//
// tools/make_shader_seed.py builds the seed from a cache that was played on.
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <rex/filesystem.h>
#include <rex/hash.h>
#include <rex/logging.h>

#include "inflate.h"

namespace vp_shader_seed
{
  namespace fs = std::filesystem;

  // One zlib stream of a package and how much of it has to be decoded.
  struct Stream
  {
    std::string package;
    uint64_t offset = 0;
    uint32_t packed = 0;
    uint32_t want = 0;
  };

  struct Shader
  {
    uint64_t hash = 0;     // XXH3 of the microcode: the SDK's name for the shader
    uint32_t dwords = 0;   // microcode size in 32-bit words
    bool pixel = false;
    bool in_image = false; // true: `where` is a guest address inside default.xex
    uint32_t stream = 0;   // false: `where` is an offset in this decoded stream
    uint32_t where = 0;
    std::vector<std::pair<uint32_t, uint32_t>> patch; // {word index, XOR mask of the big-endian word}
  };

  struct PipelineFile
  {
    std::string name;
    uint32_t record = 0; // bytes per stored pipeline; the first 8 are the hash of the rest
  };

  struct Seed
  {
    uint32_t storage_version = 0; // of the .xsh format the seed was made for
    std::vector<Stream> streams;
    std::vector<Shader> shaders;
    std::vector<PipelineFile> pipelines;
  };

  inline bool ReadFile(const fs::path &path, std::vector<uint8_t> &out)
  {
    out.clear();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
      return false;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || size > (std::streamoff(1) << 30))
    {
      return false;
    }
    out.resize(size_t(size));
    file.seekg(0);
    return size == 0 || bool(file.read(reinterpret_cast<char *>(out.data()), size));
  }

  // File names in the seed are plain names: no folders, no "..".
  inline bool PlainName(const std::string &name)
  {
    return !name.empty() && name.find("..") == std::string::npos &&
           name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") ==
               std::string::npos;
  }

  // The seed is a text file, one record per line (see the file's own header).
  inline bool Parse(const std::string &text, Seed &seed)
  {
    std::istringstream lines(text);
    std::string line, word, kind, place;
    while (std::getline(lines, line))
    {
      std::istringstream in(line);
      if (!(in >> word) || word[0] == '#')
      {
        continue;
      }
      if (word == "format")
      {
        int format = 0;
        if (!(in >> format) || format != 1)
        {
          return false;
        }
      }
      else if (word == "storage_version")
      {
        in >> std::hex >> seed.storage_version;
      }
      else if (word == "stream")
      {
        Stream stream;
        size_t index = 0;
        in >> index >> stream.package >> stream.offset >> stream.packed >> stream.want;
        constexpr uint32_t kMaxBytes = 1u << 28; // far above any stream of the game
        if (!in || index != seed.streams.size() || !PlainName(stream.package) || !stream.packed ||
            stream.packed > kMaxBytes || !stream.want || stream.want > kMaxBytes)
        {
          return false;
        }
        seed.streams.push_back(stream);
      }
      else if (word == "shader")
      {
        Shader shader;
        in >> std::hex >> shader.hash >> kind >> std::dec >> shader.dwords >> place;
        shader.pixel = kind == "p";
        shader.in_image = place == "x";
        if (shader.in_image)
        {
          in >> std::hex >> shader.where >> std::dec;
        }
        else
        {
          in >> shader.stream >> shader.where;
        }
        if (!in || !shader.dwords || shader.dwords > 0xFFFF || (kind != "p" && kind != "v") ||
            (place != "x" && place != "s"))
        {
          return false;
        }
        // Optional: "d <count>" and <count> pairs of a word index and its XOR mask (hex).
        std::string tag;
        if (in >> tag)
        {
          size_t count = 0;
          if (tag != "d" || !(in >> count) || count > shader.dwords)
          {
            return false;
          }
          for (size_t i = 0; i < count; ++i)
          {
            uint32_t index = 0, mask = 0;
            in >> std::dec >> index >> std::hex >> mask >> std::dec;
            if (!in || index >= shader.dwords)
            {
              return false;
            }
            shader.patch.emplace_back(index, mask);
          }
        }
        seed.shaders.push_back(shader);
      }
      else if (word == "pipelines")
      {
        PipelineFile file;
        in >> file.name >> file.record;
        if (!in || !PlainName(file.name) || file.record < 16 || file.record > 4096)
        {
          return false;
        }
        seed.pipelines.push_back(file);
      }
    }
    return seed.storage_version != 0;
  }

  // Cuts the file to `keep` bytes (dropping a torn record at its end) and appends.
  inline bool Append(const fs::path &path, uint64_t keep, const std::vector<uint8_t> &bytes)
  {
    std::error_code ec;
    if (fs::exists(path, ec) && fs::file_size(path, ec) != keep)
    {
      fs::resize_file(path, keep, ec);
      if (ec)
      {
        return false;
      }
    }
    std::ofstream file(path, std::ios::binary | std::ios::app);
    return bool(file.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size())));
  }

  // The shader from its place in the game files, patched; true when the hash matches.
  inline bool Cut(const Shader &shader, const uint8_t *source, std::vector<uint8_t> &code)
  {
    const size_t bytes = size_t(shader.dwords) * 4;
    code.assign(source, source + bytes);
    for (const auto &[index, mask] : shader.patch)
    {
      for (size_t k = 0; k < 4; ++k)
      {
        code[size_t(index) * 4 + k] ^= uint8_t(mask >> (24 - 8 * k));
      }
    }
    return XXH3_64bits(code.data(), bytes) == shader.hash;
  }

  // A shader as the SDK stores it: hash, size and type, microcode in guest byte order.
  inline void PutShader(std::vector<uint8_t> &out, const Shader &shader, const uint8_t *code)
  {
    const uint32_t size_and_type = shader.dwords | (shader.pixel ? 0x80000000u : 0u);
    const size_t at = out.size();
    out.resize(at + 12 + size_t(shader.dwords) * 4);
    std::memcpy(&out[at], &shader.hash, 8);
    std::memcpy(&out[at + 8], &size_and_type, 4);
    std::memcpy(&out[at + 12], code, size_t(shader.dwords) * 4);
  }

  // seed_dir:   assets/shader_seed of this checkout
  // game_root:  the extracted disc (the folder with default.xex and Beta\)
  // cache_root: the SDK's cache folder of this user
  // guest_base: host address of guest address 0; the image is the loaded default.xex
  inline void Apply(const fs::path &seed_dir, const fs::path &game_root, const fs::path &cache_root,
                    const uint8_t *guest_base, uint32_t image_base, uint32_t image_size)
  {
    const auto started = std::chrono::steady_clock::now();
    const std::string title = "4D5307F2";
    std::vector<uint8_t> raw;
    Seed seed;
    if (cache_root.empty() || !ReadFile(seed_dir / (title + ".seed.txt"), raw))
    {
      return; // this checkout has no seed
    }
    if (!Parse(std::string(raw.begin(), raw.end()), seed))
    {
      REXLOG_WARN("[shader seed] {}.seed.txt is damaged; the shader storage is left as it is", title);
      return;
    }

    // The fingerprint of the whole seed. Once it has been applied to this
    // cache folder there is nothing to do on later runs.
    uint64_t id = XXH3_64bits(raw.data(), raw.size());
    std::vector<std::vector<uint8_t>> pipeline_data(seed.pipelines.size());
    for (size_t i = 0; i < seed.pipelines.size(); ++i)
    {
      ReadFile(seed_dir / seed.pipelines[i].name, pipeline_data[i]);
      id = XXH3_64bits_withSeed(pipeline_data[i].data(), pipeline_data[i].size(), id);
    }
    char id_text[17];
    std::snprintf(id_text, sizeof(id_text), "%016llX", static_cast<unsigned long long>(id));

    const fs::path dir = cache_root / "shaders" / "shareable";
    const fs::path marker_path = dir / (title + ".seed");
    const fs::path shader_path = dir / (title + ".xsh");
    std::error_code ec;
    std::vector<uint8_t> marker;
    if (ReadFile(marker_path, marker) && std::string(marker.begin(), marker.end()) == id_text &&
        fs::exists(shader_path, ec))
    {
      return;
    }
    fs::create_directories(dir, ec);

    // What the storage holds already. Its header is the magic "XESH" and the
    // format version with its bytes swapped; then one record per shader.
    const uint32_t version = seed.storage_version;
    const uint32_t header[2] = {0x48534558u, (version >> 24) | ((version >> 8) & 0xFF00u) |
                                                 ((version << 8) & 0xFF0000u) | (version << 24)};
    std::vector<uint8_t> existing;
    std::unordered_set<uint64_t> have;
    uint64_t keep = 0;
    if (ReadFile(shader_path, existing) && existing.size() >= sizeof(header))
    {
      if (std::memcmp(existing.data(), header, sizeof(header)) != 0)
      {
        // Written by another version of the SDK, which will start the file
        // over. Try again on the next run.
        REXLOG_INFO("[shader seed] the shader storage has another format version; not touching it");
        return;
      }
      size_t at = sizeof(header);
      while (at + 12 <= existing.size())
      {
        uint64_t hash;
        uint32_t size_and_type;
        std::memcpy(&hash, &existing[at], 8);
        std::memcpy(&size_and_type, &existing[at + 8], 4);
        const size_t bytes = size_t(size_and_type & 0x7FFFFFFFu) * 4;
        if (at + 12 + bytes > existing.size())
        {
          break;
        }
        have.insert(hash);
        at += 12 + bytes;
      }
      keep = at;
    }

    std::vector<uint8_t> add;
    if (keep == 0)
    {
      const auto *bytes = reinterpret_cast<const uint8_t *>(header);
      add.assign(bytes, bytes + sizeof(header));
    }
    size_t added = 0, present = 0, not_found = 0;
    std::vector<uint8_t> code;

    // Shaders inside default.xex come straight from guest memory.
    std::vector<std::vector<const Shader *>> by_stream(seed.streams.size());
    for (const Shader &shader : seed.shaders)
    {
      const size_t bytes = size_t(shader.dwords) * 4;
      if (have.count(shader.hash))
      {
        ++present;
      }
      else if (!shader.in_image)
      {
        if (shader.stream < by_stream.size())
        {
          by_stream[shader.stream].push_back(&shader);
        }
        else
        {
          ++not_found;
        }
      }
      else if (guest_base && shader.where >= image_base &&
               uint64_t(shader.where) + bytes <= uint64_t(image_base) + image_size &&
               Cut(shader, guest_base + shader.where, code))
      {
        PutShader(add, shader, code.data());
        have.insert(shader.hash);
        ++added;
      }
      else
      {
        ++not_found;
      }
    }

    // The rest come from the packages, one zlib stream at a time.
    std::vector<uint8_t> packed, plain;
    for (size_t i = 0; i < by_stream.size(); ++i)
    {
      if (by_stream[i].empty())
      {
        continue;
      }
      const Stream &stream = seed.streams[i];
      bool decoded = false;
      std::ifstream file(game_root / "Beta" / "bundles_packages" / stream.package, std::ios::binary);
      if (file && file.seekg(std::streamoff(stream.offset)))
      {
        packed.resize(stream.packed);
        file.read(reinterpret_cast<char *>(packed.data()), std::streamsize(packed.size()));
        decoded = vp_inflate::Inflate(packed.data(), size_t(file.gcount()), stream.want, plain);
      }
      for (const Shader *shader : by_stream[i])
      {
        const size_t bytes = size_t(shader->dwords) * 4;
        if (have.count(shader->hash))
        {
          ++present;
        }
        else if (decoded && uint64_t(shader->where) + bytes <= plain.size() &&
                 Cut(*shader, &plain[shader->where], code))
        {
          PutShader(add, *shader, code.data());
          have.insert(shader->hash);
          ++added;
        }
        else
        {
          ++not_found;
        }
      }
    }
    if (added && !Append(shader_path, keep, add))
    {
      REXLOG_WARN("[shader seed] cannot write {}", rex::path_to_utf8(shader_path));
      return;
    }

    // Pipelines: fixed-size records after a 12-byte header that names the
    // API, the render path and the format version. Only records that pass
    // their own hash and are not stored yet are added, and only to a file
    // with the same header.
    size_t pipelines = 0;
    for (size_t i = 0; i < seed.pipelines.size(); ++i)
    {
      const std::vector<uint8_t> &source = pipeline_data[i];
      const size_t record = seed.pipelines[i].record;
      const fs::path target_path = dir / seed.pipelines[i].name;
      if (source.size() < 12 + record)
      {
        continue;
      }
      std::vector<uint8_t> target;
      std::unordered_set<uint64_t> known;
      uint64_t target_keep = 0;
      if (ReadFile(target_path, target) && target.size() >= 12)
      {
        if (std::memcmp(target.data(), source.data(), 12) != 0)
        {
          continue;
        }
        const size_t count = (target.size() - 12) / record;
        for (size_t k = 0; k < count; ++k)
        {
          uint64_t hash;
          std::memcpy(&hash, &target[12 + k * record], 8);
          known.insert(hash);
        }
        target_keep = 12 + count * record;
      }
      std::vector<uint8_t> more;
      if (target_keep == 0)
      {
        more.assign(source.begin(), source.begin() + 12);
      }
      size_t count = 0;
      for (size_t at = 12; at + record <= source.size(); at += record)
      {
        uint64_t hash;
        std::memcpy(&hash, &source[at], 8);
        if (XXH3_64bits(&source[at + 8], record - 8) == hash && known.insert(hash).second)
        {
          more.insert(more.end(), source.begin() + at, source.begin() + at + record);
          ++count;
        }
      }
      if (count && Append(target_path, target_keep, more))
      {
        pipelines += count;
      }
    }

    std::ofstream(marker_path, std::ios::binary | std::ios::trunc) << id_text;
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    REXLOG_INFO("[shader seed] {} shaders added from the game files ({} were stored already, {} not found), "
                "{} pipelines added, {:.0f} ms",
                added, present, not_found, pipelines, ms);
  }

  // Developer helper for tools/make_shader_seed.py: saves the loaded
  // default.xex image, so the tool can find the shaders that live in it.
  // The file is game code: keep it out of the repository.
  inline bool DumpImage(const fs::path &path, const uint8_t *guest_base, uint32_t image_base, uint32_t image_size)
  {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return guest_base && file &&
           bool(file.write(reinterpret_cast<const char *>(guest_base + image_base), image_size));
  }
} // namespace vp_shader_seed
