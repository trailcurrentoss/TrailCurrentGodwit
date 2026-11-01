SUMMARY = "Dragon Q6A firmware not carried by linux-firmware"
DESCRIPTION = "The board-signed Adreno zap shader (qcom/qcs6490/a660_zap.mbn) \
that the Q6A device tree requests. Without it the GPU stays in secure mode and \
Mesa falls back to software rendering. The -cdsp package has the compute \
DSP's side of FastRPC (fastrpc_shell_3 and its libraries), which the NPU \
language model runs through, and tells fastrpc where they are."
HOMEPAGE = "https://github.com/radxa-pkg/radxa-firmware"
LICENSE = "CLOSED"

SRC_URI = "git://github.com/radxa-pkg/radxa-firmware.git;protocol=https;branch=main \
           file://radxa-dragon-q6a.yaml"
SRCREV = "330b4bf933b927f9a6f3383dd57d28f6c652810c"

inherit allarch

do_compile[noexec] = "1"

DSP_DIR = "${datadir}/qcom/qcs6490/radxa/dragon-q6a/dsp/cdsp"

do_install() {
    install -Dm 0644 ${S}/radxa-firmware-qcs6490/lib/firmware/qcom/qcs6490/a660_zap.mbn \
        ${D}${nonarch_base_libdir}/firmware/qcom/qcs6490/a660_zap.mbn
    # The compute DSP's libraries; the DSP firmware itself (cdsp.mbn) comes
    # from linux-firmware-qcom-qcs6490-radxa-dragon-q6a-compute
    install -d ${D}${DSP_DIR}
    install -m 0644 ${S}/radxa-firmware-qcs6490/usr/share/qcom/qcs6490/radxa/dragon-q6a/dsp/cdsp/* ${D}${DSP_DIR}/
    # fastrpc finds them through the board's device tree model
    install -Dm 0644 ${UNPACKDIR}/radxa-dragon-q6a.yaml ${D}${datadir}/qcom/conf.d/radxa-dragon-q6a.yaml
}

PACKAGES =+ "${PN}-cdsp"
FILES:${PN} = "${nonarch_base_libdir}/firmware"
FILES:${PN}-cdsp = "${datadir}/qcom"
INSANE_SKIP:${PN} += "arch"
# Hexagon DSP code, loaded by the DSP rather than the CPU
INSANE_SKIP:${PN}-cdsp += "arch ldflags file-rdeps libdir"
SKIP_FILEDEPS:${PN}-cdsp = "1"
