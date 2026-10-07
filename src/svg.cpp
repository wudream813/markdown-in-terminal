// svg.cpp : tiny SVG (subset) parser + software rasteriser (nonzero winding,
//           active edge table, supersampled anti-aliasing).
#include "svg.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace mdt {

Mat Mat::rotate(double deg) {
  double r = deg * 3.14159265358979 / 180.0;
  return Mat{std::cos(r), std::sin(r), -std::sin(r), std::cos(r), 0, 0};
}

Mat Mat::parse(const std::string& s) {
  Mat m;
  size_t i = 0;
  while (i < s.size()) {
    while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == ',')) i++;
    size_t start = i;
    while (i < s.size() && isalpha((unsigned char)s[i])) i++;
    std::string fn = s.substr(start, i - start);
    while (i < s.size() && (isspace((unsigned char)s[i]) || s[i] == '(')) i++;
    size_t argstart = i;
    int depth = 1;
    while (i < s.size() && depth > 0) {
      if (s[i] == '(') depth++;
      if (s[i] == ')') depth--;
      i++;
    }
    size_t argend = (i > argstart) ? i - 1 : argstart;
    std::string args = s.substr(argstart, argend > argstart ? argend - argstart : 0);
    std::vector<double> v;
    {
      std::string cur;
      for (char c : args + " ") {
        if (isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') cur += c;
        else if (!cur.empty()) { v.push_back(atof(cur.c_str())); cur.clear(); }
      }
    }
    Mat t;
    if (fn == "translate" && v.size() >= 1) t = translate(v[0], v.size() > 1 ? v[1] : 0);
    else if (fn == "scale" && v.size() >= 1) t = scale(v[0], v.size() > 1 ? v[1] : v[0]);
    else if (fn == "rotate" && v.size() >= 1) {
      t = rotate(v[0]);
      if (v.size() >= 3) t = translate(v[1], v[2]).mul(t).mul(translate(-v[1], -v[2]));
    } else if (fn == "matrix" && v.size() >= 6) t = Mat{v[0], v[1], v[2], v[3], v[4], v[5]};
    m = m.mul(t);
    if (fn.empty()) break;
  }
  return m;
}

// ------------------------------------------------------------ path parse ----
namespace {

struct PathParser {
  const std::string& d;
  size_t i = 0;
  explicit PathParser(const std::string& s) : d(s) {}
  void skip() {
    while (i < d.size() && (isspace((unsigned char)d[i]) || d[i] == ',')) i++;
  }
  bool eof() { skip(); return i >= d.size(); }
  bool number(double& out) {
    skip();
    if (i >= d.size()) return false;
    size_t start = i;
    if (d[i] == '-' || d[i] == '+') i++;
    while (i < d.size() && isdigit((unsigned char)d[i])) i++;
    if (i < d.size() && d[i] == '.') { i++; while (i < d.size() && isdigit((unsigned char)d[i])) i++; }
    if (i < d.size() && (d[i] == 'e' || d[i] == 'E')) {
      size_t save = i; i++;
      if (i < d.size() && (d[i] == '-' || d[i] == '+')) i++;
      if (i < d.size() && isdigit((unsigned char)d[i])) { while (i < d.size() && isdigit((unsigned char)d[i])) i++; }
      else i = save;
    }
    if (i == start) return false;
    out = atof(d.substr(start, i - start).c_str());
    return true;
  }
  bool flag(bool& out) {
    skip();
    if (i >= d.size()) return false;
    if (d[i] == '0') { out = false; i++; return true; }
    if (d[i] == '1') { out = true; i++; return true; }
    return false;
  }
};

inline void add_point(std::vector<Vec2>& out, Vec2 p) {
  if (out.empty() || std::fabs(out.back().x - p.x) > 1e-9 || std::fabs(out.back().y - p.y) > 1e-9)
    out.push_back(p);
}

void flatten_cubic(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3) {
  double len = std::fabs(p1.x - p0.x) + std::fabs(p1.y - p0.y) + std::fabs(p2.x - p1.x) +
               std::fabs(p2.y - p1.y) + std::fabs(p3.x - p2.x) + std::fabs(p3.y - p2.y);
  int n = (int)std::min(40.0, std::max(4.0, std::ceil(len / 2.5)));
  for (int k = 1; k <= n; k++) {
    double t = (double)k / n, u = 1 - t;
    double x = u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x;
    double y = u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y;
    add_point(out, {x, y});
  }
}
void flatten_quad(std::vector<Vec2>& out, Vec2 p0, Vec2 p1, Vec2 p2) {
  Vec2 c1{p0.x + 2.0 / 3 * (p1.x - p0.x), p0.y + 2.0 / 3 * (p1.y - p0.y)};
  Vec2 c2{p2.x + 2.0 / 3 * (p1.x - p2.x), p2.y + 2.0 / 3 * (p1.y - p2.y)};
  flatten_cubic(out, p0, c1, c2, p2);
}

const double kPi = 3.14159265358979;

void arc_to(std::vector<Vec2>& out, Vec2 p0, double rx, double ry, double rot_deg, bool large, bool sweep, Vec2 p1) {
  if (rx == 0 || ry == 0) { add_point(out, p1); return; }
  rx = std::fabs(rx); ry = std::fabs(ry);
  double phi = rot_deg * kPi / 180.0;
  double cosphi = std::cos(phi), sinphi = std::sin(phi);
  double dx2 = (p0.x - p1.x) / 2, dy2 = (p0.y - p1.y) / 2;
  double x1p = cosphi * dx2 + sinphi * dy2;
  double y1p = -sinphi * dx2 + cosphi * dy2;
  double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
  if (lambda > 1) { double s = std::sqrt(lambda); rx *= s; ry *= s; }
  double sign = (large != sweep) ? 1 : -1;
  double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
  double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
  double co = sign * std::sqrt(std::max(0.0, num / (den == 0 ? 1 : den)));
  double cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx;
  double cx = cosphi * cxp - sinphi * cyp + (p0.x + p1.x) / 2;
  double cy = sinphi * cxp + cosphi * cyp + (p0.y + p1.y) / 2;
  auto angle = [](double ux, double uy, double vx, double vy) {
    double dot = ux * vx + uy * vy;
    double len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    double a = std::acos(std::max(-1.0, std::min(1.0, dot / (len == 0 ? 1 : len))));
    return (ux * vy - uy * vx < 0) ? -a : a;
  };
  double theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
  double dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
  if (!sweep && dtheta > 0) dtheta -= 2 * kPi;
  if (sweep && dtheta < 0) dtheta += 2 * kPi;
  int segs = std::max(1, std::min(64, (int)std::ceil(std::fabs(dtheta) / (kPi / 2))));
  double delta = dtheta / segs;
  double t = 8.0 / 3 * std::sin(delta / 4) * std::sin(delta / 4) / std::sin(delta / 2);
  double th = theta1;
  Vec2 prev = p0;
  for (int k = 0; k < segs; k++) {
    double th2 = th + delta;
    auto pt = [&](double a) {
      return Vec2{cx + rx * std::cos(a) * cosphi - ry * std::sin(a) * sinphi,
                  cy + rx * std::cos(a) * sinphi + ry * std::sin(a) * cosphi};
    };
    Vec2 p_end = pt(th2);
    Vec2 d1{-rx * std::sin(th) * cosphi - ry * std::cos(th) * sinphi,
            -rx * std::sin(th) * sinphi + ry * std::cos(th) * cosphi};
    Vec2 d2{-rx * std::sin(th2) * cosphi - ry * std::cos(th2) * sinphi,
            -rx * std::sin(th2) * sinphi + ry * std::cos(th2) * cosphi};
    Vec2 c1{prev.x + t * d1.x, prev.y + t * d1.y};
    Vec2 c2{p_end.x - t * d2.x, p_end.y - t * d2.y};
    flatten_cubic(out, prev, c1, c2, p_end);
    prev = p_end;
    th = th2;
  }
}

void parse_path(const std::string& d, const Mat& m, std::vector<std::vector<Vec2>>& contours) {
  PathParser p(d);
  std::vector<Vec2> cur;
  Vec2 start{0, 0}, cp{0, 0}, curp{0, 0};
  char prev_cmd = 0;
  auto flush = [&](bool close) {
    if (cur.size() >= 2) {
      if (close && (std::fabs(cur.back().x - cur.front().x) > 1e-9 ||
                    std::fabs(cur.back().y - cur.front().y) > 1e-9))
        cur.push_back(cur.front());
      std::vector<Vec2> out;
      out.reserve(cur.size());
      for (auto& pt : cur) out.push_back(m.apply(pt));
      contours.push_back(std::move(out));
    }
    cur.clear();
  };
  while (!p.eof()) {
    char c;
    {
      p.skip();
      if (p.i >= p.d.size()) break;
      c = p.d[p.i];
      if (isalpha((unsigned char)c)) p.i++;
      else c = prev_cmd ? prev_cmd : 'L';
    }
    bool rel = islower((unsigned char)c);
    char base = (char)toupper((unsigned char)c);
    bool have = true;
    switch (base) {
      case 'M': {
        double x, y;
        if (!p.number(x) || !p.number(y)) { have = false; break; }
        flush(false);
        if (rel) { x += curp.x; y += curp.y; }
        curp = {x, y};
        start = curp;
        add_point(cur, curp);
        c = rel ? 'l' : 'L';
        break;
      }
      case 'L': {
        double x, y;
        if (!p.number(x) || !p.number(y)) { have = false; break; }
        if (rel) { x += curp.x; y += curp.y; }
        curp = {x, y};
        add_point(cur, curp);
        break;
      }
      case 'H': {
        double x;
        if (!p.number(x)) { have = false; break; }
        if (rel) x += curp.x;
        curp.x = x;
        add_point(cur, curp);
        break;
      }
      case 'V': {
        double y;
        if (!p.number(y)) { have = false; break; }
        if (rel) y += curp.y;
        curp.y = y;
        add_point(cur, curp);
        break;
      }
      case 'C': {
        double x1, y1, x2, y2, x, y;
        if (!p.number(x1) || !p.number(y1) || !p.number(x2) || !p.number(y2) || !p.number(x) || !p.number(y)) { have = false; break; }
        if (rel) { x1 += curp.x; y1 += curp.y; x2 += curp.x; y2 += curp.y; x += curp.x; y += curp.y; }
        if (cur.empty()) add_point(cur, curp);
        flatten_cubic(cur, curp, {x1, y1}, {x2, y2}, {x, y});
        cp = {x2, y2}; curp = {x, y};
        break;
      }
      case 'S': {
        double x2, y2, x, y;
        if (!p.number(x2) || !p.number(y2) || !p.number(x) || !p.number(y)) { have = false; break; }
        if (rel) { x2 += curp.x; y2 += curp.y; x += curp.x; y += curp.y; }
        Vec2 c1 = curp;
        char pb = (char)toupper((unsigned char)prev_cmd);
        if (pb == 'C' || pb == 'S') c1 = {2 * curp.x - cp.x, 2 * curp.y - cp.y};
        if (cur.empty()) add_point(cur, curp);
        flatten_cubic(cur, curp, c1, {x2, y2}, {x, y});
        cp = {x2, y2}; curp = {x, y};
        break;
      }
      case 'Q': {
        double x1, y1, x, y;
        if (!p.number(x1) || !p.number(y1) || !p.number(x) || !p.number(y)) { have = false; break; }
        if (rel) { x1 += curp.x; y1 += curp.y; x += curp.x; y += curp.y; }
        if (cur.empty()) add_point(cur, curp);
        flatten_quad(cur, curp, {x1, y1}, {x, y});
        cp = {x1, y1}; curp = {x, y};
        break;
      }
      case 'T': {
        double x, y;
        if (!p.number(x) || !p.number(y)) { have = false; break; }
        if (rel) { x += curp.x; y += curp.y; }
        Vec2 c1 = curp;
        char pb = (char)toupper((unsigned char)prev_cmd);
        if (pb == 'Q' || pb == 'T') c1 = {2 * curp.x - cp.x, 2 * curp.y - cp.y};
        if (cur.empty()) add_point(cur, curp);
        flatten_quad(cur, curp, c1, {x, y});
        cp = c1; curp = {x, y};
        break;
      }
      case 'A': {
        double rx, ry, rot, x, y;
        bool large = false, sweep = false;
        if (!p.number(rx) || !p.number(ry) || !p.number(rot) || !p.flag(large) || !p.flag(sweep) ||
            !p.number(x) || !p.number(y)) { have = false; break; }
        if (rel) { x += curp.x; y += curp.y; }
        if (cur.empty()) add_point(cur, curp);
        arc_to(cur, curp, rx, ry, rot, large, sweep, {x, y});
        curp = {x, y};
        break;
      }
      case 'Z': {
        curp = start;
        flush(true);
        prev_cmd = c;
        continue;
      }
      default: have = false; break;
    }
    if (!have) break;
    prev_cmd = c;
  }
  flush(false);
}

// ------------------------------------------------------------- XML bits -----
struct XmlParser {
  const std::string& s;
  size_t i = 0;
  explicit XmlParser(const std::string& t) : s(t) {}
  bool next_tag(std::string& name, std::vector<std::pair<std::string, std::string>>& attrs, bool& closing,
                bool& selfclose) {
    attrs.clear();
    closing = selfclose = false;
    while (i < s.size()) {
      size_t lt = s.find('<', i);
      if (lt == std::string::npos) return false;
      i = lt + 1;
      if (i < s.size() && s[i] == '!') {
        if (s.compare(i, 3, "!--") == 0) {
          size_t c = s.find("-->", i);
          if (c == std::string::npos) return false;
          i = c + 3;
        } else {
          size_t end = s.find('>', i);
          if (end == std::string::npos) return false;
          i = end + 1;
        }
        continue;
      }
      if (i < s.size() && s[i] == '?') {
        size_t end = s.find("?>", i);
        if (end == std::string::npos) return false;
        i = end + 2;
        continue;
      }
      if (i < s.size() && s[i] == '/') { closing = true; i++; }
      size_t ns = i;
      while (i < s.size() && !isspace((unsigned char)s[i]) && s[i] != '>' && s[i] != '/') i++;
      name = s.substr(ns, i - ns);
      while (i < s.size() && s[i] != '>' && !(s[i] == '/' && i + 1 < s.size() && s[i + 1] == '>')) {
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        if (i < s.size() && (s[i] == '>' || s[i] == '/')) break;
        size_t as = i;
        while (i < s.size() && !isspace((unsigned char)s[i]) && s[i] != '=' && s[i] != '>' && s[i] != '/') i++;
        std::string an = s.substr(as, i - as);
        while (i < s.size() && isspace((unsigned char)s[i])) i++;
        std::string av;
        if (i < s.size() && s[i] == '=') {
          i++;
          while (i < s.size() && isspace((unsigned char)s[i])) i++;
          if (i < s.size() && (s[i] == '"' || s[i] == '\'')) {
            char q = s[i++];
            size_t vs = i;
            size_t ve = s.find(q, i);
            if (ve == std::string::npos) ve = s.size();
            av = s.substr(vs, ve - vs);
            i = ve + 1;
          } else {
            size_t vs = i;
            while (i < s.size() && !isspace((unsigned char)s[i]) && s[i] != '>') i++;
            av = s.substr(vs, i - vs);
          }
        }
        if (!an.empty()) attrs.emplace_back(an, av);
      }
      if (i < s.size() && s[i] == '/' && i + 1 < s.size() && s[i + 1] == '>') { selfclose = true; i += 2; }
      else if (i < s.size() && s[i] == '>') i++;
      return true;
    }
    return false;
  }
};

std::string attr(const std::vector<std::pair<std::string, std::string>>& a, const std::string& n) {
  for (auto& kv : a) if (kv.first == n) return kv.second;
  return "";
}
double parse_len(const std::string& v, double dv) { return v.empty() ? dv : atof(v.c_str()); }

void numbers_of(const std::string& s, std::vector<double>& v) {
  std::string cur;
  for (char c : s + " ") {
    if (isdigit((unsigned char)c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') cur += c;
    else if (!cur.empty()) { v.push_back(atof(cur.c_str())); cur.clear(); }
  }
}

}  // namespace

bool svg_parse(const std::string& xml, SvgImage& out, std::string* err) {
  XmlParser p(xml);
  std::string name;
  std::vector<std::pair<std::string, std::string>> attrs;
  bool closing = false, self = false;
  struct State { Mat m; RGB color; bool has_color; double opacity; };
  std::vector<State> stack;
  stack.push_back(State{Mat{}, RGB{0, 0, 0}, false, 1.0});
  bool in_svg = false, done = false;
  int skip_depth = 0;
  // MathJax draws an extensible delimiter (a bracketed matrix with three rows
  // or more, big parentheses around anything tall) as a nested <svg> holding
  // one stretchy piece.  Those have their own viewport, and only the outermost
  // </svg> may end the document.
  int svg_depth = 0;
  while (!done && p.next_tag(name, attrs, closing, self)) {
    if (closing) {
      if (!stack.empty()) stack.pop_back();
      if (skip_depth > 0) skip_depth--;
      if (name == "svg") {
        if (svg_depth > 0) svg_depth--;
        if (svg_depth == 0) done = true;
      }
      continue;
    }
    if (skip_depth > 0 || name == "defs" || name == "title" || name == "desc" || name == "style" ||
        name == "metadata" || name == "clipPath" || name == "mask") {
      if (!self) { skip_depth++; stack.push_back(stack.back()); }
      continue;
    }
    if (name == "svg" && !in_svg) {
      in_svg = true;
      std::string ws = attr(attrs, "width"), hs = attr(attrs, "height");
      double w = parse_len(ws, 0), h = parse_len(hs, 0);
      bool w_ex = ws.find("ex") != std::string::npos, h_ex = hs.find("ex") != std::string::npos;
      out.w_ex = w_ex ? w : 0; out.w_px = w_ex ? 0 : w;
      out.h_ex = h_ex ? h : 0; out.h_px = h_ex ? 0 : h;
      std::vector<double> v;
      numbers_of(attr(attrs, "viewBox"), v);
      if (v.size() >= 4) { out.vb_x = v[0]; out.vb_y = v[1]; out.vb_w = v[2]; out.vb_h = v[3]; }
      std::string style = attr(attrs, "style");
      size_t va = style.find("vertical-align");
      if (va != std::string::npos) {
        size_t colon = style.find(':', va);
        if (colon != std::string::npos) out.valign_ex = atof(style.c_str() + colon + 1);
      }
      std::string fill = attr(attrs, "fill");
      if (!fill.empty() && fill != "currentColor" && fill != "none" && fill != "inherit") {
        stack.back().color = parse_color(fill, RGB{0, 0, 0});
        stack.back().has_color = true;
      }
      if (out.vb_w <= 0) {
        out.vb_w = out.w_px > 0 ? out.w_px : 1;
        out.vb_h = out.h_px > 0 ? out.h_px : 1;
      }
      svg_depth = 1;
      if (!self) stack.push_back(stack.back());
      continue;
    }
    if (name == "svg") {
      // nested <svg>: a viewport of its own - x/y/width/height in the parent's
      // units plus its own viewBox, exactly as the SVG spec defines it
      svg_depth++;
      State st = stack.back();
      double x = parse_len(attr(attrs, "x"), 0);
      double y = parse_len(attr(attrs, "y"), 0);
      double w = parse_len(attr(attrs, "width"), 0);
      double h = parse_len(attr(attrs, "height"), 0);
      std::vector<double> v;
      numbers_of(attr(attrs, "viewBox"), v);
      double sx = 1, sy = 1, tx = x, ty = y;
      if (v.size() >= 4 && v[2] > 0 && v[3] > 0) {
        if (w > 0) sx = w / v[2];
        if (h > 0) sy = h / v[3];
        tx = x - v[0] * sx;
        ty = y - v[1] * sy;
      }
      st.m = st.m.mul(Mat::translate(tx, ty)).mul(Mat::scale(sx, sy));
      if (!self) stack.push_back(st);
      continue;
    }
    if (!in_svg) continue;

    State st = stack.back();
    std::string tf = attr(attrs, "transform");
    if (!tf.empty()) st.m = st.m.mul(Mat::parse(tf));
    std::string fill = attr(attrs, "fill");
    if (!fill.empty()) {
      if (fill == "none") { st.opacity = 0; }
      else if (fill == "currentColor" || fill == "inherit") { st.has_color = false; st.opacity = 1; }
      else { st.color = parse_color(fill, st.color); st.has_color = true; st.opacity = 1; }
    }
    std::string style = attr(attrs, "style");
    if (!style.empty()) {
      size_t fpos = style.find("fill:");
      if (fpos != std::string::npos) {
        size_t semi = style.find(';', fpos);
        std::string fv = style.substr(fpos + 5, semi == std::string::npos ? std::string::npos : semi - fpos - 5);
        if (fv.find("none") != std::string::npos) st.opacity = 0;
        else if (fv.find("currentColor") == std::string::npos && !fv.empty()) {
          st.color = parse_color(fv, st.color);
          st.has_color = true;
        }
      }
      size_t fp = style.find("fill-opacity");
      if (fp != std::string::npos) { size_t c = style.find(':', fp); if (c != std::string::npos) st.opacity = atof(style.c_str() + c + 1); }
    }
    std::string fo = attr(attrs, "fill-opacity");
    if (!fo.empty()) st.opacity = atof(fo.c_str());
    std::string op = attr(attrs, "opacity");
    if (!op.empty()) st.opacity = atof(op.c_str());

    auto push_shape = [&](std::vector<std::vector<Vec2>>&& contours) {
      if (contours.empty() || st.opacity <= 0.001) return;
      Shape sh;
      sh.contours = std::move(contours);
      sh.has_color = st.has_color;
      sh.color = st.color;
      sh.opacity = st.opacity;
      out.shapes.push_back(std::move(sh));
    };

    if (name == "path" || name == "glyph") {
      std::string d = attr(attrs, "d");
      if (!d.empty()) {
        std::vector<std::vector<Vec2>> contours;
        parse_path(d, st.m, contours);
        push_shape(std::move(contours));
      }
    } else if (name == "rect") {
      double x = parse_len(attr(attrs, "x"), 0), y = parse_len(attr(attrs, "y"), 0);
      double w = parse_len(attr(attrs, "width"), 0), h = parse_len(attr(attrs, "height"), 0);
      if (w > 0 && h > 0) {
        std::vector<std::vector<Vec2>> c(1);
        Vec2 pts[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
        for (auto& pt : pts) c[0].push_back(st.m.apply(pt));
        c[0].push_back(c[0][0]);
        push_shape(std::move(c));
      }
    } else if (name == "line") {
      double x1 = parse_len(attr(attrs, "x1"), 0), y1 = parse_len(attr(attrs, "y1"), 0);
      double x2 = parse_len(attr(attrs, "x2"), 0), y2 = parse_len(attr(attrs, "y2"), 0);
      double sw = parse_len(attr(attrs, "stroke-width"), 1);
      if (sw <= 0) sw = 1;
      Vec2 a = st.m.apply({x1, y1}), b = st.m.apply({x2, y2});
      double dx = b.x - a.x, dy = b.y - a.y;
      double len = std::sqrt(dx * dx + dy * dy);
      if (len > 1e-9) {
        double nx = -dy / len * sw / 2, ny = dx / len * sw / 2;
        std::vector<std::vector<Vec2>> c(1);
        c[0] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}, {a.x + nx, a.y + ny}};
        push_shape(std::move(c));
      }
    } else if (name == "polygon" || name == "polyline") {
      std::vector<double> v;
      numbers_of(attr(attrs, "points"), v);
      std::vector<Vec2> c;
      for (size_t k = 0; k + 1 < v.size(); k += 2) c.push_back(st.m.apply({v[k], v[k + 1]}));
      if (c.size() >= 2) {
        c.push_back(c[0]);
        std::vector<std::vector<Vec2>> cc;
        cc.push_back(c);
        push_shape(std::move(cc));
      }
    } else if (name == "circle" || name == "ellipse") {
      double cx = parse_len(attr(attrs, "cx"), 0), cy = parse_len(attr(attrs, "cy"), 0);
      double rx = name == "circle" ? parse_len(attr(attrs, "r"), 0) : parse_len(attr(attrs, "rx"), 0);
      double ry = name == "circle" ? rx : parse_len(attr(attrs, "ry"), 0);
      if (rx > 0 && ry > 0) {
        std::vector<std::vector<Vec2>> c(1);
        for (int k = 0; k <= 48; k++) {
          double a = 2 * kPi * k / 48;
          c[0].push_back(st.m.apply({cx + rx * std::cos(a), cy + ry * std::sin(a)}));
        }
        push_shape(std::move(c));
      }
    }
    if (!self) stack.push_back(st);
  }
  out.ok = in_svg && !out.shapes.empty();
  if (!out.ok && err) *err = in_svg ? "SVG has no drawable shapes" : "not an <svg> document";
  return out.ok;
}

// -------------------------------------------------------------- raster ------
namespace {
struct REdge { double x0, y0, x1, y1; };
struct Cross { double x; int dir; };

void rasterize_contours(const std::vector<std::vector<Vec2>>& contours, double vb_x, double vb_y,
                        double scale, int SS, int W, int H, std::vector<uint8_t>& acc) {
  const int SW = W * SS, SH = H * SS;
  std::vector<REdge> edges;
  for (auto& contour : contours) {
    for (size_t i = 0; i + 1 < contour.size(); i++) {
      REdge e;
      e.x0 = (contour[i].x - vb_x) * scale * SS;
      e.y0 = (contour[i].y - vb_y) * scale * SS;
      e.x1 = (contour[i + 1].x - vb_x) * scale * SS;
      e.y1 = (contour[i + 1].y - vb_y) * scale * SS;
      if (e.y0 == e.y1) continue;
      edges.push_back(e);
    }
  }
  if (edges.empty()) return;
  std::vector<std::vector<int>> buckets((size_t)SH + 2);
  for (size_t i = 0; i < edges.size(); i++) {
    double ymin = std::min(edges[i].y0, edges[i].y1);
    int y0 = (int)std::floor(ymin);
    if (y0 < 0) y0 = 0;
    if (y0 > SH) y0 = SH;
    buckets[(size_t)y0].push_back((int)i);
  }
  std::vector<int> active;
  std::vector<Cross> cross;
  std::vector<uint8_t> cov((size_t)SW + 2, 0);
  for (int sy = 0; sy < SH; sy++) {
    for (int idx : buckets[(size_t)sy]) active.push_back(idx);
    active.erase(std::remove_if(active.begin(), active.end(),
                                [&](int idx) {
                                  const REdge& e = edges[(size_t)idx];
                                  return std::max(e.y0, e.y1) <= sy + 0.5;
                                }),
                 active.end());
    if (active.empty()) continue;
    double yy = sy + 0.5;
    cross.clear();
    for (int idx : active) {
      const REdge& e = edges[(size_t)idx];
      double ymin = std::min(e.y0, e.y1), ymax = std::max(e.y0, e.y1);
      if (yy < ymin || yy >= ymax) continue;
      double t = (yy - e.y0) / (e.y1 - e.y0);
      cross.push_back(Cross{e.x0 + t * (e.x1 - e.x0), e.y1 > e.y0 ? 1 : -1});
    }
    if (cross.size() < 2) continue;
    std::sort(cross.begin(), cross.end(), [](const Cross& a, const Cross& b) {
      if (a.x != b.x) return a.x < b.x;
      return a.dir < b.dir;
    });
    int wind = 0;
    double span_start = 0;
    int lo = SW, hi = 0;
    for (const Cross& c : cross) {
      int prev = wind;
      wind += c.dir;
      if (prev == 0 && wind != 0) {
        span_start = c.x;
      } else if (prev != 0 && wind == 0) {
        int xa = (int)std::floor(span_start), xb = (int)std::ceil(c.x);
        if (xa < 0) xa = 0;
        if (xb > SW) xb = SW;
        for (int px = xa; px < xb; px++) {
          double l = std::max((double)px, span_start), r = std::min((double)px + 1, c.x);
          if (r > l) {
            int v = cov[(size_t)px] + (int)std::lround((r - l) * 255.0);
            cov[(size_t)px] = (uint8_t)std::min(255, v);
          }
        }
        if (xa < lo) lo = xa;
        if (xb > hi) hi = xb;
      }
    }
    if (hi <= lo) continue;
    int ty = sy / SS;
    for (int tx = lo / SS; tx < W && tx * SS < hi; tx++) {
      int sum = 0;
      for (int s = 0; s < SS; s++) sum += cov[(size_t)(tx * SS + s)];
      int c = sum / SS;
      if (c > acc[(size_t)ty * W + tx]) acc[(size_t)ty * W + tx] = (uint8_t)c;
    }
    for (int px = lo; px < hi && px < SW; px++) cov[(size_t)px] = 0;
  }
}
}  // namespace

bool svg_rasterize(const SvgImage& doc, double target_w_px, RGB fg, Image& out, int supersample,
                   double target_h_px, bool fit) {
  if (!doc.ok || doc.vb_w <= 0 || doc.vb_h <= 0 || target_w_px <= 0) return false;
  double scale = target_w_px / doc.vb_w;
  double ox = 0, oy = 0;  // centring offset in output pixels
  if (fit && target_h_px > 0) {
    scale = std::min(target_w_px / doc.vb_w, target_h_px / doc.vb_h);
    ox = (target_w_px - doc.vb_w * scale) / 2;
    oy = (target_h_px - doc.vb_h * scale) / 2;
  }
  int W = fit && target_h_px > 0 ? (int)std::ceil(target_w_px) : (int)std::ceil(doc.vb_w * scale);
  int H = fit && target_h_px > 0 ? (int)std::ceil(target_h_px) : (int)std::ceil(doc.vb_h * scale);
  if (W < 1 || H < 1 || W > 8192 || H > 8192) return false;
  if (supersample <= 0) supersample = (W * H > 250000) ? 2 : 4;
  int SS = std::max(1, std::min(4, supersample));
  out.w = W; out.h = H;
  out.rgba.assign((size_t)W * H * 4, 0);
  std::vector<uint8_t> acc((size_t)W * H, 0);
  for (const Shape& sh : doc.shapes) {
    if (sh.opacity <= 0.001) continue;
    RGB col = sh.has_color ? sh.color : fg;
    std::fill(acc.begin(), acc.end(), 0);
    rasterize_contours(sh.contours, doc.vb_x - ox / scale, doc.vb_y - oy / scale, scale, SS, W, H, acc);
    for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
        uint8_t c = acc[(size_t)y * W + x];
        if (!c) continue;
        double a = (c / 255.0) * sh.opacity;
        uint8_t* px = &out.rgba[((size_t)y * W + x) * 4];
        double dr = px[0], dg = px[1], db = px[2], da = px[3] / 255.0;
        double oa = a + da * (1 - a);
        if (oa <= 0) { px[0] = px[1] = px[2] = px[3] = 0; continue; }
        px[0] = (uint8_t)std::lround((col.r * a + dr * da * (1 - a)) / oa);
        px[1] = (uint8_t)std::lround((col.g * a + dg * da * (1 - a)) / oa);
        px[2] = (uint8_t)std::lround((col.b * a + db * da * (1 - a)) / oa);
        px[3] = (uint8_t)std::lround(oa * 255);
      }
    }
  }
  return true;
}

}  // namespace mdt
