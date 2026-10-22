SUMMARY = "Grow the root filesystem to fill its disk on first boot"
DESCRIPTION = "The image's root partition is only as large as the root \
filesystem. On first boot this grows the partition to the end of the disk and \
the ext4 filesystem with it, online, so the map bundle fits."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://godwit-growroot \
           file://godwit-growroot.service"
S = "${UNPACKDIR}"

inherit systemd allarch

SYSTEMD_SERVICE:${PN} = "godwit-growroot.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -Dm 0755 ${UNPACKDIR}/godwit-growroot ${D}${sbindir}/godwit-growroot
    install -Dm 0644 ${UNPACKDIR}/godwit-growroot.service ${D}${systemd_system_unitdir}/godwit-growroot.service
}

RDEPENDS:${PN} = " \
    util-linux-findmnt \
    util-linux-lsblk \
    util-linux-sfdisk \
    util-linux-partx \
    e2fsprogs-resize2fs \
"
