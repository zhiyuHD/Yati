#include "scheme.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>

#include "include/cef_request.h"
#include "include/cef_resource_handler.h"
#include "include/cef_response.h"
#include "include/cef_scheme.h"
#include "util.h"

namespace yati {

namespace {

namespace fs = std::filesystem;

std::string MimeFor(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) {
    return "application/octet-stream";
  }
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  static const std::map<std::string, std::string> kTypes = {
      {"html", "text/html"},
      {"htm", "text/html"},
      {"js", "text/javascript"},
      {"mjs", "text/javascript"},
      {"css", "text/css"},
      {"json", "application/json"},
      {"txt", "text/plain"},
      {"map", "application/json"},
      {"svg", "image/svg+xml"},
      {"png", "image/png"},
      {"jpg", "image/jpeg"},
      {"jpeg", "image/jpeg"},
      {"gif", "image/gif"},
      {"webp", "image/webp"},
      {"avif", "image/avif"},
      {"ico", "image/x-icon"},
      {"woff", "font/woff"},
      {"woff2", "font/woff2"},
      {"ttf", "font/ttf"},
      {"otf", "font/otf"},
      {"wasm", "application/wasm"},
      {"webmanifest", "application/manifest+json"},
      {"xml", "application/xml"},
      {"mp4", "video/mp4"},
      {"webm", "video/webm"},
  };
  const auto it = kTypes.find(ext);
  return it == kTypes.end() ? "application/octet-stream" : it->second;
}

std::string PercentDecode(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size() &&
        std::isxdigit(static_cast<unsigned char>(value[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(value[i + 2]))) {
      out += static_cast<char>(std::stoi(value.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += value[i];
    }
  }
  return out;
}

// 只取 URL 的路径部分：app://yati/a/b.js?x=1 -> /a/b.js
std::string PathOf(const std::string& url) {
  const size_t scheme = url.find("://");
  const size_t slash = url.find('/', scheme == std::string::npos ? 0 : scheme + 3);
  if (slash == std::string::npos) {
    return "/";
  }
  std::string path = url.substr(slash);
  const size_t query = path.find_first_of("?#");
  if (query != std::string::npos) {
    path = path.substr(0, query);
  }
  return PercentDecode(path);
}

bool ReadWholeFile(const fs::path& path, std::string* out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }
  out->assign(std::istreambuf_iterator<char>(stream),
              std::istreambuf_iterator<char>());
  return true;
}

class FileResourceHandler : public CefResourceHandler {
 public:
  FileResourceHandler(std::string root, std::string url_path)
      : root_(std::move(root)), url_path_(std::move(url_path)) {}

  bool Open(CefRefPtr<CefRequest> request,
            bool& handle_request,
            CefRefPtr<CefCallback> callback) override {
    handle_request = true;
    Resolve();
    return true;
  }

  void GetResponseHeaders(CefRefPtr<CefResponse> response,
                          int64_t& response_length,
                          CefString& redirect_url) override {
    response->SetStatus(status_);
    response->SetStatusText(ToCef(status_ == 200 ? "OK" : "Not Found"));
    // SetMimeType 只接受基础类型；带 "; charset=..." 会让 Chromium 的类型判断
    // 匹配不上，从而回落到纯文本查看器（HTML 直接被当文字显示）。
    response->SetMimeType(ToCef(mime_));
    const std::string content_type =
        mime_.rfind("text/", 0) == 0 ? mime_ + "; charset=utf-8" : mime_;
    response->SetHeaderByName(ToCef("Content-Type"), ToCef(content_type), true);
    // 开发期改动立即可见；发布包内容不会变，缓存也没有收益。
    response->SetHeaderByName(ToCef("Cache-Control"), ToCef("no-cache"), true);
    response_length = static_cast<int64_t>(body_.size());
  }

  bool Read(void* data_out,
            int bytes_to_read,
            int& bytes_read,
            CefRefPtr<CefResourceReadCallback> callback) override {
    return CopyOut(data_out, bytes_to_read, bytes_read);
  }

  // 兼容旧调用路径（Read 被绕过时 CEF 会回落到这里）。
  bool ReadResponse(void* data_out,
                    int bytes_to_read,
                    int& bytes_read,
                    CefRefPtr<CefCallback> callback) override {
    return CopyOut(data_out, bytes_to_read, bytes_read);
  }

  void Cancel() override {}

  IMPLEMENT_REFCOUNTING(FileResourceHandler);

 private:
  bool CopyOut(void* data_out, int bytes_to_read, int& bytes_read) {
    const size_t remaining = body_.size() - offset_;
    const size_t count =
        std::min(static_cast<size_t>(bytes_to_read), remaining);
    if (count == 0) {
      bytes_read = 0;
      return false;
    }
    std::memcpy(data_out, body_.data() + offset_, count);
    offset_ += count;
    bytes_read = static_cast<int>(count);
    return true;
  }

  void Resolve() {
    const fs::path root = fs::path(root_).lexically_normal();
    std::string rel = url_path_;
    if (rel.empty() || rel == "/") {
      rel = "/index.html";
    }

    fs::path target = root / fs::path(rel.substr(1));
    std::error_code error;

    if (fs::is_directory(target, error)) {
      target /= "index.html";
    }

    // 目录式路径（/about）回退到 /about/index.html，够 Next 的静态导出用。
    if (!fs::exists(target, error)) {
      fs::path fallback = target / "index.html";
      fs::path html = target;
      html += ".html";
      if (fs::exists(fallback, error)) {
        target = fallback;
      } else if (fs::exists(html, error)) {
        target = html;
      }
    }

    const fs::path norm = target.lexically_normal();
    // 路径穿越防护：只允许根目录之下的文件。
    const std::string root_str = root.string();
    const std::string norm_str = norm.string();
    const bool inside =
        norm_str.compare(0, root_str.size(), root_str) == 0 &&
        (norm_str.size() == root_str.size() || norm_str[root_str.size()] == '/');

    if (!inside || !fs::is_regular_file(norm, error) ||
        !ReadWholeFile(norm, &body_)) {
      status_ = 404;
      mime_ = "text/plain";
      body_ = "404 Not Found: " + rel + "\n";
      return;
    }

    status_ = 200;
    mime_ = MimeFor(norm_str);
  }

  std::string root_;
  std::string url_path_;
  std::string body_;
  std::string mime_ = "text/plain";
  int status_ = 200;
  size_t offset_ = 0;
};

class AppSchemeFactory : public CefSchemeHandlerFactory {
 public:
  explicit AppSchemeFactory(std::string root) : root_(std::move(root)) {}

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& scheme_name,
                                       CefRefPtr<CefRequest> request) override {
    if (!request) {
      return nullptr;
    }
    return new FileResourceHandler(root_, PathOf(ToUtf8(request->GetURL())));
  }

  IMPLEMENT_REFCOUNTING(AppSchemeFactory);

 private:
  std::string root_;
};

}  // namespace

void RegisterAppScheme(const std::string& root) {
  static bool registered = false;
  if (registered) {
    return;
  }
  registered = true;

  CefRegisterSchemeHandlerFactory(ToCef("app"), ToCef("yati"),
                                  new AppSchemeFactory(root));
}

}  // namespace yati
