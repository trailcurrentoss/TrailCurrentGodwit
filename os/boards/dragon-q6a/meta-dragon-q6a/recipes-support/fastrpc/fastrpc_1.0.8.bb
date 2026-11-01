SUMMARY = "Qualcomm FastRPC library and daemons"
DESCRIPTION = "The application side of FastRPC: libcdsprpc and the DSP \
daemons, through which a program on the CPU runs code on the Hexagon DSPs. \
The NPU language model (Qualcomm Genie, godwit-llm) uses it to reach the \
compute DSP. Adapted from Qualcomm's meta-qcom (recipes-support/fastrpc)."
HOMEPAGE = "https://github.com/qualcomm/fastrpc"
SECTION = "devel"

LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE.txt;md5=b67986b6880754696d418dbaa2cf51d1"
# libbsd for strlcpy, libyaml for the per-board config in /usr/share/qcom/conf.d
DEPENDS = "libbsd libyaml"

SRCREV = "d247519650fe5cb16de6c78edaa95bcc4be25073"
SRC_URI = "git://github.com/qualcomm/fastrpc.git;branch=main;protocol=https;tag=v${PV}"

inherit autotools systemd pkgconfig useradd

EXTRA_OECONF += "\
    --with-systemdsystemunitdir=${systemd_system_unitdir} \
    --with-udevrulesdir=${nonarch_base_libdir}/udev/rules.d \
    --with-sysusersdir=${nonarch_libdir}/sysusers.d \
"

# Only the compute DSP's daemon, for the NPU. The audio DSP is driven by the
# kernel's audio drivers, so its daemons (adsprpcd*) stay off; their units
# are installed but not enabled.
SYSTEMD_SERVICE:${PN} = "cdsprpcd.service"

do_install:append() {
    install -d ${D}${datadir}/qcom/
}

FILES:${PN} += " \
    ${libdir}/libadsp_default_listener.so \
    ${libdir}/libcdsp_default_listener.so \
    ${libdir}/libsdsp_default_listener.so \
    ${libdir}/libadsprpc.so \
    ${libdir}/libcdsprpc.so \
    ${libdir}/libsdsprpc.so \
    ${datadir}/qcom/ \
    ${systemd_system_unitdir} \
    ${nonarch_base_libdir}/udev/rules.d/60-fastrpc.rules \
    ${nonarch_libdir}/sysusers.d/fastrpc.conf \
"
FILES:${PN}-dev:remove = "${FILES_SOLIBSDEV}"
# Programs built against the Hexagon SDK open the unversioned libraries
INSANE_SKIP:${PN} = "dev-so"

PACKAGE_BEFORE_PN += "${PN}-tests"
FILES:${PN}-tests += " \
    ${bindir}/fastrpc-healthcheck \
    ${bindir}/fastrpc_test \
    ${libdir}/fastrpc_test/*.so \
    ${datadir}/fastrpc_test \
"
# The tests include prebuilt DSP libraries
INSANE_SKIP:${PN}-tests += "arch libdir ldflags"

# Programs that use FastRPC belong to the fastrpc group, which owns the DSP
# device nodes (60-fastrpc.rules); its DMA heap rule runs setfacl
USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "--system fastrpc"
RDEPENDS:${PN} += "acl"

COMPATIBLE_HOST = "aarch64.*-linux"
