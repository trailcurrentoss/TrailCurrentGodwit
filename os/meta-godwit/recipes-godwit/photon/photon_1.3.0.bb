SUMMARY = "Photon place search, offline"
DESCRIPTION = "Photon geocoder serving destination search to the HMI from the \
installed map bundle's photon_data index. Listens on 127.0.0.1 only; it never \
reaches the network."
HOMEPAGE = "https://github.com/komoot/photon"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/Apache-2.0;md5=89aea4e17d99a7cacdbeed46a0096b10"

# 1.3.0 embeds OpenSearch 3.8, which opens the bundle's index (written by
# OpenSearch 3.5 on Photon's export server)
SRC_URI = "https://github.com/komoot/photon/releases/download/${PV}/photon-${PV}.jar;unpack=0 \
           file://photon.service"
SRC_URI[sha256sum] = "a89707c0045e4807b2a1180e132e68e108d998709f48b6c94b98a6e281f571a5"
S = "${UNPACKDIR}"

inherit systemd useradd allarch

USERADD_PACKAGES = "${PN}"
# OpenSearch will not run as root. The bundle's photon_data must be owned by
# this user: OpenSearch writes its lock and state files beside the index.
USERADD_PARAM:${PN} = "--system --no-create-home --home-dir /nonexistent --shell /sbin/nologin photon"

SYSTEMD_SERVICE:${PN} = "photon.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -Dm 0644 ${UNPACKDIR}/photon-${PV}.jar ${D}${datadir}/photon/photon.jar
    install -Dm 0644 ${UNPACKDIR}/photon.service ${D}${systemd_system_unitdir}/photon.service
}

FILES:${PN} += "${datadir}/photon"

RDEPENDS:${PN} = "temurin-jre-bin"
