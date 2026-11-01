SUMMARY = "Finds audio and video streams on YouTube and other sites"
DESCRIPTION = "Used by godwit-ytmusic to find each song's audio stream. \
YouTube's player needs a JavaScript runtime for this (Node.js here) and the \
solver scripts in python3-yt-dlp-ejs."
HOMEPAGE = "https://github.com/yt-dlp/yt-dlp"
LICENSE = "Unlicense"
LIC_FILES_CHKSUM = "file://LICENSE;md5=7246f848faa4e9c9fc0ea91122d6e680"

PYPI_PACKAGE = "yt_dlp"
SRC_URI[sha256sum] = "abe5c6b94c3b2f0560500add1c42e0a97641752743a5b65588ac2007a6e23eb6"

inherit pypi python_hatchling

# Shell completions: nobody types yt-dlp commands on the head unit
do_install:append() {
    rm -rf ${D}${datadir}/zsh ${D}${datadir}/bash-completion ${D}${datadir}/fish
}

# The standard library modules it imports, by OE's Python package split
RDEPENDS:${PN} = " \
    python3-core \
    python3-asyncio \
    python3-compression \
    python3-crypt \
    python3-ctypes \
    python3-datetime \
    python3-email \
    python3-html \
    python3-io \
    python3-json \
    python3-logging \
    python3-math \
    python3-mime \
    python3-netclient \
    python3-numbers \
    python3-shell \
    python3-sqlite3 \
    python3-stringold \
    python3-terminal \
    python3-unixadmin \
    python3-xml \
    python3-yt-dlp-ejs \
    nodejs \
"
