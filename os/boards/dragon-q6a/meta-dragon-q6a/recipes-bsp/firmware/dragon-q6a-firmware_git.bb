SUMMARY = "Dragon Q6A firmware not carried by linux-firmware"
DESCRIPTION = "The board-signed Adreno zap shader (qcom/qcs6490/a660_zap.mbn) \
that the Q6A device tree requests. Without it the GPU stays in secure mode and \
Mesa falls back to software rendering."
HOMEPAGE = "https://github.com/radxa-pkg/radxa-firmware"
LICENSE = "CLOSED"

SRC_URI = "git://github.com/radxa-pkg/radxa-firmware.git;protocol=https;branch=main"
SRCREV = "330b4bf933b927f9a6f3383dd57d28f6c652810c"

inherit allarch

do_compile[noexec] = "1"

do_install() {
    install -Dm 0644 ${S}/radxa-firmware-qcs6490/lib/firmware/qcom/qcs6490/a660_zap.mbn \
        ${D}${nonarch_base_libdir}/firmware/qcom/qcs6490/a660_zap.mbn
}

FILES:${PN} = "${nonarch_base_libdir}/firmware"
INSANE_SKIP:${PN} += "arch"
