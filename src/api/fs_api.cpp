#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "api.h"
#include "include/cef_parser.h"
#include "util.h"

namespace yati {
namespace api {

namespace {

namespace fs = std::filesystem;

const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const std::string& input) {
  std::string out;
  out.reserve(((input.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 2 < input.size()) {
    const uint32_t n = (static_cast<uint8_t>(input[i]) << 16) |
                       (static_cast<uint8_t>(input[i + 1]) << 8) |
                       static_cast<uint8_t>(input[i + 2]);
    out += kBase64Chars[(n >> 18) & 0x3F];
    out += kBase64Chars[(n >> 12) & 0x3F];
    out += kBase64Chars[(n >> 6) & 0x3F];
    out += kBase64Chars[n & 0x3F];
    i += 3;
  }
  if (i + 1 == input.size()) {
    const uint32_t n = static_cast<uint8_t>(input[i]) << 16;
    out += kBase64Chars[(n >> 18) & 0x3F];
    out += kBase64Chars[(n >> 12) & 0x3F];
    out += "==";
  } else if (i + 2 == input.size()) {
    const uint32_t n = (static_cast<uint8_t>(input[i]) << 16) |
                       (static_cast<uint8_t>(input[i + 1]) << 8);
    out += kBase64Chars[(n >> 18) & 0x3F];
    out += kBase64Chars[(n >> 12) & 0x3F];
    out += kBase64Chars[(n >> 6) & 0x3F];
    out += '=';
  }
  return out;
}

int Base64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

std::string Base64Decode(const std::string& input) {
  std::string out;
  out.reserve(input.size() / 4 * 3);
  uint32_t buffer = 0;
  int bits = 0;
  for (const char c : input) {
    if (c == '=') {
      break;
    }
    const int value = Base64Value(c);
    if (value < 0) {
      continue;
    }
    buffer = (buffer << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buffer >> bits) & 0xFF);
    }
  }
  return out;
}

// Expands a leading "~" and removes trailing separators.
std::string ExpandPath(const std::string& raw) {
  std::string path = raw;
  if (path == "~" || path.rfind("~/", 0) == 0) {
    const char* home = std::getenv("HOME");
    path = std::string(home ? home : "/") + path.substr(1);
  }
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

bool ReadFileBytes(const std::string& path, std::string* out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }
  std::string content((std::istreambuf_iterator<char>(stream)),
                      std::istreambuf_iterator<char>());
  *out = std::move(content);
  return true;
}

CefRefPtr<CefDictionaryValue> StatToDict(const std::string& path) {
  CefRefPtr<CefDictionaryValue> dict = CefDictionaryValue::Create();
  dict->SetString("path", path);

  struct stat info {};
  if (stat(path.c_str(), &info) != 0) {
    dict->SetBool("exists", false);
    return dict;
  }

  dict->SetBool("exists", true);
  dict->SetBool("isFile", S_ISREG(info.st_mode));
  dict->SetBool("isDirectory", S_ISDIR(info.st_mode));
  dict->SetBool("isSymlink", S_ISLNK(info.st_mode));
  dict->SetDouble("size", static_cast<double>(info.st_size));
  dict->SetDouble("mode", static_cast<double>(info.st_mode & 07777));
  dict->SetDouble("mtimeMs",
                  static_cast<double>(info.st_mtim.tv_sec) * 1000.0 +
                      static_cast<double>(info.st_mtim.tv_nsec) / 1e6);
  dict->SetDouble("ctimeMs",
                  static_cast<double>(info.st_ctim.tv_sec) * 1000.0 +
                      static_cast<double>(info.st_ctim.tv_nsec) / 1e6);
  return dict;
}

// Wraps an std::filesystem operation, turning exceptions into IPC errors.
template <typename Fn>
void Guarded(const CefRefPtr<Call>& call, Fn&& fn) {
  try {
    fn();
  } catch (const fs::filesystem_error& error) {
    call->Reject(kIoError, error.what());
  } catch (const std::exception& error) {
    call->Reject(kInternal, error.what());
  }
}

}  // namespace

void RegisterFsApi(Registry* registry) {
  registry->Add("fs.readFile", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    const std::string path = ExpandPath(raw);
    std::string content;
    if (!ReadFileBytes(path, &content)) {
      call->Reject(kNotFound, "Cannot read file: " + path);
      return;
    }
    const std::string encoding = call->String("encoding", "utf8");
    CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
    result->SetString("path", path);
    result->SetString("encoding", encoding);
    result->SetInt("size", static_cast<int>(content.size()));
    result->SetString("data",
                      encoding == "base64" ? Base64Encode(content) : content);
    call->Resolve(result);
  });

  registry->Add("fs.writeFile", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    const std::string path = ExpandPath(raw);
    const std::string encoding = call->String("encoding", "utf8");
    std::string data = call->String("data");
    if (encoding == "base64") {
      data = Base64Decode(data);
    }

    Guarded(call, [&]() {
      const fs::path target(path);
      if (call->Bool("mkdirParents", true) && target.has_parent_path()) {
        fs::create_directories(target.parent_path());
      }
      std::ofstream stream(path, std::ios::binary |
                                    (call->Bool("append", false) ? std::ios::app
                                                                 : std::ios::trunc));
      if (!stream) {
        call->Reject(kIoError, "Cannot open for writing: " + path);
        return;
      }
      stream.write(data.data(), static_cast<std::streamsize>(data.size()));
      if (!stream) {
        call->Reject(kIoError, "Write failed: " + path);
        return;
      }
      CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
      result->SetString("path", path);
      result->SetInt("bytes", static_cast<int>(data.size()));
      call->Resolve(result);
    });
  });

  registry->Add("fs.readJson", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    const std::string path = ExpandPath(raw);
    std::string content;
    if (!ReadFileBytes(path, &content)) {
      call->Reject(kNotFound, "Cannot read file: " + path);
      return;
    }
    CefRefPtr<CefValue> parsed = CefParseJSON(ToCef(content), JSON_PARSER_RFC);
    if (!parsed) {
      call->Reject(kInvalidParams, "Invalid JSON in " + path);
      return;
    }
    // Resolve the parsed document directly.
    call->Resolve(parsed);
  });

  registry->Add("fs.writeJson", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    const std::string path = ExpandPath(raw);
    CefRefPtr<CefDictionaryValue> params = call->params();
    if (!params || !params->HasKey("data")) {
      call->Reject(kInvalidParams, "Missing \"data\"");
      return;
    }
    const std::string json =
        ToUtf8(CefWriteJSON(params->GetValue("data"), JSON_WRITER_PRETTY_PRINT));

    Guarded(call, [&]() {
      const fs::path target(path);
      if (call->Bool("mkdirParents", true) && target.has_parent_path()) {
        fs::create_directories(target.parent_path());
      }
      std::ofstream stream(path, std::ios::binary | std::ios::trunc);
      if (!stream) {
        call->Reject(kIoError, "Cannot open for writing: " + path);
        return;
      }
      stream << json;
      CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
      result->SetString("path", path);
      result->SetInt("bytes", static_cast<int>(json.size()));
      call->Resolve(result);
    });
  });

  registry->Add("fs.readDir", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    const std::string path = ExpandPath(raw);
    Guarded(call, [&]() {
      std::error_code error;
      if (!fs::is_directory(path, error)) {
        call->Reject(kNotFound, "Not a directory: " + path);
        return;
      }
      std::vector<std::string> names;
      for (const auto& entry : fs::directory_iterator(path, error)) {
        names.push_back(entry.path().filename().string());
      }
      std::sort(names.begin(), names.end());

      CefRefPtr<CefListValue> list = CefListValue::Create();
      int index = 0;
      for (const auto& name : names) {
        const std::string child = path + "/" + name;
        CefRefPtr<CefDictionaryValue> item = StatToDict(child);
        item->SetString("name", name);
        list->SetDictionary(index++, item);
      }
      CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
      result->SetString("path", path);
      result->SetList("entries", list);
      call->Resolve(result);
    });
  });

  registry->Add("fs.stat", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    call->Resolve(StatToDict(ExpandPath(raw)));
  });

  registry->Add("fs.exists", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    std::error_code error;
    call->ResolveBool(fs::exists(ExpandPath(raw), error));
  });

  registry->Add("fs.mkdir", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    const std::string path = ExpandPath(raw);
    Guarded(call, [&]() {
      if (call->Bool("recursive", true)) {
        fs::create_directories(path);
      } else {
        fs::create_directory(path);
      }
      call->ResolveBool(true);
    });
  });

  registry->Add("fs.remove", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    const std::string path = ExpandPath(raw);
    Guarded(call, [&]() {
      call->ResolveInt(static_cast<int>(fs::remove_all(path)));
    });
  });

  registry->Add("fs.rename", [](const CefRefPtr<Call>& call) {
    std::string from;
    std::string to;
    if (!call->RequireString("from", &from) || !call->RequireString("to", &to)) {
      return;
    }
    Guarded(call, [&]() {
      fs::rename(ExpandPath(from), ExpandPath(to));
      call->ResolveBool(true);
    });
  });

  registry->Add("fs.copy", [](const CefRefPtr<Call>& call) {
    std::string from;
    std::string to;
    if (!call->RequireString("from", &from) || !call->RequireString("to", &to)) {
      return;
    }
    const std::string source = ExpandPath(from);
    const std::string target = ExpandPath(to);
    const bool recursive = call->Bool("recursive", true);
    const bool overwrite = call->Bool("overwrite", true);
    Guarded(call, [&]() {
      const auto options = recursive
                               ? (overwrite ? fs::copy_options::recursive |
                                                  fs::copy_options::overwrite_existing
                                            : fs::copy_options::recursive)
                               : (overwrite ? fs::copy_options::overwrite_existing
                                            : fs::copy_options::none);
      fs::copy(source, target, options);
      call->ResolveBool(true);
    });
  });

  registry->Add("fs.realpath", [](const CefRefPtr<Call>& call) {
    std::string raw;
    if (!call->RequireString("path", &raw)) {
      return;
    }
    Guarded(call, [&]() {
      call->ResolveStr(fs::weakly_canonical(ExpandPath(raw)).string());
    });
  });

  registry->Add("fs.tempDir", [](const CefRefPtr<Call>& call) {
    std::error_code error;
    call->ResolveStr(fs::temp_directory_path(error).string());
  });
}

}  // namespace api
}  // namespace yati
