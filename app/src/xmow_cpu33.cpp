// XMOW 0.1.33 - render thread CPU cuts: the report of the xmow_cpu33_*.cpp files.
// gow2_gpu.cpp Report() calls xmow::cpu33::Informe(seconds) next to its "[xmow perf]" lines (every 10 s).

#include "xmow_cpu33.h"

namespace xmow::cpu33 {

void Informe(double seconds) {
  InformeParametros(seconds);
  InformeMaterial(seconds);
}

}  // namespace xmow::cpu33
