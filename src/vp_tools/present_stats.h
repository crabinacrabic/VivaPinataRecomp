// vp_tools/present_stats.h - how evenly the host presentation shows the game
// frames: Presenter::GetPresentStats of our SDK (src/ui/presenter.cpp), shown
// by the F2 overlay and appended to the --vp_tools_log_ticks lines. At 30 FPS
// every interval should be 33.3 ms; with vsync on a 165 Hz monitor a frame
// stays for 5 or 6 refreshes (30.3 / 36.4 ms), which shows up as the spread.
#pragma once

#include <string>

#include <fmt/format.h>

#include <rex/graphics/graphics_system.h>
#include <rex/runtime.h>
#include <rex/ui/presenter.h>

namespace vp_tools
{
  using PresentStats = rex::ui::Presenter::PresentStats;

  // The last complete window (about 1 s); window_seconds is 0 before the first.
  inline PresentStats CurrentPresentStats()
  {
    rex::Runtime *rt = rex::Runtime::instance();
    auto *gs = rt ? static_cast<rex::graphics::GraphicsSystem *>(rt->graphics_system()) : nullptr;
    rex::ui::Presenter *presenter = gs ? gs->presenter() : nullptr;
    return presenter ? presenter->GetPresentStats() : PresentStats{};
  }

  // "frames 30.0/s, 33.3 ± 2.9 ms (30-37), presents 165/s"
  inline std::string PresentStatsText(const PresentStats &p, bool ru)
  {
    if (p.window_seconds <= 0.0)
    {
      return ru ? "вывод: нет данных" : "present: no data";
    }
    return fmt::format(fmt::runtime(ru ? "вывод {:.1f}/с, {:.1f} ± {:.1f} мс ({:.0f}–{:.0f}), всего {:.0f}/с"
                                       : "present {:.1f}/s, {:.1f} ± {:.1f} ms ({:.0f}-{:.0f}), all {:.0f}/s"),
                       p.guest_frames / p.window_seconds, p.guest_interval_avg_ms, p.guest_interval_stddev_ms,
                       p.guest_interval_min_ms, p.guest_interval_max_ms, p.presents / p.window_seconds);
  }

  // Uneven enough to see: the spread of 5/6 refreshes at 165 Hz is ~3 ms.
  inline bool PresentStatsUneven(const PresentStats &p)
  {
    return p.window_seconds > 0.0 &&
           (p.guest_interval_stddev_ms >= 2.5 || p.guest_interval_max_ms >= 1.5 * p.guest_interval_avg_ms);
  }
}  // namespace vp_tools
