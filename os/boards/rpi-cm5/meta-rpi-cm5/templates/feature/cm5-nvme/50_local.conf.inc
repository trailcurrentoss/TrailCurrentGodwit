# Boot the CM5 from NVMe.
#
# AGL boots the Pi through U-Boot, but U-Boot's boot script only reads
# mmc 0:1 and it has no BCM2712 PCIe driver, so it cannot see NVMe. The
# Pi firmware boots NVMe itself, so load the kernel straight from it.
RPI_USE_U_BOOT:rpi = "0"

WKS_FILE:rpi = "cm5-nvme.wks"
CMDLINE_ROOT_PARTITION:rpi = "/dev/nvme0n1p2"

# Enable the PCIe x1 lane the NVMe drive sits on
RPI_EXTRA_CONFIG:append:rpi = "\n# NVMe on PCIe x1\ndtparam=pciex1\n"
