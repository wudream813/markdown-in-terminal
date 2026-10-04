// dev tool: parse an SVG, rasterise it, dump a PNG (for eyeballing the renderer)
#include <cstdio>
#include <string>
#include "../src/svg.h"
#include "../src/image.h"
using namespace mdt;
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: test_svg in.svg out.png [width] [rrggbb] [bghex]\n"); return 1; }
  std::string xml;
  if (!read_file(argv[1], xml)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
  double width = argc > 3 ? atof(argv[3]) : 600;
  RGB fg = argc > 4 ? parse_color(argv[4], RGB{255,255,255}) : RGB{255,255,255};
  RGB bg = argc > 5 ? parse_color(argv[5], RGB{24,26,31}) : RGB{24,26,31};
  SvgImage doc; std::string err;
  if (!svg_parse(xml, doc, &err)) { fprintf(stderr, "parse failed: %s\n", err.c_str()); printf("svg len=%zu head=%.300s\n", xml.size(), xml.c_str()); return 2; }
  printf("viewBox=(%g,%g %gx%g) w_ex=%g h_ex=%g valign=%g shapes=%zu\n",
         doc.vb_x, doc.vb_y, doc.vb_w, doc.vb_h, doc.w_ex, doc.h_ex, doc.valign_ex, doc.shapes.size());
  Image img;
  if (!svg_rasterize(doc, width, fg, img)) { fprintf(stderr, "raster failed\n"); return 3; }
  printf("raster %dx%d\n", img.w, img.h);
  // composite onto bg so the PNG is easy to look at
  for (size_t i = 0; i < (size_t)img.w * img.h; i++) {
    uint8_t* p = &img.rgba[i*4];
    int a = p[3];
    p[0] = (uint8_t)((p[0]*a + bg.r*(255-a))/255);
    p[1] = (uint8_t)((p[1]*a + bg.g*(255-a))/255);
    p[2] = (uint8_t)((p[2]*a + bg.b*(255-a))/255);
    p[3] = 255;
  }
  auto png = png_encode(img.rgba.data(), img.w, img.h);
  FILE* f = fopen(argv[2], "wb"); fwrite(png.data(), 1, png.size(), f); fclose(f);
  printf("wrote %s (%zu bytes)\n", argv[2], png.size());
  return 0;
}
