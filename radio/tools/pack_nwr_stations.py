#!/usr/bin/env python3
"""Packs NOAA's list of Weather Radio transmitters into the table the HMI reads.

usage: pack_nwr_stations.py ccl-data.js ../data/nwr-stations.tsv [YYYY-MM-DD]

The list comes from https://www.weather.gov/source/nwr/JS/ccl-data.js, the file
behind weather.gov/nwr/stations. The address has no version, so the result is kept
in this repository and neither the image build nor the head unit needs the internet.
Run this again to refresh it. The date is when the list was downloaded.

A row: frequency in Hz, call sign, latitude, longitude, site, state. Transmitters
out of service when the list was taken are left out, so one is not named as the
nearest while it is off the air.
"""

import datetime
import json
import sys

CHANNELS = {"162.400", "162.425", "162.450", "162.475", "162.500", "162.525", "162.550"}


def clean(text):
    return " ".join(str(text).split())


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    taken = sys.argv[3] if len(sys.argv) == 4 else datetime.date.today().isoformat()
    with open(sys.argv[1], encoding="utf-8") as f:
        text = f.read()
    stations = json.loads(text[text.index("["):text.rindex("]") + 1])

    rows = []
    skipped = {"blank": 0, "out of service": 0}
    for s in stations:
        if s.get("freq") not in CHANNELS or not s.get("callsign") or not s.get("lat") or not s.get("lon"):
            skipped["blank"] += 1
            continue
        if s.get("status") == "OUT OF SERVICE":
            skipped["out of service"] += 1
            continue
        hz = round(float(s["freq"]) * 1e6)
        rows.append((hz, clean(s["callsign"]), "%.4f" % float(s["lat"]), "%.4f" % float(s["lon"]),
                     clean(s.get("sitename", "")), clean(s.get("sitestate", ""))))
    rows.sort()

    with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as f:
        f.write("# NOAA Weather Radio transmitters, from weather.gov/nwr/stations, %s.\n" % taken)
        f.write("# Public domain, from the US National Weather Service, which does not endorse this product.\n")
        f.write("# Hz, call sign, latitude, longitude, site, state. Packed by tools/pack_nwr_stations.py.\n")
        for row in rows:
            f.write("\t".join(str(c) for c in row) + "\n")
    print("%d stations; left out %d blank, %d out of service" % (len(rows), skipped["blank"], skipped["out of service"]))


if __name__ == "__main__":
    main()
