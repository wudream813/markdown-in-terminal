#include "math.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "generated/assets_js.h"

extern "C" {
#include "../vendor/quickjs/quickjs.h"
#include "../vendor/quickjs/quickjs-libc.h"
}

namespace mdt {

// ============================================================ JS engine =====
JsMathEngine::~JsMathEngine() {
  if (ctx_) {
    JSContext* ctx = (JSContext*)ctx_;
    JS_FreeContext(ctx);
    ctx_ = nullptr;
  }
  if (rt_) {
    JSRuntime* rt = (JSRuntime*)rt_;
    JS_FreeRuntime(rt);
    rt_ = nullptr;
  }
}

static std::string js_value_to_string(JSContext* ctx, JSValueConst v) {
  const char* s = JS_ToCString(ctx, v);
  std::string out = s ? s : "";
  if (s) JS_FreeCString(ctx, s);
  return out;
}

bool JsMathEngine::init(std::string* err) {
  JSRuntime* rt = JS_NewRuntime();
  if (!rt) {
    if (err) *err = "cannot create JS runtime";
    return false;
  }
  JS_SetMemoryLimit(rt, 512 * 1024 * 1024);
  JS_SetMaxStackSize(rt, 2 * 1024 * 1024);
  rt_ = rt;
  JSContext* ctx = JS_NewContext(rt);
  if (!ctx) {
    if (err) *err = "cannot create JS context";
    return false;
  }
  ctx_ = ctx;
  js_std_init_handlers(rt);
  js_std_add_helpers(ctx, 0, nullptr);  // console.log / print

  JSValue r = JS_Eval(ctx, kMathJaxBundle, kMathJaxBundleLen, "mathjax-tex-svg.js", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(r)) {
    JSValue e = JS_GetException(ctx);
    if (err) *err = "mathjax bundle failed: " + js_value_to_string(ctx, e);
    JS_FreeValue(ctx, e);
    JS_FreeValue(ctx, r);
    return false;
  }
  JS_FreeValue(ctx, r);

  // Verify the exported entry point exists.
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue mdt = JS_GetPropertyStr(ctx, global, "MDTEX");
  JSValue fn = JS_GetPropertyStr(ctx, mdt, "render");
  bool ok = JS_IsFunction(ctx, fn);
  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, mdt);
  JS_FreeValue(ctx, global);
  if (!ok) {
    if (err) *err = "mathjax bundle did not export MDTEX.render";
    return false;
  }
  ok_ = true;
  return true;
}

bool JsMathEngine::render_svg(const std::string& tex, bool display, std::string& svg, std::string* err) {
  if (!ok_) {
    if (err) *err = "js engine unavailable";
    return false;
  }
  JSContext* ctx = (JSContext*)ctx_;
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue mdt = JS_GetPropertyStr(ctx, global, "MDTEX");
  JSValue fn = JS_GetPropertyStr(ctx, mdt, "render");
  JSValue args[2];
  args[0] = JS_NewStringLen(ctx, tex.c_str(), tex.size());
  args[1] = JS_NewBool(ctx, display);
  JSValue res = JS_Call(ctx, fn, mdt, 2, args);
  JS_FreeValue(ctx, args[0]);
  JS_FreeValue(ctx, args[1]);
  JS_FreeValue(ctx, fn);
  JS_FreeValue(ctx, mdt);
  JS_FreeValue(ctx, global);
  if (JS_IsException(res)) {
    JSValue e = JS_GetException(ctx);
    if (err) *err = js_value_to_string(ctx, e);
    JS_FreeValue(ctx, e);
    JS_FreeValue(ctx, res);
    return false;
  }
  svg = js_value_to_string(ctx, res);
  JS_FreeValue(ctx, res);
  return !svg.empty();
}

// ======================================================= math renderer ======
void MathRenderer::init(bool enable_js, std::string* err) {
  if (enable_js) {
    std::string jerr;
    js_ok_ = js_.init(&jerr);
    if (!js_ok_ && err) *err = jerr;
  }
}

static std::string metrics_key(const std::string& tex, bool display, double em_px) {
  char keybuf[64];
  snprintf(keybuf, sizeof(keybuf), "%d|%d|", display ? 1 : 0, (int)std::lround(em_px * 100));
  return std::string(keybuf) + tex;
}

bool MathRenderer::metrics_cached(const std::string& tex, bool display, double em_px,
                                  MathMetrics* out) const {
  auto it = metrics_cache_.find(metrics_key(tex, display, em_px));
  if (it == metrics_cache_.end()) return false;
  if (out) *out = *it->second;
  return true;
}

MathMetrics MathRenderer::metrics(const std::string& tex, bool display, double em_px) {
  std::string key = metrics_key(tex, display, em_px);
  auto it = metrics_cache_.find(key);
  if (it != metrics_cache_.end()) return *it->second;

  auto m = std::make_shared<MathMetrics>();
  m->text = latex_to_unicode(tex, display);
  if (js_ok_) {
    std::string svg, jerr2;
    bool rendered = js_.render_svg(tex, display, svg, &jerr2);
    if (getenv("MDT_DEBUG_SVG") && !rendered)
      fprintf(stderr, "[mdt] js failed: %s for: %s\n", jerr2.c_str(), tex.c_str());
    if (rendered) {
      if (getenv("MDT_DEBUG_SVG")) {  // dump the SVG MathJax produced
        static int n = 0;
        std::string path = "/tmp/math-" + std::to_string(n++) + ".svg";
        if (FILE* f = fopen(path.c_str(), "wb")) {
          fwrite(svg.data(), 1, svg.size(), f);
          fclose(f);
          fprintf(stderr, "[mdt] svg %s (%zu bytes) for: %s\n", path.c_str(), svg.size(),
                  tex.c_str());
        }
      }
      SvgImage doc;
      std::string perr;
      if (getenv("MDT_DEBUG_SVG"))
        fprintf(stderr, "[mdt] parse: %s\n", perr.empty() ? "(pending)" : perr.c_str());
      if (svg_parse(svg, doc, &perr) && doc.vb_w > 0 && doc.vb_h > 0) {
        double scale = em_px / 1000.0;  // MathJax viewBox units: 1000 per em
        m->px_w = doc.vb_w * scale;
        m->px_h = doc.vb_h * scale;
        m->baseline_px = -doc.vb_y * scale;
        m->svg = svg;
        m->ok = true;
      } else if (getenv("MDT_DEBUG_SVG")) {
        fprintf(stderr, "[mdt] FAILED %s (%s) vb=%gx%g shapes=%zu for: %s\n", perr.c_str(),
                doc.ok ? "ok" : "no", doc.vb_w, doc.vb_h, doc.shapes.size(), tex.c_str());
      }
    }
  }
  if (metrics_cache_.size() > limit_) metrics_cache_.clear();
  metrics_cache_[key] = m;
  return *m;
}

bool MathRenderer::raster(const std::string& tex, bool display, double em_px, int box_w, int box_h, RGB fg,
                          Image& out) {
  if (box_w <= 0 || box_h <= 0) return false;
  char keybuf[128];
  snprintf(keybuf, sizeof(keybuf), "%d|%d|%d|%d|%d|%02x%02x%02x|%s", display ? 1 : 0, box_w, box_h,
           (int)std::lround(em_px * 100), 0, fg.r, fg.g, fg.b, tex.c_str());
  std::string key = keybuf;
  auto it = raster_cache_.find(key);
  if (it != raster_cache_.end()) {
    out = *it->second;
    return !out.empty();
  }
  MathMetrics m = metrics(tex, display, em_px);
  if (!m.ok) return false;
  SvgImage doc;
  if (!svg_parse(m.svg, doc, nullptr)) return false;
  Image img;
  if (!svg_rasterize(doc, box_w, fg, img, 0, box_h, true)) return false;
  if (raster_cache_.size() > limit_) raster_cache_.clear();
  raster_cache_[key] = std::make_shared<Image>(img);
  out = img;
  return true;
}

bool MathRenderer::raster_grid(const std::string& tex, bool display, double em_px, int canvas_w,
                               int canvas_h, RGB fg, Image& out, int* off_x, int* off_y, int ink_y) {
  if (canvas_w <= 0 || canvas_h <= 0) {
    if (getenv("MDT_DEBUG_SVG"))
      fprintf(stderr, "[mdt] raster_grid: bad canvas %dx%d for: %s\n", canvas_w, canvas_h,
              tex.c_str());
    return false;
  }
  MathMetrics m = metrics(tex, display, em_px);
  if (!m.ok) return false;
  SvgImage doc;
  if (!svg_parse(m.svg, doc, nullptr)) {
    if (getenv("MDT_DEBUG_SVG")) fprintf(stderr, "[mdt] raster_grid: reparse failed: %s\n", tex.c_str());
    return false;
  }
  int ink_w = std::max(1, (int)std::lround(m.px_w));
  int ink_h = std::max(1, (int)std::lround(m.px_h));
  if (ink_w > canvas_w) ink_w = canvas_w;
  if (ink_h > canvas_h) ink_h = canvas_h;
  Image ink;
  if (!svg_rasterize(doc, ink_w, fg, ink, 0, ink_h, true)) {
    if (getenv("MDT_DEBUG_SVG"))
      fprintf(stderr, "[mdt] raster_grid: rasterise failed (%dx%d px, %zu shapes) for: %s\n",
              ink_w, ink_h, doc.shapes.size(), tex.c_str());
    return false;
  }
  int ox = (canvas_w - ink.w) / 2;
  int oy = (ink_y == -1000000) ? (canvas_h - ink.h) / 2 : ink_y;
  if (oy + ink.h > canvas_h) oy = canvas_h - ink.h;   // never clip the drawing
  if (getenv("MDT_DEBUG_BASE"))
    fprintf(stderr, "[mdt] raster_grid %s: em=%.1f box=%.1fx%.1f ink_req=%dx%d ink=%dx%d "
            "oy=%d baseline=%.1f -> canvas %dx%d\n",
            tex.substr(0, 24).c_str(), em_px, m.px_w, m.px_h, ink_w, ink_h, ink.w, ink.h,
            oy, m.baseline_px, canvas_w, canvas_h);
  if (ox < 0) ox = 0;
  if (oy < 0) oy = 0;
  out.w = canvas_w;
  out.h = canvas_h;
  out.rgba.assign((size_t)canvas_w * canvas_h * 4, 0);
  for (int y = 0; y < ink.h; y++) {
    int dy = oy + y;
    if (dy >= canvas_h) break;
    for (int x = 0; x < ink.w; x++) {
      int dx = ox + x;
      if (dx >= canvas_w) break;
      const uint8_t* sp = &ink.rgba[((size_t)y * ink.w + x) * 4];
      uint8_t* dp = &out.rgba[((size_t)dy * canvas_w + dx) * 4];
      dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
    }
  }
  if (off_x) *off_x = ox;
  if (off_y) *off_y = oy;
  return true;
}

// ==================================================== unicode fallback ======
namespace {
struct SymEntry { const char* from; const char* to; };
const SymEntry kSymbols[] = {
  {"\\alpha", "\u03b1"}, {"\\beta", "\u03b2"}, {"\\gamma", "\u03b3"}, {"\\delta", "\u03b4"},
  {"\\epsilon", "\u03b5"}, {"\\varepsilon", "\u03b5"}, {"\\zeta", "\u03b6"}, {"\\eta", "\u03b7"},
  {"\\theta", "\u03b8"}, {"\\vartheta", "\u03d1"}, {"\\iota", "\u03b9"}, {"\\kappa", "\u03ba"},
  {"\\lambda", "\u03bb"}, {"\\mu", "\u03bc"}, {"\\nu", "\u03bd"}, {"\\xi", "\u03be"},
  {"\\pi", "\u03c0"}, {"\\varpi", "\u03d6"}, {"\\rho", "\u03c1"}, {"\\sigma", "\u03c3"},
  {"\\tau", "\u03c4"}, {"\\upsilon", "\u03c5"}, {"\\phi", "\u03c6"}, {"\\varphi", "\u03c6"},
  {"\\chi", "\u03c7"}, {"\\psi", "\u03c8"}, {"\\omega", "\u03c9"},
  {"\\Gamma", "\u0393"}, {"\\Delta", "\u0394"}, {"\\Theta", "\u0398"}, {"\\Lambda", "\u039b"},
  {"\\Xi", "\u039e"}, {"\\Pi", "\u03a0"}, {"\\Sigma", "\u03a3"}, {"\\Upsilon", "\u03a5"},
  {"\\Phi", "\u03a6"}, {"\\Psi", "\u03a8"}, {"\\Omega", "\u03a9"},
  {"\\times", "\u00d7"}, {"\\div", "\u00f7"}, {"\\pm", "\u00b1"}, {"\\mp", "\u2213"},
  {"\\cdot", "\u00b7"}, {"\\ast", "\u2217"}, {"\\star", "\u22c6"}, {"\\circ", "\u2218"},
  {"\\bullet", "\u2022"}, {"\\oplus", "\u2295"}, {"\\otimes", "\u2297"}, {"\\odot", "\u2299"},
  {"\\leq", "\u2264"}, {"\\le", "\u2264"}, {"\\geq", "\u2265"}, {"\\ge", "\u2265"},
  {"\\neq", "\u2260"}, {"\\ne", "\u2260"}, {"\\approx", "\u2248"}, {"\\equiv", "\u2261"},
  {"\\sim", "\u223c"}, {"\\simeq", "\u2243"}, {"\\propto", "\u221d"}, {"\\ll", "\u226a"},
  {"\\gg", "\u226b"}, {"\\subset", "\u2282"}, {"\\supset", "\u2283"}, {"\\subseteq", "\u2286"},
  {"\\supseteq", "\u2287"}, {"\\in", "\u2208"}, {"\\ni", "\u220b"}, {"\\notin", "\u2209"},
  {"\\cup", "\u222a"}, {"\\cap", "\u2229"}, {"\\setminus", "\\"}, {"\\emptyset", "\u2205"},
  {"\\varnothing", "\u2205"}, {"\\forall", "\u2200"}, {"\\exists", "\u2203"}, {"\\nexists", "\u2204"},
  {"\\neg", "\u00ac"}, {"\\land", "\u2227"}, {"\\lor", "\u2228"}, {"\\to", "\u2192"},
  {"\\rightarrow", "\u2192"}, {"\\leftarrow", "\u2190"}, {"\\leftrightarrow", "\u2194"},
  {"\\Rightarrow", "\u21d2"}, {"\\Leftarrow", "\u21d0"}, {"\\Leftrightarrow", "\u21d4"},
  {"\\mapsto", "\u21a6"}, {"\\uparrow", "\u2191"}, {"\\downarrow", "\u2193"},
  {"\\infty", "\u221e"}, {"\\partial", "\u2202"}, {"\\nabla", "\u2207"}, {"\\sum", "\u2211"},
  {"\\prod", "\u220f"}, {"\\coprod", "\u2210"}, {"\\int", "\u222b"}, {"\\iint", "\u222c"},
  {"\\iiint", "\u222d"}, {"\\oint", "\u222e"}, {"\\lim", "lim"}, {"\\log", "log"},
  {"\\ln", "ln"}, {"\\exp", "exp"}, {"\\sin", "sin"}, {"\\cos", "cos"}, {"\\tan", "tan"},
  {"\\cot", "cot"}, {"\\sec", "sec"}, {"\\csc", "csc"}, {"\\arcsin", "arcsin"},
  {"\\arccos", "arccos"}, {"\\arctan", "arctan"}, {"\\sinh", "sinh"}, {"\\cosh", "cosh"},
  {"\\tanh", "tanh"}, {"\\max", "max"}, {"\\min", "min"}, {"\\sup", "sup"}, {"\\inf", "inf"},
  {"\\det", "det"}, {"\\dim", "dim"}, {"\\ker", "ker"}, {"\\deg", "deg"}, {"\\gcd", "gcd"},
  {"\\hbar", "\u210f"}, {"\\ell", "\u2113"}, {"\\Re", "\u211c"}, {"\\Im", "\u2111"},
  {"\\aleph", "\u2135"}, {"\\wp", "\u2118"}, {"\\angle", "\u2220"}, {"\\perp", "\u22a5"},
  {"\\parallel", "\u2225"}, {"\\cong", "\u2245"}, {"\\dots", "\u2026"}, {"\\ldots", "\u2026"},
  {"\\cdots", "\u22ef"}, {"\\vdots", "\u22ee"}, {"\\ddots", "\u22f1"}, {"\\quad", "  "},
  {"\\qquad", "    "}, {"\\,", " "}, {"\\;", " "}, {"\\!", ""}, {"\\:", " "},
  {"\\left", ""}, {"\\right", ""}, {"\\displaystyle", ""}, {"\\textstyle", ""}, {"\\limits", ""},
  {"\\mathbb", ""}, {"\\mathcal", ""}, {"\\mathrm", ""}, {"\\mathbf", ""}, {"\\mathit", ""},
  {"\\operatorname", ""}, {"\\text", ""}, {"\\mbox", ""}, {"\\hat", ""}, {"\\bar", ""},
  {"\\vec", ""}, {"\\tilde", ""}, {"\\dot", ""}, {"\\overline", ""}, {"\\boldsymbol", ""},
  {"\\big", ""}, {"\\Big", ""}, {"\\bigg", ""}, {"\\Bigg", ""}, {"\\\\", " "},
  {"\\{", "{"}, {"\\}", "}"}, {"\\%", "%"}, {"\\&", "&"}, {"\\#", "#"}, {"\\_", "_"},
  {"\\$", "$"}, {"\\;", " "},
};
const char* kSuper[] = {"\u2070", "\u00b9", "\u00b2", "\u00b3", "\u2074", "\u2075", "\u2076",
                        "\u2077", "\u2078", "\u2079", "\u207a", "\u207b", "\u207c", "\u207d",
                        "\u207e", "\u207f"};
const char* kSub[] = {"\u2080", "\u2081", "\u2082", "\u2083", "\u2084", "\u2085", "\u2086",
                      "\u2087", "\u2088", "\u2089", "\u208a", "\u208b", "\u208c", "\u208d",
                      "\u208e", "\u2093"};

}  // namespace

// Recursive-descent TeX -> Unicode transcription (best effort, no dependencies).
namespace {
struct TexToUni {
  const std::string& s;
  size_t i = 0;
  explicit TexToUni(const std::string& t) : s(t) {}

  static const char* lookup(const std::string& cmd) {
    for (auto& e : kSymbols) if (cmd == e.from) return e.to;
    return nullptr;
  }
  static const char* greek_like(const std::string& name) { (void)name; return nullptr; }

  std::string parse_seq() {
    std::string out;
    while (i < s.size()) {
      char c = s[i];
      if (c == '}') break;
      if (c == '{') { i++; out += parse_seq(); if (i < s.size() && s[i] == '}') i++; continue; }
      if (c == '\\') { out += parse_command(); continue; }
      if (c == '^' || c == '_') { out += script(c == '^'); continue; }
      if (c == '&') { out += c == '&' ? ' ' : ' '; i++; continue; }
      if (c == '~') { out += ' '; i++; continue; }
      if (c == '\n') { out += ' '; i++; continue; }
      out += c; i++;
    }
    return out;
  }
  std::string parse_arg() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i >= s.size()) return "";
    if (s[i] == '{') { i++; std::string r = parse_seq(); if (i < s.size() && s[i] == '}') i++; return r; }
    if (s[i] == '\\') return parse_command();
    std::string r(1, s[i]);
    i++;
    return r;
  }
  std::string script(bool sup) {
    i++;  // ^ or _
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    std::string arg;
    if (i < s.size() && s[i] == '{') { i++; arg = parse_seq(); if (i < s.size() && s[i] == '}') i++; }
    else if (i < s.size() && s[i] == '\\') arg = parse_command();
    else if (i < s.size()) { arg = std::string(1, s[i]); i++; }
    std::string mapped;
    bool all = !arg.empty() && arg.size() <= 4;
    for (char ch : arg) {
      if (ch >= '0' && ch <= '9') mapped += sup ? kSuper[ch - '0'] : kSub[ch - '0'];
      else if (sup && ch == 'n') mapped += "\u207f";
      else if (sup && ch == 'i') mapped += "\u2071";
      else if (!sup && ch == 'x') mapped += "\u2093";
      else if (sup && ch == '+') mapped += "\u207a";
      else if (sup && ch == '-') mapped += "\u207b";
      else if (!sup && ch == '+') mapped += "\u208a";
      else if (!sup && ch == '-') mapped += "\u208b";
      else all = false;
    }
    if (all) return mapped;
    if (arg.empty()) return "";
    return (sup ? "^" : "_") + (arg.size() > 1 ? "(" + arg + ")" : arg);
  }
  std::string parse_command() {
    i++;  // backslash
    std::string cmd = "\\";
    if (i < s.size() && isalpha((unsigned char)s[i])) {
      while (i < s.size() && isalpha((unsigned char)s[i])) { cmd += s[i]; i++; }
    } else if (i < s.size()) {
      cmd += s[i];
      i++;
    }
    if (cmd == "\\frac" || cmd == "\\dfrac" || cmd == "\\tfrac") {
      std::string a = parse_arg(), b = parse_arg();
      // "a/b" when both sides are simple, "(a+b)/(c+d)" when they are not
      auto simple = [](const std::string& t) {
        // A bare symbol or a root ("√x") needs no brackets; anything with a
        // +/- or a space does, because "a + b/c" would read as "a + (b/c)".
        for (size_t k = 0; k < t.size();) {
          if (t.compare(k, 3, "\u221a") == 0) { k += 3; continue; }
          char c = t[k];
          if (c == '+' || c == '-' || c == ' ') return false;
          k++;
        }
        return !t.empty();
      };
      if (simple(a) && simple(b)) return a + "/" + b;
      return "(" + a + ")/(" + b + ")";
    }
    if (cmd == "\\sqrt") {
      std::string a;
      if (i < s.size() && s[i] == '[') {  // nth root
        while (i < s.size() && s[i] != ']') i++;
        if (i < s.size()) i++;
      }
      a = parse_arg();
      return "\u221a(" + a + ")";
    }
    if (cmd == "\\binom") {
      std::string a = parse_arg(), b = parse_arg();
      return "C(" + a + "," + b + ")";
    }
    if (cmd == "\\text" || cmd == "\\mathrm" || cmd == "\\mbox" || cmd == "\\operatorname" ||
        cmd == "\\mathbf" || cmd == "\\mathit" || cmd == "\\mathbb" || cmd == "\\mathcal" ||
        cmd == "\\mathsf" || cmd == "\\mathtt" || cmd == "\\boldsymbol") {
      return parse_arg();
    }
    if (cmd == "\\left" || cmd == "\\right" || cmd == "\\big" || cmd == "\\Big" || cmd == "\\bigg" ||
        cmd == "\\Bigg" || cmd == "\\displaystyle" || cmd == "\\textstyle" || cmd == "\\limits" ||
        cmd == "\\nolimits" || cmd == "\\," || cmd == "\\!" || cmd == "\\:") {
      return "";
    }
    if (cmd == "\\begin" || cmd == "\\end") {
      // matrix/array environments: keep the content, drop the wrapper
      while (i < s.size() && s[i] != '}') i++;
      if (i < s.size()) i++;
      return cmd == "\\begin" ? "[" : "]";
    }
    if (cmd == "\\quad") return "  ";
    if (cmd == "\\qquad") return "    ";
    if (cmd == "\\\\") return " ";
    if (const char* r = lookup(cmd)) return r;
    // \operatorname{x}, unknown commands: drop the backslash
    std::string name = cmd.substr(1);
    bool alpha = !name.empty();
    for (char ch : name) if (!isalpha((unsigned char)ch)) alpha = false;
    return alpha ? name : name;  // escaped punctuation keeps its glyph
  }
};
}  // namespace

std::string latex_to_unicode(const std::string& tex, bool display) {
  TexToUni p(tex);
  std::string out = trim(collapse_ws(p.parse_seq()));
  if (display && !out.empty()) {
    // display equations often mix matrix separators; keep it readable
  }
  return out;
}

}  // namespace mdt
