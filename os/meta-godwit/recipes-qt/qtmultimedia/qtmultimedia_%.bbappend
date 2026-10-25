# The HMI plays videos with Qt's FFmpeg backend and sends their sound straight
# to PipeWire, where it takes the multimedia role. GStreamer, PulseAudio and
# spatial audio (which pulls in Qt Quick 3D) are left out; AGL's demo layer
# appends GStreamer, hence the :remove.
PACKAGECONFIG = "ffmpeg qml"
PACKAGECONFIG:remove = "gstreamer"
DEPENDS += "pipewire"
EXTRA_OECMAKE += "-DFEATURE_pipewire=ON"
