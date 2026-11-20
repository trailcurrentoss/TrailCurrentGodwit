SUMMARY = "Sets the hostname to the Device Name chosen in the Godwit HMI"
DESCRIPTION = "Turns the Device Name from the HMI's Settings into the network \
hostname, so routers list the head unit by that name, and sets it again at \
every boot before ConnMan asks for an address."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://godwit-hostname-apply \
           file://godwit-hostname.service \
           file://godwit-hostname.path \
           file://godwit-hostname-boot.service"
S = "${UNPACKDIR}"

inherit systemd allarch

SYSTEMD_SERVICE:${PN} = "godwit-hostname.path godwit-hostname-boot.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -Dm 0755 ${UNPACKDIR}/godwit-hostname-apply ${D}${sbindir}/godwit-hostname-apply
    install -Dm 0644 ${UNPACKDIR}/godwit-hostname.service ${D}${systemd_system_unitdir}/godwit-hostname.service
    install -Dm 0644 ${UNPACKDIR}/godwit-hostname.path ${D}${systemd_system_unitdir}/godwit-hostname.path
    install -Dm 0644 ${UNPACKDIR}/godwit-hostname-boot.service ${D}${systemd_system_unitdir}/godwit-hostname-boot.service
}

FILES:${PN} += "${systemd_system_unitdir}/godwit-hostname.service"

# connmanctl, to reconnect so the router hears the new name
RDEPENDS:${PN} = "connman-client"
