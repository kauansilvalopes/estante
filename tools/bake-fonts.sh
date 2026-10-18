#!/usr/bin/env bash
# Estante - Rebuild the baked SDF font (assets/fonts/noto.huifont) from third_party/noto/NotoSans.ttf.
# The baker comes from ps5-homebrew-ui (tools/font-baker); "european" adds accented Latin, Greek and
# Cyrillic so game names written in those scripts draw. SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cxx=$(command -v "${HOST_CXX:-clang++}")
mkdir -p "$root/build/host" "$root/assets/fonts"
"$cxx" -std=c++20 -O2 -w "$root/tools/font-baker/bake_font.cpp" -o "$root/build/host/bake_font"
"$root/build/host/bake_font" "$root/third_party/noto/NotoSans.ttf" "$root/assets/fonts/noto.huifont" 56 8 2048 european
