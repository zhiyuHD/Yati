#include "app.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "include/cef_command_line.h"
#include "include/cef_path_util.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_helpers.h"
#include "ipc.h"
#include "options.h"
#include "scheme.h"
#include "util.h"
#include "window.h"

namespace yati {

namespace {

// 每个渲染进程读一次 webui/preload.js，随后在每个主 frame 的上下文里同步求值。
// 引擎注入而不是让页面自己 <script src>，这样 App 只需要写网页，也减少页面
// 忘引、引错路径的可能。
const std::string& PreloadScript() {
  static std::string script;
  static bool loaded = false;
  if (loaded) {
    return script;
  }
  loaded = true;

  CefString exe_dir;
  if (!CefGetPath(PK_DIR_EXE, exe_dir)) {
    return script;
  }
  const std::string path = ToUtf8(exe_dir) + "/webui/preload.js";
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    std::fprintf(stderr,
                 "[yati] 警告：找不到 %s，页面需要自行引入 JS 桥\n",
                 path.c_str());
    return script;
  }
  script.assign(std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
  return script;
}

}  // namespace

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

void App::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
  // STANDARD：按站点处理（相对/绝对路径、fetch、ES module 才正常）
  // SECURE：视为安全上下文（避免被当成不安全来源限制）
  // CORS_ENABLED：允许同源/跨源请求走正常 CORS 流程
  registrar->AddCustomScheme(
      "app", CEF_SCHEME_OPTION_STANDARD | CEF_SCHEME_OPTION_SECURE |
                 CEF_SCHEME_OPTION_CORS_ENABLED);
}

void App::OnContextInitialized() {
  CEF_REQUIRE_UI_THREAD();

  // App 包模式：先把 app:// 挂上，再开窗（顺序反了首屏会 404）。
  const Options& options = GetOptions();
  if (!options.app_dir.empty()) {
    RegisterAppScheme(options.web_root.empty() ? options.app_dir : options.web_root);
  }

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

  if (!context || !frame || !frame->IsMain()) {
    return;
  }

  // window.yati：在页面脚本之前同步执行（ExecuteJavaScript 是异步的，会晚于
  // 页面内联脚本，App 在加载阶段就用不了 API）。
  const std::string& script = PreloadScript();
  if (script.empty()) {
    return;
  }
  CefRefPtr<CefV8Value> retval;
  CefRefPtr<CefV8Exception> exception;
  const bool ok = context->Eval(ToCef(script), ToCef("yati://preload.js"), 0,
                                retval, exception);
  if (!ok) {
    std::fprintf(stderr, "[yati] 注入 JS 桥失败：%s\n",
                 exception ? ToUtf8(exception->GetMessage()).c_str() : "unknown");
  }
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
