// game_timing.h - high-resolution sleep helpers
//
// Port of the idea in TiP-Recomp SleepHooks.h (Viva Pinata: Trouble in
// Paradise) and the ao2_timing block from KB CROSS_INSPECTION_AO2.md 5.1.
// The SDK maps KeDelayExecutionThread -> ::Sleep(ms), which is quantised to
// the 15.6 ms scheduler tick unless timeBeginPeriod(1) is active. TiP-Recomp
// enables exactly that in RetipApp::OnPostSetup ("optimization tom suggested").
#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#include <intrin.h>
#pragma comment(lib, "winmm.lib")
#endif

namespace vp_timing
{
  inline void EnableHighResTimer()
  {
#if defined(_WIN32)
    static std::once_flag once;
    std::call_once(once, [] { timeBeginPeriod(1); });
#endif
  }

  inline void DisableHighResTimer()
  {
#if defined(_WIN32)
    timeEndPeriod(1);
#endif
  }

  inline void CpuRelax()
  {
#if defined(_WIN32)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
  }

  // Exponential back-off for guest polling loops: pause -> yield -> short sleep.
  inline void SpinBackoff(uint32_t iteration)
  {
    if (iteration < 64)
    {
      CpuRelax();
    }
    else if (iteration < 1024)
    {
#if defined(_WIN32)
      SwitchToThread();
#else
      std::this_thread::yield();
#endif
    }
    else
    {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
  }

  // Hybrid sleep (TiP Sleep_hook): coarse part via the OS scheduler, the last
  // ~1.5 ms by spinning. ~50 us accuracy without burning a core.
  inline void PreciseSleep(double seconds)
  {
    using clock = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    if (seconds <= 0.0)
    {
      CpuRelax();
      return;
    }
    EnableHighResTimer();

    const auto target = clock::now() + std::chrono::duration_cast<clock::duration>(
                                           std::chrono::duration<double>(seconds));
    const auto remaining = target - clock::now();

    if (remaining > 2ms)
    {
      std::this_thread::sleep_until(target - 1500us);
    }
    else if (remaining > 1ms)
    {
      std::this_thread::sleep_for(1ms);  // must yield to the OS or low-priority threads starve
    }
    while (clock::now() < target)
    {
      CpuRelax();
    }
  }
} // namespace vp_timing
