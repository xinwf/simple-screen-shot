#!/usr/bin/env bash
# Build the Debian (.deb) package for SimpleScreenShot via CMake/CPack.
#
# Usage:
#   ./build-deb.sh           incremental build + package into build-deb/
#   ./build-deb.sh --clean   wipe build-deb/ first, then full rebuild + package
#
# The development build directory (build/) is not touched; packaging uses
# its own build-deb/ directory.
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

BUILD_DIR=build-deb

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR"
    echo "==> 已清除 $BUILD_DIR/"
elif [[ -n "${1:-}" ]]; then
    echo "未知参数: $1 (支持: --clean)" >&2
    exit 2
fi

for dep in cmake dpkg-shlibdeps pkg-config file; do
    if ! command -v "$dep" >/dev/null 2>&1; then
        echo "==> 缺少构建依赖: $dep" >&2
        echo "    Ubuntu/Debian 可执行:" >&2
        echo "    sudo apt install build-essential cmake pkg-config dpkg-dev file \\" >&2
        echo "        libgtk-3-dev libcairo2-dev libpango1.0-dev libfontconfig1-dev \\" >&2
        echo "        libx11-dev libxext-dev libxtst-dev libxfixes-dev libglib2.0-dev" >&2
        exit 1
    fi
done

echo "==> 配置 CMake (Release)"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "==> 编译 ($(nproc) 并行)"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> 生成 deb 包"
rm -f "$BUILD_DIR"/simplescreenshot_*.deb
(cd "$BUILD_DIR" && cpack -G DEB)

DEBS=( "$BUILD_DIR"/simplescreenshot_*.deb )
DEB="${DEBS[${#DEBS[@]}-1]}"
echo "==> 完成: $DEB ($(du -h "$DEB" | cut -f1))"

if command -v lintian >/dev/null 2>&1; then
    echo "==> lintian 质检"
    lintian "$DEB" && echo "==> lintian 通过,无问题" || true
fi
