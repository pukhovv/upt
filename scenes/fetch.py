#!/usr/bin/env python3
"""Fetch pinned benchmark meshes and convert the underwater assets."""
import hashlib
from pathlib import Path
import sys
import tarfile
import tempfile
from urllib.request import urlretrieve

assets = Path(__file__).resolve().parent / "assets"
assets.mkdir(exist_ok=True)
if sys.argv[1:] == ["crown"]:
    from fetch_crown import fetch_crown
    fetch_crown(assets)
    raise SystemExit
for archive, member, name, expected in [
    ("bunny.tar.gz", "bunny/reconstruction/bun_zipper.ply", "bunny.ply", "b1acc63bece78444aa2e15bdcc72371a201279b98c6f5d4b74c993d02f0566fe"),
    ("dragon/dragon_recon.tar.gz", "dragon_recon/dragon_vrip.ply", "dragon.ply", "fea87ff48f2aba22fb53e7b67c3ff3f7b8c2a3b3a0653af62c48bba67c6d5744"),
    ("happy/happy_recon.tar.gz", "happy_recon/happy_vrip.ply", "buddha.ply", "2283371216d748a08376a3c88698e283cc8f18d10ced348d6d133051bcf217ab"),
]:
    target = assets / name
    if not target.exists():
        url = "https://graphics.stanford.edu/pub/3Dscanrep/" + archive
        print("Fetching", url, flush=True)
        with tempfile.NamedTemporaryFile() as download:
            urlretrieve(url, download.name)
            with tarfile.open(download.name, "r:gz") as source:
                model = source.extractfile(member)
                if model is None:
                    raise ValueError("Missing mesh: " + member)
                target.write_bytes(model.read())
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    if digest != expected:
        raise ValueError("Unexpected mesh hash: " + name + " " + digest)
    print(name, digest)

from fetch_underwater import fetch_underwater
fetch_underwater(assets)
