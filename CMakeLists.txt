cmake_minimum_required(VERSION 3.21)

# 版本号只有一个来源：仓库根的 VERSION 文件。
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _yati_version_raw)
string(STRIP "${_yati_version_raw}" YATI_VERSION)
if(YATI_VERSION STREQUAL "")
  message(FATAL_ERROR "VERSION 文件为空")
endif()

# CEF's cmake modules probe both C and C++ compiler flags.
project(Yati VERSION ${YATI_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# CEF SDK
#
# Path to the CEF binary distribution downloaded from
# https://cef-builds.spotifycdn.com/ (see README.md for the exact package).
# ---------------------------------------------------------------------------
# 默认从 vendor/cef/ 里自动挑最新解压的分发包，不在构建文件里写死版本串；
# 需要指定别的目录时传 -DCEF_ROOT=...。
# 缓存里的路径失效（目录被删/改名）时重新探测。
if(CEF_ROOT AND NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
  message(STATUS "缓存的 CEF_ROOT 无效，重新探测：${CEF_ROOT}")
  unset(CEF_ROOT CACHE)
endif()

if(NOT CEF_ROOT)
  # 只认目录：同一目录下的 .tar.bz2 也会被通配符匹配到，必须过滤掉。
  file(GLOB _yati_cef_entries "${CMAKE_CURRENT_SOURCE_DIR}/vendor/cef/cef_binary_*_linux64_*")
  set(_yati_cef_dists "")
  foreach(_entry IN LISTS _yati_cef_entries)
    if(IS_DIRECTORY "${_entry}/cmake")
      list(APPEND _yati_cef_dists "${_entry}")
    endif()
  endforeach()
  list(SORT _yati_cef_dists)
  list(LENGTH _yati_cef_dists _yati_cef_count)
  if(_yati_cef_count EQUAL 0)
    message(FATAL_ERROR
      "vendor/cef/ 下没有找到 CEF 分发包（需要含 cmake/FindCEF.cmake 的目录）。\n"
      "下载并解压到该目录，或传 -DCEF_ROOT=<解压后的目录>。\n"
      "地址见 docs/technical-notes.md。")
  elseif(_yati_cef_count GREATER 1)
    message(STATUS "发现 ${_yati_cef_count} 个 CEF 分发包，使用最后一个：${_yati_cef_dists}")
  endif()
  list(GET _yati_cef_dists -1 CEF_ROOT)
endif()

set(CEF_ROOT "${CEF_ROOT}" CACHE PATH "Path to the CEF binary distribution" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
add_subdirectory("${CEF_ROOT}/libcef_dll" libcef_dll_wrapper)

# A CEF application is one executable that re-launches itself as the
# renderer / GPU / utility process, so libcef.so and the resource bundles must
# live in the same directory as the binary.
set(YATI_BIN_DIR "${CMAKE_BINARY_DIR}/bin")

add_executable(yati
  src/main.cpp
  src/app.cpp
  src/window.cpp
  src/client.cpp
  src/ipc.cpp
  src/process.cpp
  src/api/api.cpp
  src/api/core_api.cpp
  src/api/fs_api.cpp
  src/api/window_api.cpp
  src/api/dialog_api.cpp)

# CEF headers are included as "include/cef_*.h" and "wrapper/cef_*.h", so the
# distribution root must be on the include path.
target_include_directories(yati PRIVATE
  "${CEF_ROOT}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src")

# 代码里的版本号全部来自这个宏，不在源码里重复写字面量。
target_compile_definitions(yati PRIVATE YATI_VERSION="${YATI_VERSION}")

# libcef.so ships without a SONAME, so link it through the library search path
# (rather than by absolute path) to get a relocatable DT_NEEDED entry that the
# $ORIGIN rpath can resolve next to the executable.
target_link_directories(yati PRIVATE "${CEF_BINARY_DIR}")
target_link_libraries(yati PRIVATE libcef_dll_wrapper cef ${CEF_STANDARD_LIBS})

set_target_properties(yati PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${YATI_BIN_DIR}"
  BUILD_RPATH "$ORIGIN"
  INSTALL_RPATH "$ORIGIN")

# Stage the CEF runtime next to the executable. Symlinks are used on purpose:
# copying libcef.so (1.4 GB) on every build would be wasteful.
set(YATI_CEF_BIN_FILES
  chrome-sandbox
  libcef.so
  libvk_swiftshader.so
  libvulkan.so.1
  v8_context_snapshot.bin
  vk_swiftshader_icd.json)

set(YATI_CEF_RESOURCE_FILES
  chrome_100_percent.pak
  chrome_200_percent.pak
  icudtl.dat
  resources.pak)

foreach(_file IN LISTS YATI_CEF_BIN_FILES)
  add_custom_command(TARGET yati POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f "${YATI_BIN_DIR}/${_file}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CEF_BINARY_DIR}/${_file}" "${YATI_BIN_DIR}/${_file}"
    COMMENT "Staging ${_file}")
endforeach()

foreach(_file IN LISTS YATI_CEF_RESOURCE_FILES)
  add_custom_command(TARGET yati POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f "${YATI_BIN_DIR}/${_file}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CEF_RESOURCE_DIR}/${_file}" "${YATI_BIN_DIR}/${_file}"
    COMMENT "Staging resources/${_file}")
endforeach()

add_custom_command(TARGET yati POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${YATI_BIN_DIR}/locales"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
          "${CEF_RESOURCE_DIR}/locales" "${YATI_BIN_DIR}/locales"
  COMMENT "Staging locales")

# The web UI is linked rather than copied so edits under webui/ are live on the
# next page reload. Packaging replaces this with real files.
add_custom_target(stage_webui ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${YATI_BIN_DIR}/webui"
  COMMAND ${CMAKE_COMMAND} -E create_symlink
          "${CMAKE_CURRENT_SOURCE_DIR}/webui" "${YATI_BIN_DIR}/webui"
  COMMENT "Linking webui from source")

add_custom_target(run
  COMMAND "${YATI_BIN_DIR}/yati" --no-sandbox
  DEPENDS yati
  USES_TERMINAL)
