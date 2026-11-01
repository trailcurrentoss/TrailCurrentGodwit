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

A detection needs two 80 ms chunks in a row scoring over 0.85, and quiet
chunks are not scored. Peregrine asks for three; measured on the head unit's
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
