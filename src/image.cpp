#include "image.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <process.h>
#define popen _popen
#define pclose _pclose
#else
#include <sys/wait.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_PIC
#include "../vendor/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../vendor/stb_image_resize2.h"

namespace mdt {

Image Image::scaled(int nw, int nh) const {
  Image o;
  if (empty() || nw <= 0 || nh <= 0) return o;
  if (nw == w && nh == h) return *this;
  o.w = nw; o.h = nh;
  o.rgba.resize((size_t)nw * nh * 4);
  unsigned char* r = stbir_resize_uint8_linear(rgba.data(), w, h, 0, o.rgba.data(), nw, nh, 0, STBIR_RGBA);
  if (!r) { o.rgba.clear(); o.w = o.h = 0; }
  return o;
}

bool image_load_memory(const uint8_t* data, size_t len, Image& out, std::string* err) {
  if (len > (size_t)INT32_MAX) { if (err) *err = "image too large"; return false; }
  int w = 0, h = 0, comp = 0;
  unsigned char* px = stbi_load_from_memory(data, (int)len, &w, &h, &comp, 4);
  if (!px) { if (err) *err = std::string("decode failed: ") + (stbi_failure_reason() ? stbi_failure_reason() : "?"); return false; }
  out.w = w; out.h = h;
  out.rgba.assign(px, px + (size_t)w * h * 4);
  stbi_image_free(px);
  return true;
}

bool image_load_file(const std::string& path, Image& out, std::string* err) {
  std::string data;
  if (!read_file(path, data)) { if (err) *err = "cannot read " + path; return false; }
  return image_load_memory((const uint8_t*)data.data(), data.size(), out, err);
}

bool is_remote_url(const std::string& s) {
  return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}
bool is_svg(const std::string& p) {
  std::string l = to_lower(p);
  return ends_with(l, ".svg") || l.find("<svg") != std::string::npos;
}

bool http_get(const std::string& url, std::string& out, std::string* err) {
#if MDT_HAVE_CURL
  extern bool curl_http_get(const std::string& url, std::string& out, std::string* err);
  return curl_http_get(url, out, err);
#else
  // Fall back to the curl binary when present (keeps the binary dependency-free).
  if (!getenv("MDT_NO_CURL")) {
    std::string cmd = "curl -sfL --max-time 20 --max-filesize 26214400 \"" + url + "\" 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (f) {
      char buf[65536];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
      int rc = pclose(f);
      if (rc == 0 && !out.empty()) return true;
      if (err) *err = "curl exit " + std::to_string(rc);
      return false;
    }
  }
  if (err) *err = "remote images need libcurl or the curl binary";
  return false;
#endif
}

bool image_load_source(const std::string& src, Image& out, std::string* err, const std::string* base_dir) {
  if (is_remote_url(src)) {
    std::string data;
    if (!http_get(src, data, err)) return false;
    return image_load_memory((const uint8_t*)data.data(), data.size(), out, err);
  }
  std::string path = src;
  if (path.rfind("file://", 0) == 0) path = path.substr(7);
  if (path.empty() || path[0] == '/') {
  } else if (path[0] == '~') {
    const char* home = getenv("HOME");
    if (home) path = std::string(home) + path.substr(1);
  } else if (base_dir && !base_dir->empty()) {
    path = *base_dir + "/" + path;
  }
  std::string decoded = replace_all(path, "%20", " ");
  return image_load_file(decoded, out, err);
}

std::vector<uint8_t> png_encode(const uint8_t* rgba, int w, int h) {
  std::vector<uint8_t> out;
  auto cb = [](void* ctx, void* data, int size) {
    auto* v = (std::vector<uint8_t>*)ctx;
    v->insert(v->end(), (uint8_t*)data, (uint8_t*)data + size);
  };
  stbi_write_png_to_func(cb, &out, w, h, 4, rgba, w * 4);
  return out;
}

}  // namespace mdt

#if MDT_HAVE_CURL
#include <curl/curl.h>
namespace mdt {
namespace {
size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
  auto* s = (std::string*)userdata;
  s->append(ptr, size * nmemb);
  return size * nmemb;
}
bool curl_https_ok() {
  static int ok = -1;
  if (ok < 0) {
    curl_version_info_data* vi = curl_version_info(CURLVERSION_NOW);
    ok = (vi && (vi->features & CURL_VERSION_SSL)) ? 1 : 0;
  }
  return ok == 1;
}
}  // namespace

bool curl_http_get(const std::string& url, std::string& out, std::string* err) {
  static bool inited = false;
  if (!inited) { curl_global_init(CURL_GLOBAL_DEFAULT); inited = true; }
  if (url.rfind("https://", 0) == 0 && !curl_https_ok()) {
    if (err) *err = "this curl build has no TLS support";
    return false;
  }
  CURL* h = curl_easy_init();
  if (!h) {
    if (err) *err = "cannot init curl";
    return false;
  }
  char ebuf[CURL_ERROR_SIZE] = {0};
  curl_easy_setopt(h, CURLOPT_URL, url.c_str());
  curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(h, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 8L);
  curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
  curl_easy_setopt(h, CURLOPT_USERAGENT, "mdt/0.1 (+terminal markdown reader)");
  curl_easy_setopt(h, CURLOPT_ERRORBUFFER, ebuf);
  curl_easy_setopt(h, CURLOPT_MAXFILESIZE, 32L * 1024 * 1024);
  CURLcode rc = curl_easy_perform(h);
  long code = 0;
  curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(h);
  if (rc != CURLE_OK || code >= 400 || out.empty()) {
    if (err) *err = std::string("http failed: ") + (ebuf[0] ? ebuf : curl_easy_strerror(rc));
    return false;
  }
  return true;
}
}  // namespace mdt
#endif
