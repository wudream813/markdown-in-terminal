// font.h : small TrueType text rasteriser used by --screenshot (pure stb_truetype).
#pragma once
#include <map>
#include <string>
#include <vector>

#include "image.h"
#include "term.h"
#include "util.h"

namespace mdt {

class FontRaster {
 public:
  // Loads the first usable monospace font (or a CJK fallback for wide glyphs).
  bool init(int cell_w, int cell_h, std::string* err = nullptr);
  bool ok() const { return ok_; }
  const std::string& path() const { return path_; }

  // Draws one code point into the cell box using the given colours.
  void draw_cell(Image& img, int cx, int cy, uint32_t cp, RGB fg, RGB bg, uint8_t attr);
  int advance() const { return adv_; }
  int ascent() const { return asc_; }
  int descriptor() const { return desc_; }
  bool cjk_loaded() const { return info_cjk_ != nullptr; }

 private:
  struct GlyphCacheKey { uint32_t cp; int bold; bool operator<(const GlyphCacheKey& o) const { return cp != o.cp ? cp < o.cp : bold < o.bold; } };
  struct Glyph {
    int w = 0, h = 0, off_x = 0, off_y = 0;
    std::vector<uint8_t> a;
  };
  const Glyph* glyph(uint32_t cp, bool bold);

  std::vector<uint8_t> ttf_, ttf_cjk_;
  void* info_ = nullptr;      // stbtt_fontinfo*
  void* info_cjk_ = nullptr;  // stbtt_fontinfo*
  std::map<GlyphCacheKey, Glyph> cache_;
  std::map<uint32_t, Glyph> cjk_cache_;
  bool ok_ = false;
  int cell_w_ = 8, cell_h_ = 16, asc_ = 12, desc_ = 4, adv_ = 8;
  std::string path_;
};

}  // namespace mdt
