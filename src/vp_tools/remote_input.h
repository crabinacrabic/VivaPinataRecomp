// vp_tools/remote_input.h - remote control for unattended test runs
//
// --vp_tools_remote=<dir> adds a synthetic pad (merged into guest user 0, like
// the SDK's keyboard pad) that plays commands from <dir>\cmd.txt. It needs no
// window focus and sends no OS input, so a lab copy of the game can be driven
// while the PC is in use. Write the commands to another name and rename the
// file to cmd.txt; the remote reads and deletes it, runs the commands in order
// and appends one line per finished command to <dir>\log.txt.
//
//   press A+B [ms]     hold buttons (A B X Y START BACK LB RB LS RS UP DOWN
//                      LEFT RIGHT), default 150 ms, then 150 ms released
//   stick L|R x y ms   hold a stick (x, y in -1..1; y > 0 is up)
//   trigger L|R v ms   hold a trigger (v in 0..1)
//   wait ms            pause before the next command
//   shot name          guest output -> <dir>\name.bmp
//   freeze on|off      stop / resume the game tick (frames keep being drawn)
//   status             log the tick count
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input_driver.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/presenter.h>

REXCVAR_DEFINE_STRING(vp_tools_remote, "", "Tools",
                      "Remote control directory for unattended test runs: commands from <dir>\\cmd.txt "
                      "(pad input without window focus, screenshots, freeze); empty = off");

namespace vp_tools
{
  using rex::X_RESULT;
  using rex::X_STATUS;

  // Tick control shared with the appMainTickPreDraw hook (vp_tools/hooks.h).
  inline std::atomic<bool> g_remote_freeze{false};
  inline std::atomic<int64_t> g_ticks_run{0};

  class RemoteInputDriver final : public rex::input::InputDriver
  {
  public:
    RemoteInputDriver(rex::ui::Window *window, std::filesystem::path dir)
        : InputDriver(window, 0), dir_(std::move(dir))
    {
    }

    ~RemoteInputDriver() override
    {
      stop_ = true;
      if (worker_.joinable())
      {
        worker_.join();
      }
    }

    X_STATUS Setup() override
    {
      std::error_code ec;
      std::filesystem::create_directories(dir_, ec);
      std::filesystem::remove(dir_ / "cmd.txt", ec);
      Log("remote ready");
      worker_ = std::thread([this]() { Run(); });
      REXLOG_INFO("[vp_tools] remote control: {}", dir_.string());
      return X_STATUS_SUCCESS;
    }

    void EnumerateDevices(std::vector<rex::input::DeviceInfo> &out) override
    {
      ++enumerate_calls_;
      rex::input::DeviceInfo info;
      info.id = kDevice;
      info.name = "VP Tools remote";
      info.synthetic = true;
      out.push_back(info);
    }

    X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE *out_state) override
    {
      if (id != kDevice)
      {
        return X_ERROR_DEVICE_NOT_CONNECTED;
      }
      ++state_calls_;
      if (out_state)
      {
        std::lock_guard lock(mutex_);
        std::memset(out_state, 0, sizeof(*out_state));
        out_state->packet_number = packet_;
        out_state->gamepad.buttons = pad_.buttons;
        out_state->gamepad.left_trigger = pad_.left_trigger;
        out_state->gamepad.right_trigger = pad_.right_trigger;
        out_state->gamepad.thumb_lx = pad_.thumb[0];
        out_state->gamepad.thumb_ly = pad_.thumb[1];
        out_state->gamepad.thumb_rx = pad_.thumb[2];
        out_state->gamepad.thumb_ry = pad_.thumb[3];
      }
      return X_ERROR_SUCCESS;
    }

    X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                   rex::input::X_INPUT_CAPABILITIES *out_caps) override
    {
      if (id != kDevice)
      {
        return X_ERROR_DEVICE_NOT_CONNECTED;
      }
      if (out_caps)
      {
        std::memset(out_caps, 0, sizeof(*out_caps));
        out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
        out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
        out_caps->gamepad.buttons = 0xFFFF;
        out_caps->gamepad.left_trigger = 0xFF;
        out_caps->gamepad.right_trigger = 0xFF;
        out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
        out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
        out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
        out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
      }
      return X_ERROR_SUCCESS;
    }

    X_RESULT SetDeviceVibration(rex::input::DeviceId id, rex::input::X_INPUT_VIBRATION *) override
    {
      return id == kDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
    }

    X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                                rex::input::X_INPUT_KEYSTROKE *) override
    {
      return id == kDevice ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
    }

  private:
    static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x56505200);

    struct Pad
    {
      uint16_t buttons = 0;
      uint8_t left_trigger = 0;
      uint8_t right_trigger = 0;
      int16_t thumb[4] = {};
    };

    void SetPad(const Pad &pad)
    {
      std::lock_guard lock(mutex_);
      pad_ = pad;
      ++packet_;
    }

    void Log(const std::string &line)
    {
      std::ofstream log(dir_ / "log.txt", std::ios::app);
      log << line << "\n";
    }

    // Sleeps in small steps so shutdown is not held up by a long command.
    bool SleepMs(int64_t ms)
    {
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
      while (!stop_ && std::chrono::steady_clock::now() < until)
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      return !stop_;
    }

    static int16_t Axis(double v)
    {
      v = std::clamp(v, -1.0, 1.0);
      return static_cast<int16_t>(v * 32767.0);
    }

    static bool ParseButtons(const std::string &spec, uint16_t &buttons)
    {
      static const std::pair<const char *, uint16_t> kNames[] = {
          {"A", 0x1000},     {"B", 0x2000},    {"X", 0x4000},    {"Y", 0x8000},
          {"START", 0x0010}, {"BACK", 0x0020}, {"LS", 0x0040},   {"RS", 0x0080},
          {"LB", 0x0100},    {"RB", 0x0200},   {"UP", 0x0001},   {"DOWN", 0x0002},
          {"LEFT", 0x0004},  {"RIGHT", 0x0008}};
      buttons = 0;
      std::stringstream ss(spec);
      std::string name;
      while (std::getline(ss, name, '+'))
      {
        std::transform(name.begin(), name.end(), name.begin(), ::toupper);
        bool found = false;
        for (const auto &[n, bit] : kNames)
        {
          if (name == n)
          {
            buttons |= bit;
            found = true;
          }
        }
        if (!found)
        {
          return false;
        }
      }
      return buttons != 0;
    }

    bool Shot(const std::string &name)
    {
      auto *runtime = rex::Runtime::instance();
      auto *graphics = runtime ? runtime->graphics_system() : nullptr;
      auto *presenter = graphics ? graphics->presenter() : nullptr;
      rex::ui::RawImage image;
      if (!presenter || !presenter->CaptureGuestOutput(image) || !image.width || !image.height)
      {
        return false;
      }
      // 24-bit bottom-up BMP.
      const uint32_t row = (image.width * 3 + 3) & ~3u;
      const uint32_t size = 54 + row * image.height;
      std::vector<uint8_t> file(size, 0);
      auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&file[at], &v, 4); };
      auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&file[at], &v, 2); };
      file[0] = 'B';
      file[1] = 'M';
      put32(2, size);
      put32(10, 54);
      put32(14, 40);
      put32(18, image.width);
      put32(22, image.height);
      put16(26, 1);
      put16(28, 24);
      put32(34, row * image.height);
      for (uint32_t y = 0; y < image.height; ++y)
      {
        const uint8_t *src = image.data.data() + size_t(y) * image.stride;
        uint8_t *dst = &file[54 + size_t(image.height - 1 - y) * row];
        for (uint32_t x = 0; x < image.width; ++x)
        {
          dst[x * 3 + 0] = src[x * 4 + 2];
          dst[x * 3 + 1] = src[x * 4 + 1];
          dst[x * 3 + 2] = src[x * 4 + 0];
        }
      }
      std::ofstream out(dir_ / (name + ".bmp"), std::ios::binary);
      out.write(reinterpret_cast<const char *>(file.data()), file.size());
      return bool(out);
    }

    // One command; false = unknown or failed.
    bool Execute(const std::string &line)
    {
      std::stringstream ss(line);
      std::string op;
      ss >> op;
      if (op == "press")
      {
        std::string spec;
        int64_t ms = 150;
        ss >> spec;
        if (!(ss >> ms))
        {
          ms = 150;
        }
        Pad pad;
        if (!ParseButtons(spec, pad.buttons))
        {
          return false;
        }
        SetPad(pad);
        SleepMs(ms);
        SetPad(Pad{});
        return SleepMs(150);
      }
      if (op == "stick" || op == "trigger")
      {
        std::string side;
        double x = 0.0, y = 0.0;
        int64_t ms = 0;
        Pad pad;
        if (op == "stick")
        {
          if (!(ss >> side >> x >> y >> ms))
          {
            return false;
          }
          const int base = (side == "R" || side == "r") ? 2 : 0;
          pad.thumb[base] = Axis(x);
          pad.thumb[base + 1] = Axis(y);
        }
        else
        {
          if (!(ss >> side >> x >> ms))
          {
            return false;
          }
          const auto v = static_cast<uint8_t>(std::clamp(x, 0.0, 1.0) * 255.0);
          ((side == "R" || side == "r") ? pad.right_trigger : pad.left_trigger) = v;
        }
        SetPad(pad);
        SleepMs(ms);
        SetPad(Pad{});
        return SleepMs(150);
      }
      if (op == "wait")
      {
        int64_t ms = 0;
        ss >> ms;
        return SleepMs(ms);
      }
      if (op == "shot")
      {
        std::string name;
        ss >> name;
        return !name.empty() && Shot(name);
      }
      if (op == "freeze")
      {
        std::string v;
        ss >> v;
        g_remote_freeze = (v == "on");
        return true;
      }
      if (op == "status")
      {
        Log(fmt::format("ticks {} frozen {} enumerate {} state {}", g_ticks_run.load(),
                        g_remote_freeze.load() ? 1 : 0, enumerate_calls_.load(), state_calls_.load()));
        return true;
      }
      return false;
    }

    void Run()
    {
      const auto cmd_path = dir_ / "cmd.txt";
      while (!stop_)
      {
        std::error_code ec;
        if (!std::filesystem::exists(cmd_path, ec))
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        }
        std::vector<std::string> lines;
        {
          std::ifstream in(cmd_path);
          std::string line;
          while (std::getline(in, line))
          {
            if (!line.empty() && line.back() == '\r')
            {
              line.pop_back();
            }
            if (!line.empty() && line[0] != '#')
            {
              lines.push_back(line);
            }
          }
        }
        std::filesystem::remove(cmd_path, ec);
        for (const std::string &line : lines)
        {
          if (stop_)
          {
            break;
          }
          const bool ok = Execute(line);
          Log(fmt::format("{} {}", ok ? "ok" : "FAIL", line));
        }
        Log("idle");
      }
    }

    std::filesystem::path dir_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> enumerate_calls_{0};
    std::atomic<uint64_t> state_calls_{0};
    std::mutex mutex_;
    Pad pad_;
    uint32_t packet_ = 1;
  };
}  // namespace vp_tools
