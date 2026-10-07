#include "image.h"

#include "platform.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
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
  // Windows: the system stack (WinINet), so no curl.exe and no shell.
  if (plat::http_get_system(url, out, err)) return true;
  if (getenv("MDT_NO_CURL")) return false;  // (no shell here: report instead)
  // Elsewhere: fall back to the curl binary when present.
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
#endif  // MDT_HAVE_CURL

namespace mdt {
// ------------------------------------------------ remote image downloads ---
// The worker holds a reference to State, so the loader can be destroyed while a
// download is still in flight: the thread finishes, sees "stop" and exits.  The
// caller is never blocked.
struct RemoteImageLoader::State {
  std::mutex mu;
  std::deque<std::string> queue;
  std::set<std::string> seen;             // queued or finished at least once
  std::map<std::string, Result> results;  // url -> finished download
  bool stop = false;
  bool changed = false;
  int in_flight = 0;
};

static bool remote_fetch(const std::string& url, RemoteImage& out) {
  Image img;
  std::string err;
  if (image_load_source(url, img, &err, nullptr)) {
    out.img = img;
    out.ok = true;
    return true;
  }
  out.err = err.empty() ? std::string("download failed") : err;
  return false;
}

RemoteImageLoader::~RemoteImageLoader() {
  if (st_) {
    std::lock_guard<std::mutex> lk(st_->mu);
    st_->stop = true;
  }
  st_.reset();  // the worker keeps its own reference and exits by itself
}

void RemoteImageLoader::worker_entry(void* self) {
  std::unique_ptr<std::shared_ptr<State>> holder((std::shared_ptr<State>*)self);
  std::shared_ptr<State> st = *holder;
  holder.reset();
  for (;;) {
    std::string url;
    {
      std::lock_guard<std::mutex> lk(st->mu);
      if (st->stop) return;
      if (!st->queue.empty()) {
        url = st->queue.front();
        st->queue.pop_front();
        st->in_flight++;
      }
    }
    if (url.empty()) {
      // Park between requests: a newly queued picture is picked up within
      // 100 ms (the placeholder is on screen until then), and an idle reader
      // does not wake up the CPU every few milliseconds.
      plat::sleep_ms(100);
      continue;
    }
    auto r = std::make_shared<RemoteImage>();
    r->url = url;
    remote_fetch(url, *r);
    r->done = true;
    std::lock_guard<std::mutex> lk(st->mu);
    st->in_flight--;
    st->results[url] = r;
    st->changed = true;
  }
}

void RemoteImageLoader::request(const std::string& url) {
  if (!st_) st_ = std::make_shared<State>();
  {
    std::lock_guard<std::mutex> lk(st_->mu);
    if (st_->seen.count(url)) return;  // already queued or fetched
    st_->seen.insert(url);
  }
  if (!started_ && !no_thread_) {
    started_ = true;
    auto* arg = new std::shared_ptr<State>(st_);
    if (!plat::thread_start(&RemoteImageLoader::worker_entry, arg)) {
      delete arg;
      no_thread_ = true;  // no threads here: fetch when asked
      started_ = false;
    }
  }
  if (no_thread_) {
    auto r = std::make_shared<RemoteImage>();
    r->url = url;
    remote_fetch(url, *r);
    r->done = true;
    std::lock_guard<std::mutex> lk(st_->mu);
    st_->results[url] = r;
    st_->changed = true;
    return;
  }
  std::lock_guard<std::mutex> lk(st_->mu);
  st_->queue.push_back(url);
}

RemoteImageLoader::Result RemoteImageLoader::get(const std::string& url) const {
  if (!st_) return nullptr;
  std::lock_guard<std::mutex> lk(st_->mu);
  auto it = st_->results.find(url);
  return it == st_->results.end() ? nullptr : it->second;
}

bool RemoteImageLoader::take_changed() {
  if (!st_) return false;
  std::lock_guard<std::mutex> lk(st_->mu);
  bool c = st_->changed;
  st_->changed = false;
  return c;
}

bool RemoteImageLoader::busy() const {
  if (!st_) return false;
  std::lock_guard<std::mutex> lk(st_->mu);
  return st_->in_flight > 0 || !st_->queue.empty();
}

void RemoteImageLoader::clear() {
  if (!st_) return;
  std::lock_guard<std::mutex> lk(st_->mu);
  st_->results.clear();
  st_->seen.clear();
  st_->queue.clear();
}

}  // namespace mdt
