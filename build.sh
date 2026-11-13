#!/usr/bin/env bash
# Builds the Yati runtime (and its CEF wrapper) with a project-local CMake and
# Ninja, so no root access / system packages are required.
#
# 工具路径不写死版本号：vendor/tools/ 里放什么版本就用什么版本。
# 需要指定具体可执行文件时用 YATI_CMAKE / YATI_NINJA 覆盖。
set -euo pipefail

cd "$(dirname "$0")"

TOOLS_DIR="${YATI_TOOLS_DIR:-$PWD/vendor/tools}"

# 取目录下第一个匹配项（list 结果按名字排序，多版本时用最新解压的那个）。
first_match() {
  local pattern=$1
  local found
  found=$(ls -d $pattern 2>/dev/null | sort | tail -1 || true)
  printf '%s' "$found"
}

CMAKE="${YATI_CMAKE:-$(first_match "$TOOLS_DIR"/cmake-*/bin/cmake)}"
NINJA="${YATI_NINJA:-$(first_match "$TOOLS_DIR"/ninja)}"

if [ -z "$CMAKE" ] || [ -z "$NINJA" ] || [ ! -x "$CMAKE" ] || [ ! -x "$NINJA" ]; then
  echo "找不到构建工具（cmake / ninja）。" >&2
  echo "  期望位置: $TOOLS_DIR/cmake-*/bin/cmake 与 $TOOLS_DIR/ninja" >&2
  echo "  也可用 YATI_CMAKE / YATI_NINJA 指定可执行文件路径" >&2
  exit 1
fi

BUILD_TYPE="${1:-Release}"
BUILD_DIR="${YATI_BUILD_DIR:-build}"

"$CMAKE" -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_MAKE_PROGRAM="$NINJA" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

"$CMAKE" --build "$BUILD_DIR" --parallel "$(nproc)"

echo
echo "built: $BUILD_DIR/bin/yati"
echo "run:   $BUILD_DIR/bin/yati --no-sandbox"
