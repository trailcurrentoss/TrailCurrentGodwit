SUMMARY = "AIC8800 USB Wi-Fi and Bluetooth driver and firmware"
DESCRIPTION = "Out-of-tree driver for the Dragon Q6A's onboard AIC8800 Wi-Fi/Bluetooth \
module, which sits on USB. Built the way Radxa's aic8800-usb-dkms package builds \
it: Radxa's patch series (including the fixes for newer kernels) applied to the \
vendor source, then aic_load_fw (loads firmware onto the chip), aic8800_fdrv \
(Wi-Fi) and aic_btusb (Bluetooth over BlueZ)."
HOMEPAGE = "https://github.com/radxa-pkg/aic8800"
# Driver source GPL-2.0; Radxa's packaging and patches GPL-3.0-or-later
LICENSE = "GPL-2.0-only & GPL-3.0-or-later"
LIC_FILES_CHKSUM = "file://LICENSE;md5=1ebbd3e34237af26da5dc08a4e440464 \
                    file://debian/copyright;md5=ccd22839cbff32b2bbe9a6ec05d11739"

SRC_URI = "git://github.com/radxa-pkg/aic8800.git;protocol=https;branch=main"
SRCREV = "fce88024ed40279def034e203128e0892fdbcac3"
PV = "5.0+git"

inherit module

# Radxa keeps its fixes as a quilt series in debian/patches. Some vendor
# sources have CRLF line endings and some patches do not, so normalise both
# to LF before applying; it makes no difference to the build.
do_patch:append() {
    bb.build.exec_func('aic8800_apply_series', d)
}
aic8800_apply_series() {
    cd ${S}
    find src -type f \( -name '*.c' -o -name '*.h' -o -name Makefile -o -name Kconfig -o -name '*.mk' \) \
        -exec sed -i 's/\r$//' {} +
    while read -r p; do
        [ -n "$p" ] || continue
        sed 's/\r$//' debian/patches/$p | patch -p1 --forward --no-backup-if-mismatch
    done < debian/patches/series
}

AIC_USB = "${S}/src/USB/driver_fw/drivers"

do_compile() {
    unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS
    for dir in aic8800 aic_btusb; do
        # Same toolchain arguments as module.bbclass, so build paths are remapped
        oe_runmake -C ${STAGING_KERNEL_DIR} M=${AIC_USB}/$dir \
            KERNEL_SRC=${STAGING_KERNEL_DIR} KDIR=${STAGING_KERNEL_DIR} \
            O=${STAGING_KERNEL_BUILDDIR} \
            CC="${KERNEL_CC}" LD="${KERNEL_LD}" AR="${KERNEL_AR}" \
            OBJCOPY="${KERNEL_OBJCOPY}" STRIP="${KERNEL_STRIP}" \
            ARCH=${ARCH} CROSS_COMPILE=${TARGET_PREFIX} modules
    done
}

do_install() {
    moddir=${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/updates
    install -d $moddir
    install -m 0644 ${AIC_USB}/aic8800/aic_load_fw/aic_load_fw.ko $moddir/
    install -m 0644 ${AIC_USB}/aic8800/aic8800_fdrv/aic8800_fdrv.ko $moddir/
    install -m 0644 ${AIC_USB}/aic_btusb/aic_btusb.ko $moddir/

    fwdir=${D}${nonarch_base_libdir}/firmware/aic8800_fw/USB
    install -d $fwdir
    cp -r ${S}/src/USB/driver_fw/fw/* $fwdir/
}

# Firmware rides with the modules: one package to install
FILES:${PN} += "${nonarch_base_libdir}/firmware/aic8800_fw"
INSANE_SKIP:${PN} += "arch"
