#!/usr/bin/env python3
"""Stage 2 of the track data: racing geometry from extract_osm.py -> tracks.

    build_tracks.py EXTRACT.json OUT_DIR [--photon URL]

With --photon, raceways that have no name and no venue are named from the
nearest racing venue a Photon geocoder knows within 1.5 km (the head unit
runs one; reach it with an SSH tunnel). Without it they are left out.

Writes OUT_DIR/tracks.json, OUT_DIR/audit.json (every candidate that did not
become a track, and why) and, per track, OUT_DIR/svg/<id>-light.svg and
OUT_DIR/svg/<id>-dark.svg.

A track is one timed layout at one venue: a road course, an oval or a drag
strip. A venue with both a circuit and a drag strip gives two tracks. Each has

  outline   every raceway at the venue, as lines of [lat, lon], for drawing
  loop      the main circuit as one closed ring of [lat, lon] (not drag strips)
  length_m  the main circuit's length, or the drag strip's racing distance
  gate      the start/finish line: two [lat, lon] ends across the track, and
            how it was placed (pit_lane, longest_straight). Laps are timed from
            one crossing to the next, so lap times are right wherever the gate
            sits; placing it at the real start/finish line only matters for
            comparing against official timing.
  drag      for drag strips: a start gate at each end and the finish distance.
            OpenStreetMap does not say which end is the start, so the app
            settles it from the direction of the first run.
  geofence  a ring of [lat, lon] around the venue, to know when we arrive

Geometry is OpenStreetMap's, traced from aerial imagery by its contributors:
(c) OpenStreetMap contributors, ODbL 1.0. Distribute it with that attribution.
Needs shapely 2.
"""
import json
import math
import os
import re
import sys
from collections import Counter, defaultdict

from shapely import make_valid
from shapely.geometry import LineString, MultiLineString, Point, Polygon, MultiPolygon
from shapely.ops import linemerge, polygonize, unary_union, nearest_points

EXCLUDE_SPORTS = {"motocross", "karting", "horse_racing", "rc_car", "bmx", "cycling", "running",
                  "athletics", "equestrian", "dog_racing", "greyhound_racing", "soccer", "swimming",
                  "harness_racing", "skateboard", "model_aerodrome", "mountainbike"}
MOTOR_SPORTS = {"motor", "motorsport", "motor_racing", "motorsports", "drag_racing", "racing",
                "autocross", "auto_racing", "car_racing", "stock_car", "rallycross", "drifting",
                "motorcycle_racing", "speedway"}
PIT_NAME = re.compile(r"\bpit", re.I)
# Not places to time a car: dirt bikes, karts, horses, police and factory test courses
NOT_RACING = re.compile(r"moto-?\s?cross|\bmx\b|\bbmx\b|off-?road|test (track|course)|proving|"
                        r"emergency vehicle|vehicle operations|driver('s)? (training|safety)|police|sheriff|"
                        r"\bkarts?\b|go-?karts?|karting|horse|equestrian|rodeo|harness|greyhound|"
                        r"\brc\b|radio control|bicycle|velodrome|pump ?track|skate|skid ?pad|autocross|"
                        r"\batv\b|\butv\b|\bohv\b|off[- ]highway|off[- ]road|4x4|mud ?bog|"
                        r"snowmobile|\bsled|lawn ?mower|grass drag|tractor pull", re.I)
# A street that happens to be named for a track: "Racetrack Road"
STREET = re.compile(r"\b(road|rd|street|st|avenue|ave|drive|dr|lane|ln|boulevard|blvd|highway|hwy|parkway|pkwy)\.?$", re.I)
# A name like this says nothing about where you are
GENERIC = re.compile(r"(race ?track|racetrack|raceway|track|oval|dirt track|drag ?strip|dragstrip|grandstands?( area)?|"
                     r"speedway|circuit|test track)s?", re.I)
DRAG_NAME = re.compile(r"drag|the strip|(1/4|1/8|quarter|eighth)[ -]mile (drag|strip)", re.I)
RACING_NAME = re.compile(r"\b(raceway|speedway|motorsports?|motor sports?|motorplex|motor park|motor club|"
                         r"circuit|circuito|dragway|drag ?strip|race ?track|race ?park|racing|grand prix|safety track|"
                         r"aut[oó]dromo|autodrome|pista|raceway park)\b", re.I)
QUARTER_MILE = 402.336
EIGHTH_MILE = 201.168

MIN_LOOP_M = 380          # a quarter-mile oval; venue filtering keeps out karting
MIN_SECOND_M = 1200       # an unnamed second course shorter than this is a kart track
MIN_STRIP_M = 300
GEOFENCE_BUFFER_M = 250
GATE_HALF_M = 14          # half the gate's length: a wide track plus margin
GROUP_GAP_M = 60          # raceways this close belong to one venue when no area does
COURSE_JOIN_M = 8         # raceways this close are one course


# ---------------------------------------------------------------- geometry

class Local:
    """Equirectangular metres around a point: plenty accurate at a venue's scale."""

    def __init__(self, lon, lat):
        self.lon0, self.lat0 = lon, lat
        self.kx = 111320.0 * math.cos(math.radians(lat))
        self.ky = 110540.0

    def fwd(self, lon, lat):
        return ((lon - self.lon0) * self.kx, (lat - self.lat0) * self.ky)

    def inv(self, x, y):
        return (self.lon0 + x / self.kx, self.lat0 + y / self.ky)

    def line(self, pts):
        return LineString([self.fwd(*p) for p in pts])

    def ll(self, geom_coords, nd=6):
        """[lat, lon] pairs, as the app reads them."""
        out = []
        for x, y in geom_coords:
            lon, lat = self.inv(x, y)
            out.append([round(lat, nd), round(lon, nd)])
        return out


def sports(tags):
    return {s.strip() for s in tags.get("sport", "").split(";") if s.strip()}


def slow(tags):
    """A posted limit under 10 mph: a ride, not a racetrack. (Circuits that are
    park roads between events post real limits: Gilles-Villeneuve has 30 km/h.)"""
    m = re.match(r"\s*(\d+(?:\.\d+)?)\s*(mph|km/h|kmh)?", tags.get("maxspeed", ""))
    if not m:
        return False
    v = float(m.group(1)) * (1 if m.group(2) == "mph" else 0.621)
    return v < 10


def excluded(tags):
    if tags.get("guide_track") == "yes" or slow(tags):
        return True  # theme park car rides run on a guide rail
    s = sports(tags)
    return bool(s) and not (s & MOTOR_SPORTS) and bool(s & EXCLUDE_SPORTS)


def straightness(line):
    a, b = line.coords[0], line.coords[-1]
    return math.dist(a, b) / line.length if line.length else 0


def venue_polygon(v):
    if v["kind"] == "way":
        poly = Polygon(v["rings"][0])
    else:
        merged = linemerge([LineString(p) for p in v["parts"] if len(p) >= 2])
        polys = list(polygonize(merged))
        if not polys:
            return None
        poly = unary_union(polys)
    if not poly.is_valid:
        poly = make_valid(poly)
    if poly.is_empty or poly.area == 0:
        return None
    return poly


# ---------------------------------------------------------------- grouping

def group(raceways, venues):
    """{key: {"venue": venue or None, "raceways": [...]}} by containing venue,
    then by proximity for raceways no venue contains."""
    vpolys = []
    for v in venues:
        if excluded(v["tags"]) or not v["tags"].get("name"):
            continue
        p = venue_polygon(v)
        if p is not None:
            vpolys.append((v, p))
    # Small venues first, so a drag strip area inside a speedway's area wins
    vpolys.sort(key=lambda vp: vp[1].area)

    groups = {}
    loose = []
    for r in raceways:
        if excluded(r["tags"]) or len(r["line"]) < 2:
            continue
        line = LineString(r["line"])
        mid = line.interpolate(0.5, normalized=True)
        for v, p in vpolys:
            if p.covers(mid) or p.intersection(line).length > 0.5 * line.length:
                g = groups.setdefault(("v", v["id"]), {"venue": v, "poly": p, "raceways": []})
                g["raceways"].append(r)
                break
        else:
            loose.append(r)

    # Raceways outside any venue: join neighbours, keep groups that have a name
    loc = None
    parent = list(range(len(loose)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    if loose:
        lines = []
        for r in loose:
            if loc is None:
                loc = Local(*r["line"][0])
            lines.append(r)
        # Bucket by a coarse grid so this stays fast for the whole country
        grid = defaultdict(list)
        geoms = []
        for i, r in enumerate(loose):
            lm = Local(*r["line"][0])
            geoms.append((lm, r))
            gx, gy = int(r["line"][0][0] * 50), int(r["line"][0][1] * 50)
            grid[(gx, gy)].append(i)
        for (gx, gy), idx in list(grid.items()):
            near = [j for dx in (-1, 0, 1) for dy in (-1, 0, 1) for j in grid.get((gx + dx, gy + dy), [])]
            for i in idx:
                lm, ri = geoms[i]
                li = lm.line(ri["line"])
                for j in near:
                    if j <= i:
                        continue
                    lj = lm.line(loose[j]["line"])
                    if li.distance(lj) < GROUP_GAP_M:
                        parent[find(i)] = find(j)
        for i, r in enumerate(loose):
            groups.setdefault(("r", find(i)), {"venue": None, "poly": None, "raceways": []})["raceways"].append(r)
    return groups


# ---------------------------------------------------------------- tracks

def track_id(name, used):
    base = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")[:48] or "track"
    tid, n = base, 2
    while tid in used:
        tid, n = f"{base}-{n}", n + 1
    used.add(tid)
    return tid


def gate_across(line, dist, local):
    """A gate across LINE at DIST metres along it: two ends, [lat, lon]."""
    d0, d1 = max(0, dist - 3), min(line.length, dist + 3)
    a, b = line.interpolate(d0), line.interpolate(d1)
    dx, dy = b.x - a.x, b.y - a.y
    n = math.hypot(dx, dy) or 1
    px, py = -dy / n * GATE_HALF_M, dx / n * GATE_HALF_M
    c = line.interpolate(dist)
    heading = (math.degrees(math.atan2(dx, dy)) + 360) % 360
    return local.ll([(c.x + px, c.y + py), (c.x - px, c.y - py)]), round(heading, 1)


def main_loop(lines):
    """The main circuit: the outer ring of everything the raceways enclose."""
    noded = unary_union(lines)
    faces = list(polygonize(noded))
    if not faces:
        return None
    shape = unary_union(faces)
    if isinstance(shape, MultiPolygon):
        shape = max(shape.geoms, key=lambda p: p.area)
    ring = LineString(shape.exterior.coords)
    return ring if ring.length >= MIN_LOOP_M else None


def longest_straight(ring):
    s = ring.simplify(4)
    best, at = 0, 0.0
    run = 0.0
    coords = list(s.coords)
    for a, b in zip(coords, coords[1:]):
        seg = math.dist(a, b)
        if seg > best:
            best = seg
            at = run + seg / 2
        run += seg
    # Back onto the unsimplified ring
    p = s.interpolate(at)
    return ring.project(p), best


def build_track(name, venue, raceways, poly, kind_hint, used_ids):
    pts = [p for r in raceways for p in r["line"]]
    lon0 = sum(p[0] for p in pts) / len(pts)
    lat0 = sum(p[1] for p in pts) / len(pts)
    local = Local(lon0, lat0)
    pit, race = [], []
    for r in raceways:
        t = r["tags"]
        is_pit = t.get("raceway") in ("pit", "pit_lane", "pitlane") or PIT_NAME.search(t.get("name", ""))
        (pit if is_pit else race).append(local.line(r["line"]))
    if not race:
        return None

    track = {"name": name}
    if kind_hint == "drag":
        strip = max(race, key=lambda l: l.length)
        if strip.length < MIN_STRIP_M:
            return None
        dist = QUARTER_MILE if strip.length >= QUARTER_MILE + 150 else EIGHTH_MILE
        a_gate, a_head = gate_across(strip, 0, local)
        b_gate, b_head = gate_across(strip, strip.length, local)
        track.update(type="drag", length_m=round(dist, 1),
                     drag={"distance_m": round(dist, 1), "strip_m": round(strip.length),
                           "starts": [{"gate": a_gate, "heading": a_head},
                                      {"gate": b_gate, "heading": (b_head + 180) % 360}]})
        main = strip
    else:
        ring = main_loop(race)
        if ring is None:
            return None
        if pit:
            # On the raceway that runs beside the pits: the start/finish
            # straight, which every layout at the venue shares
            pit_mid = unary_union(pit).centroid
            on = min(race, key=lambda l: l.distance(pit_mid))
            gate, heading = gate_across(on, on.project(pit_mid), local)
            how = "pit_lane"
        else:
            at, _ = longest_straight(ring)
            gate, heading = gate_across(ring, at, local)
            how = "longest_straight"
        hull = ring.convex_hull
        fill = Polygon(ring.coords).area / hull.area if hull.area else 0
        oval = ring.length < 4500 and (fill > 0.97 or (fill > 0.85 and re.search(r"speedway|oval", name, re.I)))
        track.update(type="oval" if oval else "road", length_m=round(ring.length),
                     gate={"ends": gate, "heading": heading, "placed": how,
                           "direction_known": any(r["tags"].get("oneway") == "yes" for r in raceways)},
                     loop=local.ll(ring.simplify(1.5).coords))
        main = ring

    # Geofence: the venue's own boundary, or the track's surroundings, plus a margin
    if poly is not None:
        area = unary_union([local.line(list(c)) for c in []] or [Polygon([local.fwd(*xy) for xy in poly.exterior.coords])]) \
            if isinstance(poly, Polygon) else unary_union(
                [Polygon([local.fwd(*xy) for xy in g.exterior.coords]) for g in poly.geoms])
    else:
        area = unary_union(race + pit).convex_hull
    fence = area.buffer(GEOFENCE_BUFFER_M, quad_segs=4).simplify(25)
    if isinstance(fence, MultiPolygon):
        fence = max(fence.geoms, key=lambda p: p.area)

    c = fence.centroid
    clon, clat = local.inv(c.x, c.y)
    drawn = [l.simplify(1.5) for l in race + pit]
    track.update(
        id=track_id(name, used_ids),
        center=[round(clat, 6), round(clon, 6)],
        geofence=local.ll(fence.exterior.coords, 6),
        outline=[local.ll(l.coords) for l in drawn],
        pit=[local.ll(l.simplify(1.5).coords) for l in pit],
        osm={"venue": venue["id"] if venue else None, "ways": sorted(r["id"] for r in raceways)},
    )
    # The venue's own postal address, when mapped: what the track calls its town
    if venue and venue["tags"].get("addr:city"):
        track["addr"] = {"city": venue["tags"]["addr:city"], "state": venue["tags"].get("addr:state")}
    track["_draw"] = {"race": [l.simplify(1.5) for l in race], "pit": [l.simplify(1.5) for l in pit],
                      "main": main, "local": local}
    return track


def is_pit(r):
    t = r["tags"]
    return t.get("raceway") in ("pit", "pit_lane", "pitlane") or bool(PIT_NAME.search(t.get("name", "")))


def courses(raceways, venue_name):
    """Split a venue's circuit raceways into separate courses: Big Willow and
    Streets of Willow are two tracks, not one drawing. Raceways that touch (within
    COURSE_JOIN_M) are one course; pit lanes go with the course they run beside;
    a course must hold a loop worth timing. The largest takes the venue's name."""
    if not raceways:
        return []
    pts = [p for r in raceways for p in r["line"]]
    local = Local(sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
    race = [r for r in raceways if not is_pit(r)]
    pits = [r for r in raceways if is_pit(r)]
    geo = {id(r): local.line(r["line"]) for r in raceways}
    parent = {id(r): id(r) for r in race}

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for a in range(len(race)):
        for b in range(a + 1, len(race)):
            if geo[id(race[a])].distance(geo[id(race[b])]) < COURSE_JOIN_M:
                parent[find(id(race[a]))] = find(id(race[b]))
    comps = defaultdict(list)
    for r in race:
        comps[find(id(r))].append(r)
    keep = []
    for members in comps.values():
        ring = main_loop([geo[id(r)] for r in members])
        if ring is not None:
            keep.append((ring.length, members))
    if not keep:
        return []
    keep.sort(key=lambda k: -k[0])
    for p in pits:
        best = min(keep, key=lambda k: min(geo[id(p)].distance(geo[id(r)]) for r in k[1]))
        best[1].append(p)
    out, used = [], set()
    for n, (loop_m, members) in enumerate(keep):
        own = Counter(r["tags"]["name"] for r in members if r["tags"].get("name") and not is_pit(r)
                      and not GENERIC.fullmatch(r["tags"]["name"].strip()))
        cname = own.most_common(1)[0][0] if own else None
        if n > 0 and ((not cname and loop_m < MIN_SECOND_M) or (cname and NOT_RACING.search(cname))):
            continue
        if n == 0:
            cname = venue_name
        elif not cname or cname in used:
            cname = f"{venue_name} Course {n + 1}"
        elif venue_name.lower() not in cname.lower():
            cname = f"{venue_name} – {cname}"
        used.add(cname)
        out.append((members, cname))
    return out


def venue_size(v):
    """The venue's longest side in metres, roughly: big dropped venues first in the audit."""
    pts = [p for ring in (v.get("rings") or v.get("parts") or []) for p in ring]
    if not pts:
        return 0
    lm = Local(pts[0][0], pts[0][1])
    xy = [lm.fwd(*p) for p in pts]
    return round(max(max(x for x, _ in xy) - min(x for x, _ in xy), max(y for _, y in xy) - min(y for _, y in xy)))


def dropped_group(g):
    """Why a group of raceways made no track, for the audit."""
    v, rws = g["venue"], g["raceways"]
    vname = v["tags"].get("name") if v else None
    names = Counter(r["tags"].get("name") for r in rws if r["tags"].get("name"))
    name = vname or (names.most_common(1)[0][0] if names else None)
    pts = [p for r in rws for p in r["line"]]
    lm = Local(pts[0][0], pts[0][1])
    total = sum(lm.line(r["line"]).length for r in rws)
    if not name:
        reason = "raceways with no name and no named venue"
    elif GENERIC.fullmatch(name.strip()):
        reason = "only a generic name"
    elif NOT_RACING.search(name):
        reason = "name says karting, motocross, test course or similar"
    elif v is None and not any(sports(r["tags"]) & MOTOR_SPORTS for r in rws) and not RACING_NAME.search(name):
        reason = "no venue and no motor sport tag"
    else:
        reason = "no closed loop of at least %d m and no drag strip" % MIN_LOOP_M
    return {"name": name, "reason": reason, "where": [pts[0][1], pts[0][0]],
            "osm": f"{v['kind']}/{v['id']}" if v else f"way/{rws[0]['id']}",
            "raceway_m": round(total), "size_m": venue_size(v) if v else round(total)}


PHOTON = None   # base URL of a Photon geocoder, for --photon
NEARBY_KM = 1.5
VENUE_KINDS = {("leisure", "track"), ("leisure", "sports_centre"), ("leisure", "stadium"),
               ("highway", "raceway"), ("landuse", "recreation_ground")}


def nearby_racing_name(raceways):
    """Raceways with no name of their own and no venue drawn around them: the
    venue's name is often on a point or a small area beside them (Dominion
    Raceway, zMAX Dragway). Take the nearest racing name Photon knows nearby."""
    import urllib.parse
    import urllib.request
    pts = [p for r in raceways for p in r["line"]]
    lon = sum(p[0] for p in pts) / len(pts)
    lat = sum(p[1] for p in pts) / len(pts)
    q = urllib.parse.urlencode({"lat": lat, "lon": lon, "limit": 20, "radius": NEARBY_KM})
    try:
        with urllib.request.urlopen(f"{PHOTON}/reverse?{q}", timeout=20) as r:
            feats = json.load(r).get("features", [])
    except OSError:
        return None
    for f in feats:
        p = f["properties"]
        name = p.get("name") or ""
        if not name or NOT_RACING.search(name) or GENERIC.fullmatch(name.strip()):
            continue
        if RACING_NAME.search(name) or (p.get("osm_key"), p.get("osm_value")) in VENUE_KINDS:
            return name
    return None


def tracks_for_group(g, used_ids):
    venue = g["venue"]
    raceways = g["raceways"]
    vname = venue["tags"].get("name") if venue else None
    names = Counter(r["tags"].get("name") for r in raceways if r["tags"].get("name")
                    and not PIT_NAME.search(r["tags"]["name"]))
    rname = names.most_common(1)[0][0] if names else None
    name = vname or rname
    # A circuit inside a multi-sport park is known by its own name, not the
    # park's: Autódromo Hermanos Rodríguez in Ciudad Deportiva Magdalena Mixhuca
    if vname and rname and rname != vname and RACING_NAME.search(rname) and not GENERIC.fullmatch(rname.strip()):
        other_sports = sports(venue["tags"]) - MOTOR_SPORTS
        if len(other_sports) >= 2 or not RACING_NAME.search(vname):
            name = rname
    named_nearby = False
    if not name and PHOTON:
        name = nearby_racing_name(raceways)
        named_nearby = bool(name)
    if not name or GENERIC.fullmatch(name.strip()) or NOT_RACING.search(name) or STREET.search(name.strip()):
        return []
    # With no venue around them, raceways must say they are for motor racing:
    # theme park rides and private test loops are tagged highway=raceway too
    if venue is None and not named_nearby and not (any(sports(r["tags"]) & MOTOR_SPORTS for r in raceways)
                                                   or RACING_NAME.search(name)):
        return []

    pts = [p for r in raceways for p in r["line"]]
    gl = Local(sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
    geo = {r["id"]: gl.line(r["line"]) for r in raceways}

    def is_drag(r):
        t = r["tags"]
        line = geo[r["id"]]
        straight = MIN_STRIP_M <= line.length < 1600 and straightness(line) > 0.985
        # A straight that touches no other raceway is a drag strip, whatever it is
        # called ("Maple Grove Raceway"); one joined to others is part of a circuit
        alone = all(geo[q["id"]].distance(line) > COURSE_JOIN_M for q in raceways
                    if q is not r and not is_pit(q))
        return (t.get("raceway") in ("drag_strip", "dragstrip") or "drag_racing" in sports(t)
                or DRAG_NAME.search(t.get("name", ""))
                or (straight and (alone or "drag" in name.lower())))

    drag = [r for r in raceways if is_drag(r)]
    rest = [r for r in raceways if r not in drag]
    out = []
    for i, (course, cname) in enumerate(courses(rest, name)):
        t = build_track(cname, venue, course, g["poly"], "circuit", used_ids)
        if t:
            out.append(t)
    if drag:
        dname = next((r["tags"]["name"] for r in drag if r["tags"].get("name")
                      and r["tags"]["name"] != name and not re.fullmatch(r"(drag ?strip|dragstrip)", r["tags"]["name"], re.I)), None)
        if not dname:
            dname = name if not out else f"{name} Drag Strip"
        t = build_track(dname, venue, drag, g["poly"], "drag", used_ids)
        if t:
            out.append(t)
    return out


# ---------------------------------------------------------------- drawing

THEMES = {
    # From hmi/qml/Theme.qml: track band, centre line, start/finish (acc)
    "light": {"band": "#e3e3e3", "line": "#a8a8a8", "pit": "#c4c4c4", "gate": "#3D7B31"},
    "dark": {"band": "#2f2f2f", "line": "#7a7a7a", "pit": "#4a4a4a", "gate": "#7BC96A"},
}


def svg(track, theme):
    d = track["_draw"]
    geoms = d["race"] + d["pit"]
    minx = min(g.bounds[0] for g in geoms)
    miny = min(g.bounds[1] for g in geoms)
    maxx = max(g.bounds[2] for g in geoms)
    maxy = max(g.bounds[3] for g in geoms)
    span = max(maxx - minx, maxy - miny, 50)
    pad = span * 0.08
    w, h = (maxx - minx) + 2 * pad, (maxy - miny) + 2 * pad
    band, line = span * 0.045, span * 0.012
    col = THEMES[theme]

    def path(g):
        # SVG y grows down; north is up
        return "M" + " L".join(f"{x - minx + pad:.1f},{maxy - y + pad:.1f}" for x, y in g.coords)

    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.1f} {h:.1f}">',
             f"<!-- {track['name']}. Map data (c) OpenStreetMap contributors, ODbL 1.0 -->",
             f'<g fill="none" stroke-linecap="round" stroke-linejoin="round">']
    for g in d["pit"]:
        parts.append(f'<path d="{path(g)}" stroke="{col["pit"]}" stroke-width="{line * 1.4:.1f}" '
                     f'stroke-dasharray="{line * 3:.1f} {line * 2:.1f}"/>')
    for g in d["race"]:
        parts.append(f'<path d="{path(g)}" stroke="{col["band"]}" stroke-width="{band:.1f}"/>')
    for g in d["race"]:
        parts.append(f'<path d="{path(g)}" stroke="{col["line"]}" stroke-width="{line:.1f}"/>')
    gates = [track["gate"]["ends"]] if "gate" in track else [s["gate"] for s in track["drag"]["starts"]]
    for ends in gates:
        # Drawn across the band at a size that reads at any scale; the gate in
        # tracks.json keeps its true length
        (la1, lo1), (la2, lo2) = ends
        x1, y1 = d["local"].fwd(lo1, la1)
        x2, y2 = d["local"].fwd(lo2, la2)
        cx, cy = (x1 + x2) / 2, (y1 + y2) / 2
        ux, uy = (x2 - x1), (y2 - y1)
        un = math.hypot(ux, uy) or 1
        half = band * 1.25
        ax, ay = cx - ux / un * half, cy - uy / un * half
        bx, by = cx + ux / un * half, cy + uy / un * half
        parts.append(f'<path d="M{ax - minx + pad:.1f},{maxy - ay + pad:.1f} L{bx - minx + pad:.1f},{maxy - by + pad:.1f}" '
                     f'stroke="{col["gate"]}" stroke-width="{line * 2.4:.1f}"/>')
    parts.append("</g></svg>")
    return "\n".join(parts) + "\n"


# ---------------------------------------------------------------- main

TYPE_WORD = {"road": "Road Course", "oval": "Oval", "drag": "Drag Strip"}


def implied_type(name):
    n = name.lower()
    if re.search(r"drag", n):
        return "drag"
    if re.search(r"speedway|oval", n):
        return "oval"
    return "road"


def disambiguate(tracks):
    """Tracks with one name within a few km of each other are layouts at one
    venue. The layout the name implies (a Dragway's strip, a Speedway's oval),
    else the longest, keeps the plain name; the others are told apart by type,
    then by number. Same names far apart are different places, and their towns
    tell them apart."""
    taken = {t["name"] for t in tracks}
    by_name = defaultdict(list)
    for t in tracks:
        by_name[t["name"]].append(t)
    for name, ts in by_name.items():
        if len(ts) < 2:
            continue
        clusters = []
        for t in sorted(ts, key=lambda t: -t["length_m"]):
            for c in clusters:
                a, b = c[0]["center"], t["center"]
                if math.hypot((a[0] - b[0]) * 110.5, (a[1] - b[1]) * 111.3 * math.cos(math.radians(a[0]))) < 3:
                    c.append(t)
                    break
            else:
                clusters.append([t])
        for c in clusters:
            want = implied_type(name)
            c.sort(key=lambda t: (t["type"] != want, -t["length_m"]))
            for t in c[1:]:
                word = TYPE_WORD[t["type"]]
                label = f"{name} {word}"
                n = 2
                while label in taken:
                    label = f"{name} {word} {n}"
                    n += 1
                taken.add(label)
                t["name"] = label


def main(src, out_dir):
    data = json.load(open(src))
    groups = group(data["raceways"], data["venues"])
    used, tracks = set(), []
    audit = []
    for g in groups.values():
        made = tracks_for_group(g, used)
        tracks.extend(made)
        if not made:
            audit.append(dropped_group(g))
    # Venues no raceway fell inside: the track itself is not mapped
    grouped = {g["venue"]["id"] for g in groups.values() if g["venue"]}
    for v in data["venues"]:
        if v["id"] in grouped or not v["tags"].get("name") or excluded(v["tags"]):
            continue
        name = v["tags"]["name"]
        if NOT_RACING.search(name) or GENERIC.fullmatch(name.strip()):
            continue
        ring = v.get("rings", [None])[0] or (v.get("parts") or [[None]])[0]
        audit.append({"name": name, "reason": "no highway=raceway mapped inside the venue",
                      "where": [ring[0][1], ring[0][0]] if ring and ring[0] else None,
                      "osm": f"{v['kind']}/{v['id']}", "size_m": venue_size(v)})
    os.makedirs(os.path.join(out_dir, "svg"), exist_ok=True)
    for t in tracks:
        for theme in THEMES:
            with open(os.path.join(out_dir, "svg", f"{t['id']}-{theme}.svg"), "w") as f:
                f.write(svg(t, theme))
        t["svg"] = {theme: f"svg/{t['id']}-{theme}.svg" for theme in THEMES}
        del t["_draw"]
    disambiguate(tracks)
    tracks.sort(key=lambda t: t["name"].lower())
    with open(os.path.join(out_dir, "tracks.json"), "w") as f:
        json.dump({"attribution": "Map data (c) OpenStreetMap contributors, ODbL 1.0",
                   "source": data.get("source"), "tracks": tracks}, f, separators=(",", ":"))
    audit.sort(key=lambda a: -(a.get("size_m") or 0))
    with open(os.path.join(out_dir, "audit.json"), "w") as f:
        json.dump(audit, f, indent=1)
    kinds = Counter(t["type"] for t in tracks)
    print(f"{len(audit)} candidates not made into tracks: see audit.json")
    print(f"{len(tracks)} tracks: " + ", ".join(f"{n} {k}" for k, n in kinds.most_common()))


if __name__ == "__main__":
    if len(sys.argv) == 5 and sys.argv[3] == "--photon":
        PHOTON = sys.argv[4].rstrip("/")
        del sys.argv[3:]
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
