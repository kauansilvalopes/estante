#!/usr/bin/env python3
"""Fill only missing SwanStationPS5 box art from libretro-thumbnails.

The script reads the game's folders and existing boxart from the owner's PS5 through Elf Arsenal,
matches only high-confidence titles in the public libretro thumbnail index, and uploads PNG files to
/data/SwanStationPS5/covers/boxart. Existing files are never replaced unless --force is supplied.

Artwork is downloaded for personal use and is deliberately kept out of this repository. Run with
--dry-run first to review every proposed match.
"""
import argparse
import json
import os
import re
import sys
import time
import unicodedata
import urllib.parse
import urllib.request
from ftplib import FTP

THUMBNAILS = "https://thumbnails.libretro.com/Sony%20-%20PlayStation/Named_Boxarts/"
GAMES = "/data/SwanStationPS5/games"
BOXART = "/data/SwanStationPS5/covers/boxart"
UA = "Estante cover sync (personal library)"
# Localized folder names found in this setup whose original release title differs. Aliases stay explicit
# so a future scan never substitutes artwork based on a fuzzy title match.
ALIASES = {
    "akumajou d x br": ["Castlevania - Symphony of the Night", "Akumajou Dracula X - Gekka no Yasoukyoku"],
    "looney tunes sheep raider pt br": ["Sheep, Dog 'n' Wolf"],
    "tintin destination adventure br": ["Tintin - Destination Adventure"],
    "disney atlantida o continente perdido pt": ["Atlantis - The Lost Empire"],
    "idainaru dragon ball densetsu br": ["Dragon Ball - Final Bout"],
}


def fetch(url, timeout=90):
    request = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def elf_json(host, path):
    # Elf Arsenal accepts the slash-delimited path directly; encoding every slash makes some builds return 500.
    url = "http://%s:6969/api/fs/list?path=%s" % (host, urllib.parse.quote(path, safe="/"))
    return json.loads(fetch(url, timeout=20))


def swan_filename(title):
    # Mirror library.c: clean the folder title into char title[96], then finish_game
    # turns at most 63 bytes into the fallback id. Keep byte behavior for UTF-8.
    raw = title.encode("utf-8")[:255]
    dot = raw.rfind(b".")
    ext = raw[dot + 1:].lower() if dot >= 0 else b""
    if dot >= 0 and ext in {b"m3u", b"pbp", b"chd", b"cue", b"ccd", b"iso", b"img", b"mdf", b"bin"}:
        raw = raw[:dot]
    result = bytearray()
    i = 0
    while i < len(raw):
        if raw[i] == ord("[") and (end := raw.find(b"]", i + 1)) >= 0:
            i = end + 1
            continue
        disc = (raw[i:i + 5] in (b"(Disc", b"(disc") or raw[i:i + 3] == b"(CD" or
                raw[i:i + 7] == b"(SwanSt")
        if raw[i:i + 1] == b"(" and disc and (end := raw.find(b")", i + 1)) >= 0:
            i = end + 1
            continue
        byte = ord(" ") if raw[i] == ord("_") else raw[i]
        if byte in b" \t\n\r\v\f":
            if result and result[-1] != ord(" "):
                result.append(ord(" "))
        else:
            result.append(byte)
        i += 1
    result = result.strip(b" ")[:95]
    return "".join(chr(b) if b < 0x80 and chr(b).isalnum() else "_" for b in result[:63]) + ".png"


def key(text):
    text = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode("ascii").lower()
    text = re.sub(r"\[[^]]*\]|\([^)]*(?:usa|europe|japan|world|en|fr|de|es|it|pt|br)[^)]*\)", " ", text)
    return re.sub(r"[^a-z0-9]+", " ", text).strip()


def words(text):
    return {w for w in key(text).split() if len(w) > 1 and w not in {"the", "of", "and", "in"}}


def score(title, candidate):
    a, b = words(title), words(candidate)
    if not a or not b:
        return 0.0
    return len(a & b) / len(a | b)


def source_candidates(title):
    """Exact names only. Region suffixes are tried in a deliberate order; no fuzzy title matching."""
    base = re.sub(r"\s*(?:\[[^]]*\]|\((?:BR|PT-BR|PT)\))\s*", "", title).strip()
    ascii_base = unicodedata.normalize("NFKD", base).encode("ascii", "ignore").decode("ascii")
    variants = [base, ascii_base, ascii_base.replace(" - ", " "), ascii_base.replace(": ", " - ")]
    seen = set()
    for name in variants:
        for suffix in ("", " (USA)", " (Europe)", " (Japan)",
                       " (Europe) (En,Fr,De,Es,It,Nl)", " (USA) (En,Fr,Es)"):
            filename = name + suffix + ".png"
            if filename not in seen:
                seen.add(filename)
                yield filename, THUMBNAILS + urllib.parse.quote(filename, safe="")


def cue_titles(host, title):
    """Use the real cue filename too: it normally carries the Redump region/language suffix."""
    path = GAMES + "/" + title
    try:
        entries = elf_json(host, path).get("entries", [])
    except (OSError, ValueError, KeyError):
        return []
    result = []
    for entry in entries:
        name = entry.get("name", "")
        if not entry.get("dir") and name.lower().endswith(".cue"):
            result.append(os.path.splitext(name)[0])
    return result


def source_for(title, media_titles):
    # Folder names are localized, whereas the .cue often gives the dump's canonical title.
    seen = set()
    aliases = ALIASES.get(key(title), [])
    for candidate_title in aliases + media_titles + [title]:
        for filename, url in source_candidates(candidate_title):
            if url in seen:
                continue
            seen.add(url)
            request = urllib.request.Request(url, method="HEAD", headers={"User-Agent": UA})
            try:
                with urllib.request.urlopen(request, timeout=12):
                    return filename, url
            except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
                pass
    return None, None


def source_for_old(title):
    """Kept only to make the exact matching rule easy to inspect in history."""
    for filename, url in source_candidates(title):
        request = urllib.request.Request(url, method="HEAD", headers={"User-Agent": UA})
        try:
            with urllib.request.urlopen(request, timeout=20):
                return filename, url
        except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
            pass
    return None, None


def upload(host, local, remote):
    ftp = FTP()
    ftp.connect(host, 2121, timeout=45)
    ftp.login("anonymous", "estante")
    with open(local, "rb") as handle:
        ftp.storbinary("STOR " + remote, handle)
    ftp.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="192.168.2.116")
    parser.add_argument("--out", default=os.path.join("build", "emulator-covers", "swanstation"))
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--force", action="store_true", help="replace an existing local boxart file")
    parser.add_argument("--only", help="one exact SwanStation folder name, for reviewing a single game")
    parser.add_argument("--only-key", help="ASCII title key, e.g. looney-tunes-sheep-raider")
    parser.add_argument("--manual-source", help="a personally supplied fallback image to upload for --only/--only-key")
    args = parser.parse_args()

    games = [e["name"] for e in elf_json(args.host, GAMES).get("entries", []) if e.get("dir")]
    if args.only:
        games = [title for title in games if title == args.only]
        if not games:
            raise RuntimeError("game folder not found: %s" % args.only)
    if args.only_key:
        wanted = re.sub(r"[^a-z0-9]+", " ", args.only_key.lower()).strip()
        games = [title for title in games if key(title) == wanted]
        if not games:
            raise RuntimeError("game key not found: %s" % args.only_key)
    if args.manual_source and len(games) != 1:
        raise RuntimeError("--manual-source requires exactly one selected game")
    existing = {e["name"] for e in elf_json(args.host, BOXART).get("entries", []) if not e.get("dir")}
    os.makedirs(args.out, exist_ok=True)
    uploaded = skipped = unresolved = 0
    for title in sorted(games):
        destination = swan_filename(title)
        if destination in existing and not args.force:
            print("keep    %-45s %s" % (title, destination))
            skipped += 1
            continue
        if args.manual_source:
            if args.dry_run:
                print("manual  %-45s <- %s" % (title, args.manual_source))
                continue
            upload(args.host, args.manual_source, BOXART + "/" + destination)
            print("manual  %-45s <- %s" % (title, args.manual_source))
            uploaded += 1
            continue
        media = cue_titles(args.host, title)
        source_name, source_url = source_for(title, media)
        if not source_url:
            print("missing %-45s no exact source title%s" %
                  (title, " (cue: %s)" % media[0] if media else ""))
            unresolved += 1
            continue
        detail = " (cue: %s)" % media[0] if media else ""
        print("match   %-45s <- %s%s" % (title, source_name, detail))
        if args.dry_run:
            continue
        local = os.path.join(args.out, destination)
        if not os.path.exists(local):
            with open(local, "wb") as f:
                f.write(fetch(source_url, timeout=120))
        upload(args.host, local, BOXART + "/" + destination)
        uploaded += 1
    print("done: %d uploaded, %d already present, %d need manual review" % (uploaded, skipped, unresolved))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("error: %s" % error, file=sys.stderr)
        raise SystemExit(1)
