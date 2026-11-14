#!/usr/bin/env python3
"""Assemble the GitHub Pages site from a release channel and a development channel.

    tools/assemble_site.py --out site [--release DIR] [--devel DIR]

Each DIR holds what a build publishes: the UF2 files, their manifest.json, and the web page
either as web.tar.gz or as a web/ folder. Layout written to --out:

    index.html, picoboot.js      the release page (the devel page if there is no release yet)
    firmware/*.uf2               release builds
    firmware/devel/*.uf2         development builds
    firmware/manifest.json       release entries first, so the release full build is the default
    devel/                       the devel page, with the same firmware list, devel entries first
"""
import argparse
import json
import os
import shutil
import tarfile


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
    put_web(a.release if rel else a.devel, a.out)
    rel_e = entries(rel, a.release, fw, "", "release", "") if rel else []
    dev_e = entries(dev, a.devel, fw, "devel", "devel", "Development: ") if dev else []
    json.dump({"files": rel_e + dev_e}, open(os.path.join(fw, "manifest.json"), "w"), indent=2)

    if dev:
        # The devel page reads firmware/manifest.json relative to itself.
        dpage = os.path.join(a.out, "devel")
        put_web(a.devel, dpage)
        dfw = os.path.join(dpage, "firmware")
        d2 = entries(dev, a.devel, dfw, "", "devel", "Development: ")
        r2 = [dict(e, path="../../firmware/" + e["name"]) for e in rel_e]
        json.dump({"files": d2 + r2}, open(os.path.join(dfw, "manifest.json"), "w"), indent=2)

    for e in rel_e + dev_e:
        print("%-8s %-22s v%s %s" % (e["channel"], e["name"], e["version"], e["commit"]))


if __name__ == "__main__":
    main()
