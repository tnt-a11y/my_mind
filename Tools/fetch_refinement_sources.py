"""Fetch the public art and audio used in the second pass."""

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.request import Request, urlopen
from zipfile import ZipFile
from array import array
import json
import math
import re
import shutil
import subprocess
import sys
import wave


root = Path(sys.argv[1]).resolve()
root.mkdir(parents=True, exist_ok=True)


def request(url, method="GET"):
    return urlopen(Request(url, headers={"User-Agent": "Mozilla/5.0"}, method=method), timeout=100)


def fetch(url, path):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.is_file():
        temp = path.with_suffix(".partial")
        with request(url) as response, temp.open("wb") as output:
            shutil.copyfileobj(response, output)
        temp.replace(path)
    return path


def poly(slug, groups, size):
    with request("https://api.polyhaven.com/files/" + slug) as response:
        data = json.load(response)
    for group, ext in groups:
        part = data[group][size][ext]
        parent = root / slug
        fetch(part["url"], parent / Path(part["url"]).name)
        for name, item in part.get("include", {}).items():
            fetch(item["url"], parent / name)
    print(slug, flush=True)


def field_recording(name, page, sound_id):
    with request(page) as response:
        html = response.read().decode("utf-8")
    pattern = rf"https://cdn\.freesound\.org/previews/\d+/{sound_id}_\d+-(hq|lq)\.mp3"
    match = re.search(pattern, html)
    if not match:
        raise RuntimeError(f"No public preview URL for recording {name}")
    return fetch(match[0].replace("-lq.mp3", "-hq.mp3"), root / "audio" / (name + ".mp3"))


def ffmpeg(*args):
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", *map(str,args)], check=True)


def split_steps(name, source, single=False):
    base = root / "audio" / (name + "_source.wav")
    ffmpeg("-i", source, "-t", 65, "-ac", 1, "-ar", 44100, "-af", "highpass=f=65", base)
    with wave.open(str(base), "rb") as audio:
        rate = audio.getframerate()
        samples = array("h", audio.readframes(audio.getnframes()))
    block = int(rate * .025)
    envelopes = [max(abs(v) for v in samples[i:i+block]) for i in range(0,len(samples),block)]
    picks = []
    for pos in sorted(range(len(envelopes)), key=envelopes.__getitem__, reverse=True):
        if all(abs(other - pos) > 13 for other in picks):
            picks.append(pos)
        if len(picks) >= (1 if single else 5):
            break
    for n, pos in enumerate(picks, 1):
        start = max(pos * block - round(rate * .085), 0)
        frame = samples[start:start + round(rate * (.24 if single else .38))]
        peak = max((abs(v) for v in frame), default=1) or 1
        gain = 15000 / peak
        result = array("h", (int(v * gain * min(1, i/(rate*.015), (len(frame)-i-1)/(rate*.071)))
                             for i,v in enumerate(frame)))
        with wave.open(str(root / "audio" / f"SW_{name}_{n:02d}.wav"), "wb") as out:
            out.setparams((1,2,rate,0,"NONE","not compressed"))
            out.writeframes(result.tobytes())
    print(f"{len(picks)} {name} footsteps", flush=True)


def swim_library():
    endpoint = "https://quaternius.itch.io/universal-animation-library/file/17958403?source=game_download&as_props=1"
    try:
        with request(endpoint, "POST") as response:
            signed = json.load(response)["url"]
        download = fetch(signed, root / "UAL1.zip")
        with ZipFile(download) as archive:
            name = next(name for name in archive.namelist() if name.endswith("UAL1_Standard.glb"))
            (root / "UAL1.glb").write_bytes(archive.read(name))
    except Exception:
        fetch("https://raw.githubusercontent.com/Seyamalam/blood-league-kickoff/main/public/assets/vendor/quaternius/universal-animation-library.glb",
              root / "UAL1.glb")
    print("Quaternius swim library", flush=True)


if __name__ == "__main__":
    ground = [("Diffuse","jpg"),("nor_dx","jpg"),("Rough","jpg")]
    with ThreadPoolExecutor(max_workers=8) as pool:
        tasks = [pool.submit(poly, slug, ground, "2k") for slug in ("coast_sand_05", "forrest_ground_01")]
        tasks += [pool.submit(poly, slug, [("gltf","gltf"),("Alpha","png")], "1k")
                  for slug in ("fern_02","shrub_sorrel_01")]
        tasks.append(pool.submit(swim_library))
        audio = [
            ("Sand", "https://freesound.org/people/felix.blume/sounds/705513/", 705513, False),
            ("Leaves", "https://freesound.org/people/SpliceSound/sounds/218305/", 218305, False),
            ("Wade", "https://freesound.org/people/Breviceps/sounds/450621/", 450621, True),
        ]
        for name, page, identifier, single in audio:
            tasks.append(pool.submit(lambda n,p,i,s: split_steps(n, field_recording(n,p,i),s), name,page,identifier,single))
        for task in tasks:
            try:
                task.result()
            except Exception as error:
                print(type(error).__name__, error, flush=True)
    surf = Path(sys.argv[2]) / "Surf.wav"
    ffmpeg("-i",surf,"-af","lowpass=f=430,lowpass=f=290,volume=1.55", "-ac",2,
           root / "audio/SW_Submerged.wav")
