#!/usr/bin/env python3
"""Build manifest.tsv for the distress call clips from their DVIDS pages.

Reads clips.tsv (DVIDS ID, short name, page URL), fetches each page, and
records the media URL, the page metadata, the license marking, and the
SHA-256 of the downloaded MP3. Uses the Python standard library only.

Usage: build_manifest.py [clips.tsv] [manifest.tsv]
"""

import csv
import hashlib
import html
import re
import sys
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
CDN = "https://d34w7g4gy10iej.cloudfront.net/audio/"
USER_AGENT = "opssat-pretty-doomed-manifest/1.0"

COLUMNS = [
    "id", "name", "page_url", "media_url", "sha256", "length", "date_taken",
    "location", "credit", "courtesy", "public_domain",
]


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read()


def page_lines(page_html):
    """Return the visible text lines of a page, without scripts and styles."""
    text = re.sub(r"<script.*?</script>|<style.*?</style>", "", page_html, flags=re.S)
    text = html.unescape(re.sub(r"<[^>]+>", "\n", text))
    return [line.strip() for line in text.split("\n") if line.strip()]


def field_after(lines, label):
    """Return the line that follows a label line such as 'Length:'."""
    for i, line in enumerate(lines[:-1]):
        if line == label:
            return lines[i + 1]
    return ""


def iso_date(dvids_date):
    """Convert a DVIDS date (MM.DD.YYYY) to ISO 8601 (YYYY-MM-DD)."""
    month, day, year = dvids_date.split(".")
    return f"{year}-{month}-{day}"


def parse_page(page_html):
    lines = page_lines(page_html)
    filename = field_after(lines, "Filename:")
    if not filename.endswith(".mp3"):
        raise ValueError(f"no MP3 filename on the page (found {filename!r})")
    return {
        "media_url": CDN + filename,
        "length": field_after(lines, "Length:"),
        "date_taken": iso_date(field_after(lines, "Date Taken:")),
        "location": field_after(lines, "Location:"),
        "credit": field_after(lines, "Courtesy Audio") or field_after(lines, "Audio by"),
        "courtesy": "yes" if "Courtesy Audio" in lines else "no",
        "public_domain": "yes" if 'class="public-domain"' in page_html else "no",
    }


def main():
    clips_path = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "clips.tsv"
    manifest_path = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "manifest.tsv"

    rows = []
    with clips_path.open(newline="") as f:
        for clip_id, name, page_url in csv.reader(f, delimiter="\t"):
            meta = parse_page(fetch(page_url).decode("utf-8", errors="replace"))
            meta["sha256"] = hashlib.sha256(fetch(meta["media_url"])).hexdigest()
            rows.append({"id": clip_id, "name": name, "page_url": page_url, **meta})
            print(f"{clip_id} {name}: {meta['length']} public_domain={meta['public_domain']}")

    with manifest_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=COLUMNS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    print(f"Wrote {manifest_path} ({len(rows)} clips)")


if __name__ == "__main__":
    main()
