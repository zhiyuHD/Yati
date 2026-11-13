#include "api.h"

#include <utility>

#include "include/cef_parser.h"
#include "util.h"

namespace yati {
namespace api {

CefRefPtr<CefValue> MakeString(const std::string& value) {
  CefRefPtr<CefValue> result = CefValue::Create();
  result->SetString(ToCef(value));
  return result;
}

CefRefPtr<CefValue> MakeInt(int value) {
  CefRefPtr<CefValue> result = CefValue::Create();
  result->SetInt(value);
  return result;
}

CefRefPtr<CefValue> MakeDouble(double value) {
  CefRefPtr<CefValue> result = CefValue::Create();
  result->SetDouble(value);
  return result;
}

CefRefPtr<CefValue> MakeBool(bool value) {
  CefRefPtr<CefValue> result = CefValue::Create();
  result->SetBool(value);
  return result;
}

CefRefPtr<CefValue> Wrap(CefRefPtr<CefDictionaryValue> dict) {
  CefRefPtr<CefValue> result = CefValue::Create();
  result->SetDictionary(dict);
  return result;
}

CefRefPtr<CefValue> Wrap(CefRefPtr<CefListValue> list) {
  CefRefPtr<CefValue> result = CefValue::Create();
  result->SetList(list);
  return result;
}

std::string Serialize(CefRefPtr<CefValue> value) {
  if (!value) {
    return "null";
  }
  return ToUtf8(CefWriteJSON(value, JSON_WRITER_DEFAULT));
}

Call::Call(CefRefPtr<CefBrowser> browser,
           CefRefPtr<CefFrame> frame,
           std::string channel,
           CefRefPtr<CefDictionaryValue> params,
           CefRefPtr<CefMessageRouterBrowserSide::Callback> callback)
    : browser_(std::move(browser)),
      frame_(std::move(frame)),
      channel_(std::move(channel)),
      params_(std::move(params)),
      callback_(std::move(callback)) {}

bool Call::Has(const char* name) const {
  return params_ && params_->HasKey(name);
}

std::string Call::String(const char* name, const std::string& fallback) const {
  if (!params_ || !params_->HasKey(name)) {
    return fallback;
  }
  return ToUtf8(params_->GetString(name));
}

int Call::Int(const char* name, int fallback) const {
  if (!params_ || !params_->HasKey(name)) {
    return fallback;
  }
  return params_->GetInt(name);
}

double Call::Double(const char* name, double fallback) const {
  if (!params_ || !params_->HasKey(name)) {
    return fallback;
  }
  if (params_->GetType(name) == VTYPE_INT) {
    return static_cast<double>(params_->GetInt(name));
  }
  return params_->GetDouble(name);
}

bool Call::Bool(const char* name, bool fallback) const {
  if (!params_ || !params_->HasKey(name)) {
    return fallback;
  }
  return params_->GetBool(name);
}

CefRefPtr<CefListValue> Call::ListParam(const char* name) const {
  if (!params_ || !params_->HasKey(name) || params_->GetType(name) != VTYPE_LIST) {
    return nullptr;
  }
  return params_->GetList(name);
}

CefRefPtr<CefDictionaryValue> Call::DictParam(const char* name) const {
  if (!params_ || !params_->HasKey(name) ||
      params_->GetType(name) != VTYPE_DICTIONARY) {
    return nullptr;
  }
  return params_->GetDictionary(name);
}

bool Call::RequireString(const char* name, std::string* out) {
  const std::string value = String(name);
  if (value.empty()) {
    Reject(kInvalidParams, std::string("Missing required string parameter \"") +
                               name + "\"");
    return false;
  }
  if (out) {
    *out = value;
  }
  return true;
}

void Call::Resolve(CefRefPtr<CefValue> value) {
  SendEnvelope(true, value, 0, std::string());
}

void Call::Resolve(CefRefPtr<CefDictionaryValue> dict) {
  Resolve(Wrap(dict));
}

void Call::Resolve(CefRefPtr<CefListValue> list) {
  Resolve(Wrap(list));
}

void Call::ResolveStr(const std::string& value) {
  Resolve(MakeString(value));
}

void Call::ResolveBool(bool value) {
  Resolve(MakeBool(value));
}

void Call::ResolveInt(int value) {
  Resolve(MakeInt(value));
}

void Call::ResolveNull() {
  SendEnvelope(true, nullptr, 0, std::string());
}

void Call::Reject(int code, const std::string& message) {
  SendEnvelope(false, nullptr, code, message);
}

void Call::SendEnvelope(bool ok, CefRefPtr<CefValue> result, int code,
                        const std::string& message) {
  if (answered_.exchange(true)) {
    return;
  }
  if (!callback_) {
    return;
  }

  CefRefPtr<CefDictionaryValue> envelope = CefDictionaryValue::Create();
  envelope->SetBool("ok", ok);
  envelope->SetString("channel", channel_);

  if (ok) {
    envelope->SetValue("result", result);
  } else {
    CefRefPtr<CefDictionaryValue> error = CefDictionaryValue::Create();
    error->SetInt("code", code);
    error->SetString("message", message);
    envelope->SetDictionary("error", error);
  }

  callback_->Success(ToCef(Serialize(Wrap(envelope))));
}

Registry* Registry::Get() {
  // Process lifetime singleton, same reasoning as the IPC router: handlers hold
  // raw pointers into CEF objects that outlive normal static destruction.
  static Registry* instance = new Registry();
  return instance;
}

void Registry::Add(const std::string& channel, Handler handler) {
  handlers_[channel] = std::move(handler);
}

void Registry::EnsureRegistered() {
  if (registered_) {
    return;
  }
  registered_ = true;
  RegisterCoreApi(this);
  RegisterFsApi(this);
  RegisterWindowApi(this);
  RegisterDialogApi(this);
}

bool Registry::Dispatch(const CefRefPtr<Call>& call) {
  EnsureRegistered();
  const auto it = handlers_.find(call->channel());
  if (it == handlers_.end()) {
    return false;
  }
  it->second(call);
  return true;
}

std::vector<std::string> Registry::channels() const {
  std::vector<std::string> result;
  result.reserve(handlers_.size());
  for (const auto& entry : handlers_) {
    result.push_back(entry.first);
  }
  return result;
}

}  // namespace api
}  // namespace yati
