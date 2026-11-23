#!/usr/bin/env python3
"""Write the expected values tst_dbc checks Dbc against, using cantools.

    python3 make_dbc_fixtures.py <TrailCurrent.dbc> > dbc_fixtures.json

For every message: frames of random bytes and what cantools decodes them to,
and random raw values and the frame cantools encodes them into. Also a small
little-endian, signed and scaled message of its own, as TrailCurrent.dbc has
only big-endian ones. Run again when the DBC changes.
"""
import json
import random
import sys

import cantools

EXTRA = """VERSION ""
BS_:
BU_: A
BO_ 1792 IntelMix: 8 A
 SG_ U12 : 0|12@1+ (0.5,-10) [-10|2037.5] "" A
 SG_ S10 : 12|10@1- (1,0) [-512|511] "" A
 SG_ Flag : 22|1@1+ (1,0) [0|1] "" A
 SG_ S16 : 24|16@1- (0.01,0) [-327.68|327.67] "" A
 SG_ U24 : 40|24@1+ (1,0) [0|16777215] "" A
BO_ 1793 MotorolaOdd: 8 A
 SG_ S13 : 5|13@0- (0.1,1) [-408.6|410.5] "" A
 SG_ U3 : 25|3@0+ (1,0) [0|7] "" A
 SG_ U20 : 43|20@0+ (1,0) [0|1048575] "" A
"""


def cases(db, rng):
    out = []
    for m in db.messages:
        if m.is_multiplexed() or m.is_extended_frame:
            continue
        for _ in range(6):
            data = bytes(rng.randrange(256) for _ in range(m.length))
            dec = m.decode(data, decode_choices=False, scaling=True)
            raw = {}
            for s in m.signals:
                lo = -(1 << (s.length - 1)) if s.is_signed else 0
                hi = (1 << (s.length - 1)) - 1 if s.is_signed else (1 << s.length) - 1
                raw[s.name] = rng.randint(lo, hi)
            phys = {s.name: raw[s.name] * s.scale + s.offset for s in m.signals}
            enc = m.encode(raw, scaling=False, strict=False, padding=False)
            out.append({
                "message": m.name,
                "data": data.hex(),
                "decoded": {k: float(v) for k, v in dec.items()},
                "values": phys,
                "encoded": enc.hex(),
            })
    return out


def main():
    rng = random.Random(1)
    db = cantools.database.load_file(sys.argv[1], strict=False)
    extra = cantools.database.load_string(EXTRA, database_format="dbc", strict=False)
    json.dump({"extraDbc": EXTRA, "trailcurrent": cases(db, rng), "extra": cases(extra, rng)}, sys.stdout, indent=1)
    print()


if __name__ == "__main__":
    main()
