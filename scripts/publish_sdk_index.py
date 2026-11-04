#!/usr/bin/env python3
"""
Generates / updates directory.json — a ufbt-compatible SDK index for
FoxFW2.0, published into the separate `fox-web` repo and served from GitHub
Pages at https://foxfw.github.io/fox-web/directory.json

Schema matches the official Flipper indexer exactly (same one Momentum,
Unleashed, etc. use), so it works as a drop-in --index-url / sdk-index-url
for both `ufbt update` and flipperdevices/flipperzero-ufbt-action:

{
  "channels": [
    {
      "id": "release",
      "title": "...",
      "description": "...",
      "versions": [
        {
          "version": "2.0.0",
          "changelog": "...",
          "timestamp": 1234567890,
          "files": [
            {"url": "...", "target": "f7", "type": "sdk_zip", "sha256": "..."},
            ...
          ]
        },
        ... (older versions, newest first)
      ]
    }
  ]
}

Usage (called from CI after `./fbt updater_package`):

    python3 scripts/publish_sdk_index.py \\
        --dist-dir dist/f7 \\
        --version 2.0.0 \\
        --changelog "What's new..." \\
        --repo foxfw/flipperzero-firmware \\
        --tag v2.0.0 \\
        --channel-id release \\
        --channel-title "Stable Release Channel" \\
        --index-path fox-web-checkout/directory.json \\
        --keep 5
"""
import argparse
import hashlib
import json
import os
import re
import time

# Maps a suffix found in `flipper-z-<target>-<artifact>-<version>.<ext>` to
# the `type` field the official schema uses. Order matters — first match wins.
ARTIFACT_TYPE_MAP = [
    (re.compile(r"-sdk-.*\.zip$"), "sdk_zip"),
    (re.compile(r"-full-.*\.bin$"), "full_bin"),
    (re.compile(r"-full-.*\.dfu$"), "full_dfu"),
    (re.compile(r"-full-.*\.json$"), "full_json"),
    (re.compile(r"-update-.*\.tgz$"), "update_tgz"),
    (re.compile(r"-update-.*\.tar$"), "update_tar"),
    (re.compile(r"-updater-.*\.bin$"), "updater_bin"),
    (re.compile(r"-updater-.*\.dfu$"), "updater_dfu"),
    (re.compile(r"-updater-.*\.elf$"), "updater_elf"),
    (re.compile(r"-updater-.*\.json$"), "updater_json"),
    (re.compile(r"-firmware-.*\.elf$"), "firmware_elf"),
    (re.compile(r"-resources-.*\.tgz$"), "resources_tgz"),
    (re.compile(r"-scripts-.*\.tgz$"), "scripts_tgz"),
    (re.compile(r"-core2_firmware-.*\.tgz$"), "core2_firmware_tgz"),
]

FILENAME_RE = re.compile(r"^flipper-z-([A-Za-z0-9]+)-")


def sha256_of(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def classify(filename: str):
    for pattern, ftype in ARTIFACT_TYPE_MAP:
        if pattern.search(filename):
            return ftype
    return None


def build_files_entry(dist_dir: str, repo: str, tag: str) -> list:
    files = []
    base_url = f"https://github.com/{repo}/releases/download/{tag}"
    for name in sorted(os.listdir(dist_dir)):
        path = os.path.join(dist_dir, name)
        if not os.path.isfile(path):
            continue
        ftype = classify(name)
        if not ftype:
            continue  # skip anything we don't recognize (e.g. .tar dupes, debug dir)
        m = FILENAME_RE.match(name)
        target = m.group(1) if m else "unknown"
        files.append(
            {
                "url": f"{base_url}/{name}",
                "target": target,
                "type": ftype,
                "sha256": sha256_of(path),
            }
        )
    return files


def load_existing(index_path: str) -> dict:
    if os.path.exists(index_path):
        with open(index_path, "r") as f:
            return json.load(f)
    return {"channels": []}


def upsert_version(index: dict, channel_id: str, channel_title: str, channel_desc: str,
                    version_entry: dict, keep: int) -> dict:
    channel = next((c for c in index["channels"] if c["id"] == channel_id), None)
    if channel is None:
        channel = {
            "id": channel_id,
            "title": channel_title,
            "description": channel_desc,
            "versions": [],
        }
        index["channels"].append(channel)
    else:
        channel["title"] = channel_title
        channel["description"] = channel_desc

    # De-dupe: drop any existing entry for the same version, then prepend the
    # new one so versions[0] is always the newest (matches official convention).
    channel["versions"] = [v for v in channel["versions"] if v["version"] != version_entry["version"]]
    channel["versions"].insert(0, version_entry)
    channel["versions"] = channel["versions"][:keep]
    return index


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dist-dir", required=True, help="e.g. dist/f7 — output of ./fbt updater_package")
    ap.add_argument("--version", required=True, help="Version string, e.g. 2.0.0 or a commit hash")
    ap.add_argument("--changelog", default="", help="Changelog text for this version")
    ap.add_argument("--repo", required=True, help="owner/repo the release assets live in, e.g. foxfw/flipperzero-firmware")
    ap.add_argument("--tag", required=True, help="Git tag the release assets were uploaded under")
    ap.add_argument("--channel-id", default="release")
    ap.add_argument("--channel-title", default="Stable Release Channel")
    ap.add_argument("--channel-description", default="Stable FoxFW2.0 releases.")
    ap.add_argument("--index-path", default="sdk_index_output/directory.json")
    ap.add_argument("--keep", type=int, default=5, help="How many versions to retain per channel")
    args = ap.parse_args()

    files = build_files_entry(args.dist_dir, args.repo, args.tag)
    if not any(f["type"] == "sdk_zip" for f in files):
        raise SystemExit(
            f"No sdk_zip file found in {args.dist_dir} — did you run `./fbt updater_package` first?"
        )

    version_entry = {
        "version": args.version,
        "changelog": args.changelog,
        "timestamp": int(time.time()),
        "files": files,
    }

    index = load_existing(args.index_path)
    index = upsert_version(
        index, args.channel_id, args.channel_title, args.channel_description, version_entry, args.keep
    )

    os.makedirs(os.path.dirname(args.index_path) or ".", exist_ok=True)
    with open(args.index_path, "w") as f:
        json.dump(index, f, indent=2)
        f.write("\n")

    print(f"Wrote {args.index_path}: channel '{args.channel_id}' now has {len(index['channels'][0]['versions']) if index['channels'] else 0} version(s)")
    print(f"  Latest: {version_entry['version']} — {len(files)} file(s), including sdk_zip for: "
          + ", ".join(f["target"] for f in files if f["type"] == "sdk_zip"))


if __name__ == "__main__":
    main()
