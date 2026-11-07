SUMMARY = "Godwit's partitions, read-only system and factory reset"
DESCRIPTION = "Sets up the drive before anything else runs: makes the data and \
content partitions on first boot, keeps personal data on the data partition, \
and erases that partition for a factory reset."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://godwit-preinit \
           file://godwit-deploy \
           file://godwit-factory-reset.path \
           file://godwit-factory-reset.service"
S = "${UNPACKDIR}"

inherit systemd allarch

SYSTEMD_SERVICE:${PN} = "godwit-factory-reset.path"
SYSTEMD_AUTO_ENABLE = "enable"

do_install() {
    install -Dm 0755 ${UNPACKDIR}/godwit-preinit ${D}${sbindir}/godwit-preinit
    install -Dm 0755 ${UNPACKDIR}/godwit-deploy ${D}${sbindir}/godwit-deploy
    install -Dm 0644 ${UNPACKDIR}/godwit-factory-reset.path ${D}${systemd_system_unitdir}/godwit-factory-reset.path
    install -Dm 0644 ${UNPACKDIR}/godwit-factory-reset.service ${D}${systemd_system_unitdir}/godwit-factory-reset.service
}

FILES:${PN} += "${systemd_system_unitdir}/godwit-factory-reset.service"

# Where the kernel has it as a module. Without it nothing personal stays on the data
# partition, so nothing is covered by the factory reset.
RRECOMMENDS:${PN} = "kernel-module-overlay"

RDEPENDS:${PN} = " \
    coreutils \
    util-linux-mount \
    util-linux-umount \
    util-linux-sfdisk \
    util-linux-partx \
    util-linux-blkdiscard \
    util-linux-fstrim \
    e2fsprogs-mke2fs \
    e2fsprogs-e2fsck \
    e2fsprogs-resize2fs \
"
