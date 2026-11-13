#ifndef YATI_IPC_H_
#define YATI_IPC_H_

#include <string>
#include <vector>

#include "include/cef_base.h"
#include "include/cef_browser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace yati {

// Process-wide JavaScript <-> C++ bridge built on CefMessageRouter.
//
// The same code runs in both roles: the browser process owns the query router
// (CefMessageRouterBrowserSide), every renderer process owns the matching
// renderer-side router. Each side is created lazily, so a single instance is
// enough for both.
class Ipc : public CefMessageRouterBrowserSide::Handler {
 public:
  static Ipc* Get();

  // Browser process ---------------------------------------------------
  CefRefPtr<CefMessageRouterBrowserSide> browser_router();
  void Attach(CefRefPtr<CefBrowser> browser);
  void Detach(CefRefPtr<CefBrowser> browser);
  size_t browser_count() const { return browsers_.size(); }

  // Pushes an event to every attached renderer:
  //   window.__yatiDispatch({ event: ..., payload: ... })
  void Emit(const std::string& event, CefRefPtr<CefValue> payload);

  // Renderer process --------------------------------------------------
  CefRefPtr<CefMessageRouterRendererSide> renderer_router();

  // CefMessageRouterBrowserSide::Handler
  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;
  void OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int64_t query_id) override;

 private:
  Ipc() = default;
  ~Ipc() = default;

  void Dispatch(CefRefPtr<CefBrowser> browser,
                CefRefPtr<CefFrame> frame,
                CefRefPtr<CefDictionaryValue> request,
                CefRefPtr<Callback> callback);

  CefRefPtr<CefMessageRouterBrowserSide> browser_router_;
  CefRefPtr<CefMessageRouterRendererSide> renderer_router_;
  std::vector<CefRefPtr<CefBrowser>> browsers_;
};

}  // namespace yati

#endif  // YATI_IPC_H_
