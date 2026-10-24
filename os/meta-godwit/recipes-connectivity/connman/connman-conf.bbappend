# Keep the hostname from base-files. By default ConnMan takes the one a DHCP
# server hands back, and a router that remembers an earlier lease returns the
# old machine name, which then replaces ours until the next reboot.
do_install:append() {
    sed -i '/^\[General\]/a AllowHostnameUpdates=false' ${D}${sysconfdir}/connman/main.conf
}
