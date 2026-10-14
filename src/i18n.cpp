#include "i18n.h"

#include <cctype>

namespace mdt {
namespace {

Lang g_lang = Lang::En;
std::string g_source = "default";

// "zh_CN.UTF-8@euro" -> "zh", "en_US" -> "en", "C" -> "c"
std::string base_language(const std::string& locale) {
  std::string s = locale;
  for (char& c : s) c = (char)tolower((unsigned char)c);
  size_t cut = s.find_first_of(".@");
  if (cut != std::string::npos) s = s.substr(0, cut);
  size_t sep = s.find_first_of("_-");
  if (sep != std::string::npos) s = s.substr(0, sep);
  return s;
}

}  // namespace

bool lang_from_name(const std::string& name, Lang* out) {
  std::string base = base_language(name);
  if (base == "en") {
    *out = Lang::En;
    return true;
  }
  // zh_TW and zh_HK land here too: the reader speaks one Chinese, and it is
  // the simplified one (a Traditional translation needs a translator, not a
  // locale code).
  if (base == "zh") {
    *out = Lang::Zh;
    return true;
  }
  return false;
}

Lang lang_from_env_string(const std::string& locale) {
  Lang lang = Lang::En;
  if (lang_from_name(locale, &lang)) return lang;
  return Lang::En;  // C, POSIX, empty, or a language mdt does not speak
}

const char* lang_list() { return "auto, en, zh"; }

const char* lang_name(Lang lang) { return lang == Lang::Zh ? "zh" : "en"; }

Lang language() { return g_lang; }
void set_language(Lang lang) { g_lang = lang; }

std::string language_source() { return g_source; }
void set_language_source(const std::string& source) { g_source = source; }

const char* tr(const char* en, const char* zh) { return g_lang == Lang::Zh ? zh : en; }

}  // namespace mdt
