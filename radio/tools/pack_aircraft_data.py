#!/usr/bin/env python3
"""Packs Virtual Radar Server's standing data for the Aircraft app.

ADS-B does not say what an aircraft is or where it is going. These two tables,
looked up by its address and its callsign, do, without any internet:

    aircraft.tsv   <ICAO hex> TAB <ICAO type> TAB <registration>
    routes.tsv     <callsign> TAB <airport> <airport> [...]

Each is sorted by its key in byte order, so the HMI can binary search the file
without loading it. Airports are three-letter IATA codes where one exists, as
people know them, and four-letter ICAO codes otherwise.

Usage: pack_aircraft_data.py <standing-data directory> <output directory>
"""

import csv
import os
import sys
from pathlib import Path


def rows(root, table):
    """Every row of a table, across the many small files it is split into."""
    for path in sorted((root / table / "schema-01").rglob("*.csv")):
        with path.open(encoding="utf-8-sig", newline="") as f:
            yield from csv.DictReader(f)


def clean(text):
    """Keeps a field on its own line and in its own column."""
    return " ".join((text or "").split())


def pack_airports(root):
    airports = {}
    for row in rows(root, "airports"):
        code = clean(row.get("ICAO")) or clean(row.get("Code"))
        if code:
            airports[code] = clean(row.get("IATA")) or code
    return airports


def pack_routes(root, airports):
    routes = {}
    for row in rows(root, "routes"):
        call = clean(row.get("Callsign"))
        stops = clean(row.get("AirportCodes")).split("-")
        if call and len(stops) >= 2 and all(stops):
            routes[call] = " ".join(airports.get(stop, stop) for stop in stops)
    return routes


def pack_aircraft(root):
    aircraft = {}
    for row in rows(root, "aircraft"):
        icao = clean(row.get("ICAO")).upper()
        kind = clean(row.get("ModelICAO"))
        registration = clean(row.get("Registration"))
        if icao and (kind or registration):
            aircraft[icao] = kind + "\t" + registration
    return aircraft


def write_table(path, table):
    with path.open("wb") as f:
        for key in sorted(table, key=lambda k: k.encode("utf-8")):
            f.write(f"{key}\t{table[key]}\n".encode("utf-8"))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    root, out = Path(sys.argv[1]), Path(sys.argv[2])
    os.makedirs(out, exist_ok=True)
    aircraft = pack_aircraft(root)
    routes = pack_routes(root, pack_airports(root))
    write_table(out / "aircraft.tsv", aircraft)
    write_table(out / "routes.tsv", routes)
    print(f"{len(aircraft)} aircraft, {len(routes)} routes")


if __name__ == "__main__":
    main()
