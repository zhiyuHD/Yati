#include "app.h"

#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"
#include "ipc.h"
#include "options.h"
#include "window.h"

namespace yati {

void App::OnBeforeCommandLineProcessing(
    const CefString& process_type,
    CefRefPtr<CefCommandLine> command_line) {
  const Options& options = GetOptions();

  // Helper processes inherit these switches from the browser process, so only
  // the browser process needs to configure them.
  if (!process_type.empty()) {
    return;
  }

  if (options.disable_gpu) {
    command_line->AppendSwitch("disable-gpu");
  }
  if (options.verbose) {
    command_line->AppendSwitchWithValue("enable-logging", "stderr");
    command_line->AppendSwitchWithValue("v", "1");
  }
}

void App::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  // Create the router before the first browser so that no query can arrive
  // before a handler exists.
  Ipc::Get()->browser_router();
  if (CreateWindow(GetOptions()) == 0) {
    CefQuitMessageLoop();
  }
}

void App::OnContextCreated(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) {
  // Installs window.cefQuery / window.cefQueryCancel into the new context.
  Ipc::Get()->renderer_router()->OnContextCreated(browser, frame, context);
}

void App::OnContextReleased(CefRefPtr<CefBrowser> browser,
                            CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefV8Context> context) {
  Ipc::Get()->renderer_router()->OnContextReleased(browser, frame, context);
}

bool App::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   CefProcessId source_process,
                                   CefRefPtr<CefProcessMessage> message) {
  return Ipc::Get()->renderer_router()->OnProcessMessageReceived(
      browser, frame, source_process, message);
}

}  // namespace yati
