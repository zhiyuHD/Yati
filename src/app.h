#ifndef YATI_APP_H_
#define YATI_APP_H_

#include "include/cef_app.h"

namespace yati {

// A CEF application is one binary that serves two roles: the browser process
// (windows, IPC router, application logic) and every spawned renderer/GPU/
// utility process (V8 context hooks). This class implements both handler
// interfaces; CEF picks the right one per process.
class App : public CefApp,
            public CefBrowserProcessHandler,
            public CefRenderProcessHandler {
 public:
  App() = default;

  // CefApp
  void OnBeforeCommandLineProcessing(
      const CefString& process_type,
      CefRefPtr<CefCommandLine> command_line) override;
  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  // CefBrowserProcessHandler
  void OnContextInitialized() override;

  // CefRenderProcessHandler
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;
  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

 private:
  IMPLEMENT_REFCOUNTING(App);
};

}  // namespace yati

#endif  // YATI_APP_H_
