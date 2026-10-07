// image.h : raster image decoding / encoding / scaling / remote fetch.
#pragma once
#include <memory>
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

// ---------------------------------------------------------------------------
// Remote images are downloaded on a worker thread: a document that links to a
// few pictures must not freeze the reader while the network is slow (a 题解
// with three images on a slow CDN blocked the event loop for 17 seconds, so
// scrolling, '?' and Esc all looked dead).
// ---------------------------------------------------------------------------
struct RemoteImage {
  std::string url;
  bool done = false;
  bool ok = false;
  std::string err;
  Image img;  // decoded, at its natural size
};

class RemoteImageLoader {
 public:
  using Result = std::shared_ptr<const RemoteImage>;

  ~RemoteImageLoader();
  // Queue a download (no-op when the URL was already queued or fetched).  If
  // no worker thread could be started, the fetch happens right here instead.
  void request(const std::string& url);
  // Finished result, or null while the download is still running.
  Result get(const std::string& url) const;
  // True once after a new result arrived (the event loop uses it to redraw).
  bool take_changed();
  bool busy() const;  // queued or in flight
  void clear();       // forget everything (images are fetched again)

 private:
  struct State;
  std::shared_ptr<State> st_;
  bool started_ = false;
  bool no_thread_ = false;
  static void worker_entry(void* self);
};

}  // namespace mdt
