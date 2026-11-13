#ifndef YATI_OPTIONS_H_
#define YATI_OPTIONS_H_

#include <string>

#include "version.h"

namespace yati {

// 默认值集中在这里，命令行参数与代码其它地方都引用这些名字。
constexpr int kDefaultWindowWidth = 1200;
constexpr int kDefaultWindowHeight = 800;
constexpr int kDefaultInspectPort = 9222;

// Command line options, parsed once in main() and read afterwards by the
// browser process UI thread.
struct Options {
  std::string url;              // Empty means "load the bundled webui/index.html".
  std::string title = YATI_PRODUCT_NAME;
  int width = kDefaultWindowWidth;
  int height = kDefaultWindowHeight;
  bool no_sandbox = true;       // Dev default; pass --sandbox for packaged apps.
  bool disable_gpu = false;
  bool inspect = false;         // Expose the DevTools protocol.
  int inspect_port = kDefaultInspectPort;
  bool verbose = false;
};

const Options& GetOptions();
void SetOptions(const Options& options);

}  // namespace yati

#endif  // YATI_OPTIONS_H_
