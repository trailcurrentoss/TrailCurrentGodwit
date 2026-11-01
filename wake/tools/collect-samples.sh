#!/usr/bin/env bash
# Record "Hey Peregrine" samples from the head unit's own cabin microphone,
# for training the wake word on the voices and microphone it will really hear.
#
# The head unit beeps before each sample; say "Hey Peregrine" after the beep.
# Then it records a stretch of ordinary cabin sound (talk, music, road) as
# negatives. The clips come back to this computer and are deleted from the
# head unit.
#
#   wake/tools/collect-samples.sh --board root@<head unit> --out <dir> [--count 50] [--ambient 300]
#
# The board can also come from GODWIT_BOARD. --out must be OUTSIDE this
# repository: the clips are people's voices and never belong in git. Peregrine's
# training/real_clips/ (git-ignored there) is the usual place:
#   positives -> <out>/positive/*.wav, negatives -> <out>/negative/*.wav
#
# Turn "Hey Peregrine" off in Settings > Voice Assistant first, or the head
# unit will answer every sample.
set -euo pipefail

board="${GODWIT_BOARD:-}"
out=""
count=50
ambient=300
while [ $# -gt 0 ]; do
    case "$1" in
        --board) board="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        --count) count="$2"; shift 2 ;;
        --ambient) ambient="$2"; shift 2 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
done
if [ -z "$board" ] && [ -t 0 ]; then
    read -rp "Head unit (user@host): " board
fi
if [ -z "$board" ]; then
    echo "No head unit: pass --board user@host, set GODWIT_BOARD, or run interactively." >&2
    exit 2
fi
if [ -z "$out" ]; then
    echo "Pass --out <dir> outside the repository." >&2
    exit 2
fi
repo=$(git -C "$(dirname "$0")" rev-parse --show-toplevel 2>/dev/null || true)
out=$(realpath -m "$out")
if [ -n "$repo" ] && [[ "$out/" == "$repo/"* ]]; then
    echo "--out is inside the repository ($repo). Choose a folder outside it." >&2
    exit 2
fi
mkdir -p "$out/positive" "$out/negative"

stamp=$(date +%Y%m%d-%H%M%S)
remote="/data/wake-samples-$stamp"
echo "Recording $count samples, then $ambient s of cabin sound, on $board"

# Everything runs on the head unit in one session; the clips are 16 kHz mono WAV
ssh "$board" COUNT="$count" AMBIENT="$ambient" DIR="$remote" STAMP="$stamp" 'bash -s' <<'EOF'
set -eu
export PIPEWIRE_RUNTIME_DIR=/run/pipewire
mkdir -p "$DIR/positive" "$DIR/negative"
chmod 700 "$DIR"
beep="$DIR/beep.wav"
python3 - "$beep" <<'PY'
import math, struct, sys, wave
w = wave.open(sys.argv[1], "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
n = int(22050 * 0.15)
w.writeframes(b"".join(struct.pack("<h", int(8000 * min(1, i / 200, (n - i) / 200) * math.sin(2 * math.pi * 880 * i / 22050))) for i in range(n)))
w.close()
PY
say() {
    if [ -x /usr/lib/piper/piper ]; then
        echo "$1" | /usr/lib/piper/piper --quiet --model /usr/share/piper/voices/en_US-ljspeech-medium.onnx \
            --espeak_data /usr/lib/piper/espeak-ng-data --output_file "$DIR/say.wav" >/dev/null 2>&1 \
            && pw-play --media-role Navigation "$DIR/say.wav"
    fi
}
say "After each beep, say Hey Peregrine. Vary how you say it, and where you face."
sleep 1
for i in $(seq -w 1 "$COUNT"); do
    pw-play --media-role Navigation "$beep"
    timeout 2.5 pw-record --raw --rate 16000 --channels 1 --format s16 --media-role Communication - \
        > "$DIR/positive/${STAMP}_$i.raw" 2>/dev/null || true
    echo "sample $i of $COUNT"
    sleep 1.2
done
say "Done. Now talk, play music, or just drive normally, without saying Hey Peregrine."
timeout "$AMBIENT" pw-record --raw --rate 16000 --channels 1 --format s16 --media-role Communication - \
    > "$DIR/negative/ambient.raw" 2>/dev/null || true
say "Recording finished."
# Raw PCM to WAV, and the ambient stretch cut into 2 second clips
python3 - "$DIR" "$STAMP" <<'PY'
import os, sys, wave
d, stamp = sys.argv[1], sys.argv[2]
def save(path, pcm):
    w = wave.open(path, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(16000); w.writeframes(pcm); w.close()
for f in sorted(os.listdir(d + "/positive")):
    p = d + "/positive/" + f
    save(p[:-4] + ".wav", open(p, "rb").read()); os.remove(p)
pcm = open(d + "/negative/ambient.raw", "rb").read()
step = 32000 * 2
for k in range(0, len(pcm) - step + 1, step):
    save("%s/negative/%s_ambient_%04d.wav" % (d, stamp, k // step), pcm[k:k + step])
os.remove(d + "/negative/ambient.raw")
PY
EOF

echo "Copying the clips here and deleting them from the head unit"
ssh "$board" "tar -C '$remote' -cf - positive negative" | tar -C "$out" -xf -
ssh "$board" "rm -rf '$remote'"
echo "Positives: $(ls "$out/positive" | wc -l), negatives: $(ls "$out/negative" | wc -l), in $out"
