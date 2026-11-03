# WebRTC's audio processing for the echo-cancel module, which the voice
# assistant's microphone goes through (62-godwit-mic.conf). AGL's PACKAGECONFIG
# leaves it out, so only the null canceller would be built.
PACKAGECONFIG:append:class-target = " webrtc-echo-cancelling"
