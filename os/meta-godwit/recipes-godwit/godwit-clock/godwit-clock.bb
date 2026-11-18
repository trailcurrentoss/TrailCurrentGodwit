SUMMARY = "Sets the time zone and the clock for the Godwit HMI"
DESCRIPTION = "Applies the time zone chosen in the HMI's Settings and moves \
the clock (set by hand, or from the GNSS receiver) when systemd-timedated \
refuses the HMI because polkit is not installed."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://godwit-clock-apply \
           file://godwit-clock.service \
           file://godwit-clock.path"
S = "${UNPACKDIR}"

inherit systemd allarch

SYSTEMD_SERVICE:${PN} = "godwit-clock.path"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -Dm 0755 ${UNPACKDIR}/godwit-clock-apply ${D}${sbindir}/godwit-clock-apply
    install -Dm 0644 ${UNPACKDIR}/godwit-clock.service ${D}${systemd_system_unitdir}/godwit-clock.service
    install -Dm 0644 ${UNPACKDIR}/godwit-clock.path ${D}${systemd_system_unitdir}/godwit-clock.path
}

FILES:${PN} += "${systemd_system_unitdir}/godwit-clock.service"

# timedatectl and busctl
RDEPENDS:${PN} = "systemd"
