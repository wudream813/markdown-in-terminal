#include "font.h"

#include "platform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../vendor/stb_truetype.h"

namespace mdt {

namespace {
const char* kMonoCandidates[] = {
  "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
  "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
  "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
  "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
  "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
  "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
  "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
  "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
  "/System/Library/Fonts/Menlo.ttc",
  "/System/Library/Fonts/Monaco.ttf",
  "/Library/Fonts/Arial Unicode.ttf",
  "C:/Windows/Fonts/consola.ttf",
  "C:/Windows/Fonts/cour.ttf",
};
// Extra names worth trying, in priority order (matched case-insensitively
// against the file name found in the system font directories).
const char* kPreferredMono[] = {
  "consola", "cascadia", "lucon", "cour", "menlo", "monaco", "dejavu", "liberation",
  "ubuntumono", "ubuntumono", "notosansmono", "jetbrains", "sourcecodepro", "hack", "terminus",
};
const char* kCjkCandidates[] = {
  "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
  "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
  "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
  "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
  "/usr/share/fonts/truetype/arphic/uming.ttc",
  "/System/Library/Fonts/PingFang.ttc",
  "C:/Windows/Fonts/msyh.ttc",
  "C:/Windows/Fonts/simsun.ttc",
};
// Scans the platform font directories and returns candidate paths; on Windows
// this is how the reader finds consola.ttf / msyh.ttc without hard-coded drives.
std::vector<std::string> scan_font_dirs() {
  std::vector<std::string> out;
  std::string wf, uf;
  plat::system_font_dirs(wf, uf);
  const char* extra[] = {
    "/usr/share/fonts", "/usr/local/share/fonts", "/Library/Fonts", "/System/Library/Fonts",
  };
  std::vector<std::string> dirs;
  if (!wf.empty()) dirs.push_back(wf);
  if (!uf.empty()) dirs.push_back(uf);
  for (const char* e : extra) dirs.push_back(e);
  struct Entry { std::string path; int score; };
  std::vector<Entry> found;
  for (const std::string& dir : dirs) {
    DIR* d = opendir(dir.c_str());
    if (!d) continue;
    while (struct dirent* e = readdir(d)) {
      std::string name = e->d_name;
      std::string low = to_lower(name);
      if (low.size() < 4) continue;
      std::string ext = low.substr(low.size() - 4);
      if (ext != ".ttf" && ext != ".ttc" && ext != ".otf") continue;
      int score = 100;
      for (size_t i = 0; i < sizeof(kPreferredMono) / sizeof(kPreferredMono[0]); i++) {
        if (low.find(kPreferredMono[i]) != std::string::npos) { score = (int)i; break; }
      }
      if (score == 100) continue;
      found.push_back({dir + "/" + name, score});
    }
    closedir(d);
  }
  std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b) {
    if (a.score != b.score) return a.score < b.score;
    return a.path < b.path;
  });
  for (auto& f : found) out.push_back(f.path);
  return out;
}

std::vector<std::string> cjk_scan() {
  std::vector<std::string> out;
  std::string wf, uf;
  plat::system_font_dirs(wf, uf);
  std::vector<std::string> dirs;
  if (!wf.empty()) dirs.push_back(wf);
  if (!uf.empty()) dirs.push_back(uf);
  dirs.push_back("/usr/share/fonts/opentype/noto");
  dirs.push_back("/usr/share/fonts/truetype/noto");
  const char* keys[] = {"msyh", "msjh", "simsun", "simhei", "notosanscjk", "notoserifcjk",
                        "sourcehansans", "pingfang", "wqy", "uming", "ukai", "hiragino"};
  for (const std::string& dir : dirs) {
    DIR* d = opendir(dir.c_str());
    if (!d) continue;
    while (struct dirent* e = readdir(d)) {
      std::string low = to_lower(e->d_name);
      if (low.size() < 4) continue;
      std::string ext = low.substr(low.size() - 4);
      if (ext != ".ttf" && ext != ".ttc" && ext != ".otf") continue;
      for (const char* k : keys) {
        if (low.find(k) != std::string::npos) { out.push_back(dir + "/" + e->d_name); break; }
      }
    }
    closedir(d);
  }
  return out;
}

}  // namespace

bool FontRaster::init(int cell_w, int cell_h, std::string* err) {
  cell_w_ = std::max(4, cell_w);
  cell_h_ = std::max(8, cell_h);
  std::vector<std::string> mono_paths;
  for (const char* p : kMonoCandidates) mono_paths.push_back(p);
  for (auto& p : scan_font_dirs()) mono_paths.push_back(p);
  for (const std::string& p : mono_paths) {
    if (!file_exists(p)) continue;
    std::string data;
    if (!read_file(p, data)) continue;
    std::vector<uint8_t> buf(data.begin(), data.end());
    auto* info = new stbtt_fontinfo();
    int off = stbtt_GetFontOffsetForIndex(buf.data(), 0);
    if (off < 0 || !stbtt_InitFont(info, buf.data(), off)) { delete info; continue; }
    ttf_.swap(buf);
    info_ = info;
    path_ = p;
    break;
  }
  if (!info_) {
    if (err) *err = "no usable monospace font found";
    return false;
  }
  std::vector<std::string> cjk_paths;
  for (const char* p : kCjkCandidates) cjk_paths.push_back(p);
  for (auto& p : cjk_scan()) cjk_paths.push_back(p);
  for (const std::string& p : cjk_paths) {
    if (!file_exists(p)) continue;
    std::string data;
    if (!read_file(p, data)) continue;
    std::vector<uint8_t> buf(data.begin(), data.end());
    auto* info = new stbtt_fontinfo();
    int off = stbtt_GetFontOffsetForIndex(buf.data(), 0);
    if (off < 0 || !stbtt_InitFont(info, buf.data(), off)) { delete info; continue; }
    ttf_cjk_.swap(buf);
    info_cjk_ = info;
    break;
  }
  // metric fitting: pick the scale so that ascent+descent fills the cell height
  auto* info = (stbtt_fontinfo*)info_;
  float scale = stbtt_ScaleForPixelHeight(info, (float)cell_h_ * 0.98f);
  int a = 0, d = 0, g = 0;
  stbtt_GetFontVMetrics(info, &a, &d, &g);
  asc_ = (int)std::lround(a * scale);
  desc_ = (int)std::lround(-d * scale);
  adv_ = (int)std::lround((stbtt_GetCodepointKernAdvance(info, 'm', 'm') + 0) * 0) + cell_w_;
  ok_ = true;
  return true;
}

const FontRaster::Glyph* FontRaster::glyph(uint32_t cp, bool bold) {
  auto* info = (stbtt_fontinfo*)info_;
  bool use_cjk = info_cjk_ && cp > 0x2E7F;
  if (use_cjk) {
    auto it = cjk_cache_.find(cp);
    if (it != cjk_cache_.end()) return it->second.w ? &it->second : nullptr;
    auto* ci = (stbtt_fontinfo*)info_cjk_;
    // CJK fonts usually declare a much larger ascent+descent than 1 em, so map
    // the em box to the cell height instead of using ScaleForPixelHeight.
    float scale = stbtt_ScaleForMappingEmToPixels(ci, (float)cell_h_ * 0.86f);
    int gi = stbtt_FindGlyphIndex(ci, (int)cp);
    if (!gi) { Glyph empty; cjk_cache_[cp] = empty; return nullptr; }
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(ci, gi, scale, scale, &x0, &y0, &x1, &y1);
    Glyph g;
    g.w = x1 - x0; g.h = y1 - y0; g.off_x = x0; g.off_y = y0;
    if (g.w > 0 && g.h > 0) {
      g.a.assign((size_t)g.w * g.h, 0);
      stbtt_MakeGlyphBitmap(ci, g.a.data(), g.w, g.h, g.w, scale, scale, gi);
    }
    if (getenv("MDT_DEBUG_FONT"))
      fprintf(stderr, "cjk U+%04X gi=%d box=%dx%d\n", cp, gi, g.w, g.h);
    cjk_cache_[cp] = g;
    return g.w ? &cjk_cache_[cp] : nullptr;
  }
  GlyphCacheKey key{cp, bold ? 1 : 0};
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second.w ? &it->second : nullptr;
  float scale = stbtt_ScaleForPixelHeight(info, (float)cell_h_ * 0.98f);
  int gi = stbtt_FindGlyphIndex(info, (int)cp);
  if (!gi && cp == ' ') {
    Glyph g;
    g.w = 0; g.h = 0;
    cache_[key] = g;
    return nullptr;
  }
  int x0, y0, x1, y1;
  stbtt_GetGlyphBitmapBox(info, gi, scale, scale, &x0, &y0, &x1, &y1);
  Glyph g;
  g.w = x1 - x0; g.h = y1 - y0; g.off_x = x0; g.off_y = y0;
  if (g.w > 0 && g.h > 0) {
    g.a.assign((size_t)g.w * g.h, 0);
    stbtt_MakeGlyphBitmap(info, g.a.data(), g.w, g.h, g.w, scale, scale, gi);
    if (bold) {  // cheap synthetic bold: horizontal smear
      std::vector<uint8_t> b = g.a;
      for (int y = 0; y < g.h; y++)
        for (int x = 0; x < g.w - 1; x++) {
          int v = b[(size_t)y * g.w + x + 1];
          uint8_t& d = g.a[(size_t)y * g.w + x];
          d = (uint8_t)std::min(255, d + v / 2);
        }
    }
  }
  cache_[key] = g;
  return g.w ? &cache_[key] : nullptr;
}

void FontRaster::draw_cell(Image& img, int cx, int cy, uint32_t cp, RGB fg, RGB bg, uint8_t attr) {
  if (img.empty()) return;
  if (cp == 0) return;  // right half of a wide character: the glyph pass painted it
  int cwid = cp_width(cp);
  RGB use_bg = bg;
  RGB use_fg = fg;
  if (attr & A_REVERSE) std::swap(use_bg, use_fg);
  int px = cx * cell_w_, py = cy * cell_h_;
  int box_w = cell_w_ * std::max(1, cwid);
  for (int y = 0; y < cell_h_; y++) {
    int yy = py + y;
    if (yy < 0 || yy >= img.h) continue;
    for (int x = 0; x < box_w; x++) {
      int xx = px + x;
      if (xx < 0 || xx >= img.w) continue;
      uint8_t* p = &img.rgba[((size_t)yy * img.w + xx) * 4];
      p[0] = use_bg.r; p[1] = use_bg.g; p[2] = use_bg.b; p[3] = 255;
    }
  }
  if (cp == ' ') return;
  int baseline = asc_;
  const Glyph* g = glyph(cp, (attr & A_BOLD) != 0);
  if (!g) return;
  int gx = px + (cp_width(cp) == 2 ? (cell_w_ * 2 - g->w) / 2 : (cell_w_ - g->w) / 2);
  int gy = py + baseline + g->off_y;
  for (int y = 0; y < g->h; y++) {
    int yy = gy + y;
    if (yy < 0 || yy >= img.h) continue;
    for (int x = 0; x < g->w; x++) {
      int xx = gx + x;
      if (xx < 0 || xx >= img.w) continue;
      uint8_t a = g->a[(size_t)y * g->w + x];
      if (!a) continue;
      if (attr & A_DIM) a = (uint8_t)(a / 2);
      uint8_t* p = &img.rgba[((size_t)yy * img.w + xx) * 4];
      p[0] = (uint8_t)((use_fg.r * a + p[0] * (255 - a)) / 255);
      p[1] = (uint8_t)((use_fg.g * a + p[1] * (255 - a)) / 255);
      p[2] = (uint8_t)((use_fg.b * a + p[2] * (255 - a)) / 255);
      p[3] = 255;
    }
  }
  if (attr & A_UNDER) {
    int yy = py + asc_ + 1;
    if (yy >= 0 && yy < img.h) {
      for (int x = 0; x < cell_w_ * cp_width(cp); x++) {
        int xx = px + x;
        if (xx < 0 || xx >= img.w) continue;
        uint8_t* p = &img.rgba[((size_t)yy * img.w + xx) * 4];
        p[0] = use_fg.r; p[1] = use_fg.g; p[2] = use_fg.b; p[3] = 255;
      }
    }
  }
  if (attr & A_STRIKE) {
    int yy = py + asc_ - cell_h_ / 3;
    if (yy >= 0 && yy < img.h) {
      for (int x = 0; x < cell_w_ * cp_width(cp); x++) {
        int xx = px + x;
        if (xx < 0 || xx >= img.w) continue;
        uint8_t* p = &img.rgba[((size_t)yy * img.w + xx) * 4];
        p[0] = use_fg.r; p[1] = use_fg.g; p[2] = use_fg.b; p[3] = 255;
      }
    }
  }
}

}  // namespace mdt
