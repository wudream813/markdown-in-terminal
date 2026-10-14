// i18n.h : the two languages the user interface speaks.
//
// Not gettext.  mdt is one self-contained binary with no data files to install,
// so the strings sit at the call site - tr("cannot read ", "无法读取 ") - with
// the English and the Chinese next to each other, which also means a string
// cannot drift away from its translation into another file.
//
// The language is decided before the first message can be printed: --lang
// beats MDT_LANG, which beats the config file, which beats LC_ALL /
// LC_MESSAGES / LANG.  `LANG=zh_CN.UTF-8 mdt 文档.md` therefore works out of
// the box, and `--lang=en` gets English back even then.
//
// What is translated: --help, the errors on the command line and from files,
// the config file's complaints, and the status line of the interactive reader.
// What is not: --diag and --list-caps (they are bug-report material and stay
// greppable in one language) and the placeholders the renderer draws inside a
// document ("[remote image]"), which are part of the page, not of the UI.
#pragma once

#include <string>

namespace mdt {

enum class Lang { En, Zh };

// True when `name` is a language mdt speaks.  Both "zh" and a full locale
// ("zh_CN.UTF-8", "zh-Hans", "zh-TW") are accepted; "auto" is not a language
// and is dealt with by the caller.
bool lang_from_name(const std::string& name, Lang* out);

// The language of a locale string from the environment: "zh_CN.UTF-8" -> Zh,
// "en_US.UTF-8" -> En, "C"/"POSIX"/anything unrecognised -> En.  Never fails:
// the environment is not an explicit request, so it cannot be wrong.
Lang lang_from_env_string(const std::string& locale);

// "auto, en, zh" - what --lang and the config file accept.
const char* lang_list();

// "en" / "zh" - what --diag prints.
const char* lang_name(Lang lang);

Lang language();
void set_language(Lang lang);

// What decided the language ("--lang", "LANG=zh_CN.UTF-8", "config", ...).
// Shown by --diag so a bug report says why the reader speaks what it speaks.
std::string language_source();
void set_language_source(const std::string& source);

// The whole mechanism: give both, get the current one back.
const char* tr(const char* en, const char* zh);

}  // namespace mdt
