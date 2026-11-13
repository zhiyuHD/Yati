#!/usr/bin/env bash
# 下载构建依赖：CEF 二进制分发包 + 项目自带的 cmake/ninja。
#
# 这些文件比较大（CEF 解压后 1.5 GB），所以不进版本库，第一次构建前跑一次即可。
# 已存在的内容会跳过，可以重复执行。
#
# 想换版本就用环境变量覆盖，脚本里的默认值是当前验证过的组合：
#   CEF_VERSION=156.0.28+g57389f0+chromium-156.0.8078.88  CEF_SHA1=...
#   CMAKE_VERSION=4.2.1  NINJA_VERSION=1.13.1
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT=$PWD

CEF_VERSION="${CEF_VERSION:-156.0.28+g57389f0+chromium-156.0.8078.88}"
CEF_SHA1="${CEF_SHA1:-6bd0b932a37503fb0206ac77bf628ebd0496f120}"
CEF_DIST="cef_binary_${CEF_VERSION}_linux64_minimal"
CEF_ARCHIVE="$CEF_DIST.tar.bz2"
# 下载地址里的 "+" 必须转成 %2B，否则服务端会把它当空格
CEF_URL="https://cef-builds.spotifycdn.com/${CEF_ARCHIVE//+/%2B}"

CMAKE_VERSION="${CMAKE_VERSION:-4.2.1}"
NINJA_VERSION="${NINJA_VERSION:-1.13.1}"

mkdir -p "$ROOT/vendor/cef" "$ROOT/vendor/tools"

say() { printf '\n==> %s\n' "$1"; }

fetch() { # fetch <url> <dest>
  if [ -s "$2" ]; then
    echo "    已存在，跳过：$2"
    return
  fi
  echo "    下载 $1"
  curl -fL --retry 5 --retry-delay 3 --retry-all-errors -o "$2.part" "$1"
  mv "$2.part" "$2"
}

# --- CEF -------------------------------------------------------------------
say "CEF $CEF_VERSION"
if [ -d "$ROOT/vendor/cef/$CEF_DIST/cmake" ]; then
  echo "    已解压，跳过"
else
  fetch "$CEF_URL" "$ROOT/vendor/cef/$CEF_ARCHIVE"
  echo "    校验 SHA1"
  echo "$CEF_SHA1  $ROOT/vendor/cef/$CEF_ARCHIVE" | sha1sum -c -
  echo "    解压"
  tar xjf "$ROOT/vendor/cef/$CEF_ARCHIVE" -C "$ROOT/vendor/cef"
  rm -f "$ROOT/vendor/cef/$CEF_ARCHIVE"
fi

# --- cmake / ninja ---------------------------------------------------------
say "CMake $CMAKE_VERSION"
if [ -x "$ROOT/vendor/tools/cmake-$CMAKE_VERSION-linux-x86_64/bin/cmake" ]; then
  echo "    已存在，跳过"
else
  fetch "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz" \
        "$ROOT/vendor/tools/cmake.tar.gz"
  tar xzf "$ROOT/vendor/tools/cmake.tar.gz" -C "$ROOT/vendor/tools"
  rm -f "$ROOT/vendor/tools/cmake.tar.gz"
fi

say "Ninja $NINJA_VERSION"
if [ -x "$ROOT/vendor/tools/ninja" ]; then
  echo "    已存在，跳过"
else
  fetch "https://github.com/ninja-build/ninja/releases/download/v$NINJA_VERSION/ninja-linux.zip" \
        "$ROOT/vendor/tools/ninja-linux.zip"
  unzip -oq "$ROOT/vendor/tools/ninja-linux.zip" -d "$ROOT/vendor/tools"
  chmod +x "$ROOT/vendor/tools/ninja"
  rm -f "$ROOT/vendor/tools/ninja-linux.zip"
fi

cat <<'EOF'

依赖就绪。接下来：

  ./build.sh                # CMake 构建 → build/bin/yati
  yubild build --release    # 或用 Yubild → build-yubild/release/bin/yati
  build/bin/yati --no-sandbox
EOF
