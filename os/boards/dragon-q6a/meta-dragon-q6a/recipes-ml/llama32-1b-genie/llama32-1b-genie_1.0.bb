SUMMARY = "Llama 3.2 1B for the QCS6490's Hexagon NPU, with Qualcomm Genie"
DESCRIPTION = "Radxa's build of Llama 3.2 1B Instruct for the Hexagon v68 NPU \
(1024-token context), with the Qualcomm AI Runtime libraries it was compiled \
against: Genie, which runs the model, and QNN's HTP backend, whose skel \
library runs on the DSP. The same bundle TrailCurrent Peregrine uses. \
godwit-llm adds the Genie config and serves it."
HOMEPAGE = "https://www.modelscope.cn/models/radxa/Llama3.2-1B-1024-qairt-v68"
# The weights are under the Llama 3.2 Community License (Meta); Genie and the
# QNN libraries under Qualcomm's AI stack license. Neither is a license
# Yocto knows, and neither allows the image to be distributed without them.
LICENSE = "CLOSED"

# One revision of the model repository, so the libraries and the compiled
# model always match
MODEL_REV = "f236ee8ebc1e2cd79a8fb0622ac91bfddf664395"
MODEL_URL = "https://www.modelscope.cn/models/radxa/Llama3.2-1B-1024-qairt-v68/resolve/${MODEL_REV}"
SRC_URI = " \
    ${MODEL_URL}/libGenie.so;name=genie;downloadfilename=llama32-1b-v68-libGenie.so;unpack=0;subdir=bundle \
    ${MODEL_URL}/libQnnHtp.so;name=htp;downloadfilename=llama32-1b-v68-libQnnHtp.so;unpack=0;subdir=bundle \
    ${MODEL_URL}/libQnnHtpNetRunExtensions.so;name=netrun;downloadfilename=llama32-1b-v68-libQnnHtpNetRunExtensions.so;unpack=0;subdir=bundle \
    ${MODEL_URL}/libQnnHtpV68Skel.so;name=skel;downloadfilename=llama32-1b-v68-libQnnHtpV68Skel.so;unpack=0;subdir=bundle \
    ${MODEL_URL}/libQnnHtpV68Stub.so;name=stub;downloadfilename=llama32-1b-v68-libQnnHtpV68Stub.so;unpack=0;subdir=bundle \
    ${MODEL_URL}/libQnnSystem.so;name=system;downloadfilename=llama32-1b-v68-libQnnSystem.so;unpack=0;subdir=bundle \
    ${MODEL_URL}/genie-t2t-run;name=cli;downloadfilename=llama32-1b-v68-genie-t2t-run;unpack=0;subdir=bundle \
    ${MODEL_URL}/tokenizer.json;name=tokenizer;downloadfilename=llama32-1b-v68-tokenizer.json;unpack=0;subdir=bundle \
    ${MODEL_URL}/models/weight_sharing_model_1_of_1.serialized.bin;name=model;downloadfilename=llama32-1b-v68-weight_sharing_model_1_of_1.serialized.bin;unpack=0;subdir=bundle \
"
SRC_URI[genie.sha256sum] = "468972fb7949c1d8d71fe5b26a684c0d1745f6f45d93168a56c03702fe02ad1a"
SRC_URI[htp.sha256sum] = "1542866d5210c5190bfb7c2d9e92f5fe8217d0e66daba59309ae831d53b3b3a2"
SRC_URI[netrun.sha256sum] = "c1759ef969d9a021494e29eabbbb01b707961617897d683b442c357cf448606d"
SRC_URI[skel.sha256sum] = "4760f9173629b9197ef557cbda0e07961c55452bd6376bfa694179adb8214987"
SRC_URI[stub.sha256sum] = "49496a7d82ff734e4d305abb0ef127a23e4798bcaed5c02c95543b17b148c808"
SRC_URI[system.sha256sum] = "5e47bfbc4a89acc58cde324fb6b041b2bc741d21e378b4fe39ccc2eb21e82e77"
SRC_URI[cli.sha256sum] = "db6ba32ae2040cf25ca10c8b2fff5a79cad00e08a6382628e9ae4f2ee8bbac21"
SRC_URI[tokenizer.sha256sum] = "79e3e522635f3171300913bb421464a87de6222182a0570b9b2ccba2a964b2b4"
SRC_URI[model.sha256sum] = "dfe35ce9624c4231779ae52cf2d66a0154a941fb477f8df1eadf1d9ea675eb9a"

COMPATIBLE_MACHINE = "dragon-q6a"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

LLM_DIR = "${datadir}/godwit/llm"

do_install() {
    src="${UNPACKDIR}/bundle"
    install -d ${D}${LLM_DIR}/models
    for lib in libGenie.so libQnnHtp.so libQnnHtpNetRunExtensions.so libQnnHtpV68Stub.so libQnnSystem.so; do
        install -m 0755 $src/llama32-1b-v68-$lib ${D}${LLM_DIR}/$lib
    done
    # Loaded by the DSP, found through ADSP_LIBRARY_PATH
    install -m 0644 $src/llama32-1b-v68-libQnnHtpV68Skel.so ${D}${LLM_DIR}/libQnnHtpV68Skel.so
    # Genie's own command line, for testing the NPU by hand
    install -m 0755 $src/llama32-1b-v68-genie-t2t-run ${D}${LLM_DIR}/genie-t2t-run
    install -m 0644 $src/llama32-1b-v68-tokenizer.json ${D}${LLM_DIR}/tokenizer.json
    install -m 0644 $src/llama32-1b-v68-weight_sharing_model_1_of_1.serialized.bin \
        ${D}${LLM_DIR}/models/weight_sharing_model_1_of_1.serialized.bin
}

FILES:${PN} = "${LLM_DIR}"
# Named here rather than read from the prebuilt libraries: they ask for
# libcdsprpc.so by its unversioned name, and the skel's libc++ is the DSP's
RDEPENDS:${PN} = "fastrpc dragon-q6a-firmware-cdsp"
SKIP_FILEDEPS:${PN} = "1"

# Prebuilt and stripped; the libraries are private to the bundle and found
# through LD_LIBRARY_PATH, and the skel is DSP code
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
EXCLUDE_FROM_SHLIBS = "1"
INSANE_SKIP:${PN} = "already-stripped ldflags dev-so libdir arch file-rdeps"
