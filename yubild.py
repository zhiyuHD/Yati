import glob
import os

import yubild

# 版本号只有在 VERSION 文件里写一次；构建系统读它并注入到代码里。
ROOT = os.path.dirname(os.path.abspath(__file__))
VERSION = open(os.path.join(ROOT, "VERSION"), encoding="utf-8").read().strip()
assert VERSION, "VERSION 文件为空"

# CEF 分发包目录名自带版本串，这里按模式挑一个，不把版本写进配置。
# 只认解压后的目录：同一目录里的 .tar.bz2 也匹配通配符，必须过滤。
_dists = sorted(
    d
    for d in glob.glob(os.path.join(ROOT, "vendor/cef/cef_binary_*_linux64_*"))
    if os.path.isdir(os.path.join(d, "cmake"))
)
if not _dists:
    raise yubild.YubildError(
        "vendor/cef/ 下没有找到 CEF 分发包；下载解压后重试（地址见 README 技术笔记）"
    )
CEF_ROOT = os.path.relpath(_dists[-1], ROOT)
CEF_RELEASE = f"{CEF_ROOT}/Release"
CEF_RESOURCES = f"{CEF_ROOT}/Resources"

yubild.project(name="yati", version=VERSION, cxx_std=20, build_dir="build-yubild")

# --- 前导片段（project() 之后、target 之前） ---
# 版本号以编译期宏进代码，避免在 C++/JS 里重复写字面量。
yubild.cmake(f'add_compile_definitions(YATI_VERSION="{VERSION}")')
yubild.cmake(f'set(CEF_ROOT "${{YUBILD_SOURCE_ROOT}}/{CEF_ROOT}")')
yubild.cmake('list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")')
yubild.cmake("find_package(CEF REQUIRED)")

# --- CEF 的 C++ 包装层 ---
yubild.dependency("cef", kind="subdir", subdir=f"{CEF_ROOT}/libcef_dll")

yubild.executable(
    "yati",
    sources=[
        "src/main.cpp",
        "src/app.cpp",
        "src/window.cpp",
        "src/client.cpp",
        "src/ipc.cpp",
        "src/process.cpp",
        "src/manifest.cpp",
        "src/scheme.cpp",
        "src/api/api.cpp",
        "src/api/core_api.cpp",
        "src/api/fs_api.cpp",
        "src/api/window_api.cpp",
        "src/api/dialog_api.cpp",
    ],
    # CEF 头文件按 "include/cef_app.h" 引用，所以根目录本身要在搜索路径里。
    include_dirs=[CEF_ROOT, "src"],
    link_libraries=["libcef_dll_wrapper", "cef", "X11", "${CMAKE_DL_LIBS}"],
    # -L 指向 CEF 的 Release 目录（CEF_BINARY_DIR 由 find_package(CEF) 定义）；
    # $ORIGIN 让运行期从 exe 同目录找 libcef.so。
    link_options=["-L${CEF_BINARY_DIR}", "-Wl,-rpath,$ORIGIN"],
    post_build=[
        # 运行期必需：动态库与沙箱
        yubild.copy(
            f"{CEF_RELEASE}/libcef.so",
            f"{CEF_RELEASE}/chrome-sandbox",
            f"{CEF_RELEASE}/libvk_swiftshader.so",
            f"{CEF_RELEASE}/libvulkan.so.1",
            f"{CEF_RELEASE}/v8_context_snapshot.bin",
            f"{CEF_RELEASE}/vk_swiftshader_icd.json",
            dest="exe_dir",
        ),
        # 资源：注意 156 的分发把 pak/icudtl 放在 Resources/，不是 Release/
        yubild.copy(
            f"{CEF_RESOURCES}/chrome_100_percent.pak",
            f"{CEF_RESOURCES}/chrome_200_percent.pak",
            f"{CEF_RESOURCES}/resources.pak",
            f"{CEF_RESOURCES}/icudtl.dat",
            dest="exe_dir",
        ),
        yubild.copy_dir(f"{CEF_RESOURCES}/locales", dest="exe_dir"),
        # 前端资源：拷到 exe 旁边的 webui/
        yubild.copy_dir("webui", dest="exe_dir"),
    ],
)

# 单元测试：process.cpp 是纯 POSIX（fork/exec + 管道），不依赖 CEF，适合单独测。
yubild.test(
    "process_tests",
    sources=["tests/process_test.cpp"],
    deps=["yati_process"],
)

yubild.static_library(
    "yati_process",
    sources=["src/process.cpp"],
    public_includes=["src"],
)
