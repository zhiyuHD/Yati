#include <sys/utsname.h>
#include <unistd.h>

#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "api.h"
#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/cef_cookie.h"
#include "include/cef_path_util.h"
#include "include/cef_task.h"
#include "include/cef_version.h"
#include "include/wrapper/cef_helpers.h"
#include "ipc.h"
#include "options.h"
#include "process.h"
#include "util.h"
#include "version.h"

namespace yati {
namespace api {

namespace {

// 超时与轮询间隔集中在这里，不散落在调用点。
constexpr int kDefaultExecTimeoutMs = 10000;
constexpr int kClipboardTimeoutMs = 2000;
constexpr int kCookieFlushDelayMs = 250;
constexpr int64_t kOneYearInMicroseconds = 365LL * 24 * 3600 * 1000000;

void SetStr(cef_string_t* target, const std::string& value) {
  CefString(target).FromString(value);
}

std::string StrOf(const cef_string_t& value) {
  return CefString(&value).ToString();
}

std::string EnvOr(const char* name, const std::string& fallback) {
  const char* value = std::getenv(name);
  return (value && *value) ? std::string(value) : fallback;
}

std::string PathFor(cef_path_key_t key) {
  CefString path;
  if (CefGetPath(key, path)) {
    return ToUtf8(path);
  }
  return std::string();
}

bool IsWayland() {
  const char* session = std::getenv("WAYLAND_DISPLAY");
  return session && *session;
}

// Resolves a call with a pre-computed result on the UI thread. Used by work
// that runs off-thread (child processes).
class ResolveTask : public CefTask {
 public:
  ResolveTask(CefRefPtr<Call> call, CefRefPtr<CefValue> result)
      : call_(std::move(call)), result_(std::move(result)) {}

  void Execute() override { call_->Resolve(result_); }

  IMPLEMENT_REFCOUNTING(ResolveTask);

 private:
  CefRefPtr<Call> call_;
  CefRefPtr<CefValue> result_;
};

// --- Cookies ---------------------------------------------------------------

CefRefPtr<CefDictionaryValue> CookieToDict(const CefCookie& cookie) {
  CefRefPtr<CefDictionaryValue> dict = CefDictionaryValue::Create();
  dict->SetString("name", StrOf(cookie.name));
  dict->SetString("value", StrOf(cookie.value));
  dict->SetString("domain", StrOf(cookie.domain));
  dict->SetString("path", StrOf(cookie.path));
  dict->SetBool("secure", cookie.secure != 0);
  dict->SetBool("httpOnly", cookie.httponly != 0);
  dict->SetBool("hasExpires", cookie.has_expires != 0);
  if (cookie.has_expires != 0) {
    // cef_basetime_t: microseconds since the Windows epoch (1601-01-01).
    constexpr double kEpochDeltaMs = 11644473600000.0;  // 1601 -> 1970
    const double expires_ms =
        static_cast<double>(cookie.expires.val) / 1000.0 - kEpochDeltaMs;
    dict->SetDouble("expires", expires_ms / 1000.0);
    dict->SetDouble("expiresMs", expires_ms);
  }
  return dict;
}

// CefCookieManager does not report when VisitAllCookies() has finished, so the
// visitor accumulates results and a short one-shot task flushes them.
class CookieVisitor : public CefCookieVisitor {
 public:
  CookieVisitor(CefRefPtr<Call> call, std::string domain_filter)
      : call_(std::move(call)),
        domain_filter_(std::move(domain_filter)),
        items_(CefListValue::Create()) {}

  bool Visit(const CefCookie& cookie, int count, int total,
             bool& delete_cookie) override {
    delete_cookie = false;
    if (!domain_filter_.empty()) {
      const std::string domain = StrOf(cookie.domain);
      if (domain.find(domain_filter_) == std::string::npos) {
        return true;
      }
    }
    items_->SetDictionary(items_->GetSize(), CookieToDict(cookie));
    return true;
  }

  void Flush() {
    if (flushed_) {
      return;
    }
    flushed_ = true;
    call_->Resolve(items_);
  }

  IMPLEMENT_REFCOUNTING(CookieVisitor);

 private:
  CefRefPtr<Call> call_;
  std::string domain_filter_;
  CefRefPtr<CefListValue> items_;
  bool flushed_ = false;
};

class FlushCookiesTask : public CefTask {
 public:
  explicit FlushCookiesTask(CefRefPtr<CookieVisitor> visitor)
      : visitor_(std::move(visitor)) {}
  void Execute() override { visitor_->Flush(); }
  IMPLEMENT_REFCOUNTING(FlushCookiesTask);

 private:
  CefRefPtr<CookieVisitor> visitor_;
};

// Extracts a bare host name from a URL for cookie filtering.
std::string HostOf(const std::string& url) {
  const size_t scheme = url.find("://");
  const size_t start = scheme == std::string::npos ? 0 : scheme + 3;
  size_t end = url.find('/', start);
  if (end == std::string::npos) {
    end = url.size();
  }
  std::string host = url.substr(start, end - start);
  const size_t at = host.rfind('@');
  if (at != std::string::npos) {
    host = host.substr(at + 1);
  }
  const size_t colon = host.find(':');
  if (colon != std::string::npos) {
    host = host.substr(0, colon);
  }
  return host;
}

CefRefPtr<CefDictionaryValue> AppInfo() {
  CefRefPtr<CefDictionaryValue> info = CefDictionaryValue::Create();
  info->SetString("name", YATI_PRODUCT_NAME);
  info->SetString("version", YATI_VERSION);
  info->SetString("cef", CEF_VERSION);
  info->SetInt("chromium", CHROME_VERSION_MAJOR);
#if defined(__linux__)
  info->SetString("platform", "linux");
#elif defined(__APPLE__)
  info->SetString("platform", "macos");
#elif defined(_WIN32)
  info->SetString("platform", "windows");
#else
  info->SetString("platform", "unknown");
#endif
  info->SetInt("pid", static_cast<int>(getpid()));

  struct utsname uts {};
  if (uname(&uts) == 0) {
    info->SetString("arch", uts.machine);
    info->SetString("kernel", uts.release);
  }
  info->SetBool("wayland", IsWayland());

  // App 包模式下这些字段来自 yati.json；内置页面运行时回落为引擎自身。
  const Options& options = GetOptions();
  CefRefPtr<CefDictionaryValue> app = CefDictionaryValue::Create();
  app->SetString("id", options.app_id.empty() ? YATI_APP_ID : options.app_id);
  app->SetString("name",
                 options.app_name.empty() ? YATI_PRODUCT_NAME : options.app_name);
  app->SetString("version",
                 options.app_version.empty() ? YATI_VERSION : options.app_version);
  app->SetBool("bundled", !options.app_dir.empty());
  app->SetString("path", options.app_dir);
  info->SetDictionary("app", app);

  // 引擎自身版本（App 版本在上面的 app 字典里）。
  CefRefPtr<CefDictionaryValue> engine = CefDictionaryValue::Create();
  engine->SetString("name", YATI_PRODUCT_NAME);
  engine->SetString("version", YATI_VERSION);
  info->SetDictionary("engine", engine);

  return info;
}

}  // namespace

void RegisterCoreApi(Registry* registry) {
  // --- app ---------------------------------------------------------------
  registry->Add("app.info", [](const CefRefPtr<Call>& call) {
    call->Resolve(AppInfo());
  });

  registry->Add("app.paths", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefDictionaryValue> paths = CefDictionaryValue::Create();
    paths->SetString("home", EnvOr("HOME", "/"));
    paths->SetString("userData", PathFor(PK_USER_DATA));
    paths->SetString("temp", PathFor(PK_DIR_TEMP));
    paths->SetString("cwd", PathFor(PK_DIR_CURRENT));
    paths->SetString("exe", PathFor(PK_FILE_EXE));
    paths->SetString("resources", PathFor(PK_DIR_RESOURCES));
    paths->SetString("app", GetOptions().app_dir);
    call->Resolve(paths);
  });

  registry->Add("app.quit", [](const CefRefPtr<Call>& call) {
    call->ResolveStr("quitting");
    CefQuitMessageLoop();
  });

  registry->Add("app.channels", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefListValue> list = CefListValue::Create();
    int index = 0;
    for (const auto& channel : Registry::Get()->channels()) {
      list->SetString(index++, channel);
    }
    call->Resolve(list);
  });

  registry->Add("app.broadcast", [](const CefRefPtr<Call>& call) {
    const std::string event = call->String("event");
    if (event.empty()) {
      call->Reject(kInvalidParams, "Missing \"event\"");
      return;
    }
    Ipc::Get()->Emit(event, call->params() ? call->params()->GetValue("payload")
                                           : nullptr);
    call->ResolveBool(true);
  });

  // --- shell -------------------------------------------------------------
  registry->Add("shell.openExternal", [](const CefRefPtr<Call>& call) {
    std::string url;
    if (!call->RequireString("url", &url)) {
      return;
    }
    if (!CommandExists("xdg-open")) {
      call->Reject(kNotSupported, "xdg-open is not available");
      return;
    }
    if (!SpawnDetached({"xdg-open", url})) {
      call->Reject(kInternal, "failed to launch xdg-open");
      return;
    }
    call->ResolveBool(true);
  });

  registry->Add("shell.openPath", [](const CefRefPtr<Call>& call) {
    std::string path;
    if (!call->RequireString("path", &path)) {
      return;
    }
    if (!CommandExists("xdg-open")) {
      call->Reject(kNotSupported, "xdg-open is not available");
      return;
    }
    if (!SpawnDetached({"xdg-open", path})) {
      call->Reject(kInternal, "failed to launch xdg-open");
      return;
    }
    call->ResolveBool(true);
  });

  registry->Add("shell.showItemInFolder", [](const CefRefPtr<Call>& call) {
    std::string path;
    if (!call->RequireString("path", &path)) {
      return;
    }
    const size_t slash = path.find_last_of('/');
    const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
    if (!SpawnDetached({"xdg-open", dir.empty() ? "/" : dir})) {
      call->Reject(kInternal, "failed to launch xdg-open");
      return;
    }
    call->ResolveBool(true);
  });

  // Runs a child process without blocking the UI thread: the wait happens on a
  // worker thread and the answer is posted back to the UI thread.
  registry->Add("shell.exec", [](const CefRefPtr<Call>& call) {
    std::vector<std::string> argv;
    CefRefPtr<CefListValue> command_list = call->ListParam("command");
    if (command_list) {
      for (size_t i = 0; i < command_list->GetSize(); ++i) {
        argv.push_back(ToUtf8(command_list->GetString(i)));
      }
    } else {
      const std::string command = call->String("command");
      if (command.empty()) {
        call->Reject(kInvalidParams, "Missing \"command\"");
        return;
      }
      argv.push_back("/bin/sh");
      argv.push_back("-c");
      argv.push_back(command);
    }
    if (argv.empty()) {
      call->Reject(kInvalidParams, "\"command\" must not be empty");
      return;
    }

    const int timeout_ms = call->Int("timeoutMs", kDefaultExecTimeoutMs);
    const std::string input = call->String("stdin");

    std::thread([argv, input, timeout_ms, call]() {
      const ProcessResult result = RunProcess(argv, input, timeout_ms);

      CefRefPtr<CefDictionaryValue> reply = CefDictionaryValue::Create();
      reply->SetInt("code", result.code);
      reply->SetString("stdout", result.out);
      reply->SetString("stderr", result.err);
      reply->SetBool("timedOut", result.timed_out);
      reply->SetBool("spawnFailed", result.spawn_failed);

      CefPostTask(TID_UI, new ResolveTask(call, Wrap(reply)));
    }).detach();
  });

  // --- clipboard ---------------------------------------------------------
  registry->Add("clipboard.readText", [](const CefRefPtr<Call>& call) {
    std::vector<std::string> argv;
    if (IsWayland() && CommandExists("wl-paste")) {
      argv = {"wl-paste", "--no-newline"};
    } else if (CommandExists("xclip")) {
      argv = {"xclip", "-selection", "clipboard", "-o"};
    } else {
      call->Reject(kNotSupported, "no clipboard tool (wl-paste / xclip) found");
      return;
    }
    const ProcessResult result =
        RunProcess(argv, std::string(), kClipboardTimeoutMs);
    if (result.spawn_failed) {
      call->Reject(kInternal, "failed to read the clipboard");
      return;
    }
    call->ResolveStr(result.out);
  });

  registry->Add("clipboard.writeText", [](const CefRefPtr<Call>& call) {
    const std::string text = call->String("text");
    std::vector<std::string> argv;
    if (IsWayland() && CommandExists("wl-copy")) {
      argv = {"wl-copy"};
    } else if (CommandExists("xclip")) {
      argv = {"xclip", "-selection", "clipboard"};
    } else {
      call->Reject(kNotSupported, "no clipboard tool (wl-copy / xclip) found");
      return;
    }
    const ProcessResult result = RunProcess(argv, text, kClipboardTimeoutMs);
    if (result.spawn_failed || result.code != 0) {
      call->Reject(kInternal, "failed to write the clipboard");
      return;
    }
    call->ResolveBool(true);
  });

  // --- notifications -----------------------------------------------------
  registry->Add("notify.send", [](const CefRefPtr<Call>& call) {
    std::string title;
    if (!call->RequireString("title", &title)) {
      return;
    }
    if (!CommandExists("notify-send")) {
      call->Reject(kNotSupported, "notify-send is not available");
      return;
    }
    std::vector<std::string> argv = {"notify-send", "-a", YATI_PRODUCT_NAME};
    const std::string urgency = call->String("urgency");
    if (!urgency.empty()) {
      argv.push_back("-u");
      argv.push_back(urgency);
    }
    const int expires = call->Int("expireMs", 0);
    if (expires > 0) {
      argv.push_back("-t");
      argv.push_back(std::to_string(expires));
    }
    argv.push_back(title);
    argv.push_back(call->String("message"));

    if (!SpawnDetached(argv)) {
      call->Reject(kInternal, "failed to launch notify-send");
      return;
    }
    call->ResolveBool(true);
  });

  // --- session cookies ---------------------------------------------------
  registry->Add("session.cookies.get", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefCookieManager> manager =
        CefCookieManager::GetGlobalManager(nullptr);
    if (!manager) {
      call->Reject(kInternal, "no cookie manager");
      return;
    }
    const std::string url = call->String("url");
    const std::string domain = url.empty() ? std::string() : HostOf(url);

    CefRefPtr<CookieVisitor> visitor(new CookieVisitor(call, domain));
    if (!manager->VisitAllCookies(visitor)) {
      call->Reject(kInternal, "VisitAllCookies failed");
      return;
    }
    // Give the (asynchronous) visit a moment, then flush the collected list.
    CefPostDelayedTask(TID_UI, new FlushCookiesTask(visitor),
                       kCookieFlushDelayMs);
  });

  registry->Add("session.cookies.set", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefCookieManager> manager =
        CefCookieManager::GetGlobalManager(nullptr);
    if (!manager) {
      call->Reject(kInternal, "no cookie manager");
      return;
    }
    std::string url;
    if (!call->RequireString("url", &url)) {
      return;
    }
    CefCookie cookie;
    SetStr(&cookie.name, call->String("name"));
    SetStr(&cookie.value, call->String("value"));
    SetStr(&cookie.domain, call->String("domain"));
    SetStr(&cookie.path, call->String("path", "/"));

    // Chromium needs an expiry; default to one year from now.
    if (call->Bool("session", false)) {
      cookie.has_expires = 0;
    } else {
      cookie.has_expires = 1;
      cookie.expires = cef_basetime_now();
      cookie.expires.val += kOneYearInMicroseconds;
    }

    if (!manager->SetCookie(url, cookie, nullptr)) {
      call->Reject(kInternal, "SetCookie failed");
      return;
    }
    call->ResolveBool(true);
  });

  registry->Add("session.cookies.remove", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefCookieManager> manager =
        CefCookieManager::GetGlobalManager(nullptr);
    if (!manager) {
      call->Reject(kInternal, "no cookie manager");
      return;
    }
    const bool ok = manager->DeleteCookies(ToCef(call->String("url")),
                                           ToCef(call->String("name")), nullptr);
    call->ResolveBool(ok);
  });

  registry->Add("session.cookies.clear", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefCookieManager> manager =
        CefCookieManager::GetGlobalManager(nullptr);
    if (!manager) {
      call->Reject(kInternal, "no cookie manager");
      return;
    }
    call->ResolveBool(manager->DeleteCookies(CefString(), CefString(), nullptr));
  });

  // --- devtools ----------------------------------------------------------
  registry->Add("devtools.open", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefBrowser> browser = call->browser();
    if (!browser) {
      call->Reject(kInternal, "no browser");
      return;
    }
    CefWindowInfo window_info;
    CefBrowserSettings settings;
    // With a CefBrowserView the window info is ignored and CEF opens DevTools
    // in its own window.
    browser->GetHost()->ShowDevTools(window_info, nullptr, settings, CefPoint());
    call->ResolveBool(true);
  });

  registry->Add("devtools.close", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefBrowser> browser = call->browser();
    if (!browser) {
      call->Reject(kInternal, "no browser");
      return;
    }
    browser->GetHost()->CloseDevTools();
    call->ResolveBool(true);
  });

  registry->Add("devtools.isOpen", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefBrowser> browser = call->browser();
    call->ResolveBool(browser ? browser->GetHost()->HasDevTools() : false);
  });
}

}  // namespace api
}  // namespace yati
