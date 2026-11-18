SUMMARY = "Applies the CAN port settings chosen in the Godwit HMI"
DESCRIPTION = "Sets the bit rates, CAN FD and listen-only mode of the first two \
physical CAN interfaces from the HMI's Settings > CAN, at boot, when an \
interface appears, and whenever the settings change."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://godwit-can-apply \
           file://godwit-can.service \
           file://godwit-can.path \
           file://75-godwit-can.rules \
           file://50-godwit-can.network"
S = "${UNPACKDIR}"

inherit systemd allarch

SYSTEMD_SERVICE:${PN} = "godwit-can.path"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -Dm 0755 ${UNPACKDIR}/godwit-can-apply ${D}${sbindir}/godwit-can-apply
    install -Dm 0644 ${UNPACKDIR}/godwit-can.service ${D}${systemd_system_unitdir}/godwit-can.service
    install -Dm 0644 ${UNPACKDIR}/godwit-can.path ${D}${systemd_system_unitdir}/godwit-can.path
    install -Dm 0644 ${UNPACKDIR}/75-godwit-can.rules ${D}${nonarch_base_libdir}/udev/rules.d/75-godwit-can.rules
    install -Dm 0644 ${UNPACKDIR}/50-godwit-can.network ${D}${nonarch_base_libdir}/systemd/network/50-godwit-can.network
}

FILES:${PN} += " \
    ${systemd_system_unitdir}/godwit-can.service \
    ${nonarch_base_libdir}/udev/rules.d \
    ${nonarch_base_libdir}/systemd/network \
"

# The full ip, which can set CAN bit timing (BusyBox's cannot)
RDEPENDS:${PN} = "iproute2-ip"
