SUMMARY = "YouTube Music for the Godwit HMI"
DESCRIPTION = "Searches YouTube Music and finds each song's audio stream, \
for the HMI's YouTube Music source, which runs it as a child process. Needs \
an internet connection; it is an optional extra, never part of the head \
unit's core functions."
HOMEPAGE = "https://github.com/trailcurrentoss"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${UNPACKDIR}/LICENSE;md5=0c3de729fcec549a24b64cc2c1a62d48"

# The helper lives in this repository, beside os/
FILESEXTRAPATHS:prepend := "${GODWIT_TOPDIR}:"
SRC_URI = "file://ytmusic \
           file://LICENSE"
S = "${UNPACKDIR}/ytmusic"

inherit allarch

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -Dm 0755 ${S}/godwit_ytmusic.py ${D}${bindir}/godwit-ytmusic
}

# The standard library modules it imports, by OE's Python package split
# (concurrent.futures is in python3-asyncio)
RDEPENDS:${PN} = " \
    python3-core \
    python3-json \
    python3-asyncio \
    python3-ytmusicapi \
    python3-yt-dlp \
"
