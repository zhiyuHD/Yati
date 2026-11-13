#include "manifest.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "util.h"

namespace yati {

namespace fs = std::filesystem;

namespace {

std::string Trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return std::string();
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

bool ReadFile(const std::string& path, std::string* out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }
  *out = std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

std::string ResolveAppDir(const std::string& dir) {
  std::error_code error;
  const fs::path path = fs::weakly_canonical(fs::absolute(dir), error);
  if (error || !fs::is_directory(path, error)) {
    return std::string();
  }
  return path.string();
}

bool LoadManifest(const std::string& dir, AppManifest* out) {
  const std::string root = ResolveAppDir(dir);
  if (root.empty()) {
    std::fprintf(stderr, "[yati] 不是有效的 App 目录：%s\n", dir.c_str());
    return false;
  }

  out->root = root;
  out->id = fs::path(root).filename().string();

  const std::string manifest_path = root + "/yati.json";
  std::string text;
  if (!ReadFile(manifest_path, &text)) {
    return false;  // 没有清单：交给调用方决定如何处理
  }

  CefRefPtr<CefValue> parsed = CefParseJSON(ToCef(text), JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) {
    std::fprintf(stderr, "[yati] %s 不是合法的 JSON 对象\n", manifest_path.c_str());
    return false;
  }
  CefRefPtr<CefDictionaryValue> dict = parsed->GetDictionary();

  const std::string id = Trim(ToUtf8(dict->GetString("id")));
  if (!id.empty()) {
    out->id = id;
  }

  const std::string name = Trim(ToUtf8(dict->GetString("name")));
  out->name = name.empty() ? out->id : name;
  out->version = Trim(ToUtf8(dict->GetString("version")));

  const std::string entry = Trim(ToUtf8(dict->GetString("main")));
  if (!entry.empty()) {
    out->entry = entry;
  }

  // 站点根取 entry 所在目录：Next 之类的产物在 out/ 下，绝对路径 /_next/...
  // 必须落在 out/ 里，而不是 App 根。
  {
    const fs::path entry_path(out->entry);
    const std::string dir = entry_path.parent_path().string();
    out->web_root = dir.empty() ? out->root : (root + "/" + dir);
    out->entry_file = entry_path.filename().string();
  }

  out->title = Trim(ToUtf8(dict->GetString("title")));
  if (out->title.empty()) {
    out->title = out->name;
  }

  if (dict->HasKey("window") && dict->GetType("window") == VTYPE_DICTIONARY) {
    CefRefPtr<CefDictionaryValue> window = dict->GetDictionary("window");
    const std::string window_title = Trim(ToUtf8(window->GetString("title")));
    if (!window_title.empty()) {
      out->title = window_title;
    }
    out->width = window->GetInt("width");
    out->height = window->GetInt("height");
  }

  out->valid = true;
  return true;
}

}  // namespace yati
