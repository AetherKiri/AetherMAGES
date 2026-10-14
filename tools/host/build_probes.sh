#!/usr/bin/env bash
# Caller supplies the existing host build/install and output directories.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
build=${1:?host build directory}
install=${2:?host install directory}
output=${3:?probe output directory}
triplet=${4:-x64-linux-ci}
mkdir -p "$output"
includes=("-I$install/include" "-I$repo/src" "-I$build/include"
  "-I$build/_deps/magic_enum-src/include" "-I$build/vcpkg_installed/$triplet/include"
  "-I$repo/vendor/glad/include" "-I$repo/vendor" "-I$repo/vendor/pcg/include")
libs=("-L$install/lib" "-L$build/vcpkg_installed/$triplet/lib"
  "-Wl,-rpath,$install/lib")
cc -std=c11 -Wall -Wextra -Werror "${includes[@]}" "$repo/tools/host/mo8_long_run.c" \
  "${libs[@]}" -limpacto_host -lm -o "$output/mo8_long_run"
cc -std=c11 -Wall -Wextra -Werror "${includes[@]}" "$repo/tools/host/mo8_visual_test.c" \
  "${libs[@]}" -limpacto_host -o "$output/mo8_visual_test"
c++ -std=c++20 "${includes[@]}" "$repo/tools/host/mo8_movie_probe.cpp" \
  "${libs[@]}" -limpacto_host -o "$output/mo8_movie_probe"
c++ -std=c++20 "${includes[@]}" "$repo/tools/host/mo8_audio_conversion_test.cpp" \
  "${libs[@]}" -limpacto_host -lavcpp -lavformat -lavcodec -lavutil -lswresample \
  -o "$output/mo8_audio_conversion_test"
cc -std=c11 -Wall -Wextra -Werror "${includes[@]}" "$repo/tools/bink_decode_probe.c" \
  "${libs[@]}" -lavformat -lavcodec -lavutil -lswscale -lm -o "$output/bink_decode_probe"
