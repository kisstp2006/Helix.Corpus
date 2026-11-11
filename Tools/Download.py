"""Downloads the corpus assets listed in Sources.json into a staging folder, checks their SHA-256 and copies them
into Assets/.

    python -I Tools/Download.py --staging <new empty folder> [--pin]

--pin records the hashes of a first download in Sources.json; without it every file must match its recorded hash.
Zip sources are extracted by file name only (no folders from the archive), keeping the listed extensions.
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = os.path.join(ROOT, "Tools", "Sources.json")
ASSETS = os.path.join(ROOT, "Assets")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def fetch(url, path):
    request = urllib.request.Request(url, headers={"User-Agent": "Helix-Corpus-Download/1.0"})
    with urllib.request.urlopen(request, timeout=120) as response, open(path, "wb") as out:
        shutil.copyfileobj(response, out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--staging", required=True)
    parser.add_argument("--pin", action="store_true")
    args = parser.parse_args()
    staging = os.path.abspath(args.staging)
    if os.path.commonpath([staging, ROOT]) == ROOT:
        sys.exit("the staging folder must be outside the repository")
    os.makedirs(staging, exist_ok=True)

    data = json.load(open(SOURCES, encoding="utf-8"))
    failed = 0
    for i, source in enumerate(data["sources"]):
        staged = os.path.join(staging, "%04d_%s" % (i, os.path.basename(source["url"].split("?")[0])))
        if not os.path.exists(staged):
            fetch(source["url"], staged)
        digest = sha256(staged)
        if args.pin:
            source["sha256"] = digest
        elif source.get("sha256") != digest:
            print("hash mismatch: %s" % source["url"])
            failed += 1
            continue
        target = os.path.join(ASSETS, *source["path"].split("/"))
        if "extract" in source:
            os.makedirs(target, exist_ok=True)
            with zipfile.ZipFile(staged) as z:
                for member in z.infolist():
                    name = os.path.basename(member.filename)
                    if member.is_dir() or not name or os.path.splitext(name)[1].lower() not in source["extract"]:
                        continue
                    with z.open(member) as src, open(os.path.join(target, name), "wb") as dst:
                        shutil.copyfileobj(src, dst)
        else:
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copyfile(staged, target)
    if args.pin:
        with open(SOURCES, "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, indent=1, ensure_ascii=True)
            f.write("\n")
    print("%d sources, %d failed" % (len(data["sources"]), failed))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
