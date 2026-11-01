# godwit-llm

The voice assistant's language model: Llama 3.2 1B on the Dragon Q6A's Hexagon
NPU, through Qualcomm Genie. The HMI (`hmi/src/llm.*`) sends it the spoken
requests its own patterns do not understand, and it writes back one of the four
commands as JSON. Nothing leaves the head unit, and the model's work runs on the
NPU, not the processor's cores.

`godwit_llm.py` is adapted from TrailCurrent Peregrine's `genie_server.py` and
serves the same Ollama-style API on `127.0.0.1:11434`:

```
GET  /               {"status": "ok"} once the model is on the NPU (503 while it loads)
POST /api/generate   {"system": "...", "prompt": "..."}  ->  {"response": "...", "done": true}
POST /api/chat       {"messages": [{"role", "content"}], "prefill": "...", "stop": ["..."]}
                     ->  {"response": "...", "done": true}
```

It loads `libGenie.so` once with ctypes and keeps the model resident, so a
request costs only its tokens. Standard library only.

The HMI uses `/api/chat`. A 1B model given rules alone rambles: it wraps the
JSON in explanations and runs to the token limit, about 10 s a request. Given
example turns whose answers are bare JSON, with its answer begun as
`{"action":"` and stopped at the first `}` (`GenieDialog_signal` aborts the
query), it answers in 1 to 3 s. The model itself still misreads some
requests, so the HMI checks every answer and its patterns take the common
phrasings first.

## On the head unit

| Path | From |
|---|---|
| `/usr/libexec/godwit-llm/godwit_llm.py`, `godwit-llm.service` | `godwit-llm` recipe (`os/boards/dragon-q6a/meta-dragon-q6a/dynamic-layers/godwit/recipes-godwit/godwit-llm`) |
| `/usr/share/godwit/llm/godwit-genie-config.json`, `htp_backend_ext_config.json` | This folder: Genie's settings |
| `/usr/share/godwit/llm/` (model, tokenizer, Genie and QNN libraries) | `llama32-1b-genie` recipe: Radxa's [Llama3.2-1B-1024-qairt-v68](https://www.modelscope.cn/models/radxa/Llama3.2-1B-1024-qairt-v68), pinned to one revision |
| `libcdsprpc.so`, `/dev/fastrpc-cdsp` access (group `fastrpc`) | `fastrpc` recipe |
| `/usr/share/qcom/qcs6490/radxa/dragon-q6a/dsp/cdsp/` (`fastrpc_shell_3` and the DSP's libraries), `/usr/share/qcom/conf.d/radxa-dragon-q6a.yaml` | `dragon-q6a-firmware-cdsp` |

The compute DSP itself (`cdsp.mbn`) comes from linux-firmware and is started by
the kernel (`remoteproc`), as the Q6A's device tree asks.

The service runs as `godwit-llm` in the `fastrpc` group, below the HMI's CPU
priority. Logs: `journalctl -u godwit-llm`. To check the NPU by hand:

```sh
cat /sys/class/remoteproc/*/name /sys/class/remoteproc/*/state   # cdsp running
curl -s 127.0.0.1:11434/
curl -s 127.0.0.1:11434/api/generate -d '{"prompt":"Say hi"}'
```

## Genie settings

`godwit-genie-config.json` is the bundle's `htp-model-config-llama32-1b-gqa.json`
with Peregrine's tuning (no busy polling, so the host threads sit idle between
requests; the balanced performance profile, so the NPU is not held at full clock)
and answers capped at 96 tokens, plenty for a one-line command. The context is
the bundle's 1024 tokens; the HMI's prompt and examples use about 550 of them.

Loading takes 2 to 6 s. The model sits in DMA buffers shared with the DSP
(1.7 GB), which are not counted in any process's memory; check them with
`grep '^Total' /sys/kernel/debug/dma_buf/bufinfo`. The service frees them when
it stops (SIGTERM). A process that dies without freeing them leaves them held
by the kernel until reboot, and with two such copies the board runs out of
memory and the kernel kills Photon. So the unit allows only three restarts in
ten minutes, and gives a stop 30 s to free the model before SIGKILL.

## Licences

The model is under the Llama 3.2 Community License ("Built with Llama"; Settings >
System > About & Legal carries the licence text). Genie and the QNN libraries are
Qualcomm's, in binary form under Qualcomm's licence. Neither is open source, so
the recipes mark them `CLOSED`.
