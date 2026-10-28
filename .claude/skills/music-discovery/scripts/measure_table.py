"""A table of what measure_one.sh measured: loudness (LUFS), crest factor, and the low end (the
louder of 20-60 and 60-120 Hz) against the loudest band above 4 kHz, by folder.
Usage: measure_table.py <out-folder>"""

import glob
import json
import os
import sys

rows = []
for f in glob.glob(os.path.join(sys.argv[1], "*.json")):
    try:
        j = json.load(open(f))
    except ValueError:
        print("unreadable:", f)
        continue
    path = open(f + ".path").read().strip()
    bands = {b["fromHz"]: b["db"] for b in j["spectrum"]}
    low = max(bands.get(20.0, -99), bands.get(60.0, -99))
    high = max(v for k, v in bands.items() if k >= 4000)
    rows.append((os.path.basename(os.path.dirname(path)), os.path.basename(path), j["integratedLufs"], j["crestDb"], low - high, j["durationSeconds"]))

for folder, name, lufs, crest, balance, seconds in sorted(rows):
    print(f"{folder[:20]:20} {lufs:6.1f} LUFS  crest {crest:5.1f}  low-high {balance:5.1f}  {seconds / 60:4.1f} min  {name[:70]}")
print(len(rows), "files")
