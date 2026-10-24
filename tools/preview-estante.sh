#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
mkdir -p build/tests .local
clang-18 -O2 -c src/cover_decode.c -o build/tests/cover-host.o
clang-18 -O2 -c src/cJSON.c -o build/tests/cjson-preview.o
clang++-18 -std=c++20 -O1 -g -fsanitize=address,undefined -DESTANTE_HOST_PREVIEW -ffunction-sections -fdata-sections tools/preview-estante.cpp src/demo_renderer.cpp src/library.cpp src/experience.cpp build/tests/cover-host.o build/tests/cjson-preview.o -Wl,--gc-sections -pthread -o build/tests/preview-estante
for mode in home homemid shelf library libchips apps sidebar collections settings details detailsnowide menu search large rec empty i18n ps2 anim0 anim3 anim6 anim10; do
 build/tests/preview-estante ".local/preview-$mode.ppm" "$mode"
done
python3 - <<'PY'
from pathlib import Path
import struct,zlib
def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
for p in Path('.local').glob('preview-*.ppm'):
 data=p.read_bytes().split(b'\n',3)[3];w,h=1920,1080
 raw=b''.join(b'\0'+data[y*w*3:(y+1)*w*3] for y in range(h))
 p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
PY
