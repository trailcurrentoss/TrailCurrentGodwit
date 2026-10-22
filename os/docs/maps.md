# Maps, search and routing

Everything the head unit's navigation needs runs on the head unit itself:
the map, destination search and turn-by-turn routing. Nothing goes to the
internet, and none of it needs a network connection to work.

All three read a [Headwaters](https://github.com/trailcurrentoss/TrailCurrentHeadwaters)
map bundle installed on the head unit's own drive. This page covers what a
bundle holds, how to install or update one, and how to check it works.

## What runs

| Piece | Reads | How it runs |
|---|---|---|
| Map | `tiles.pmtiles` | MapLibre inside the HMI reads the tiles straight from disk. |
| Search | `photon_data/` | [Photon](https://github.com/komoot/photon) 1.3.0 on Java 21, as `photon.service`. It listens on `127.0.0.1:2322` only and starts after the HMI, so it never delays the screen. The HMI's search box sends it each query. |
| Routing | `valhalla_tiles.tar` | [Valhalla](https://github.com/valhalla/valhalla) 3.9.1, built into the HMI as a library, so no routing server runs. It reads the tar in place, through the page cache. Its settings are in `/etc/valhalla/valhalla.json`, which has no tile URL, so it can never fetch tiles. |

Photon needs about 2 GB of memory (its heap is capped at 2 GB in
`photon.service`). The rest of its index is read from disk as needed.

## Where a bundle goes

Bundles live under `/data/maps`, the same layout Headwaters uses:

```
/data/maps/
├── versions/
│   └── 2026.07.13/           one folder per bundle, named by its date
│       ├── manifest.json
│       ├── tiles.pmtiles
│       ├── valhalla_tiles.tar
│       └── photon_data/      unpacked from photon_data.tar
└── current -> versions/2026.07.13
```

The HMI, Photon and Valhalla all read `/data/maps/current`, so switching
bundles is one symlink.

`photon_data/` must be owned by the `photon` user: Photon's search engine
writes lock and state files beside the index when it starts.

A North America bundle takes about 140 GB once installed. The image's root
partition grows to fill the drive on first boot (`godwit-growroot.service`),
so on a 1 TB drive there is room for it and for a second bundle while
updating.

## Get a bundle

A bundle holds four files:

| File | Size (North America) | Used for |
|---|---|---|
| `manifest.json` | 1 KB | The region, build date and a SHA-256 of each file |
| `tiles.pmtiles` | 20 GB | The map |
| `photon_data.tar` | 95 GB | Search. The same worldwide index is in every bundle, so search finds places anywhere; only the map and routing are limited to the region. |
| `valhalla_tiles.tar` | 24 GB | Routing |

If you already have a `maps-<date>.zip` from someone else, skip to
[Install or update a bundle](#install-or-update-a-bundle).

### Build one

Bundles are built on a PC with the Headwaters repository's map pipeline,
which downloads OpenStreetMap data and builds all four files in Docker.
The full guide, with every option, is Headwaters'
[`build/maps/README.md`](https://github.com/trailcurrentoss/TrailCurrentHeadwaters/blob/main/build/maps/README.md).
In short, you need:

- Linux (or macOS) with Docker Engine and its compose plugin installed
- 400–600 GB free disk for a North America build (working files plus the
  bundle), 16–24 GB RAM
- An internet connection for the first build: the OpenStreetMap extracts
  and the search index dump are tens of GB. The head unit itself never
  needs one.

Then, from a Headwaters checkout:

```sh
cd build/maps
./bootstrap.sh                        # installs Python, curl, zip and the rest; safe to re-run
./build.sh --list-regions             # north-america, united-states, europe, california, ...
./build.sh --region north-america     # takes hours the first time
./verify-bundle.sh                    # checks the newest bundle in dist/
```

The result is `build/maps/dist/maps-<date>.zip`, and the same four files
unzipped in `build/maps/work/<region>/stage/`. Use either. Copying from
`stage/` saves unzipping 140 GB.

For a quick end-to-end try, `--region california` builds a much smaller
bundle (about 91 GB, nearly all of it the search index).

### Search index version

The search index in `photon_data.tar` must be readable by the Photon on the
head unit. Photon 1.3.0 (in this image) reads the indexes Photon's export
server publishes for the 1.x series, which is what `build.sh` downloads.
If a later bundle's `photon` service fails to start with an index or version
error in `journalctl -u photon`, the index is newer than the image's Photon:
update `meta-godwit/recipes-godwit/photon/` to a matching Photon release and
rebuild the image.

## Install or update a bundle

Copying 140 GB over Wi-Fi takes hours, so carry the bundle on a USB drive.
These steps use an exFAT drive; the image can read exFAT, ext4 and FAT.

### 1. Put the bundle on a USB drive (on the PC)

Make a folder named after the bundle's date and copy the four files into it.
`manifest.json` holds a SHA-256 for each file; check them before unplugging
the drive:

```sh
DATE=2026.07.13                       # the bundle's date
SRC=/path/to/bundle/files              # where the four files are
DST=/media/$USER/<drive>/godwit-maps/versions/$DATE

mkdir -p "$DST"
cp "$SRC"/manifest.json "$SRC"/tiles.pmtiles "$SRC"/photon_data.tar "$SRC"/valhalla_tiles.tar "$DST"/
python3 -c '
import hashlib, json, os, sys
d = sys.argv[1]
for name, a in json.load(open(os.path.join(d, "manifest.json")))["artifacts"].items():
    h = hashlib.sha256()
    with open(os.path.join(d, name), "rb") as f:
        for b in iter(lambda: f.read(1 << 24), b""):
            h.update(b)
    print(name, "OK" if h.hexdigest() == a["sha256"] else "MISMATCH")
' "$DST"
```

Every line must say `OK`. Eject the drive.

### 2. Install it (on the head unit)

Plug the drive into the head unit and log in, over SSH or on the serial
console, as root. Find the drive's partition with `cat /proc/partitions`
(usually `sda1`; the drive is then `sda`).

Copy the files onto the head unit first and check them, then unpack the
search index from there. Unpacking straight from the USB drive reads it
non-stop for a long time, which is when an unreliable drive or connection
fails part-way.

```sh
DATE=2026.07.13
V=/data/maps/versions/$DATE

modprobe exfat                                  # for an exFAT drive
echo 128 > /sys/block/sda/device/max_sectors    # smaller USB reads; some drives stall on large ones
mkdir -p /mnt/usb
mount -o ro /dev/sda1 /mnt/usb
S=/mnt/usb/godwit-maps/versions/$DATE

mkdir -p $V
cp $S/manifest.json $S/tiles.pmtiles $S/valhalla_tiles.tar $S/photon_data.tar $V/
umount /mnt/usb

# Each file against its SHA-256 in the manifest; all must say OK
for f in tiles.pmtiles valhalla_tiles.tar photon_data.tar; do
    want=$(grep -A2 "\"$f\"" $V/manifest.json | sed -n 's/.*"sha256": "\([0-9a-f]*\)".*/\1/p')
    [ "$(sha256sum $V/$f | cut -d' ' -f1)" = "$want" ] && echo "$f OK" || echo "$f MISMATCH"
done

tar xf $V/photon_data.tar -C $V
rm $V/photon_data.tar
chown -R photon:photon $V/photon_data
sync
```

If a file says `MISMATCH`, or `cp` stops with an `Input/output error`,
copy that file again (see also [Troubleshooting](#troubleshooting)). The
drive can be unplugged once `umount` has run.

Copying needs room for the search index twice over while it is unpacked,
about 230 GB in all for North America.

From a USB 3 SSD, expect about an hour for North America: around half an
hour to copy (about 60 MB/s), five minutes to check, and twenty-five to
unpack the search index.

### 3. Switch to it

```sh
ln -sfn /data/maps/versions/2026.07.13 /data/maps/current
systemctl restart photon godwit-hmi
```

The HMI checks for the map when it starts, so restart it after the first
install. Unplug the drive.

When updating, keep the old version until the new one works, then remove
it: `rm -r /data/maps/versions/<old date>`. Never unpack a new bundle over
an old one.

## Check it works

```sh
systemctl status photon                        # active (running)
journalctl -u photon -b | tail                 # "Listening on http://127.0.0.1:2322/"
wget -q -O - 'http://127.0.0.1:2322/api?q=denver&limit=1'
```

Photon takes about a minute after it starts to open the index. Until then
the search box says *Search is starting up*. The first searches take a
second or two while the index is read in; after that they take a few hundred
milliseconds. Photon uses about 2 GB of memory.

On the screen: open Navigation, tap **Where to?**, type a place, choose a
result, and the preview shows the drive time, distance and the route on the
map. **Start** begins turn-by-turn guidance.

If no route appears, the HMI logs Valhalla's reason:

```sh
journalctl -u godwit-hmi -b | grep godwit.route
```

The image leaves out Valhalla's command-line tools, which would bring
Python with them. To route from the command line on a development image, add
`valhalla-tools` to it and run
`valhalla_service /etc/valhalla/valhalla.json route '<request json>'`.

## Troubleshooting

| Symptom | Check |
|---|---|
| Map says *No map installed* | `ls -l /data/maps/current/tiles.pmtiles`, then `systemctl restart godwit-hmi` |
| Search stays on *Search is starting up* | `systemctl status photon`. It does not start without `/data/maps/current/photon_data`. A lock or permission error in `journalctl -u photon` means `photon_data` is not owned by `photon`. |
| Preview says *No route found* | The start or destination is outside the bundle's region, or `/data/maps/current/valhalla_tiles.tar` is missing. `journalctl -u godwit-hmi | grep godwit.route` shows Valhalla's reason. |
| Preview says *Routing is not installed* | `/etc/valhalla/valhalla.json` is missing (the `valhalla-conf` package). |
| Copying from the USB drive fails with `Input/output error`, and the touch screen and encoders drop out for a moment | The drive stalled under UAS and the kernel reset the USB bus. `dmesg | grep uas_eh_abort` confirms it. The image already turns UAS off for Samsung T7 drives (`usb_storage.quirks` in `meta-dragon-q6a/files/wic/dragon-q6a.wks.in`). For another drive, find its ID in `/sys/bus/usb/devices/*/idVendor` and `idProduct`, run `echo <vid>:<pid>:u > /sys/module/usb_storage/parameters/quirks`, replug it, delete the partly copied files and start that step again. Add the ID to the kernel command line there to keep it. |
