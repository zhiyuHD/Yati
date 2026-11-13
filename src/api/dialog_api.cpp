#include <string>
#include <vector>

#include "api.h"
#include "util.h"

namespace yati {
namespace api {

namespace {

// Native file dialog support. CEF runs the platform dialog and answers the
// pending call asynchronously, so the promise on the JS side simply waits.
class FileDialogCallback : public CefRunFileDialogCallback {
 public:
  explicit FileDialogCallback(CefRefPtr<Call> call) : call_(std::move(call)) {}

  void OnFileDialogDismissed(
      const std::vector<CefString>& file_paths) override {
    CefRefPtr<CefListValue> list = CefListValue::Create();
    int index = 0;
    for (const auto& path : file_paths) {
      list->SetString(index++, path);
    }
    CefRefPtr<CefDictionaryValue> result = CefDictionaryValue::Create();
    result->SetList("paths", list);
    result->SetBool("canceled", file_paths.empty());
    // Convenience: the first selection, or an empty string.
    result->SetString("path",
                      file_paths.empty() ? std::string() : ToUtf8(file_paths[0]));
    call_->Resolve(result);
  }

  IMPLEMENT_REFCOUNTING(FileDialogCallback);

 private:
  CefRefPtr<Call> call_;
};

std::vector<CefString> FiltersFrom(const CefRefPtr<Call>& call) {
  std::vector<CefString> filters;
  CefRefPtr<CefListValue> list = call->ListParam("filters");
  if (!list) {
    return filters;
  }
  for (size_t i = 0; i < list->GetSize(); ++i) {
    const std::string filter = ToUtf8(list->GetString(i));
    if (!filter.empty()) {
      filters.push_back(ToCef(filter));
    }
  }
  return filters;
}

void RunDialog(const CefRefPtr<Call>& call,
               CefBrowserHost::FileDialogMode mode) {
  CefRefPtr<CefBrowser> browser = call->browser();
  if (!browser) {
    call->Reject(kInternal, "no browser");
    return;
  }
  CefRefPtr<CefBrowserHost> host = browser->GetHost();
  if (!host) {
    call->Reject(kInternal, "no browser host");
    return;
  }
  host->RunFileDialog(mode, ToCef(call->String("title")),
                      ToCef(call->String("defaultPath")), FiltersFrom(call),
                      new FileDialogCallback(call));
}

}  // namespace

void RegisterDialogApi(Registry* registry) {
  registry->Add("dialog.openFile", [](const CefRefPtr<Call>& call) {
    RunDialog(call, call->Bool("multiple", false)
                        ? FILE_DIALOG_OPEN_MULTIPLE
                        : FILE_DIALOG_OPEN);
  });

  registry->Add("dialog.openFolder", [](const CefRefPtr<Call>& call) {
    RunDialog(call, FILE_DIALOG_OPEN_FOLDER);
  });

  registry->Add("dialog.saveFile", [](const CefRefPtr<Call>& call) {
    RunDialog(call, FILE_DIALOG_SAVE);
  });
}

}  // namespace api
}  // namespace yati
