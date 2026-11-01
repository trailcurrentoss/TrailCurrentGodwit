SUMMARY = "Godwit's language model service on the Hexagon NPU"
DESCRIPTION = "Serves Llama 3.2 1B on the Dragon Q6A's NPU through Qualcomm \
Genie on 127.0.0.1:11434, for the HMI's voice assistant to understand \
spoken requests. Runs entirely on the head unit."
HOMEPAGE = "https://github.com/trailcurrentoss"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${UNPACKDIR}/LICENSE;md5=0c3de729fcec549a24b64cc2c1a62d48"

# The service lives in this repository, beside os/
FILESEXTRAPATHS:prepend := "${THISDIR}/files:${GODWIT_TOPDIR}:"
SRC_URI = "file://llm \
           file://LICENSE \
           file://godwit-llm.service"
S = "${UNPACKDIR}/llm"

COMPATIBLE_MACHINE = "dragon-q6a"

inherit systemd useradd allarch

USERADD_PACKAGES = "${PN}"
GROUPADD_PARAM:${PN} = "-f -r fastrpc"
USERADD_PARAM:${PN} = "--system --no-create-home --home-dir /nonexistent --shell /sbin/nologin \
                       --user-group --groups fastrpc godwit-llm"

SYSTEMD_SERVICE:${PN} = "godwit-llm.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -Dm 0755 ${S}/godwit_llm.py ${D}${libexecdir}/godwit-llm/godwit_llm.py
    # Godwit's Genie settings beside the model, which they name by relative path
    install -Dm 0644 ${S}/godwit-genie-config.json ${D}${datadir}/godwit/llm/godwit-genie-config.json
    install -Dm 0644 ${S}/htp_backend_ext_config.json ${D}${datadir}/godwit/llm/htp_backend_ext_config.json
    install -Dm 0644 ${UNPACKDIR}/godwit-llm.service ${D}${systemd_system_unitdir}/godwit-llm.service
}

FILES:${PN} = "${libexecdir}/godwit-llm ${datadir}/godwit/llm ${systemd_system_unitdir}"

# http.server pulls in html, email and mimetypes
RDEPENDS:${PN} = " \
    llama32-1b-genie \
    python3-core \
    python3-ctypes \
    python3-json \
    python3-netserver \
    python3-html \
    python3-email \
    python3-netclient \
"
