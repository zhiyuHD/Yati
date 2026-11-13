#ifndef YATI_API_API_H_
#define YATI_API_API_H_

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "include/cef_base.h"
#include "include/cef_browser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_message_router.h"

namespace yati {
namespace api {

// Error codes are mirrored by YatiError.code on the JS side.
enum ErrorCode : int {
  kInvalidRequest = -32000,
  kUnknownChannel = -32001,
  kInvalidParams = -32002,
  kNotFound = -32003,
  kIoError = -32004,
  kNotSupported = -32006,
  kInternal = -32007,
};

// --- JSON value helpers ----------------------------------------------------

CefRefPtr<CefValue> MakeString(const std::string& value);
CefRefPtr<CefValue> MakeInt(int value);
CefRefPtr<CefValue> MakeDouble(double value);
CefRefPtr<CefValue> MakeBool(bool value);
CefRefPtr<CefValue> Wrap(CefRefPtr<CefDictionaryValue> dict);
CefRefPtr<CefValue> Wrap(CefRefPtr<CefListValue> list);
std::string Serialize(CefRefPtr<CefValue> value);

// --- One in-flight call ----------------------------------------------------

// A call may be answered immediately or later (native file dialogs, child
// processes). Resolve()/Reject() are idempotent: only the first answer wins.
class Call : public CefBaseRefCounted {
 public:
  Call(CefRefPtr<CefBrowser> browser,
       CefRefPtr<CefFrame> frame,
       std::string channel,
       CefRefPtr<CefDictionaryValue> params,
       CefRefPtr<CefMessageRouterBrowserSide::Callback> callback);

  CefRefPtr<CefBrowser> browser() const { return browser_; }
  CefRefPtr<CefFrame> frame() const { return frame_; }
  const std::string& channel() const { return channel_; }
  CefRefPtr<CefDictionaryValue> params() const { return params_; }

  // Parameter access. Missing keys return the fallback.
  bool Has(const char* name) const;
  std::string String(const char* name, const std::string& fallback = std::string()) const;
  int Int(const char* name, int fallback = 0) const;
  double Double(const char* name, double fallback = 0.0) const;
  bool Bool(const char* name, bool fallback = false) const;
  CefRefPtr<CefListValue> ListParam(const char* name) const;
  CefRefPtr<CefDictionaryValue> DictParam(const char* name) const;

  // Requires a non-empty string parameter; rejects the call and returns false
  // when it is missing.
  bool RequireString(const char* name, std::string* out);

  // Answers.
  void Resolve(CefRefPtr<CefValue> value);
  void Resolve(CefRefPtr<CefDictionaryValue> dict);
  void Resolve(CefRefPtr<CefListValue> list);
  void ResolveStr(const std::string& value);
  void ResolveBool(bool value);
  void ResolveInt(int value);
  void ResolveNull();
  void Reject(int code, const std::string& message);

  bool answered() const { return answered_.load(); }

 private:
  ~Call() override = default;

  void SendEnvelope(bool ok, CefRefPtr<CefValue> result, int code,
                    const std::string& message);

  CefRefPtr<CefBrowser> browser_;
  CefRefPtr<CefFrame> frame_;
  std::string channel_;
  CefRefPtr<CefDictionaryValue> params_;
  CefRefPtr<CefMessageRouterBrowserSide::Callback> callback_;
  std::atomic<bool> answered_{false};

  IMPLEMENT_REFCOUNTING(Call);
};

using Handler = std::function<void(const CefRefPtr<Call>&)>;

// --- Channel registry ------------------------------------------------------

class Registry {
 public:
  static Registry* Get();

  void Add(const std::string& channel, Handler handler);

  // Returns false when no handler is registered for the channel.
  bool Dispatch(const CefRefPtr<Call>& call);

  std::vector<std::string> channels() const;

 private:
  Registry() = default;
  void EnsureRegistered();
  bool registered_ = false;
  std::map<std::string, Handler> handlers_;
};

// Module registration entry points, one per source file.
void RegisterCoreApi(Registry* registry);    // app, shell, clipboard, notify, session, devtools
void RegisterFsApi(Registry* registry);      // fs
void RegisterWindowApi(Registry* registry);  // window
void RegisterDialogApi(Registry* registry);  // dialog

}  // namespace api
}  // namespace yati

#endif  // YATI_API_API_H_
