# Yubild × Yati：两轮实测记录

## 第一轮（16:20，缺口报告）

结论：当时 Yubild 缺两件事——任意 CMake 前导片段、POST_BUILD 命令。证据是
`SET_LIBRARY_TARGET_PROPERTIES` 未定义（CEF 的 `cef_macros.cmake` 没被加载），且 Yati 需要把
12 个运行期文件摆到 exe 旁边。

**状态：已在 16:47 版本解决**，分别对应 `yubild.cmake(...)` 与 `post_build=[copy/copy_dir/cmd]`。
当时那份「补齐后应该长什么样」的对照文件仍保留在 `docs/yubild-proof-CMakeLists.txt`。

## 第二轮（16:50，用新版本实跑）

```console
$ yubild build --release
==> 构建完成：3 个 target，产出目录 <repo>/build-yubild/release/bin
    real 2m7.6s        # 含 libcef_dll_wrapper 的 223 个编译单元

$ node scripts/check-api.mjs
✓ app.info ... ── 自检结束：15/15 通过 ──
```

产物核对：

| 检查项 | 结果 |
| --- | --- |
| `bin/` 下 12 个运行期文件 + `locales/`(220) + `webui/` | ✅ 全部由 `post_build` 摆好 |
| `readelf -d yati` | ✅ `NEEDED libcef.so`、`RUNPATH $ORIGIN` |
| 直接运行 | ✅ 窗口起来、15 项 API 自检全过 |
| 生成的 `CMakeLists.txt` | ✅ 前导片段原样注入在 `project()` 之后、`add_subdirectory` 之前 |
| `post_build` 生成形式 | ✅ `copy_if_different` + `$<TARGET_FILE_DIR:yati>` + `copy_directory` + `VERBATIM` |

`yubild test`（新增的测试系统，实测）：

```console
▶ process_tests
共 9 个用例
  [1/9] command_exists_finds_shell ✓ 0.134ms
  ...
  [7/9] run_process_kills_on_timeout ✓ 0.826ms
9 个用例：9 通过, 0 失败, 用时 0.31s
```

用例写在 `tests/process_test.cpp`，覆盖 `src/process.cpp` 的 fork/exec、stdout/stderr 采集、
stdin、退出码、超时 kill、缺失命令。测试框架的 `YB_TEST`/`YB_CHECK_EQ` 用起来没有摩擦，
逐用例实时输出比 `ctest` 友好得多。

---

## 下一轮可以修的三个点（都在变异系统上）

### 1. 变异体合法率偏低：40 个里 15 个编译不过（37.5%）

```console
  [1/6] src/process.cpp:21  < -> > ... 构建失败（变异体本身不合法）
```

第 21 行是 `std::vector<char*> BuildArgv(const std::vector<std::string>& argv)`，
`<`/`>` 被当成比较运算符、`*` 被当成乘法。同理 `std::vector<char*> result;` 里的 `* -> /` 也不合法。

数值上：`yubild mutate --filter src/process.cpp --max 40` → 杀死 11 / 存活 14 / 构建失败 15，
也就是说 **37.5% 的时间花在必然失败的重建上**，而全部 185 个变异点的完整跑估计要 ~18 分钟。

建议（按实现成本从低到高）：

1. 跳过模板实参：`<`/`>` 处于 `<>` 嵌套内、或紧跟在标识符后且后面是类型名形态时不动。
   简单可用的近似：统计该行 `<` 与 `>` 的配对深度，深度 > 0 的 `<` 不做变异。
2. 跳过声明位置的 `*`：`*` 前面是类型关键字（`char`/`int`/`std`…）或 `*` 后面紧跟 `>`、`)`、`;`、标识符 时不动。
3. 生成后先做一次语法预检（`-fsyntax-only` 单个 TU）再进重建流程——比整树重建快一个量级，
   且能把「非法变异」和「构建失败」区分开，报告里两个桶分开更有诊断价值。

### 2. mutants 副本把整个 vendor 拷了一遍：2.1 GB / 次

`build-yubild/mutants/` 是 2.1G，其中 `mutants/vendor` 占 2.1G（CEF SDK 1.5G + 源码包 318M）。
CEF 这种「依赖是仓库里的大二进制 SDK」的项目很常见，每次变异都复制一遍代价很高。

建议：`yubild.py` 里加 `project(..., mutate_exclude=["vendor", "build", "build-*"])`，
或者在复制时自动跳过 `.gitignore` 命中的路径 + 大于阈值的文件；更省事的做法是用硬链接复制
（`cp -al` 语义），变异时先删后写断链，源码树同样不会被污染。

### 3. （非 bug，权衡）`copy` 让开发期改了页面要重新构建

`post_build` 的 `copy_dir("webui", ...)` 是真实拷贝，所以改 `webui/index.html` 后必须重新 build
才生效；Yati 原来的 CMake 构建是软链到源码，改完刷新即见。

如果 `cmd()` 之外再加一个 `symlink(*sources, dest=...)` 动作，开发期就能两全：
发布用 `copy`，日常用 `symlink`。现在可以临时用
`yubild.cmd("${CMAKE_COMMAND}", "-E", "create_symlink", "@src/webui", "@exe_dir/webui")` 绕过。

---

## 顺带：`examples/cef/yubild.py` 与真实 CEF 有 5 处不符

那个示例自己标了「尚未在真实 CEF 上验证」，实测下来确实要改（修正版见
`docs/yubild-cef-example-fixed.py`，可直接替换 `examples/cef/yubild.py`）：

| # | 示例写法 | 实际需要 |
| --- | --- | --- |
| 1 | `include_dirs=[CEF_ROOT/include]` | `[CEF_ROOT]`。CEF 头文件按 `"include/cef_app.h"` 引用，多一层 `include/` 会全找不到 |
| 2 | 只有 `link_libraries=["libcef_dll_wrapper"]` | 还要 `"cef"`（libcef.so 本身）、`"X11"`、`"${CMAKE_DL_LIBS}"`；只链 wrapper 会缺 `cef_string_*` 等符号 |
| 3 | 没有 `dependency(kind="subdir", subdir="cef/libcef_dll")` | 必须有，否则 `libcef_dll_wrapper` 这个 target 根本不存在 |
| 4 | 资源取自 `cef/Release/*.pak`、`Release/locales` | CEF 156 的 `chrome_*.pak`、`resources.pak`、`icudtl.dat`、`locales/` 都在 **`Resources/`**；`Release/` 里只有 `libcef.so`、`chrome-sandbox`、swiftshader |
| 5 | `cxx_std=17`，且没有 `-L` 与 rpath | CEF 156 需要 **C++20**；`libcef.so` 没有 SONAME，必须 `-L${CEF_BINARY_DIR} -lcef` + `$ORIGIN` rpath，否则要么链接失败、要么运行期找不到库 |

## 现在的验收命令

```bash
cd Yati
yubild build --release                    # 产物在 build-yubild/release/bin
./build-yubild/release/bin/yati --no-sandbox --inspect &
node scripts/check-api.mjs                # 15/15
yubild test                               # 9/9
yubild mutate --filter 'src/process.cpp'  # 44%（15 个变异体不合法，见上）
```
