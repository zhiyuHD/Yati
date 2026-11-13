"""CEF 应用的 Yubild 配置（按 CEF 156 linux64 实测修正）。

替换 examples/cef/yubild.py 使用。相对原示例的 5 处修正：

1. include_dirs 用 CEF 根目录本身（CEF 头文件按 "include/cef_app.h" 引用）；
2. 链接项补全：libcef_dll_wrapper + cef + X11 + ${CMAKE_DL_LIBS}；
3. 用 dependency(kind="subdir") 编出 libcef_dll_wrapper；
4. 资源从 Resources/ 取（Release/ 里只有 libcef.so、chrome-sandbox、swiftshader）；
5. C++20，并加 -L${CEF_BINARY_DIR} 与 $ORIGIN rpath（libcef.so 没有 SONAME）。

准备：下载 CEF 156 linux64 minimal/standard 分发包，解压为 ./cef/，然后：

    yubild build --release
    ./build/release/bin/cefclient
"""

import os

import yubild

HERE = os.path.dirname(os.path.abspath(__file__))
CEF_ROOT = os.path.join(HERE, "cef")
RELEASE = os.path.join(CEF_ROOT, "Release")
RESOURCES = os.path.join(CEF_ROOT, "Resources")

if not os.path.isdir(CEF_ROOT):
    raise yubild.YubildError(
        f"没有找到 CEF 二进制：{CEF_ROOT}\n"
        "请下载 CEF Standard Distribution 并解压为 ./cef/ 后再执行 yubild"
    )

yubild.project(name="cefclient", version="0.1.0", cxx_std=20)

# --- 前导片段：CEF 的 FindCEF.cmake 在分发包里，必须先告诉 CMake 去哪找 ---
yubild.cmake(f'set(CEF_ROOT "{CEF_ROOT}")')
yubild.cmake('list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")')
yubild.cmake("find_package(CEF REQUIRED)")

# --- CEF 的 C++ 包装层（libcef_dll_wrapper）---
yubild.dependency("cef", kind="subdir", subdir=os.path.join("cef", "libcef_dll"))

yubild.executable(
    "cefclient",
    sources=["src/*.cpp"],
    include_dirs=[CEF_ROOT, "src"],
    link_libraries=["libcef_dll_wrapper", "cef", "X11", "${CMAKE_DL_LIBS}"],
    link_options=["-L${CEF_BINARY_DIR}", "-Wl,-rpath,$ORIGIN"],
    post_build=[
        # 动态库与沙箱
        yubild.copy(
            os.path.join(RELEASE, "libcef.so"),
            os.path.join(RELEASE, "chrome-sandbox"),
            os.path.join(RELEASE, "libvk_swiftshader.so"),
            os.path.join(RELEASE, "libvulkan.so.1"),
            os.path.join(RELEASE, "v8_context_snapshot.bin"),
            os.path.join(RELEASE, "vk_swiftshader_icd.json"),
            dest="exe_dir",
        ),
        # 资源：注意 156 把它们放在 Resources/
        yubild.copy(
            os.path.join(RESOURCES, "chrome_100_percent.pak"),
            os.path.join(RESOURCES, "chrome_200_percent.pak"),
            os.path.join(RESOURCES, "resources.pak"),
            os.path.join(RESOURCES, "icudtl.dat"),
            dest="exe_dir",
        ),
        yubild.copy_dir(os.path.join(RESOURCES, "locales"), dest="exe_dir"),
        # 应用自己的前端资源
        yubild.copy_dir("webui", dest="exe_dir"),
    ],
)
