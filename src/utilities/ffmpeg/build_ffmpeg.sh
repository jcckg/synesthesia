#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
FFMPEG_SOURCE_DIR="${FFMPEG_SOURCE_DIR:-"${ROOT_DIR}/vendor/ffmpeg"}"
FFMPEG_BUILD_ROOT="${FFMPEG_BUILD_ROOT:-"${ROOT_DIR}/build/ffmpeg"}"
FFMPEG_INSTALL_PREFIX="${FFMPEG_INSTALL_PREFIX:-"${FFMPEG_BUILD_ROOT}/install"}"
FFMPEG_STASH_MESSAGE="${FFMPEG_STASH_MESSAGE:-"synesthesia-ffmpeg-build"}"
FFMPEG_SKIP_BUILD="${FFMPEG_SKIP_BUILD:-""}"
UNAME_OUTPUT="$(uname -s)"
MAKE_CMD="make"
PLATFORM="unknown"

case "${UNAME_OUTPUT}" in
  Darwin)
    PLATFORM="macos"
    ;;
  Linux)
    PLATFORM="linux"
    ;;
  MINGW*|MSYS*|CYGWIN*)
    PLATFORM="windows"
    ;;
  *)
    PLATFORM="${UNAME_OUTPUT}"
    ;;
esac

if ! command -v "${MAKE_CMD}" >/dev/null 2>&1; then
  echo "${MAKE_CMD} not found in PATH. Install a GNU make compatible toolchain (e.g. MSYS2 make or Git for Windows) before running this script." >&2
  exit 1
fi

if [[ "${PLATFORM}" == "windows" ]]; then
  export CC=cl
  export CXX=cl
  export LD=link
  export AR=lib
  export STRIP=:
fi

normalise_path() {
  local path="$1"
  if [[ "${PLATFORM}" == "windows" ]]; then
    if command -v cygpath >/dev/null 2>&1; then
      cygpath -m "${path}"
      return
    fi
  fi
  printf "%s" "${path}"
}

if [[ -n "${FFMPEG_SKIP_BUILD}" ]]; then
  exit 0
fi

mkdir -p "${FFMPEG_BUILD_ROOT}"
mkdir -p "${FFMPEG_INSTALL_PREFIX}"

detect_cpu_count() {
  if command -v nproc >/dev/null 2>&1; then
    nproc
  elif [[ "${PLATFORM}" == "macos" ]]; then
    sysctl -n hw.logicalcpu
  elif [[ "${PLATFORM}" == "windows" && -n "${NUMBER_OF_PROCESSORS:-}" ]]; then
    printf "%s\n" "${NUMBER_OF_PROCESSORS}"
  else
    printf "1\n"
  fi
}

restore_stash_if_present() {
  local stash_ref
  stash_ref="$(git -C "${FFMPEG_SOURCE_DIR}" stash list | awk -F: "/${FFMPEG_STASH_MESSAGE}/ {print \$1; exit}")"
  if [[ -n "${stash_ref}" ]]; then
    git -C "${FFMPEG_SOURCE_DIR}" stash apply "${stash_ref}" || true
    git -C "${FFMPEG_SOURCE_DIR}" stash drop "${stash_ref}" || true
  fi
}

stash_build_artifacts() {
  local ref
  while read -r ref; do
    [[ -z "${ref}" ]] && continue
    git -C "${FFMPEG_SOURCE_DIR}" stash drop "${ref}" || true
  done < <(git -C "${FFMPEG_SOURCE_DIR}" stash list | awk -F: "/${FFMPEG_STASH_MESSAGE}/ {print \$1}")

  if [[ -n "$(git -C "${FFMPEG_SOURCE_DIR}" status --porcelain)" ]]; then
    git -C "${FFMPEG_SOURCE_DIR}" stash push -u -m "${FFMPEG_STASH_MESSAGE}" >/dev/null
  fi
}

configure_signature_file="${FFMPEG_BUILD_ROOT}/.configure-signature"
FFMPEG_BINARY_PATH="${FFMPEG_INSTALL_PREFIX}/bin/ffmpeg"
if [[ "${PLATFORM}" == "windows" ]]; then
  FFMPEG_BINARY_PATH="${FFMPEG_BINARY_PATH}.exe"
fi

current_signature="$(cd "${FFMPEG_SOURCE_DIR}" && git rev-parse HEAD)-${PLATFORM}"

if [[ -f "${configure_signature_file}" && -x "${FFMPEG_BINARY_PATH}" ]]; then
  previous_signature="$(< "${configure_signature_file}")"
  if [[ "${previous_signature}" == "${current_signature}" ]]; then
    exit 0
  fi
fi

restore_stash_if_present

pushd "${FFMPEG_SOURCE_DIR}" >/dev/null

PREFIX_FOR_CONFIG="$(normalise_path "${FFMPEG_INSTALL_PREFIX}")"

CONFIGURE_FLAGS=(
  "--prefix=${PREFIX_FOR_CONFIG}"
  "--disable-debug"
  "--disable-doc"
  "--disable-ffplay"
  "--disable-ffprobe"
  "--disable-indev=sndstat"
  "--enable-static"
  "--disable-shared"
  "--enable-muxer=mp4"
  "--enable-encoder=aac"
  "--enable-decoder=aac"
  "--enable-encoder=mpeg4"
  "--enable-decoder=mpeg4"
  "--enable-muxer=mov"
  "--enable-demuxer=mov"
  "--enable-demuxer=mp3"
)

if [[ "${PLATFORM}" != "windows" ]]; then
  CONFIGURE_FLAGS+=("--enable-pic")
fi

case "${PLATFORM}" in
  macos)
    CONFIGURE_FLAGS+=("--enable-videotoolbox")
    ;;
  linux)
    CONFIGURE_FLAGS+=("--enable-libdrm")
    ;;
  windows)
    CONFIGURE_FLAGS+=(
      "--toolchain=msvc"
      "--arch=x86_64"
      "--target-os=win64"
      "--enable-w32threads"
      "--extra-cflags=-MD"
    )
    ;;
esac

if [[ -n "${FFMPEG_EXTRA_CONFIGURE_FLAGS:-}" ]]; then
  # shellcheck disable=SC2206
  EXTRA_FLAGS=( ${FFMPEG_EXTRA_CONFIGURE_FLAGS} )
  CONFIGURE_FLAGS+=("${EXTRA_FLAGS[@]}")
fi

./configure "${CONFIGURE_FLAGS[@]}"

${MAKE_CMD} -j"$(detect_cpu_count)"
${MAKE_CMD} install

licence_dir="${FFMPEG_INSTALL_PREFIX}/licenses"
mkdir -p "${licence_dir}"
cp LICENSE.md "${licence_dir}/FFMPEG-LICENSE.md"
cp COPYING.LGPLv2.1 "${licence_dir}/LGPL-2.1.txt"

popd >/dev/null

printf "%s" "${current_signature}" > "${configure_signature_file}"

stash_build_artifacts
