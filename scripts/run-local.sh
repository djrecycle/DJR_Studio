#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
JOBS="${JOBS:-$(nproc)}"
CONFIG_DIR="${BUILD_TYPE}"
CONFIG_DIR_LOWER="$(printf '%s' "${CONFIG_DIR}" | tr '[:upper:]' '[:lower:]')"
BUILD_DIR="${REPO_ROOT}/build-${CONFIG_DIR_LOWER}"
BINARY_PATH="${BUILD_DIR}/djr_studio_artefacts/${CONFIG_DIR}/djr_studio"
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

echo "[DJR_Studio] Configure build (${BUILD_TYPE})..."
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

echo "[DJR_Studio] Build app..."
cmake --build "${BUILD_DIR}" -j"${JOBS}"

if [[ ! -x "${BINARY_PATH}" ]]; then
    echo "[DJR_Studio] Binary tidak ditemukan di: ${BINARY_PATH}" >&2
    exit 1
fi

echo "[DJR_Studio] Menjalankan binary terbaru..."
exec "${BINARY_PATH}"
