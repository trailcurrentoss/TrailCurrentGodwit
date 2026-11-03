# HTTPS for Qt Multimedia's FFmpeg backend: YouTube Music's audio streams are
# https://…googlevideo.com URLs, and without TLS FFmpeg refuses them
# ("https protocol not found") and every song is skipped.
PACKAGECONFIG:append = " openssl"

# OE-core adds --enable-nonfree whenever openssl is on, a rule from before
# OpenSSL 3. OpenSSL 3 is Apache-2.0, and FFmpeg accepts it in this LGPL build
# without nonfree; with nonfree the libraries are marked unredistributable.
USE_NONFREE = ""
