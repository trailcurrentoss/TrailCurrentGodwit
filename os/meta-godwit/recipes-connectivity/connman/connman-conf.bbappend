# Keep the hostname from base-files. By default ConnMan takes the one a DHCP
# server hands back, and a router that remembers an earlier lease returns the
# old machine name, which then replaces ours until the next reboot.
#
# AGL's own append installs main.conf, and appends run in layer order, so
# edit it once do_install has finished rather than in another append.
godwit_no_dhcp_hostname() {
    sed -i '/^\[General\]/a AllowHostnameUpdates=false' ${D}${sysconfdir}/connman/main.conf
}
do_install[postfuncs] += "godwit_no_dhcp_hostname"
