#!/usr/bin/env bash
# Build the single Android arm64 artifact used by run.sh.
set -euo pipefail

cd "$(dirname "$0")"

if [[ -z "${ANDROID_NDK_HOME:-}" ]]; then
  for candidate in "$HOME"/android-ndk-r2[6-9]* /opt/android-ndk-r2[6-9]*; do
    [[ -x "$candidate/ndk-build" ]] || continue
    export ANDROID_NDK_HOME="$candidate"
    echo "Using auto-detected ANDROID_NDK_HOME=$ANDROID_NDK_HOME"
    break
  done
fi
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to Android NDK r26d or newer.}"

make clean
make -j"$(getconf _NPROCESSORS_ONLN)"
make sha256
