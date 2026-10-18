#!/usr/bin/env python3
"""Estante - cover sync for PS5 and PS4 titles (runs on the PC, not on the console).

Reads the game list from Elf Arsenal (or --ids / --catalog), finds each game's official PlayStation Store
artwork, saves a portrait cover (2:3) and a wide picture per game and, unless --no-upload, sends them to the
console over FTP so the app can read them. The store pages are unofficial and can change; every download is
cached in build/covers so a run only fetches what is missing.

  python tools/ps5_covers_sync.py                       # whole library, upload to 192.168.2.116
  python tools/ps5_covers_sync.py --ids PPSA01284,PPSA02572 --no-upload
  python tools/ps5_covers_sync.py --host 192.168.2.50 --only PPSA01284

How a cover is found, in order (the first that works wins):
  1. The game's own store page: PORTRAIT_BANNER (a 1440x2160 box-art style picture).
  2. The same game in other store regions (en-gb, pt-br, es-mx, ja-jp).
  3. Other editions or platform versions with the same name (PS4 and PS5 are listed apart).
  4. With --steamgriddb-key (or STEAMGRIDDB_KEY): the best 600x900 grid on steamgriddb.com for that name. This is the
     answer for games the store no longer lists (small and delisted titles come back from the store with no pictures).
  5. GAMEHUB_COVER_ART, then FOUR_BY_THREE_BANNER, cropped to portrait.
  6. The square MASTER picture (or the console's own icon) laid on a blurred, darkened copy of itself.
Wide pictures use EDITION_KEY_ART, SIXTEEN_BY_NINE_BANNER, BACKGROUND, then the first SCREENSHOT.

Needs only the Python standard library; the cropped and blurred fallbacks (steps 4 and 5) also need Pillow
(`pip install pillow`).

SPDX-License-Identifier: GPL-3.0-or-later. Artwork belongs to its owners; it is fetched for personal use and
must not be added to a public repository.
"""
import argparse
import io
import json
import os
import re
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from ftplib import FTP

try:
    from PIL import Image, ImageFilter

    HAVE_PIL = True
except ImportError:  # the main path (steps 1 to 3) works without it
    HAVE_PIL = False

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/120 Safari/537.36"
TABLE_URL = "https://raw.githubusercontent.com/andshrew/PlayStation-Titles/main/Json/%s_Titles.json"
STORE_URL = "https://store.playstation.com/%s/concept/%d"
REGIONS = ["en-us", "en-gb", "pt-br", "es-mx", "ja-jp"]
PORTRAIT_WIDTH = 600  # the picture is 600x900
WIDE_WIDTH = 1280
REMOTE_DIR = "/data/estante/covers"
IMAGE_RE = [
    re.compile(r'"role":"([A-Z_]+)","type":"IMAGE","url":"(https://image\.api\.playstation\.com/[^"]+)"'),
    re.compile(r'"type":"IMAGE","role":"([A-Z_]+)","url":"(https://image\.api\.playstation\.com/[^"]+)"'),
]


def fetch(url, timeout=40, retries=2):
    last = None
    for attempt in range(retries + 1):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": UA, "Accept-Language": "en-US,en;q=0.9"})
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return response.read()
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            last = error
            time.sleep(1.5 * (attempt + 1))
    raise last


def cached_table(cache_dir, platform):
    path = os.path.join(cache_dir, "%s_Titles.json" % platform)
    if not os.path.exists(path) or time.time() - os.path.getmtime(path) > 7 * 86400:
        print("downloading the %s title table (once a week)..." % platform)
        with open(path, "wb") as f:
            f.write(fetch(TABLE_URL % platform, timeout=180))
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def normal_name(name):
    name = re.sub(r"[™®©]", "", name.lower())
    return re.sub(r"[^a-z0-9]+", " ", name).strip()


class Titles:
    """title id -> concept ids, and name -> concept ids across PS4 and PS5."""

    def __init__(self, cache_dir):
        self.by_id = {}
        self.by_name = {}
        for platform in ("PS5", "PS4"):
            for entry in cached_table(cache_dir, platform):
                tid = entry["titleId"][:9]
                self.by_id.setdefault(tid, entry)
                self.by_name.setdefault(normal_name(entry["name"]), []).append(entry["conceptId"])

    def concepts_for(self, title_id, fallback_name=""):
        entry = self.by_id.get(title_id)
        first = [entry["conceptId"]] if entry else []
        name = entry["name"] if entry else fallback_name
        others = [c for c in dict.fromkeys(self.by_name.get(normal_name(name), [])) if c not in first]
        return first, others, name


def store_media(concept, region):
    html = fetch(STORE_URL % (region, concept)).decode("utf-8", "replace")
    roles = {}
    screenshots = []
    for pattern in IMAGE_RE:
        for match in pattern.finditer(html):
            role, url = match.group(1), match.group(2)
            roles.setdefault(role, url)
            if role == "SCREENSHOT":
                screenshots.append(url)
    if screenshots:
        roles["SCREENSHOT"] = screenshots[0]
    return roles


def sized(url, width):
    return url + ("&" if "?" in url else "?") + "w=%d" % width


def image_size(data):
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", data[16:24])
    if data[:2] == b"\xff\xd8":
        i = 2
        while i + 9 < len(data):
            if data[i] != 0xFF:
                i += 1
                continue
            if data[i + 1] in (0xC0, 0xC1, 0xC2):
                height, width = struct.unpack(">HH", data[i + 5:i + 9])
                return width, height
            i += 2 + struct.unpack(">H", data[i + 2:i + 4])[0]
    return None


def to_portrait(data, size=(PORTRAIT_WIDTH, PORTRAIT_WIDTH * 3 // 2), blur_background=False):
    """Crops (or, for a square picture, centres on a blurred copy) to 2:3 and returns JPEG bytes."""
    image = Image.open(io.BytesIO(data)).convert("RGB")
    width, height = size
    if blur_background:
        background = image.resize((width, height), Image.LANCZOS)
        scale = max(width / image.width, height / image.height)
        background = image.resize((int(image.width * scale) + 1, int(image.height * scale) + 1), Image.LANCZOS)
        left, top = (background.width - width) // 2, (background.height - height) // 2
        background = background.crop((left, top, left + width, top + height)).filter(ImageFilter.GaussianBlur(28))
        background = Image.eval(background, lambda v: int(v * 0.45))
        front = image.resize((width, int(image.height * width / image.width)), Image.LANCZOS)
        background.paste(front, (0, (height - front.height) // 2))
        out = background
    else:
        scale = max(width / image.width, height / image.height)
        scaled = image.resize((int(image.width * scale) + 1, int(image.height * scale) + 1), Image.LANCZOS)
        left, top = (scaled.width - width) // 2, (scaled.height - height) // 2
        out = scaled.crop((left, top, left + width, top + height))
    buffer = io.BytesIO()
    out.save(buffer, "JPEG", quality=88, optimize=True)
    return buffer.getvalue()


def steamgriddb_cover(name, key):
    """The highest scored 600x900 grid for `name`, as JPEG bytes (needs Pillow when it is not a JPEG)."""
    def api(path):
        request = urllib.request.Request("https://www.steamgriddb.com/api/v2" + path,
                                         headers={"Authorization": "Bearer " + key, "User-Agent": UA})
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read())

    found = api("/search/autocomplete/" + urllib.parse.quote(name))
    if not found.get("success") or not found.get("data"):
        return None
    game = found["data"][0]["id"]
    grids = api("/grids/game/%d?dimensions=600x900&types=static&nsfw=false&humor=false" % game)
    if not grids.get("success") or not grids.get("data"):
        return None
    data = fetch(grids["data"][0]["url"])
    if data[:2] == b"\xff\xd8" or not HAVE_PIL:
        return data
    return to_portrait(data)


def find_cover(title_id, name, titles, console_icon, grid_key=""):
    """Returns (jpeg bytes, how it was found, wide url or None)."""
    first, others, found_name = titles.concepts_for(title_id, name)
    wide = None
    order = [(c, "its own page") for c in first] + [(c, "another edition") for c in others[:3]]
    media_by_concept = {}
    for concept, why in order:
        for region in REGIONS:
            try:
                roles = store_media(concept, region)
            except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
                continue
            media_by_concept[(concept, region)] = roles
            wide = wide or next((roles[r] for r in ("EDITION_KEY_ART", "SIXTEEN_BY_NINE_BANNER", "BACKGROUND",
                                                    "SCREENSHOT") if r in roles), None)
            if "PORTRAIT_BANNER" in roles:
                how = "portrait banner, %s (%s)" % (why, region)
                return fetch(sized(roles["PORTRAIT_BANNER"], PORTRAIT_WIDTH)), how, wide
            time.sleep(0.3)
    if grid_key and found_name:
        try:
            grid = steamgriddb_cover(found_name, grid_key)
        except (urllib.error.URLError, TimeoutError, ConnectionError, OSError, ValueError, KeyError):
            grid = None
        if grid:
            return grid, "steamgriddb 600x900", wide
    if HAVE_PIL:
        for roles in media_by_concept.values():
            for role in ("GAMEHUB_COVER_ART", "FOUR_BY_THREE_BANNER"):
                if role in roles:
                    return to_portrait(fetch(sized(roles[role], 1200))), "cropped %s" % role.lower(), wide
        for roles in media_by_concept.values():
            if "MASTER" in roles:
                return to_portrait(fetch(sized(roles["MASTER"], 600)), blur_background=True), "square master on blur", wide
        if console_icon:
            return to_portrait(console_icon, blur_background=True), "console icon on blur", wide
    return None, "no artwork found", wide


def read_catalog(path_or_host, port):
    if os.path.exists(path_or_host):
        with open(path_or_host, "r", encoding="utf-8") as f:
            data = json.load(f)
    else:
        data = json.loads(fetch("http://%s:%d/appdb" % (path_or_host, port), timeout=20))
    return {t["titleId"]: t.get("titleName", "") for t in data["titles"]}


def console_icon(host, port, title_id):
    try:
        return fetch("http://%s:%d/appdb/icon?id=%s" % (host, port, title_id), timeout=15, retries=0)
    except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
        return None


def upload(host, files):
    ftp = FTP()
    ftp.connect(host, 2121, timeout=30)
    ftp.login("anonymous", "estante")
    for directory in ("/data/estante", REMOTE_DIR, REMOTE_DIR + "/wide"):
        try:
            ftp.mkd(directory)
        except Exception:
            pass
    for local, remote in files:
        with open(local, "rb") as f:
            ftp.storbinary("STOR " + remote, f)
    ftp.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="192.168.2.116", help="PS5 address (Elf Arsenal on 6969, FTP on 2121)")
    parser.add_argument("--port", type=int, default=6969)
    parser.add_argument("--catalog", help="read a saved /appdb JSON instead of asking the console")
    parser.add_argument("--ids", help="comma separated title ids, instead of the console's list")
    parser.add_argument("--only", help="a single title id")
    parser.add_argument("--steamgriddb-key", default=os.environ.get("STEAMGRIDDB_KEY", ""),
                        help="free API key from steamgriddb.com, used for titles the store no longer lists")
    parser.add_argument("--no-upload", action="store_true", help="only fill the local cache")
    parser.add_argument("--out", default=os.path.join("build", "covers"))
    parser.add_argument("--force", action="store_true", help="fetch again even when the cache has the cover")
    args = parser.parse_args()

    cover_dir = os.path.join(args.out, "covers")
    wide_dir = os.path.join(args.out, "wide")
    os.makedirs(cover_dir, exist_ok=True)
    os.makedirs(wide_dir, exist_ok=True)
    cache_dir = os.path.join(args.out, "_tables")
    os.makedirs(cache_dir, exist_ok=True)

    if args.ids:
        games = {i.strip(): "" for i in args.ids.split(",") if i.strip()}
    else:
        games = read_catalog(args.catalog or args.host, args.port)
    if args.only:
        games = {args.only: games.get(args.only, "")}
    # Retail titles only: homebrew (PPSA9xxxx), PS2 images and apps have no store page.
    games = {t: n for t, n in games.items() if re.fullmatch(r"(PPSA|CUSA)\d{5}", t) and not (t.startswith("PPSA9") or t.startswith("CUSA9"))}
    print("%d retail titles to check" % len(games))
    titles = Titles(cache_dir)

    report = []
    to_upload = []
    for index, (title_id, name) in enumerate(sorted(games.items()), 1):
        cover_path = os.path.join(cover_dir, title_id + ".jpg")
        wide_path = os.path.join(wide_dir, title_id + ".jpg")
        if os.path.exists(cover_path) and not args.force:
            print("[%d/%d] %s %s: cached" % (index, len(games), title_id, name))
            to_upload.append((cover_path, "%s/%s.jpg" % (REMOTE_DIR, title_id)))
            if os.path.exists(wide_path):
                to_upload.append((wide_path, "%s/wide/%s.jpg" % (REMOTE_DIR, title_id)))
            continue
        icon = console_icon(args.host, args.port, title_id) if not args.catalog and not args.ids else None
        data, how, wide = find_cover(title_id, name, titles, icon, args.steamgriddb_key)
        print("[%d/%d] %s %s: %s" % (index, len(games), title_id, name, how))
        report.append((title_id, name, how))
        if data:
            with open(cover_path, "wb") as f:
                f.write(data)
            to_upload.append((cover_path, "%s/%s.jpg" % (REMOTE_DIR, title_id)))
        if wide:
            try:
                with open(wide_path, "wb") as f:
                    f.write(fetch(sized(wide, WIDE_WIDTH)))
                to_upload.append((wide_path, "%s/wide/%s.jpg" % (REMOTE_DIR, title_id)))
            except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
                pass
        time.sleep(0.4)

    with open(os.path.join(args.out, "report.txt"), "w", encoding="utf-8") as f:
        for row in report:
            f.write("%s\t%s\t%s\n" % row)
    missing = [r for r in report if r[2] == "no artwork found"]
    print("done: %d fetched, %d without artwork" % (len(report) - len(missing), len(missing)))
    if args.no_upload or not to_upload:
        print("not uploading; files are in %s" % args.out)
        return 0
    print("uploading %d files to %s:%s" % (len(to_upload), args.host, REMOTE_DIR))
    upload(args.host, to_upload)
    print("uploaded")
    return 0


if __name__ == "__main__":
    sys.exit(main())
