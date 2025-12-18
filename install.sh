#!/bin/sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
PACKAGE_VERSION="${1:-$(date +%Y%m%d)}"

JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"

# Build
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Only enable CPack if dpkg is available
if command -v dpkg >/dev/null 2>&1; then
	cmake -S .. -B . \
		-DCMAKE_BUILD_TYPE=Release \
		-DWITH_CPACK=ON \
		-DCPACK_PACKAGE_VERSION="${PACKAGE_VERSION}"
	cmake --build . --config Release -- -j "${JOBS}"
	cpack -G DEB -D CPACK_DEBIAN_PACKAGE_ARCHITECTURE="$(dpkg --print-architecture)"
else
	echo "Note: dpkg not found; building without packaging." >&2
	cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release
	cmake --build . --config Release -- -j "${JOBS}"
fi
