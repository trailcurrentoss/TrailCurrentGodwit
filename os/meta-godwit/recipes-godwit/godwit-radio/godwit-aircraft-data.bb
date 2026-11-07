SUMMARY = "What aircraft are and where they are going, for the Aircraft app"
DESCRIPTION = "Aircraft types by ICAO address and routes by callsign, from \
Virtual Radar Server's Standing Data, packed into tables the HMI looks up on \
the head unit with no internet."
HOMEPAGE = "https://github.com/vradarserver/standing-data"
LICENSE = "CC0-1.0"
LIC_FILES_CHKSUM = "file://LICENSE;md5=65d3616852dbf7b1a6d4b53b00626032"

# The packer lives in this repository, beside os/
FILESEXTRAPATHS:prepend := "${GODWIT_TOPDIR}:"
SRC_URI = "git://github.com/vradarserver/standing-data.git;protocol=https;branch=main \
           file://radio/tools/pack_aircraft_data.py"
# A snapshot of the data; it is updated often, so move this to refresh it
SRCREV = "ccf0531112ac5e16ddbe5f194901718d316bda18"
PV = "20261103"

inherit allarch python3native

do_configure[noexec] = "1"

do_compile() {
    ${PYTHON} ${UNPACKDIR}/radio/tools/pack_aircraft_data.py ${S} ${B}/tables
}

do_install() {
    install -d ${D}${datadir}/godwit/aircraft
    install -m 0644 ${B}/tables/aircraft.tsv ${B}/tables/routes.tsv ${D}${datadir}/godwit/aircraft/
}

FILES:${PN} = "${datadir}/godwit/aircraft"
