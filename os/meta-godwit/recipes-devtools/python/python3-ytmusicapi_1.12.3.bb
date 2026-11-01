SUMMARY = "Unofficial API for YouTube Music"
DESCRIPTION = "Searches YouTube Music and lists songs, albums and playlists, \
with or without a signed-in account. Used by godwit-ytmusic."
HOMEPAGE = "https://github.com/sigma67/ytmusicapi"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=95addf2156011f40dfac1266911bb6f2"

SRC_URI[sha256sum] = "be81511af7494eec8e5970c56a10e51aada6253246649e4ea6714fd6aeb654ee"

inherit pypi python_setuptools_build_meta

DEPENDS += "python3-setuptools-scm-native"

# The standard library modules it imports, by OE's Python package split
RDEPENDS:${PN} = " \
    python3-requests \
    python3-core \
    python3-crypt \
    python3-datetime \
    python3-json \
    python3-math \
    python3-netclient \
    python3-stringold \
"
