SUMMARY = "Valhalla routing engine, as a library"
DESCRIPTION = "Offline routing and turn-by-turn maneuvers for the HMI, read \
from the installed map bundle's valhalla_tiles.tar. Built as a library the \
HMI links, so no routing server runs and nothing reaches the network. The \
command-line tools are kept for checking routes on the head unit."
HOMEPAGE = "https://github.com/valhalla/valhalla"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://COPYING;md5=a6502a7ddd34cba9a24a5a8ef3264e51"

SRC_URI = "gitsm://github.com/valhalla/valhalla.git;protocol=https;branch=master \
           file://protoc-native.cmake"
SRCREV = "bafb69902220615a48307d5c790fb6c943802ba4"

DEPENDS = "boost protobuf protobuf-native zlib lz4"

inherit cmake pkgconfig python3native

# Only what routing needs: no HTTP server (prime_server), no tile building,
# no HTTP client, no bindings, no tests
EXTRA_OECMAKE += " \
    -DENABLE_TOOLS=ON \
    -DENABLE_DATA_TOOLS=OFF \
    -DENABLE_SERVICES=OFF \
    -DENABLE_HTTP=OFF \
    -DENABLE_PYTHON_BINDINGS=OFF \
    -DENABLE_NODE_BINDINGS=OFF \
    -DENABLE_TESTS=OFF \
    -DENABLE_CCACHE=OFF \
    -DENABLE_GEOTIFF=OFF \
    -DENABLE_SINGLE_FILES_WERROR=OFF \
    -DENABLE_LZ4=ON \
    -DProtobuf_PROTOC_EXECUTABLE=${STAGING_BINDIR_NATIVE}/protoc \
    -DCMAKE_PROJECT_INCLUDE=${UNPACKDIR}/protoc-native.cmake \
"

# The routing config: tiles from the installed map bundle's tar extract,
# which the kernel pages in on demand. No tile URL, so tiles are never fetched.
do_install:append() {
    install -d ${D}${sysconfdir}/valhalla
    python3 ${S}/scripts/valhalla_build_config \
        --mjolnir-tile-extract /data/maps/current/valhalla_tiles.tar \
        --mjolnir-tile-dir /data/maps/current/valhalla_tiles \
        --mjolnir-traffic-extract "" --mjolnir-admin "" --mjolnir-timezone "" \
        --mjolnir-max-cache-size 268435456 \
        > ${D}${sysconfdir}/valhalla/valhalla.json
}

# libvalhalla is static and linked into the HMI
PACKAGES =+ "${PN}-tools ${PN}-conf"
FILES:${PN}-tools = "${bindir}"
FILES:${PN}-conf = "${sysconfdir}/valhalla"
