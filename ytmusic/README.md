# godwit-ytmusic

YouTube Music for the HMI's Media app. The HMI runs `godwit_ytmusic.py` as a
child process while YouTube Music is turned on and talks to it over stdin and
stdout, one JSON object per line; the protocol is described at the top of the
script. It searches and lists with [ytmusicapi](https://github.com/sigma67/ytmusicapi)
and finds each song's audio stream with [yt-dlp](https://github.com/yt-dlp/yt-dlp),
which runs YouTube's player scripts with Node.js. The HMI plays the stream
itself, through Qt Multimedia's FFmpeg, which the image builds with OpenSSL for
these HTTPS URLs (`os/meta-godwit/recipes-multimedia/ffmpeg/ffmpeg_%.bbappend`).

Signing in is optional and uses Google's device flow with the driver's own
OAuth client (see [Music sources](../hmi/README.md#music-sources)). The token
and client are kept in the folder given with `--state`, mode 0600.

To try it on a PC:

```sh
python3 -m venv ~/ytmusic-venv
~/ytmusic-venv/bin/pip install ytmusicapi "yt-dlp[default]"
echo '{"id":1,"cmd":"search","q":"Hotel California"}' | \
    ~/ytmusic-venv/bin/python godwit_ytmusic.py --state /tmp/ytmusic-state
```

Point the simulator at it with `GODWIT_YTMUSIC_BIN`, a small script that
runs the venv's Python on `godwit_ytmusic.py` with Node.js on the PATH.

ytmusicapi and yt-dlp are unofficial clients; YouTube's terms do not permit
them, and they can stop working whenever YouTube changes.
