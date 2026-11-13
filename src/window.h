#ifndef YATI_WINDOW_H_
#define YATI_WINDOW_H_

#include <map>
#include <vector>

#include "include/cef_base.h"
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_browser_view_delegate.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "options.h"

namespace yati {

// A window as seen by the API layer.
struct WindowRecord {
  int window_id = 0;
  int browser_id = 0;
  CefRefPtr<CefWindow> window;
};

// Creates a top-level window hosting one browser. Returns the window id, or 0
// when creation failed. Must be called on the browser process UI thread.
int CreateWindow(const Options& options);

// --- Window registry (browser process UI thread only) ----------------------

void RegisterBrowserWindow(int browser_id, int window_id,
                           CefRefPtr<CefWindow> window);
void UnregisterBrowserWindow(int browser_id);
bool FindWindowByBrowser(int browser_id, WindowRecord* out);
bool FindWindowById(int window_id, WindowRecord* out);
std::vector<WindowRecord> AllWindows();
int WindowIdForBrowser(int browser_id);

// A top-level window hosting one browser view.
//
// CEF Views provides the windowing layer (GTK backed on Linux), so the
// framework does not need per-platform window code. Both the window and the
// browser view use the Alloy runtime style: no Chrome UI, and the framework
// controls the window from native code.
class Window : public CefWindowDelegate, public CefBrowserViewDelegate {
 public:
  Window(const Options& options, int window_id);

  // CefWindowDelegate
  void OnWindowCreated(CefRefPtr<CefWindow> window) override;
  void OnWindowClosing(CefRefPtr<CefWindow> window) override;
  void OnWindowDestroyed(CefRefPtr<CefWindow> window) override;
  void OnWindowBoundsChanged(CefRefPtr<CefWindow> window,
                             const CefRect& new_bounds) override;
  void OnWindowActivationChanged(CefRefPtr<CefWindow> window, bool active) override;
  CefRect GetInitialBounds(CefRefPtr<CefWindow> window) override;
  cef_runtime_style_t GetWindowRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  // CefBrowserViewDelegate
  void OnBrowserCreated(CefRefPtr<CefBrowserView> browser_view,
                        CefRefPtr<CefBrowser> browser) override;
  void OnBrowserDestroyed(CefRefPtr<CefBrowserView> browser_view,
                          CefRefPtr<CefBrowser> browser) override;
  cef_runtime_style_t GetBrowserRuntimeStyle() override {
    return CEF_RUNTIME_STYLE_ALLOY;
  }

  int window_id() const { return window_id_; }
  CefRefPtr<CefWindow> window() const { return window_; }
  CefRefPtr<CefBrowser> browser() const { return browser_; }

 private:
  Options options_;
  int window_id_ = 0;
  CefRefPtr<CefWindow> window_;
  CefRefPtr<CefBrowser> browser_;

  IMPLEMENT_REFCOUNTING(Window);
};

}  // namespace yati

#endif  // YATI_WINDOW_H_
