#ifndef YATI_SCHEME_H_
#define YATI_SCHEME_H_

#include <string>

namespace yati {

// app:// 协议：把 App 的静态目录当做一个站点的根。
//
// 为什么需要它：Next.js / Vite 这类工具产出的静态站点用的是绝对路径
// （/_next/static/...），在 file:// 下会解析到文件系统根目录，资源全部 404。
// 用 app:// 提供后，绝对路径落在 App 自己的源里，同时 ES module、fetch、
// localStorage 这些「需要正经源」的能力也才可用。
//
// 必须在创建第一个浏览器之前调用（浏览器进程 UI 线程）。
void RegisterAppScheme(const std::string& root);

}  // namespace yati

#endif  // YATI_SCHEME_H_
