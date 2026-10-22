# Boot straight into the image: no systemd-boot menu delay.
#
# wic writes "timeout <n>" to loader.conf from the wks bootloader line, but
# treats 0 as unset and falls back to 5 seconds. Rewrite loader.conf in the
# ESP once the .wic exists, before it is compressed and block-mapped. A key
# press during boot still shows the menu.
#
# The ESP is the first partition, aligned to 1 MiB in dragon-q6a.wks.in.

DRAGON_Q6A_BOOT_TIMEOUT ?= "0"
DRAGON_Q6A_ESP_OFFSET = "1048576"

do_image_wic[depends] += "mtools-native:do_populate_sysroot"

# The .wic is compressed and block-mapped within do_image_wic, so the rewrite
# has to run at the end of the wic command itself
IMAGE_CMD:wic:append() {
	dragon_q6a_loader_timeout
}

dragon_q6a_loader_timeout() {
    img=$(ls -t ${IMGDEPLOYDIR}/*.wic 2>/dev/null | head -n 1)
    [ -n "$img" ] || bbfatal "dragon-q6a-boot: no .wic in ${IMGDEPLOYDIR}"
    conf="${WORKDIR}/loader.conf"
    printf 'timeout ${DRAGON_Q6A_BOOT_TIMEOUT}\n' > "$conf"
    mcopy -o -i "$img@@${DRAGON_Q6A_ESP_OFFSET}" "$conf" ::/loader/loader.conf
    mtype -i "$img@@${DRAGON_Q6A_ESP_OFFSET}" ::/loader/loader.conf | grep -qx "timeout ${DRAGON_Q6A_BOOT_TIMEOUT}" \
        || bbfatal "dragon-q6a-boot: loader.conf was not updated"
}
