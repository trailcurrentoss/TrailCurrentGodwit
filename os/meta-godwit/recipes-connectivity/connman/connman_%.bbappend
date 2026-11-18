FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# ConnMan read kernel link events into a 4 KiB buffer, so on a 16 KiB-page
# kernel (Raspberry Pi 5) it missed wlan0 at startup and Wi-Fi could not scan
SRC_URI += "file://0001-rtnl-read-whole-netlink-replies.patch"
