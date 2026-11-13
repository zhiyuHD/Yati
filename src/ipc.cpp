#include "ipc.h"

#include <string>

#include "api/api.h"
#include "include/cef_parser.h"
#include "util.h"

namespace yati {

Ipc* Ipc::Get() {
  // Intentionally leaked: the query router holds a raw pointer to this handler
  // and is torn down after static destructors would already have run.
  static Ipc* instance = new Ipc();
  return instance;
}

CefRefPtr<CefMessageRouterBrowserSide> Ipc::browser_router() {
  if (!browser_router_) {
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    browser_router_ = CefMessageRouterBrowserSide::Create(config);
    // AddHandler() does not take ownership of the handler.
    browser_router_->AddHandler(this, true);
  }
  return browser_router_;
}

CefRefPtr<CefMessageRouterRendererSide> Ipc::renderer_router() {
  if (!renderer_router_) {
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    renderer_router_ = CefMessageRouterRendererSide::Create(config);
  }
  return renderer_router_;
}

void Ipc::Attach(CefRefPtr<CefBrowser> browser) {
  for (const auto& existing : browsers_) {
    if (existing->IsSame(browser)) {
      return;
    }
  }
  browsers_.push_back(browser);
}

void Ipc::Detach(CefRefPtr<CefBrowser> browser) {
  for (auto it = browsers_.begin(); it != browsers_.end(); ++it) {
    if ((*it)->IsSame(browser)) {
      browsers_.erase(it);
      return;
    }
  }
}

void Ipc::Emit(const std::string& event, CefRefPtr<CefValue> payload) {
  CefRefPtr<CefDictionaryValue> envelope = CefDictionaryValue::Create();
  envelope->SetString("event", event);
  envelope->SetValue("payload", payload ? payload : CefValue::Create());

  const std::string js =
      "window.__yatiDispatch && window.__yatiDispatch(" +
      ToUtf8(CefWriteJSON(api::Wrap(envelope), JSON_WRITER_DEFAULT)) + ");";

  for (const auto& browser : browsers_) {
    CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (frame) {
      frame->ExecuteJavaScript(js, frame->GetURL(), 0);
    }
  }
}

bool Ipc::OnQuery(CefRefPtr<CefBrowser> browser,
                  CefRefPtr<CefFrame> frame,
                  int64_t query_id,
                  const CefString& request,
                  bool persistent,
                  CefRefPtr<Callback> callback) {
  CefRefPtr<CefValue> parsed = CefParseJSON(request, JSON_PARSER_RFC);
  if (!parsed || parsed->GetType() != VTYPE_DICTIONARY) {
    callback->Failure(-2, "Malformed IPC request: expected a JSON object");
    return true;
  }

  Dispatch(browser, frame, parsed->GetDictionary(), callback);
  return true;
}

void Ipc::OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          int64_t query_id) {
  // Calls either answered already or are still holding their own callback;
  // nothing to clean up here.
}

void Ipc::Dispatch(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   CefRefPtr<CefDictionaryValue> request,
                   CefRefPtr<Callback> callback) {
  const std::string channel = ToUtf8(request->GetString("channel"));
  CefRefPtr<CefDictionaryValue> params = request->GetDictionary("params");

  CefRefPtr<api::Call> call(
      new api::Call(browser, frame, channel, params, callback));
  if (!api::Registry::Get()->Dispatch(call)) {
    call->Reject(api::kUnknownChannel, "Unknown channel: " + channel);
  }
}

}  // namespace yati
