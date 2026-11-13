#include "window.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "client.h"
#include "include/cef_app.h"
#include "include/cef_path_util.h"
#include "include/views/cef_display.h"
#include "ipc.h"
#include "util.h"

namespace yati {

namespace {

struct Entry {
  int window_id = 0;
  CefRefPtr<CefWindow> window;
};

std::map<int, Entry>& Registry() {
  static std::map<int, Entry> registry;
  return registry;
}

int g_next_window_id = 1;

// 内置页面相对 exe 目录的路径。
constexpr const char kEntryPage[] = "webui/index.html";

std::string DefaultUrl() {
  CefString exe_dir;
  if (CefGetPath(PK_DIR_EXE, exe_dir)) {
    return "file://" + ToUtf8(exe_dir) + "/" + kEntryPage;
  }
  return std::string();
}

void EmitWindowEvent(const char* event, int window_id,
                     CefRefPtr<CefDictionaryValue> payload) {
  CefRefPtr<CefDictionaryValue> body =
      payload ? payload : CefDictionaryValue::Create();
  body->SetInt("windowId", window_id);
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(body);
  Ipc::Get()->Emit(event, value);
}

}  // namespace

void RegisterBrowserWindow(int browser_id, int window_id,
                           CefRefPtr<CefWindow> window) {
  Registry()[browser_id] = Entry{window_id, window};
}

void UnregisterBrowserWindow(int browser_id) {
  Registry().erase(browser_id);
}

bool FindWindowByBrowser(int browser_id, WindowRecord* out) {
  const auto it = Registry().find(browser_id);
  if (it == Registry().end()) {
    return false;
  }
  if (out) {
    out->window_id = it->second.window_id;
    out->browser_id = browser_id;
    out->window = it->second.window;
  }
  return true;
}

bool FindWindowById(int window_id, WindowRecord* out) {
  for (const auto& entry : Registry()) {
    if (entry.second.window_id == window_id) {
      if (out) {
        out->window_id = window_id;
        out->browser_id = entry.first;
        out->window = entry.second.window;
      }
      return true;
    }
  }
  return false;
}

std::vector<WindowRecord> AllWindows() {
  std::vector<WindowRecord> result;
  result.reserve(Registry().size());
  for (const auto& entry : Registry()) {
    result.push_back(WindowRecord{entry.second.window_id, entry.first,
                                  entry.second.window});
  }
  std::sort(result.begin(), result.end(),
            [](const WindowRecord& a, const WindowRecord& b) {
              return a.window_id < b.window_id;
            });
  return result;
}

int WindowIdForBrowser(int browser_id) {
  WindowRecord record;
  return FindWindowByBrowser(browser_id, &record) ? record.window_id : 0;
}

int CreateWindow(const Options& options) {
  const int window_id = g_next_window_id++;
  CefRefPtr<Window> delegate(new Window(options, window_id));
  CefRefPtr<CefWindow> window = CefWindow::CreateTopLevelWindow(delegate);
  if (!window) {
    std::fprintf(stderr, "[yati] failed to create window %d\n", window_id);
    return 0;
  }
  // CefWindowDelegate::OnWindowCreated() has already added the browser view.
  window->Show();
  return window_id;
}

Window::Window(const Options& options, int window_id)
    : options_(options), window_id_(window_id) {}

CefRect Window::GetInitialBounds(CefRefPtr<CefWindow> window) {
  CefRefPtr<CefDisplay> display = CefDisplay::GetPrimaryDisplay();
  CefRect work_area = display ? display->GetWorkArea() : CefRect(0, 0, 1920, 1080);

  const int width = std::min(options_.width, work_area.width);
  const int height = std::min(options_.height, work_area.height);
  const int x = work_area.x + (work_area.width - width) / 2;
  const int y = work_area.y + (work_area.height - height) / 2;
  return CefRect(x, y, width, height);
}

void Window::OnWindowCreated(CefRefPtr<CefWindow> window) {
  window_ = window;
  window->SetTitle(ToCef(options_.title));

  const std::string url = options_.url.empty() ? DefaultUrl() : options_.url;

  CefBrowserSettings browser_settings;
  CefRefPtr<CefBrowserView> browser_view = CefBrowserView::CreateBrowserView(
      new Client(), ToCef(url), browser_settings,
      /*extra_info=*/nullptr, /*request_context=*/nullptr, /*delegate=*/this);
  if (!browser_view) {
    std::fprintf(stderr, "[yati] failed to create the browser view\n");
    return;
  }

  window->AddChildView(browser_view);
}

void Window::OnWindowClosing(CefRefPtr<CefWindow> window) {
  EmitWindowEvent("window:closing", window_id_, nullptr);
}

void Window::OnWindowDestroyed(CefRefPtr<CefWindow> window) {
  window_ = nullptr;
  EmitWindowEvent("window:closed", window_id_, nullptr);
}

void Window::OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                                   const CefRect& new_bounds) {
  CefRefPtr<CefDictionaryValue> payload = CefDictionaryValue::Create();
  payload->SetInt("x", new_bounds.x);
  payload->SetInt("y", new_bounds.y);
  payload->SetInt("width", new_bounds.width);
  payload->SetInt("height", new_bounds.height);
  payload->SetBool("maximized", window && window->IsMaximized());
  payload->SetBool("fullscreen", window && window->IsFullscreen());
  EmitWindowEvent("window:bounds", window_id_, payload);
}

void Window::OnWindowActivationChanged(CefRefPtr<CefWindow> window, bool active) {
  EmitWindowEvent(active ? "window:focus" : "window:blur", window_id_, nullptr);
}

void Window::OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                              CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  if (!window_) {
    return;
  }
  RegisterBrowserWindow(browser->GetIdentifier(), window_id_, window_);

  CefRefPtr<CefDictionaryValue> payload = CefDictionaryValue::Create();
  payload->SetInt("browserId", browser->GetIdentifier());
  payload->SetString("title", options_.title);
  payload->SetString("url", browser->GetMainFrame()
                                ? browser->GetMainFrame()->GetURL()
                                : CefString());
  EmitWindowEvent("window:created", window_id_, payload);
}

void Window::OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                                CefRefPtr<CefBrowser> browser) {
  UnregisterBrowserWindow(browser->GetIdentifier());
  browser_ = nullptr;
}

}  // namespace yati
