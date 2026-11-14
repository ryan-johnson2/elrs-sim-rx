#!/usr/bin/env python3
"""Assemble the GitHub Pages site from a release channel and a development channel.

    tools/assemble_site.py --out site [--release DIR] [--devel DIR]

Each DIR holds what a build publishes: the UF2 files, their manifest.json, and the web page
either as web.tar.gz or as a web/ folder. Layout written to --out:

    index.html, picoboot.js      the release page, or a short notice if there is no release yet
    firmware/*.uf2               release builds
    firmware/manifest.json       release builds only: the site root never offers development work
    devel/                       the devel page
    devel/firmware/              development builds, and a manifest listing them first, then
                                 the release builds
"""
import argparse
import json
import os
import shutil
import tarfile


NO_RELEASE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ELRS Sim RX</title>
<style>
  body { margin: 0; font: 16px/1.5 system-ui, -apple-system, Segoe UI, Roboto, sans-serif; background: #f6f7f9; color: #1b1f24; }
  main { max-width: 34rem; margin: 15vh auto 0; padding: 0 16px; }
  a { color: #2e7d32; }
  @media (prefers-color-scheme: dark) { body { background: #111417; color: #e6e9ec; } a { color: #4caf50; } }
</style>
</head>
<body>
<main>
<h1>ELRS Sim RX</h1>
<p>There is no released version yet.</p>
<p>The <a href="devel/">development build</a> has the latest configuration page and firmware. It changes often
and may not have been tested on hardware.</p>
</main>
</body>
</html>
"""


def load(d):
    if not d or not os.path.isfile(os.path.join(d, "manifest.json")):
        return None
    return json.load(open(os.path.join(d, "manifest.json")))["files"]


def put_web(src, dst):
    os.makedirs(dst, exist_ok=True)
    tgz = os.path.join(src, "web.tar.gz")
    if os.path.isfile(tgz):
        with tarfile.open(tgz) as t:
            t.extractall(dst, filter="data")
    else:
        shutil.copytree(os.path.join(src, "web"), dst, dirs_exist_ok=True)


def entries(files, src, dst, sub, channel, prefix):
    out = []
    os.makedirs(os.path.join(dst, sub), exist_ok=True)
    for f in files:
        shutil.copy2(os.path.join(src, f["name"]), os.path.join(dst, sub, f["name"]))
        e = dict(f)
        e["path"] = (sub + "/" if sub else "") + f["name"]
        e["channel"] = channel
        e["label"] = prefix + f.get("label", f["name"])
        out.append(e)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--release")
    ap.add_argument("--devel")
    a = ap.parse_args()
    rel, dev = load(a.release), load(a.devel)
    if not rel and not dev:
        raise SystemExit("nothing to publish: no release and no devel build found")

    fw = os.path.join(a.out, "firmware")
    os.makedirs(a.out, exist_ok=True)
    rel_e = []
    if rel:
        put_web(a.release, a.out)
        rel_e = entries(rel, a.release, fw, "", "release", "")
        json.dump({"files": rel_e}, open(os.path.join(fw, "manifest.json"), "w"), indent=2)
    else:
        with open(os.path.join(a.out, "index.html"), "w") as f:
            f.write(NO_RELEASE)

    dev_e = []
    if dev:
        # The devel page reads firmware/manifest.json relative to itself.
        dpage = os.path.join(a.out, "devel")
        put_web(a.devel, dpage)
        dev_e = entries(dev, a.devel, os.path.join(dpage, "firmware"), "", "devel", "Development: ")
        older = [dict(e, path="../../firmware/" + e["name"]) for e in rel_e]
        json.dump({"files": dev_e + older}, open(os.path.join(dpage, "firmware", "manifest.json"), "w"), indent=2)

    for e in rel_e + dev_e:
        print("%-8s %-22s v%s %s" % (e["channel"], e["name"], e["version"], e["commit"]))


if __name__ == "__main__":
    main()
