#!/usr/bin/env python3
"""Find the missing SwanStationPS5 box art by the game's boot serial instead of by its (localized) name.

For every folder in /data/SwanStationPS5/games the script reads the start of the disc image over FTP
(REST, so it can reach a SYSTEM.CNF that sits far into the image), takes the BOOT serial (for example
SLUS-00684) and looks it up in the public xlenore/psx-covers collection, which names its covers by serial.

Nothing is written unless --apply is given; a cover that already exists is never replaced unless --force is
given. The downloaded picture is converted to PNG with Pillow (pip install pillow) and saved under the name
SwanStationPS5 looks for when it finds no serial of its own: the title with every byte that is not an ASCII
letter or digit turned into '_', cut at 63 characters (src/library.c in SwanStationPS5).

Artwork is for personal use and stays out of the repository.
"""
import argparse
import io
import re
import struct
import sys
import urllib.error
import urllib.request
from ftplib import FTP

GAMES = "/data/SwanStationPS5/games"
BOXART = "/data/SwanStationPS5/covers/boxart"
COVERS = "https://raw.githubusercontent.com/xlenore/psx-covers/main/covers/default/%s.jpg"
UA = "Estante serial cover sync (personal library)"
SYNC = bytes([0]) + bytes([255]) * 10 + bytes([0])
TRACK_EXTENSIONS = (".bin", ".img", ".iso")


def connect(host):
    ftp = FTP(encoding="utf-8")
    ftp.connect(host, 2121, timeout=45)
    ftp.login("anonymous", "estante")
    ftp.set_pasv(True)
    return ftp


def listing(ftp, path):
    lines = []
    ftp.retrlines("LIST " + path, lines.append)
    out = []
    for line in lines:
        parts = line.split(None, 8)
        if len(parts) >= 9 and parts[8] not in (".", ".."):
            out.append((parts[0][0] == "d", parts[8], int(parts[4])))
    return out


class _Enough(Exception):
    pass


def read_at(host, path, offset, length):
    """Read `length` bytes at `offset` with FTP REST. A fresh connection per read keeps the abort simple."""
    ftp = connect(host)
    chunks, got = [], 0

    def sink(block):
        nonlocal got
        chunks.append(block)
        got += len(block)
        if got >= length:
            raise _Enough

    try:
        ftp.retrbinary("RETR " + path, sink, blocksize=65536, rest=offset)
    except _Enough:
        pass
    finally:
        try:
            ftp.close()
        except OSError:
            pass
    return b"".join(chunks)[:length]


class Image:
    """2048-byte sectors out of a cooked or raw (2352-byte) disc image."""

    def __init__(self, host, path):
        self.host, self.path = host, path
        head = read_at(host, path, 0, 2352 * 17)
        if head[:12] == SYNC:
            self.size = 2352
            self.offset = 24 if head[15] == 2 else 16
        else:
            self.size, self.offset = 2048, 0

    def sector(self, lba, count=1):
        raw = read_at(self.host, self.path, lba * self.size, self.size * count)
        return b"".join(raw[i * self.size + self.offset:i * self.size + self.offset + 2048] for i in range(count))


def boot_serial(image):
    pvd = image.sector(16)
    if pvd[1:6] != b"CD001":
        return None
    root_lba = struct.unpack("<I", pvd[158:162])[0]
    root_len = struct.unpack("<I", pvd[166:170])[0]
    if root_len == 0 or root_len > 64 * 2048:
        return None
    directory = image.sector(root_lba, (root_len + 2047) // 2048)
    pos = 0
    while pos < root_len and pos < len(directory):
        length = directory[pos]
        if length == 0:
            pos = (pos // 2048 + 1) * 2048
            continue
        name = directory[pos + 33:pos + 33 + directory[pos + 32]].decode("latin1")
        if name.upper().startswith("SYSTEM.CNF"):
            lba = struct.unpack("<I", directory[pos + 2:pos + 6])[0]
            text = image.sector(lba).decode("latin1")
            match = re.search(r"BOOT\s*=\s*cdrom:\\?([A-Z]{4})[_-]?(\d{3})\.?(\d{2})", text, re.I)
            if match:
                return "%s-%s%s" % (match.group(1).upper(), match.group(2), match.group(3))
            return None
        pos += length
    return None


def swan_filename(title):
    # SwanStationPS5 src/library.c: per BYTE, ASCII letters and digits only, 63 characters at most.
    raw = title.encode("utf-8")
    return "".join(chr(b) if b < 0x80 and chr(b).isalnum() else "_" for b in raw)[:63] + ".png"


def track_for(host, ftp, folder):
    files = listing(ftp, GAMES + "/" + folder)
    cue = [n for _, n, _ in files if n.lower().endswith(".cue")]
    if cue:
        buffer = io.BytesIO()
        ftp.retrbinary("RETR %s/%s/%s" % (GAMES, folder, cue[0]), buffer.write)
        match = re.search(r'FILE\s+"([^"]+)"', buffer.getvalue().decode("latin1"))
        if match:
            return match.group(1)
    tracks = sorted(n for _, n, _ in files if n.lower().endswith(TRACK_EXTENSIONS))
    return tracks[0] if tracks else None


def to_png(jpeg, limit=900_000):
    from PIL import Image as Picture  # imported late so a dry run needs no dependency

    picture = Picture.open(io.BytesIO(jpeg)).convert("RGB")
    for edge in (640, 512, 400):
        copy = picture.copy()
        copy.thumbnail((edge, edge))
        out = io.BytesIO()
        copy.save(out, "PNG", optimize=True)
        if out.tell() <= limit:
            return out.getvalue()
    return out.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="192.168.2.116")
    parser.add_argument("--apply", action="store_true", help="download, convert and upload (default: only report)")
    parser.add_argument("--force", action="store_true", help="replace a cover that already exists")
    args = parser.parse_args()

    ftp = connect(args.host)
    existing = {n for d, n, _ in listing(ftp, BOXART) if not d}
    if args.apply:
        try:
            import PIL  # noqa: F401
        except ImportError:
            sys.exit("--apply needs Pillow: pip install pillow")
    folders = [n for d, n, _ in listing(ftp, GAMES) if d]
    for folder in folders:
        target = swan_filename(folder)
        track = track_for(args.host, ftp, folder)
        if not track:
            print("no image    %-50s (no .cue/.bin/.iso; a .chd cannot be read this way)" % folder)
            continue
        try:
            serial = boot_serial(Image(args.host, "%s/%s/%s" % (GAMES, folder, track)))
        except (OSError, struct.error, ValueError) as error:
            serial = None
            print("read error  %-50s %r" % (folder, error))
            continue
        if not serial:
            print("no serial   %-50s" % folder)
            continue
        url = COVERS % serial
        request = urllib.request.Request(url, method="HEAD", headers={"User-Agent": UA})
        try:
            with urllib.request.urlopen(request, timeout=20):
                found = True
        except (urllib.error.URLError, OSError):
            found = False
        have = target in existing
        state = "keep" if have and not args.force else ("cover" if found else "missing")
        print("%-11s %-50s %-11s %s" % (state, folder, serial, target if found else "(not in psx-covers)"))
        if args.apply and found and (not have or args.force):
            data = urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": UA}), timeout=60).read()
            png = to_png(data)
            ftp.storbinary("STOR %s/%s" % (BOXART, target), io.BytesIO(png))
            print("            uploaded %d bytes" % len(png))


if __name__ == "__main__":
    main()
