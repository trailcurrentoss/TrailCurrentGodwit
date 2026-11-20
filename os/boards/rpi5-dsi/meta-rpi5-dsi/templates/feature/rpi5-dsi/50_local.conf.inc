# Waveshare 10.1-DSI-TOUCH-B on the Pi 5's DSI1 connector (the one nearer the
# Ethernet port). The panel, its backlight and the Goodix touch controller are
# driven by panel-waveshare-dsi-v2, waveshare-panel-regulator and goodix-ts,
# which the image already installs as modules. What the BSP leaves out is the
# overlay that tells the kernel the panel is there.
RPI_KERNEL_DEVICETREE_OVERLAYS:append:rpi = " overlays/vc4-kms-dsi-waveshare-panel-v2.dtbo"
RPI_EXTRA_CONFIG:append:rpi = "\n# Waveshare 10.1-DSI-TOUCH-B on DSI1 (add ,dsi0 for the other connector)\ndtoverlay=vc4-kms-dsi-waveshare-panel-v2,10_1_inch_b\n"

# The Pi 5's fan connector. Its device tree ships the fan (and the RP1 PWM
# that drives it) disabled until this parameter turns them on; Raspberry Pi
# OS sets it, meta-raspberrypi does not. Without it the fan never runs and the
# board overheats under load, which reset it mid-route.
RPI_EXTRA_CONFIG:append:rpi = "\n# Pi 5 fan connector: the kernel drives the fan from the CPU temperature\ndtparam=cooling_fan=on\n"

# USB current. A Pi 5 that does not see a 5 A USB-PD supply allows its USB
# ports only 600 mA between them. Two Waveshare knob dials (screen plus
# vibration motor each) and the USB audio adapter go over that: every tap made
# the ports report over-current, cut power, and reset both dials and the
# audio. This board's supply delivers 5 A, so the ports get the full 1.6 A.
# Only for a supply that really gives 5 A; a weaker one browns out the Pi.
RPI_EXTRA_CONFIG:append:rpi = "\n# The supply delivers 5 A: let the USB ports draw 1.6 A\nusb_max_current_enable=1\n"

# The TrailCurrent boot logo, drawn by the kernel (fullscreen_logo) as soon as
# the panel comes up, instead of the firmware's rainbow square and boot text.
# The image is portrait, turned like the HMI; see tools/make-bootlogo.py.
DISABLE_SPLASH:rpi = "1"
CMDLINE:append:rpi = " fullscreen_logo=1 fullscreen_logo_name=godwit-logo.tga vt.global_cursor_default=0"
IMAGE_INSTALL:append = " godwit-bootlogo"

# The Wi-Fi regulatory database. Without it the kernel logs "failed to load
# regulatory.db" and the radio keeps to the most restrictive world settings.
IMAGE_INSTALL:append = " wireless-regdb-static"
