#!/usr/bin/env bash
# Renders every screen through the GPU path (kit renderer on Mesa's surfaceless EGL) to .local/gl-*.png.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
mkdir -p build/tests .local
clang-18 -O2 -c src/cover_decode.c -o build/tests/cover-host.o
clang-18 -O2 -c src/cJSON.c -o build/tests/cjson-preview.o
SDK=.deps/ps5-opengl/current/include
clang++-18 -std=c++20 -O1 -g -DESTANTE_HOST_PREVIEW -DESTANTE_GL=1 -DGL_GLEXT_PROTOTYPES=1 \
  -Isrc -I$SDK -ffunction-sections -fdata-sections \
  tools/preview-estante.cpp tools/gl_host_platform.cpp src/gl_canvas.cpp src/gl3d.cpp src/gfx/*.cpp src/core/save_file.cpp \
  src/library.cpp src/experience.cpp build/tests/cover-host.o build/tests/cjson-preview.o \
  -Wl,--gc-sections -pthread -l:libEGL.so.1 -l:libGL.so.1 -lm -o build/tests/preview-estante-gl
modes=${*:-home homemid homewant homenowide library libraryscroll libchips details detailstroph detailsnowide shelf apps sidebar collections settings menu search large rec empty ps2 anim0 anim6 anim10 flip turn4 back6}
for mode in $modes; do
  build/tests/preview-estante-gl ".local/gl-$mode.ppm" "$mode"
done
python3 - <<'PY'
from pathlib import Path
import struct,zlib
def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
for p in Path('.local').glob('gl-*.ppm'):
 data=p.read_bytes().split(b'\n',3)[3];w,h=1920,1080
 raw=b''.join(b'\0'+data[y*w*3:(y+1)*w*3] for y in range(h))
 p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
PY
