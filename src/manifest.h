#ifndef YATI_MANIFEST_H_
#define YATI_MANIFEST_H_

#include <string>

namespace yati {

// 一个「基于 Yati 的 App」= 一个目录 + 一份 yati.json：
//
//   my-app/
//   ├── yati.json
//   └── index.html
//
// 引擎负责开窗、注入 JS API、按 App 隔离缓存；App 只提供网页与这份清单。
struct AppManifest {
  std::string root;                    // App 目录（绝对路径）
  std::string id;                      // 缓存/标识用，缺省取目录名
  std::string name;                    // 显示名，缺省取 id
  std::string version;                 // App 自身版本
  std::string entry = "index.html";    // 入口页（相对 App 目录）
  std::string entry_file = "index.html";  // 入口文件名（相对站点根）
  std::string web_root;                // 站点根 = entry 所在目录，app:// 以它为根
  std::string title;                   // 窗口标题，缺省取 name
  int width = 0;                       // 0 表示用引擎默认值
  int height = 0;
  bool valid = false;
};

// 读取 <dir>/yati.json。目录里没有清单时返回 false（由调用方决定是回退到
// 内置页面还是报错），清单存在但格式错误时也会返回 false 并打印原因。
bool LoadManifest(const std::string& dir, AppManifest* out);

// 把 App 目录解析成绝对路径；目录不存在时返回空串。
std::string ResolveAppDir(const std::string& dir);

}  // namespace yati

#endif  // YATI_MANIFEST_H_
