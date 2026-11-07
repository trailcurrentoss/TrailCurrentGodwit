SUMMARY = "Godwit's FM tuner and its aircraft, ship and weather radio receivers"
DESCRIPTION = "Stereo FM and RDS for the HMI's Radio app, ADS-B for its Aircraft \
app, AIS for its Ships app and NOAA Weather Radio for its Weather Radio app, from \
an RTL-SDR USB dongle, with a table of the weather transmitters. Runs entirely on \
the head unit."
HOMEPAGE = "https://github.com/trailcurrentoss"
# The code in this repository is MIT. The programs link librtlsdr, which is
# GPL-2.0-or-later, so the binaries are distributed on those terms. They run as
# their own processes, so the HMI is not affected. The table of weather transmitters
# is public domain, from the US National Weather Service.
LICENSE = "MIT & GPL-2.0-or-later & PD"
LIC_FILES_CHKSUM = "file://${UNPACKDIR}/LICENSE;md5=0c3de729fcec549a24b64cc2c1a62d48 \
                    file://${COMMON_LICENSE_DIR}/GPL-2.0-or-later;md5=fed54355545ffd980b814dab4a3b312c"

# The code lives in this repository, beside os/
FILESEXTRAPATHS:prepend := "${GODWIT_TOPDIR}:"
SRC_URI = "file://radio \
           file://LICENSE \
           file://72-godwit-sdr.rules \
           file://godwit-sdr.conf"
S = "${UNPACKDIR}/radio"

DEPENDS = "rtl-sdr"

inherit cmake

do_install:append() {
    # The dongle, for the HMI's user
    install -Dm 0644 ${UNPACKDIR}/72-godwit-sdr.rules ${D}${sysconfdir}/udev/rules.d/72-godwit-sdr.rules
    # Keeps the kernel's DVB-T driver off it
    install -Dm 0644 ${UNPACKDIR}/godwit-sdr.conf ${D}${sysconfdir}/modprobe.d/godwit-sdr.conf
}

FILES:${PN} += "${datadir}/godwit/weather"
