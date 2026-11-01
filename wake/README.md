# godwit-wake

Listens for the wake word **"Hey Peregrine"** for the HMI's voice assistant. It
reads 16 kHz mono 16-bit audio on stdin and prints `wake <score>` on stdout each
time it hears the phrase. The HMI (`hmi/src/wake.*`) runs it behind `pw-record`:

```sh
pw-record --raw --rate 16000 --channels 1 --format s16 - | godwit-wake
```

It runs [openWakeWord](https://github.com/dscripka/openWakeWord)'s three models
with ONNX Runtime on one core, streaming them the same way openWakeWord's own
Python does, so any model trained with openWakeWord's tools works:

| Model | What it does | Where it comes from |
|---|---|---|
| `melspectrogram.onnx` | Audio to mel frames | openWakeWord v0.5.1 release (Apache-2.0), fetched by the recipe |
| `embedding_model.onnx` | 76 mel frames to 96 features, every 80 ms | openWakeWord v0.5.1 release (Apache-2.0), fetched by the recipe |
| `models/hey_peregrine.onnx` (+ `.data`) | Scores the last 16 features | Trained for TrailCurrent Peregrine with its pipeline (`training/` in that repository) |

A detection needs two 80 ms chunks in a row scoring over 0.85. Only a window
with no speech anywhere in its 1.28 s is skipped: the score peaks as the
phrase ends, in chunks already quiet, so gating chunk by chunk dropped 50
recorded cabin attempts from 46 caught to 31. Peregrine asks for three; measured on the head unit's
cabin microphone, three caught 16 of 28 clear attempts and two caught 24, at
about one false wake an hour of music and talk. `--trace` prints every chunk's
level and score for tuning. After a detection it waits two seconds
before scoring again, so one phrase counts once.

| Option | Default |
|---|---|
| `--dir` | `/usr/share/godwit/wake` |
| `--model` | `hey_peregrine` |
| `--threshold` | `0.85` |
| `--activations` | `2` |
| `--min-level` | `300` (mean absolute sample value) |
| `--verbose` | Report near misses on stderr |
| `--trace` | Every chunk's time, level and score on stderr |

On the Dragon Q6A it takes about 4% of one core.

## Build on a PC

Against an unpacked ONNX Runtime release (`onnxruntime-linux-x64-*.tgz`):

```sh
cmake -S wake -B build/wake -DCMAKE_PREFIX_PATH=<onnxruntime-linux-x64-1.20.1>
cmake --build build/wake
```

Try it on a recording, with the two openWakeWord models copied beside
`models/hey_peregrine.onnx`:

```sh
ffmpeg -i clip.mp3 -ar 16000 -ac 1 -f s16le - | build/wake/godwit-wake --dir <models> --verbose
```

The image builds it with `os/meta-godwit/recipes-godwit/godwit-wake/godwit-wake.bb`,
on ONNX Runtime's prebuilt release (`onnxruntime-bin`).

## Recording training samples

`tools/collect-samples.sh` records "Hey Peregrine" from the head unit's own
microphone, for training the model on the voices and microphone it really
hears. Turn **Hey Peregrine** off in Settings > Voice Assistant first, then:

```sh
wake/tools/collect-samples.sh --board root@<head unit> --out <folder> [--count 50] [--ambient 300]
```

The head unit beeps before each sample (say the phrase once after each
beep), then records `--ambient` seconds of ordinary cabin sound as
negatives: talk, music and near misses such as "Peregrine" alone, but never
the phrase. The clips come back as 16 kHz WAV files in `<folder>/positive`
and `<folder>/negative` and are deleted from the head unit. The head unit
can also come from `GODWIT_BOARD`.

The clips are people's voices: `--out` must be outside this repository, and
the script refuses a folder inside it. Peregrine's training kit
(`training/` in TrailCurrent Peregrine, whose `real_clips/` folder is
git-ignored) trains the model; its config takes the clips as
`real_positive_clips_dir` and `real_negative_clips_dirs`.

Hold some clips back to compare a new model with the current one, at the
same false wakes per hour, before replacing `models/hey_peregrine.onnx`. One
round of 50 samples did not beat the current model: a few dozen real clips
are outweighed by the 50,000 synthetic ones the kit generates, so a real
improvement needs a few hundred, from more than one voice and with music,
fan and road noise.
