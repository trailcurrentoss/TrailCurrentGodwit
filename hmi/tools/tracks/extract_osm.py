#!/usr/bin/env python3
"""Stage 1 of the track data: pull racing geometry out of an OpenStreetMap extract.

    extract_osm.py REGION.osm.pbf OUT.json

Keeps:
  * raceways: every way tagged highway=raceway, as a line of [lon, lat]
  * venues:   every area (closed way or multipolygon relation) that could be a
              racing venue - motor sport tags, or a racing name on a sports area
Grouping raceways into tracks, classifying them, geofences, start/finish gates
and drawings are done by build_tracks.py from OUT, so they can change without
reading the extract again.

Memory: the extract is read three times, and only the ways, relations and nodes
that racing features use are ever kept - a few million node locations for the
whole US, rather than an index of every node. pyosmium's C++ filters do the
selecting. Even so, run it under a memory limit on a desktop, for example:

    systemd-run --user --scope -p MemoryMax=16G python3 extract_osm.py ...

Data (c) OpenStreetMap contributors, ODbL 1.0. Needs pyosmium 4.
"""
import json
import re
import sys
import time

import osmium

MOTOR_SPORTS = {"motor", "motorsport", "motor_racing", "motorsports", "drag_racing", "racing",
                "autocross", "auto_racing", "car_racing", "stock_car", "rallycross", "drifting",
                "motorcycle_racing", "karting", "speedway"}
RACING_NAME = re.compile(r"raceway|speedway|motorsport|motor sport|motorplex|motor park|circuit|"
                         r"dragway|drag ?strip|race ?track|race ?park|racing|grand prix|"
                         r"proving ground", re.I)
VENUE_KEYS = ("leisure", "landuse", "sport", "amenity", "tourism")


def sports(tags):
    return {s.strip() for s in tags.get("sport", "").split(";") if s.strip()}


def is_venue(tags):
    if sports(tags) & MOTOR_SPORTS:
        return True
    name = tags.get("name", "")
    return bool(name and RACING_NAME.search(name) and any(k in tags for k in VENUE_KEYS))


def main(pbf, out):
    t0 = time.time()
    raceways = {}       # way id -> {tags, refs}
    venue_ways = {}     # closed way id -> {tags, refs}
    venue_rels = {}     # relation id -> {tags, outer way ids}
    nodes = osmium.IdTracker()
    member_ways = osmium.IdTracker()

    # 1. Ways and relations: what is racing, and what it is made of
    for obj in osmium.FileProcessor(pbf, osmium.osm.WAY | osmium.osm.RELATION) \
            .with_filter(osmium.filter.KeyFilter("highway", *VENUE_KEYS)):
        tags = dict(obj.tags)
        if obj.type_str() == "w":
            refs = [n.ref for n in obj.nodes]
            if tags.get("highway") == "raceway":
                raceways[obj.id] = {"tags": tags, "refs": refs}
                nodes.add_references(obj)
            elif len(refs) > 3 and refs[0] == refs[-1] and is_venue(tags):
                venue_ways[obj.id] = {"tags": tags, "refs": refs}
                nodes.add_references(obj)
        elif tags.get("type") in ("multipolygon", "boundary") and is_venue(tags):
            outer = [m.ref for m in obj.members if m.type == "w" and m.role in ("outer", "")]
            if outer:
                venue_rels[obj.id] = {"tags": tags, "ways": outer}
                for w in outer:
                    member_ways.add_way(w)
    print(f"pass 1: {len(raceways)} raceways, {len(venue_ways)} venue ways, "
          f"{len(venue_rels)} venue relations ({time.time() - t0:.0f} s)", flush=True)

    # 2. The ways that venue relations are built from
    rel_way_refs = {}
    if venue_rels:
        for w in osmium.FileProcessor(pbf, osmium.osm.WAY).with_filter(member_ways.id_filter()):
            rel_way_refs[w.id] = [n.ref for n in w.nodes]
            nodes.add_references(w)
    print(f"pass 2: {len(rel_way_refs)} member ways ({time.time() - t0:.0f} s)", flush=True)

    # 3. Locations of just the nodes all of that uses
    loc = {}
    for n in osmium.FileProcessor(pbf, osmium.osm.NODE).with_filter(nodes.id_filter()):
        loc[n.id] = (round(n.location.lon, 7), round(n.location.lat, 7))
    print(f"pass 3: {len(loc)} node locations ({time.time() - t0:.0f} s)", flush=True)

    def line(refs):
        pts = [loc[r] for r in refs if r in loc]
        return [list(p) for p in pts] if len(pts) == len(refs) else None

    out_raceways = []
    for wid, w in raceways.items():
        ln = line(w["refs"])
        if ln:
            out_raceways.append({"id": wid, "tags": w["tags"], "line": ln})
    out_venues = []
    for wid, w in venue_ways.items():
        ring = line(w["refs"])
        if ring:
            out_venues.append({"id": wid, "kind": "way", "tags": w["tags"], "rings": [ring]})
    for rid, r in venue_rels.items():
        parts = [line(rel_way_refs[w]) for w in r["ways"] if w in rel_way_refs]
        parts = [p for p in parts if p]
        if parts:
            # Outer ways, not yet joined into rings: build_tracks.py polygonizes them
            out_venues.append({"id": rid, "kind": "relation", "tags": r["tags"], "parts": parts})

    with open(out, "w") as f:
        json.dump({"source": pbf.rsplit("/", 1)[-1], "raceways": out_raceways, "venues": out_venues}, f)
    print(f"{len(out_raceways)} raceways, {len(out_venues)} venues in {time.time() - t0:.0f} s")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
