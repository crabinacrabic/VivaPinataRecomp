// vivapinata - ReXGlue Recompiled Project (Viva Pinata, 2006, 4D5307F2)
//
// Single host translation unit. Order matters:
//   1. roundeven shims  - SIMDe (pulled in by the generated code) needs the
//                         C23 roundeven/roundevenf that the Windows UCRT lacks.
//   2. generated init.h - PPC context, image constants, DECLARE_REX_FUNC for
//                         every sub_XXXXXXXX (weak aliases live in the recomp objs).
//   3. game_fixes.h     - ALL guest overrides. Strong `extern "C" sub_X`
//                         symbols replace the weak generated aliases at link
//                         time, so this header must be compiled exactly once.
//   4. app header       - ReXApp subclass + REX_DEFINE_APP entry point.

#include <cmath>

#if defined(_WIN32)
extern "C" {
float roundevenf(float x) {
  float r = std::round(x);
  if (std::abs(x - r) == 0.5f) {
    if (std::fmod(r, 2.0f) != 0.0f) {
      r += (x > r) ? 1.0f : -1.0f;
    }
  }
  return r;
}

double roundeven(double x) {
  double r = std::round(x);
  if (std::abs(x - r) == 0.5) {
    if (std::fmod(r, 2.0) != 0.0) {
      r += (x > r) ? 1.0 : -1.0;
    }
  }
  return r;
}
}
#endif

#include "generated/default/vivapinata_init.h"
#include "game_fixes.h"

#include "vivapinata_app.h"

REX_DEFINE_APP(vivapinata, VivapinataApp::Create)
