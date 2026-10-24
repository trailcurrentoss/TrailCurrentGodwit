FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:dragon-q6a = " file://0001-ucm2-codecs-wcd938x-Use-CLS_H_LOHIFI.patch"

# With the board's SMBIOS data the card's long name is
# RadxaComputerCo.Ltd.-RadxaDragonQ6A-1.0; upstream maps it in d92d8fe9afcd
do_install:append:dragon-q6a() {
    ln -sf QCS6490-Radxa-Dragon-Q6A.conf \
        ${D}${datadir}/alsa/ucm2/conf.d/qcs6490/RadxaComputerCo.Ltd.-RadxaDragonQ6A-1.0.conf
}
