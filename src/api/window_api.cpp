#include <string>

#include "api.h"
#include "ipc.h"
#include "util.h"
#include "version.h"
#include "window.h"

namespace yati {
namespace api {

namespace {

// Resolves the target window: an explicit |windowId| parameter wins, otherwise
// the window that owns the calling browser is used.
CefRefPtr<CefWindow> TargetWindow(const CefRefPtr<Call>& call, int* window_id) {
  const int requested = call->Int("windowId", 0);
  WindowRecord record;
  if (requested > 0) {
    if (!FindWindowById(requested, &record)) {
      call->Reject(kNotFound, "No window with id " + std::to_string(requested));
      return nullptr;
    }
  } else {
    CefRefPtr<CefBrowser> browser = call->browser();
    if (!browser || !FindWindowByBrowser(browser->GetIdentifier(), &record)) {
      call->Reject(kNotFound, "The calling browser has no window");
      return nullptr;
    }
  }
  if (window_id) {
    *window_id = record.window_id;
  }
  return record.window;
}

CefRefPtr<CefDictionaryValue> StateDict(CefRefPtr<CefWindow> window,
                                        int window_id) {
  CefRefPtr<CefDictionaryValue> state = CefDictionaryValue::Create();
  state->SetInt("windowId", window_id);
  const CefRect bounds = window->GetBounds();
  state->SetInt("x", bounds.x);
  state->SetInt("y", bounds.y);
  state->SetInt("width", bounds.width);
  state->SetInt("height", bounds.height);
  state->SetBool("maximized", window->IsMaximized());
  state->SetBool("fullscreen", window->IsFullscreen());
  state->SetString("title", window->GetTitle());
  return state;
}

}  // namespace

void RegisterWindowApi(Registry* registry) {
  registry->Add("window.getState", [](const CefRefPtr<Call>& call) {
    int window_id = 0;
    CefRefPtr<CefWindow> window = TargetWindow(call, &window_id);
    if (!window) {
      return;
    }
    call->Resolve(StateDict(window, window_id));
  });

  registry->Add("window.setBounds", [](const CefRefPtr<Call>& call) {
    int window_id = 0;
    CefRefPtr<CefWindow> window = TargetWindow(call, &window_id);
    if (!window) {
      return;
    }
    CefRect bounds = window->GetBounds();
    bounds.x = call->Int("x", bounds.x);
    bounds.y = call->Int("y", bounds.y);
    bounds.width = call->Int("width", bounds.width);
    bounds.height = call->Int("height", bounds.height);
    if (bounds.width <= 0 || bounds.height <= 0) {
      call->Reject(kInvalidParams, "width/height must be positive");
      return;
    }
    window->SetBounds(bounds);
    call->Resolve(StateDict(window, window_id));
  });

  registry->Add("window.getTitle", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    call->ResolveStr(ToUtf8(window->GetTitle()));
  });

  registry->Add("window.setTitle", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->SetTitle(ToCef(call->String("title")));
    call->ResolveStr(ToUtf8(window->GetTitle()));
  });

  registry->Add("window.minimize", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->Minimize();
    call->ResolveBool(true);
  });

  registry->Add("window.maximize", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->Maximize();
    call->ResolveBool(true);
  });

  registry->Add("window.restore", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->Restore();
    call->ResolveBool(true);
  });

  registry->Add("window.setFullscreen", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->SetFullscreen(call->Bool("fullscreen", true));
    call->ResolveBool(true);
  });

  registry->Add("window.setAlwaysOnTop", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->SetAlwaysOnTop(call->Bool("onTop", true));
    call->ResolveBool(true);
  });

  registry->Add("window.show", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->Show();
    call->ResolveBool(true);
  });

  registry->Add("window.hide", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->Hide();
    call->ResolveBool(true);
  });

  registry->Add("window.activate", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->Activate();
    call->ResolveBool(true);
  });

  registry->Add("window.center", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    window->CenterWindow(window->GetSize());
    call->ResolveBool(true);
  });

  registry->Add("window.close", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefWindow> window = TargetWindow(call, nullptr);
    if (!window) {
      return;
    }
    call->ResolveBool(true);
    window->Close();
  });

  registry->Add("window.list", [](const CefRefPtr<Call>& call) {
    CefRefPtr<CefListValue> list = CefListValue::Create();
    int index = 0;
    for (const auto& record : AllWindows()) {
      if (!record.window) {
        continue;
      }
      CefRefPtr<CefDictionaryValue> state = StateDict(record.window, record.window_id);
      state->SetInt("browserId", record.browser_id);
      list->SetDictionary(index++, state);
    }
    call->Resolve(list);
  });

  registry->Add("window.create", [](const CefRefPtr<Call>& call) {
    Options options;
    options.url = call->String("url");
    options.title = call->String("title", YATI_PRODUCT_NAME);
    options.width = call->Int("width", 1000);
    options.height = call->Int("height", 700);

    const int window_id = CreateWindow(options);
    if (window_id == 0) {
      call->Reject(kInternal, "window creation failed");
      return;
    }
    CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
    result->SetInt("windowId", window_id);
    result->SetString("url", options.url);
    call->Resolve(result);
  });
}

}  // namespace api
}  // namespace yati
