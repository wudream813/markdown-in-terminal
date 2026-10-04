#include <cmath>
#include <cstdio>
#include "../src/math.h"
using namespace mdt;
int main(int argc, char** argv) {
  std::string tex = argc > 1 ? argv[1] : "\\sum_{i=1}^{n} i^2 = \\frac{n(n+1)(2n+1)}{6}";
  bool display = argc > 2 ? atoi(argv[2]) != 0 : true;
  const char* out = argc > 3 ? argv[3] : "/tmp/math_test.png";
  MathRenderer mr; std::string err;
  double t0 = now_ms();
  mr.init(true, &err);
  printf("js engine: %s (%s) init=%.0fms\n", mr.has_js() ? "yes" : "NO", err.c_str(), now_ms() - t0);
  double em = 16.0;
  t0 = now_ms();
  MathMetrics m = mr.metrics(tex, display, em);
  printf("metrics ok=%d size=%.1fx%.1fpx baseline=%.1f  (%.1fms)\n", m.ok, m.px_w, m.px_h, m.baseline_px, now_ms()-t0);
  if (!m.ok) { printf("unicode fallback: %s\n", m.text.c_str()); return 1; }
  int bw = (int)std::ceil(m.px_w) + 4, bh = (int)std::ceil(m.px_h) + 4;
  Image img; t0 = now_ms();
  if (!mr.raster(tex, display, em, bw, bh, RGB{230,232,238}, img)) { printf("raster failed\n"); return 2; }
  printf("raster %dx%d (%.1fms)\n", img.w, img.h, now_ms()-t0);
  // composite on dark bg for viewing
  for (size_t i = 0; i < (size_t)img.w*img.h; i++) {
    uint8_t* p = &img.rgba[i*4]; int a = p[3];
    p[0] = (p[0]*a + 24*(255-a))/255; p[1] = (p[1]*a + 26*(255-a))/255; p[2] = (p[2]*a + 31*(255-a))/255; p[3]=255;
  }
  auto png = png_encode(img.rgba.data(), img.w, img.h);
  FILE* f = fopen(out, "wb"); fwrite(png.data(), 1, png.size(), f); fclose(f);
  printf("wrote %s\n", out);
  printf("unicode: %s\n", m.text.c_str());
  return 0;
}
