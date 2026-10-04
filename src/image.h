// image.h : raster image decoding / encoding / scaling / remote fetch.
#pragma once
#include <string>
#include <vector>

#include "util.h"

namespace mdt {

struct Image {
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;  // w*h*4, straight alpha
  bool empty() const { return w <= 0 || h <= 0 || rgba.empty(); }
  Image scaled(int nw, int nh) const;
};

// Decode a file (png/jpg/gif/bmp/tga/psd/hdr/pic) into RGBA.
bool image_load_file(const std::string& path, Image& out, std::string* err = nullptr);
// Decode from memory.
bool image_load_memory(const uint8_t* data, size_t len, Image& out, std::string* err = nullptr);
// Fetch http(s):// URL (libcurl if compiled in, otherwise the curl binary).
bool http_get(const std::string& url, std::string& out, std::string* err = nullptr);
// URL or path -> pixels (http/https/local file). SVGs are rasterised by the caller.
bool image_load_source(const std::string& src, Image& out, std::string* err = nullptr,
                       const std::string* base_dir = nullptr);
bool is_remote_url(const std::string& s);
bool is_svg(const std::string& path_or_data);

std::vector<uint8_t> png_encode(const uint8_t* rgba, int w, int h);

}  // namespace mdt
