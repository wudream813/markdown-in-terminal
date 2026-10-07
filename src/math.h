// math.h : TeX/KaTeX-syntax maths -> terminal bitmap.
//
// Engine 1 (default): MathJax's TeX->SVG input+output jax, bundled into a single
// JS file, executed by the embedded QuickJS interpreter. Nothing external is
// needed at run time - everything lives inside the binary.
// Engine 2: a plain Unicode transcription, used when the JS engine is absent.
#pragma once
#include <tuple>   // libc++'s <map> needs std::forward_as_tuple (Xcode 15+)
#include <map>
#include <memory>
#include <string>

#include "image.h"
#include "svg.h"
#include "util.h"

namespace mdt {

// ------------------------------------------------------------- JS engine ----
class JsMathEngine {
 public:
  bool init(std::string* err = nullptr);
  bool render_svg(const std::string& tex, bool display, std::string& svg, std::string* err = nullptr);
  bool available() const { return ok_; }
  ~JsMathEngine();

 private:
  void* rt_ = nullptr;
  void* ctx_ = nullptr;
  bool ok_ = false;
};

// ---------------------------------------------------------- math renderer ---
struct MathMetrics {
  bool ok = false;
  double px_w = 0, px_h = 0;   // natural size at the requested em size
  double baseline_px = 0;      // distance from the top of the box to the baseline
  std::string svg;
  std::string text;            // unicode fallback
};

class MathRenderer {
 public:
  void init(bool enable_js, std::string* err = nullptr);
  bool has_js() const { return js_ok_; }

  // Metrics (fonts/geometry) for `tex`; `em_px` is the pixel size of 1em.
  MathMetrics metrics(const std::string& tex, bool display, double em_px);
  // Cache-only lookup: true when the metrics are already known (no typesetting).
  bool metrics_cached(const std::string& tex, bool display, double em_px, MathMetrics* out) const;

  // Rasterised formula, fitted into box_w x box_h pixels.
  bool raster(const std::string& tex, bool display, double em_px, int box_w, int box_h, RGB fg,
              Image& out);
  // Like raster(), but the drawing keeps its natural pixel size and is centred
  // inside a canvas that is exactly the cell grid (canvas_w x canvas_h).  The
  // terminals then place the image 1:1 instead of rescaling it, which is what
  // made formulas look soft.  *off_x/*off_y receive the drawing's offset.
  // ink_y places the drawing at that row of the canvas instead of centring it
  // (used to bake a sub-cell offset into the bitmap; the default centres).
  bool raster_grid(const std::string& tex, bool display, double em_px, int canvas_w, int canvas_h,
                   RGB fg, Image& out, int* off_x, int* off_y, int ink_y = -1000000);

  size_t cache_entries() const { return raster_cache_.size() + metrics_cache_.size(); }

 private:
  JsMathEngine js_;
  bool js_ok_ = false;
  std::map<std::string, std::shared_ptr<MathMetrics>> metrics_cache_;
  std::map<std::string, std::shared_ptr<Image>> raster_cache_;
  size_t limit_ = 512;
};

// Equations as plain Unicode text (used for --math=text / non-graphics terminals).
std::string latex_to_unicode(const std::string& tex, bool display);

}  // namespace mdt
