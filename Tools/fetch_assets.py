"""Download the original, publicly licensed art used by the island."""

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.request import Request, urlopen
import json
import shutil
import sys
import time

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)


def open_url(url):
    return urlopen(Request(url, headers={"User-Agent": "Saltwind UE environment asset import (personal project)"}), timeout=80)


def download(url, dest):
    dest = root / dest
    if dest.is_file():
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(3):
        try:
            with open_url(url) as response, dest.with_suffix(dest.suffix + ".part").open("wb") as output:
                shutil.copyfileobj(response, output)
            dest.with_suffix(dest.suffix + ".part").replace(dest)
            print(dest.name, flush=True)
            return
        except Exception:
            if attempt == 2:
                raise
            time.sleep(attempt + 1)


def poly(slug, group, size, ext):
    with open_url("https://api.polyhaven.com/files/" + slug) as response:
        descriptor = json.load(response)[group][size][ext]
    name = Path(descriptor["url"]).name
    download(descriptor["url"], Path(slug) / name)
    for name, item in descriptor.get("include", {}).items():
        download(item["url"], Path(slug) / name)


tasks = []
with ThreadPoolExecutor(max_workers=10) as executor:
    for slug in ["coast_sand_03", "forest_leaves_02", "coast_sand_rocks_02", "damp_beach_sand_02"]:
        for group in ["Diffuse", "nor_dx", "Rough"]:
            tasks.append(executor.submit(poly, slug, group, "2k", "jpg"))
    for slug in ["boulder_01", "coast_rocks_01", "mountainside", "rock_moss_set_01"]:
        tasks.append(executor.submit(poly, slug, "gltf", "2k", "gltf"))
    for slug in ["royal-palm", "banana-plant", "tree-fern", "mangrove-tree", "golden-cane-palm", "cycad", "coconut-palm"]:
        tasks.append(executor.submit(download, "https://vinish-models.pages.dev/palms/" + slug + ".glb", slug.replace("-", "_") + ".glb"))
    tasks.append(executor.submit(download, "https://grab3d.com/dl/palm-tree/palm-tree.glb", "shore_palm.glb"))
    for task in tasks:
        try:
            task.result()
        except Exception as error:
            print(error, flush=True)
