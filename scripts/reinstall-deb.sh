#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc)}"
CONFIG_DIR_LOWER="$(printf '%s' "${BUILD_TYPE}" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="${REPO_ROOT}/build-${CONFIG_DIR_LOWER}"
JUCE_SOURCE_DIR=""
for candidate in \
    "${BUILD_DIR}/_deps/juce-src" \
    "${REPO_ROOT}/build/_deps/juce-src" \
    "${REPO_ROOT}/build-debug/_deps/juce-src" \
    "${REPO_ROOT}/build-release/_deps/juce-src"
do
    if [[ -d "${candidate}" ]]; then
        JUCE_SOURCE_DIR="${candidate}"
        break
    fi
done

echo "[DJR_Studio] Configure package build (${BUILD_TYPE})..."
cmake_args=(
    -S "${REPO_ROOT}"
    -B "${BUILD_DIR}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
)

if [[ -n "${JUCE_SOURCE_DIR}" ]]; then
    echo "[DJR_Studio] Pakai cache JUCE lokal dari ${JUCE_SOURCE_DIR}"
    cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=${JUCE_SOURCE_DIR}")
fi

cmake "${cmake_args[@]}"

# CPack expands the version declared in CMakeLists.txt into this generated
# configuration. Reading it here keeps the installer aligned with future
# version bumps instead of duplicating a package name in this script.
CPACK_CONFIG="${BUILD_DIR}/CPackConfig.cmake"

if [[ ! -f "${CPACK_CONFIG}" ]]; then
    echo "[DJR_Studio] Konfigurasi CPack tidak ditemukan: ${CPACK_CONFIG}" >&2
    exit 1
fi

PACKAGE_BASENAME="$(sed -nE 's/^set\(CPACK_PACKAGE_FILE_NAME "([^"]+)"\)$/\1/p' "${CPACK_CONFIG}" | head -n 1)"

if [[ -z "${PACKAGE_BASENAME}" ]]; then
    echo "[DJR_Studio] Nama paket tidak ditemukan di: ${CPACK_CONFIG}" >&2
    exit 1
fi

PACKAGE_PATH="${BUILD_DIR}/${PACKAGE_BASENAME}.deb"

echo "[DJR_Studio] Build app..."
cmake --build "${BUILD_DIR}" -j"${JOBS}"

echo "[DJR_Studio] Build .deb package..."
(cd "${BUILD_DIR}" && cpack -G DEB)

if [[ ! -f "${PACKAGE_PATH}" ]]; then
    echo "[DJR_Studio] Package tidak ditemukan di: ${PACKAGE_PATH}" >&2
    exit 1
fi

echo "[DJR_Studio] Install ulang package..."
sudo apt install -y "${PACKAGE_PATH}"

echo "[DJR_Studio] Selesai. Jalankan dengan command: djr_studio"
