#!/usr/bin/env python3
"""Stage 3 of the track data: name the nearest city for each track ("Tooele, UT").

    label_tracks.py TRACKS.json PHOTON_URL

Uses the venue's own postal town (addr:city) when OpenStreetMap has one;
otherwise asks a Photon geocoder (the head unit runs one on 127.0.0.1:2322; reach it with
an SSH tunnel) for the nearest city, then town, then village around each
track's centre, and writes TRACKS.json back with "place" and "state" filled in.
Nothing leaves the local network.
"""
import json
import sys
import urllib.parse
import urllib.request

STATES = {
    "Alabama": "AL", "Alaska": "AK", "Arizona": "AZ", "Arkansas": "AR", "California": "CA",
    "Colorado": "CO", "Connecticut": "CT", "Delaware": "DE", "District of Columbia": "DC",
    "Florida": "FL", "Georgia": "GA", "Hawaii": "HI", "Idaho": "ID", "Illinois": "IL",
    "Indiana": "IN", "Iowa": "IA", "Kansas": "KS", "Kentucky": "KY", "Louisiana": "LA",
    "Maine": "ME", "Maryland": "MD", "Massachusetts": "MA", "Michigan": "MI", "Minnesota": "MN",
    "Mississippi": "MS", "Missouri": "MO", "Montana": "MT", "Nebraska": "NE", "Nevada": "NV",
    "New Hampshire": "NH", "New Jersey": "NJ", "New Mexico": "NM", "New York": "NY",
    "North Carolina": "NC", "North Dakota": "ND", "Ohio": "OH", "Oklahoma": "OK", "Oregon": "OR",
    "Pennsylvania": "PA", "Rhode Island": "RI", "South Carolina": "SC", "South Dakota": "SD",
    "Tennessee": "TN", "Texas": "TX", "Utah": "UT", "Vermont": "VT", "Virginia": "VA",
    "Washington": "WA", "West Virginia": "WV", "Wisconsin": "WI", "Wyoming": "WY",
    "Puerto Rico": "PR",
    # Canada
    "Alberta": "AB", "British Columbia": "BC", "Manitoba": "MB", "New Brunswick": "NB",
    "Newfoundland and Labrador": "NL", "Nova Scotia": "NS", "Ontario": "ON",
    "Prince Edward Island": "PE", "Quebec": "QC", "Québec": "QC", "Saskatchewan": "SK",
    "Northwest Territories": "NT", "Nunavut": "NU", "Yukon": "YT",
    # Mexican states keep their names: there is no abbreviation everyone reads
}
# A city or town right by the track wins; otherwise the nearest city, then
# anything smaller. (kind, radius in km)
TIERS = [("city", 5), ("town", 5), ("village", 5), ("city", 30), ("town", 30), ("village", 20), ("hamlet", 15)]


def nearest(base, lat, lon, kind, radius):
    q = urllib.parse.urlencode({"lat": lat, "lon": lon, "limit": 1, "radius": radius,
                                "osm_tag": f"place:{kind}", "lang": "en"})
    with urllib.request.urlopen(f"{base}/reverse?{q}", timeout=20) as r:
        feats = json.load(r).get("features", [])
    return feats[0]["properties"] if feats else None


def main(path, base):
    data = json.load(open(path))
    for t in data["tracks"]:
        lat, lon = t["center"]
        addr = t.pop("addr", None) or {}
        t["place"], t["state"], t["country"] = None, None, None
        for kind, radius in TIERS:
            p = nearest(base, lat, lon, kind, radius)
            if p and p.get("name"):
                # The venue's own postal town, when mapped, over the nearest place;
                # the nearest place still gives the state and country if it lacks them
                state = addr.get("state") or p.get("state", "")
                t["place"] = addr.get("city") or p["name"]
                t["state"] = STATES.get(state, state)
                t["country"] = (p.get("countrycode") or "").upper() or None
                break
        if not t["place"] and addr.get("city"):
            t["place"], t["state"] = addr["city"], STATES.get(addr.get("state") or "", addr.get("state"))
    with open(path, "w") as f:
        json.dump(data, f, separators=(",", ":"))
    missing = sum(1 for t in data["tracks"] if not t.get("place"))
    print(f"labelled {len(data['tracks']) - missing} of {len(data['tracks'])} tracks")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2].rstrip("/"))
