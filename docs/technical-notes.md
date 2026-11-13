# Yati 技术笔记

实现、构建、打包与排查的细节都放这里；README 只讲「是什么、干什么、怎么用」。

## 目录

1. [CEF 二进制](#1-cef-二进制)
2. [构建](#2-构建)
3. [命令行参数](#3-命令行参数)
4. [API 实现：Registry 与 Call](#4-api-实现registry-与-call)
5. [通道清单](#5-通道清单)
6. [错误码](#6-错误码)
7. [事件](#7-事件)
8. [架构](#8-架构)
9. [代码结构](#9-代码结构)
10. [验证](#10-验证)
11. [打包注意](#11-打包注意)
12. [下一步](#12-下一步)
13. [已知问题](#13-已知问题)

---

## 1. CEF 二进制

| 项目 | 值 |
| --- | --- |
| 版本 | CEF `156.0.28+g57389f0+chromium-156.0.8078.88`（stable） |
| Chromium | `156.0.8078.88` |
| 平台 | linux64 |
| 包类型 | `minimal`（含全部头文件 / libcef / 资源，去掉调试符号；standard 包 697 MB） |
| 文件 | `vendor/cef/cef_binary_156.0.28_linux64_minimal.tar.bz2`（333,365,976 字节） |
| 下载地址 | `https://cef-builds.spotifycdn.com/cef_binary_156.0.28%2Bg57389f0%2Bchromium-156.0.8078.88_linux64_minimal.tar.bz2` |
| SHA1 | `6bd0b932a37503fb0206ac77bf628ebd0496f120` ← 已比对官方 index.json |
| 解压后 | `vendor/cef/cef_binary_156.0.28+g57389f0+chromium-156.0.8078.88_linux64_minimal/`（1.5 GB） |

分发包不进版本库（1.5 GB 超过 GitHub 单文件上限），由 `scripts/fetch-deps.sh` 下载并校验上面的 SHA1；
脚本里的 `CEF_VERSION` / `CEF_SHA1` 可用环境变量覆盖。

SDK 内部结构（与老版本不同，156 把产物集中在 `Release/` 与 `Resources/`）：

```
include/     282 个头文件（cef_*.h、capi/、views/、wrapper/、internal/）
libcef_dll/  C++ 包装层源码，编译成 libcef_dll_wrapper.a
cmake/       FindCEF.cmake / cef_variables.cmake / cef_macros.cmake
Release/     libcef.so(1.4G)、chrome-sandbox、libvk_swiftshader.so、libvulkan.so.1、
             v8_context_snapshot.bin、vk_swiftshader_icd.json
Resources/   chrome_100/200_percent.pak、resources.pak、icudtl.dat、locales/
```

两个坑：`minimal` 包里**没有** `tests/cefclient` 示例源码（要 standard 包或 GitHub）；
`cef_settings.h` / `cef_browser_host.h` 在 156 里已不存在，`CefSettings` 变成
`internal/cef_types_wrappers.h` 里的 `CefStructBase` 别名，runtime style 也改成按窗口粒度由
`CefWindowDelegate::GetWindowRuntimeStyle()` 配置。

## 2. 构建

### 2.1 CMake（默认）

宿主系统里没有 cmake、ninja 且 sudo 不可用时，用**官方免安装预编译包**：`scripts/fetch-deps.sh` 会把
CMake 与 Ninja 下到 `vendor/tools/`（同样不进版本库），`build.sh` 通过通配符自动探测，不需要 root：

```bash
./build.sh              # 默认 Release
./build.sh Debug        # 需要调试符号时
```

要求：GCC 13+/Clang 16+（C++20）、`libX11` 开发符号链接、GTK3/NSS/GBM 等运行时库。

产物在 `build/bin/`：`yati`、软链到 SDK 的 `libcef.so` 与资源、软链到源码的 `webui/`（改页面即时生效）。

### 2.2 Yubild（自研构建系统，已跑通）

`yubild.py` 是 Yubild 配置，产物到 `build-yubild/`，与 CMake 的 `build/` 隔离：

```bash
yubild build --release          # 产物 build-yubild/release/bin
yubild test                     # 单元测试 tests/process_test.cpp
yubild mutate --filter 'src/process.cpp'
```

实测 223 个编译单元约 2 分 07 秒；12 个运行期文件、`locales/`(220)、`webui/` 全由 `post_build` 摆好；
`readelf -d` 为 `NEEDED libcef.so` + `RUNPATH $ORIGIN`；运行后 15 项 API 自检全过。

用到的两个 Yubild 能力：`yubild.cmake(...)` 注入前导片段（`set(CEF_ROOT)` + `CMAKE_MODULE_PATH` +
`find_package(CEF)`）、`post_build=[copy(...), copy_dir(...)]`。迁移经过与遗留改进点见
[yubild-gaps.md](yubild-gaps.md)。

### 2.3 版本号与路径约定

代码里不留会漂移的字面量，凡是「会变的东西」都有唯一定义处：

| 东西 | 唯一定义处 | 怎么传到使用方 |
| --- | --- | --- |
| 版本号 | 仓库根 `VERSION` 文件 | CMake：`project(VERSION)` + `YATI_VERSION` 宏；Yubild：`project(version=)` + `add_compile_definitions` |
| 产品名 / app id | `src/version.h`（`YATI_PRODUCT_NAME` / `YATI_APP_ID`） | 标题栏、通知、`app.info`、缓存目录统一引用 |
| CEF 分发包目录 | `vendor/cef/cef_binary_*_linux64_*` 自动探测（只认含 `cmake/FindCEF.cmake` 的目录） | `-DCEF_ROOT=` 可覆盖 |
| 构建工具 | `vendor/tools/` 自动探测 | `YATI_CMAKE` / `YATI_NINJA` / `YATI_TOOLS_DIR` 可覆盖 |
| CDP 端口 | `src/options.h` 的 `kDefaultInspectPort` | `--inspect-port=<n>`；检查脚本用 `YATI_INSPECT_PORT` / `--endpoint=` |
| 默认窗口尺寸 | `src/options.h` 的 `kDefaultWindowWidth/Height` | `--width` / `--height` |
| 超时与轮询间隔 | `src/api/core_api.cpp` 的命名常量 | `shell.exec` 的 `timeoutMs` 参数可覆盖 |

源码与构建文件里没有绝对路径；`vendor/` 之外的文件移动位置不影响构建。

⚠️ CEF 只发 **Release** 二进制（分发包里没有 `Debug/`），两个构建系统都必须 Release——
`yubild build` 默认 Debug，会去 `-L.../Debug` 这个不存在的目录，链接必失败。

## 3. 命令行参数

| 参数 | 说明 |
| --- | --- |
| `--url=<url>` | 要加载的地址，默认 `<exe目录>/webui/index.html` |
| `--title=<text>` `--width=<n>` `--height=<n>` | 窗口标题与初始尺寸（默认 1200×800，按工作区居中裁剪） |
| `--sandbox` / `--no-sandbox` | 是否启用 Chromium 沙箱，**默认 `--no-sandbox`** |
| `--inspect` | 打开 CDP 端口（默认 9222） |
| `--inspect-port=<n>` | 指定 CDP 端口（隐含 `--inspect`） |
| `--disable-gpu` / `--verbose` | 关 GPU / 打开 Chromium 日志 |

## 4. API 实现：Registry 与 Call

```
页面 JS/TS
   └─ window.yati.app/fs/window/...        webui/preload.js（Promise 封装、事件、YatiError）
        └─ window.cefQuery(JSON)           CEF message router
             └─ Ipc::OnQuery               src/ipc.cpp（解析信封，构造 api::Call）
                  └─ api::Registry          src/api/api.cpp（通道注册表）
                       └─ 模块 handler       core_api / fs_api / window_api / dialog_api
                            └─ Call::Resolve / Reject → JS Promise resolve/reject
```

- **通道即插槽**：`Registry::Add("fs.readFile", handler)` 一行注册一个能力，加 API 不用改 IPC 层。
- **同步/异步统一**：handler 立刻 `Resolve()` 就是同步应答；把 `Call` 留着稍后应答就是异步
  （原生文件对话框、子进程）。
- **一次只答一次**：`Call::answered_` 是原子的，重复应答被忽略，避免悬空 Promise。
- **错误结构化**：失败走 `{ok:false, error:{code, message}}`，JS 侧抛 `YatiError`。
- **不阻塞 UI**：`shell.exec` 在工作线程 `waitpid`，结果用 `CefPostTask(TID_UI, ...)` 回投。

## 5. 通道清单

51 个通道，`yati.app.channels()` 可运行时列举。

| 模块 | JS 调用 | 通道数 |
| --- | --- | --- |
| app | `info()` `paths()` `quit()` `channels()` `broadcast(event, payload)` | 5 |
| window | `getState()` `setBounds()` `getTitle()` `setTitle()` `minimize/maximize/restore` `setFullscreen()` `setAlwaysOnTop()` `show/hide/activate/center/close` `list()` `create()` | 16 |
| fs | `readFile/readText/readBase64/writeText/writeBase64` `readJson/writeJson` `readDir` `stat` `exists` `mkdir` `remove` `rename` `copy` `realpath` `tempDir` | 14 |
| dialog | `openFile()` `openFolder()` `saveFile()` | 3 |
| shell | `openExternal()` `openPath()` `showItemInFolder()` `exec()` | 4 |
| clipboard | `readText()` `writeText()` | 2 |
| notify | `send(title, message, { urgency, expireMs })` | 1 |
| session | `cookies.get/set/remove/clear` | 4 |
| devtools | `open()` `close()` `isOpen()` | 3 |

`yati.invoke(channel, params)` 可直接打任意通道，用来试探或接入自定义通道。

几个行为约定：

- `window.*` 默认作用于**调用方所在的窗口**；传 `{ windowId }` 可操作其它窗口。`window.create()`
  返回新的 `windowId`，随后会收到 `window:created` 事件。
- `fs.*` 支持 `~` 展开；`readFile` 支持 `encoding: 'base64'` 处理二进制；`remove` 返回删除条目数。
- `shell.exec` 传字符串走 `/bin/sh -c`，传数组则直接 exec（不经 shell，注入风险更低）；默认 10s 超时。
- `session.cookies.get` 是「先收集后冲刷」：CEF 的 `VisitAllCookies` 没有完成回调，收集 250ms 后统一返回。

## 6. 错误码

| code | 含义 |
| --- | --- |
| `-32000` | 请求格式错误 |
| `-32001` | 未知通道 |
| `-32002` | 参数非法（缺字段/值不合法） |
| `-32003` | 找不到（文件、窗口、路径等） |
| `-32004` | 文件系统 IO 失败 |
| `-32006` | 当前环境不支持（缺少 wl-copy / notify-send / xdg-open 等） |
| `-32007` | 内部错误 |

## 7. 事件

C++ 通过 `Ipc::Emit()` 把事件推到每个窗口，JS 用 `yati.on(name, cb)` 订阅：

| 事件 | 载荷 |
| --- | --- |
| `window:created` | `{ windowId, browserId, title, url }` |
| `window:bounds` | `{ windowId, x, y, width, height, maximized, fullscreen }` |
| `window:focus` / `window:blur` | `{ windowId }` |
| `window:closing` / `window:closed` | `{ windowId }` |
| `app:ready` | `{ url, status }` |

`yati.on('*', cb)` 收所有事件；`app.broadcast()` 广播自定义事件到所有窗口。

生命周期：窗口计数驱动应用退出（Electron 语义）——最后一个浏览器关闭时 `CefQuitMessageLoop()`。

## 8. 架构

**一个可执行文件，两个角色**：CEF 应用不是「主进程 + 子进程可执行文件」两套程序，而是同一个二进制
反复自我启动——需要渲染器时用同一份 `yati` 加 `--type=renderer` 重新 exec。

- `main()` 第一件事是 `CefExecuteProcess()`：子进程调用后**永不返回**；只有浏览器进程继续往下走。
- `CefApp` 同时实现 `CefBrowserProcessHandler`（窗口、IPC 路由、业务）和 `CefRenderProcessHandler`
  （每个渲染进程的 V8 上下文钩子）。
- `libcef.so` 与三份 `.pak`、`icudtl.dat`、`locales/` 必须和可执行文件**同目录**。

**窗口层**：CEF Views（Linux 下 GTK 后端）+ `CefBrowserView`，两者都用 `CEF_RUNTIME_STYLE_ALLOY`
（不要 Chrome 的地址栏/标签页）。窗口 id → 浏览器 id → `CefWindow` 的注册表让 IPC 层反查到窗口对象。

**IPC 路由**：浏览器进程 `CefMessageRouterBrowserSide`，渲染进程 `CefMessageRouterRendererSide`
（在 `OnContextCreated` 注入 `window.cefQuery`）。转发链有三处——`Client::OnProcessMessageReceived`、
`Client::OnBeforeBrowse`、`App::OnContextCreated/OnContextReleased/OnProcessMessageReceived`——
漏一个 IPC 就静默失效。

## 9. 代码结构

```
src/main.cpp        参数解析、CefExecuteProcess、CefSettings、消息循环
src/app.cpp         App：命令行开关、OnContextInitialized 开窗、渲染侧 V8 钩子
src/window.cpp      Window：Views 窗口、窗口 id、注册表、窗口事件推送
src/client.cpp      Client：生命周期、加载、控制台转发、弹窗拦截
src/ipc.cpp         Ipc：消息路由、事件广播、浏览器计数
src/process.cpp     fork/exec + 管道采集（clipboard/shell/notify 的后端）
src/api/api.h|cpp   Call（调用上下文/异步应答）、Registry（通道注册表）、JSON 工具
src/api/core_api.cpp  app / shell / clipboard / notify / cookies / devtools
src/api/fs_api.cpp    fs
src/api/window_api.cpp window
src/api/dialog_api.cpp 原生文件对话框
webui/preload.js    JS API 门面（命名空间、事件、YatiError）
webui/index.html    API 实验台 + 自检
types/yati.d.ts     TypeScript 类型声明
scripts/check-api.mjs CDP 无头验证脚本
tests/process_test.cpp  yubild test 的用例
```

## 10. 验证

页面启动时自动跑 15 个用例（app/fs/window/clipboard/shell/cookies/devtools），也可以无头验证：

```bash
build/bin/yati --no-sandbox --inspect &
node scripts/check-api.mjs      # 退出码 0 表示全部通过
```

```
✓ app.info / app.paths / app.channels（51 channels）
✓ window.getState / window.list
✓ fs.writeText / readText / readDir / writeJson+readJson
✓ clipboard.writeText / readText
✓ shell.exec → "Linux 7.3.3-1-cachyos"
✓ session.cookies.set / get → ["yati_selftest=ok"]
✓ devtools.isOpen
── 自检结束：15/15 通过 ──
```

另已单独验证：错误码透传（`-32003`、`-32001`）、多窗口 `create/list/close`、DevTools `open/isOpen/close`、
原生文件对话框弹出且标题与 `.txt` 过滤器生效。`yubild test` 跑 `tests/process_test.cpp` 9/9 通过。

## 11. 打包注意

- **必须同目录**：`yati`、`libcef.so`、`chrome-sandbox`、三份 `.pak`、`icudtl.dat`、
  `v8_context_snapshot.bin`、`vk_swiftshader_icd.json`、`libvk_swiftshader.so`、`libvulkan.so.1`、
  `locales/`。当前 CMake 构建用软链，正式发布要换成真实文件（含 `webui/`）。
- **rpath**：`libcef.so` 没有 SONAME，用 `-L<Release> -lcef` 链接（而非绝对路径），DT_NEEDED 才是
  `libcef.so`，配 `$ORIGIN` 才能搬目录。`readelf -d yati | grep -E 'NEEDED|RUNPATH'` 可验证。
- **沙箱**：需要 setuid root 的 `chrome-sandbox` 或 user namespace；开发默认 `--no-sandbox`，
  发布时把 `chrome-sandbox` 设为 `root:root 4755` 并传 `--sandbox`。
- **缓存**：`CefSettings.root_cache_path` 固定为 `$XDG_CACHE_HOME/yati`（默认 `~/.cache/yati`）。
- **外部依赖**：剪贴板用 `wl-copy/wl-paste`（Wayland）或 `xclip`；通知用 `notify-send`；
  `shell.open*` 用 `xdg-open`。缺失时返回 `-32006`。

## 12. 下一步

1. **`app://` 自定义协议替代 `file://`**：`file://` 下 `fetch()`、ES module、service worker 基本不可用，
   注册 `CefRegisterSchemeHandlerFactory` 才能把前端当正常站点跑。
2. **preload 隔离**：现在靠页面 `<script src="preload.js">`，改为 `OnContextCreated` 里用 V8 绑定注入。
3. **主进程脚本（Electron 的 `main.js`）**：嵌入式 JS 引擎在浏览器进程跑用户逻辑，`ipcMain` 风格通道。
4. **文件监听 / 托盘 / 菜单**：`fs.watch` 需要轮询线程；托盘要用 GTK 的 `AppIndicator`。
5. **打包**：AppImage/deb + `chrome-sandbox` 权限 + 真实资源文件。

## 13. 已知问题

- **原生文件对话框的「取消」回调未自动验证**：对话框能弹出、标题与过滤器正确，但本机 `wtype` 注入的
  Escape 传不到 CEF 的 GTK 对话框，无法自动关闭，`canceled: true` 这条路径靠 CEF 自身回调保证。
- **`session.cookies.get` 有 250ms 冲刷延迟**：`VisitAllCookies` 没有完成回调；Cookie 很多时可能截断。
- 早期版本启动时见过一次连续 6 条 `*** stack smashing detected ***`（进程存活、功能正常），此后多次
  复跑未复现，未能定位。
- 无 `/dev/dri` 的环境会打印 `drmGetDevices2()` 错误，属正常降级，加 `--disable-gpu` 即可。
