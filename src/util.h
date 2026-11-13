#ifndef YATI_UTIL_H_
#define YATI_UTIL_H_

#include <string>

#include "include/internal/cef_string.h"

namespace yati {

inline std::string ToUtf8(const CefString& value) {
  return value.ToString();
}

inline CefString ToCef(const std::string& value) {
  CefString result;
  result.FromString(value);
  return result;
}

}  // namespace yati

#endif  // YATI_UTIL_H_
