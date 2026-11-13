#include "client.h"

#include <cstdio>
#include <string>

#include "include/cef_app.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"
#include "ipc.h"
#include "util.h"

namespace yati {

bool Client::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  return Ipc::Get()->browser_router()->OnProcessMessageReceived(
      browser, frame, source_process, message);
}

bool Client::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefRequest> request,
                            bool user_gesture,
                            bool is_redirect) {
  if (frame) {
    Ipc::Get()->browser_router()->OnBeforeBrowse(browser, frame);
  }
  return false;
}

bool Client::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           int popup_id,
                           const CefString& target_url,
                           const CefString& target_frame_name,
                           cef_window_open_disposition_t target_disposition,
                           bool user_gesture,
                           const CefPopupFeatures& popup_features,
                           CefWindowInfo& window_info,
                           CefRefPtr<CefClient>& client,
                           CefBrowserSettings& settings,
                           CefRefPtr<CefDictionaryValue>& extra_info,
                           bool* no_javascript_access) {
  // Frameworks decide window creation from native code (or an explicit IPC
  // call), never from a page calling window.open().
  std::printf("[yati] popup blocked: %s\n", ToUtf8(target_url).c_str());
  std::fflush(stdout);
  return true;
}

void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  Ipc::Get()->Attach(browser);
}

bool Client::DoClose(CefRefPtr<CefBrowser> browser) {
  return false;
}

void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  Ipc::Get()->Detach(browser);
  // App lifetime follows the window count (Electron semantics): the last
  // closing window quits the message loop.
  if (Ipc::Get()->browser_count() == 0) {
    CefQuitMessageLoop();
  }
}

void Client::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int http_status_code) {
  if (!frame || !frame->IsMain()) {
    return;
  }
  std::printf("[yati] loaded: %s\n", ToUtf8(frame->GetURL()).c_str());
  std::fflush(stdout);

  CefRefPtr<CefDictionaryValue> payload = CefDictionaryValue::Create();
  payload->SetString("url", frame->GetURL());
  payload->SetInt("status", http_status_code);
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(payload);
  Ipc::Get()->Emit("app:ready", value);
}

void Client::OnLoadError(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         ErrorCode error_code,
                         const CefString& error_text,
                         const CefString& failed_url) {
  std::fprintf(stderr, "[yati] load error %d on %s: %s\n", static_cast<int>(error_code),
               ToUtf8(failed_url).c_str(), ToUtf8(error_text).c_str());
  std::fflush(stderr);
}

bool Client::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                              cef_log_severity_t level,
                              const CefString& message,
                              const CefString& source,
                              int line) {
  std::printf("[webui] %s (%s:%d)\n", ToUtf8(message).c_str(),
              ToUtf8(source).c_str(), line);
  std::fflush(stdout);
  return false;
}

}  // namespace yati
