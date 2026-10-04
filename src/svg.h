// svg.h : tiny SVG (subset) parser + software rasteriser.
// Used to turn MathJax / built-in math output into terminal-ready bitmaps.
#pragma once
#include <string>
#include <vector>

#include "image.h"
#include "util.h"

namespace mdt {

struct Vec2 { double x = 0, y = 0; };

struct Mat {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;  // [a c e; b d f]
  Vec2 apply(Vec2 p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
  Mat mul(const Mat& o) const {
    return Mat{a * o.a + c * o.b, b * o.a + d * o.b,
               a * o.c + c * o.d, b * o.c + d * o.d,
               a * o.e + c * o.f + e, b * o.e + d * o.f + f};
  }
  static Mat translate(double x, double y) { return Mat{1, 0, 0, 1, x, y}; }
  static Mat scale(double x, double y) { return Mat{x, 0, 0, y, 0, 0}; }
  static Mat rotate(double deg);
  static Mat parse(const std::string& s);
};

struct Shape {
  std::vector<std::vector<Vec2>> contours;  // in viewBox units
  bool has_color = false;
  RGB color{0, 0, 0};
  double opacity = 1.0;
};

struct SvgImage {
  double vb_x = 0, vb_y = 0, vb_w = 0, vb_h = 0;  // viewBox
  double w_ex = 0, h_ex = 0;                        // width/height in ex units
  double w_px = 0, h_px = 0;                        // if given in px
  double valign_ex = 0;                             // style="vertical-align:-2.8ex"
  std::vector<Shape> shapes;
  bool ok = false;
};

bool svg_parse(const std::string& xml, SvgImage& out, std::string* err = nullptr);
// Rasterise into a target_w x target_h box.
//   fit = true  : keep the aspect ratio, centre inside the box ("meet")
//   fit = false : width is target_w, height follows the aspect ratio
bool svg_rasterize(const SvgImage& doc, double target_w_px, RGB fg, Image& out, int supersample = 0,
                   double target_h_px = 0, bool fit = false);

}  // namespace mdt
