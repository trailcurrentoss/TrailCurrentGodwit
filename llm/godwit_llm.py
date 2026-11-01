#!/usr/bin/env python3
"""Godwit's language model on the Hexagon NPU, behind a loopback HTTP API.

Llama 3.2 1B, compiled for the QCS6490's Hexagon v68 by Radxa, runs through
Qualcomm Genie (libGenie.so). The model is loaded onto the NPU once at start
and stays resident, so a request costs only its tokens.

The API is the Ollama-style one TrailCurrent Peregrine's genie_server.py
serves, which this file is adapted from:

  GET  /               {"status": "ok"} once the model is loaded
  POST /api/generate   {"system": str, "prompt": str} -> {"response": str, "done": true}
  POST /api/chat       {"messages": [{"role", "content"}], "prefill": str, "stop": [str]}
                       -> {"response": str, "done": true}

/api/chat takes example turns, starts the answer with prefill (returned as
part of the response), and ends it at the first stop string (kept). A 1B
model follows example turns far better than rules, and stopping at the end
of what is wanted keeps an answer to a second or two.

The HMI (hmi/src/llm.cpp) uses it to turn spoken requests into commands.
Standard library only; bound to 127.0.0.1.

Environment:
  GENIE_DIR     the model bundle (default /usr/share/godwit/llm)
  GENIE_CONFIG  its Genie dialog config (default <GENIE_DIR>/godwit-genie-config.json)
  LLM_HOST, LLM_PORT  where to listen (default 127.0.0.1:11434)
"""

import ctypes
import json
import os
import signal
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer

GENIE_DIR = os.getenv("GENIE_DIR", "/usr/share/godwit/llm")
GENIE_LIB = os.path.join(GENIE_DIR, "libGenie.so")
GENIE_CONFIG = os.getenv("GENIE_CONFIG", os.path.join(GENIE_DIR, "godwit-genie-config.json"))
HOST = os.getenv("LLM_HOST", "127.0.0.1")
PORT = int(os.getenv("LLM_PORT", "11434"))
MODEL_LABEL = "llama3.2:1b-npu"

# Genie C API (GenieCommon.h, GenieDialog.h)
GENIE_STATUS_SUCCESS = 0
# GenieDialog_SentenceCode_t for a whole query
_SENTENCE_COMPLETE = 0
# GenieDialog_query's status when the prompt does not fit the context
_STATUS_CONTEXT_FULL = 4
# GenieDialog_Action_t
_ACTION_ABORT = 1


def log(msg):
    print(f"[godwit-llm] {msg}", flush=True)


def build_prompt(system, user, prefill=""):
    """System and user text in Llama 3.2's chat template."""
    return build_chat([{"role": "system", "content": system}, {"role": "user", "content": user}], prefill)


def build_chat(messages, prefill=""):
    """Turns in Llama 3.2's chat template, then the assistant's answer begun
    with prefill"""
    parts = ["<|begin_of_text|>"]
    for m in messages:
        role, content = m.get("role"), m.get("content", "")
        if role in ("system", "user", "assistant") and content:
            parts.append(f"<|start_header_id|>{role}<|end_header_id|>\n\n{content}<|eot_id|>")
    parts.append("<|start_header_id|>assistant<|end_header_id|>\n\n" + prefill)
    return "".join(parts)


class Genie:
    """libGenie.so with one GenieDialog kept loaded on the NPU."""

    # void (*)(const char *response, GenieDialog_SentenceCode_t code, const void *userData)
    _Callback = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_int, ctypes.c_void_p)

    def __init__(self):
        L = self._lib = ctypes.CDLL(GENIE_LIB, mode=ctypes.RTLD_GLOBAL)
        handle_p = ctypes.POINTER(ctypes.c_void_p)
        for name, args in (
            ("GenieDialogConfig_createFromJson", [ctypes.c_char_p, handle_p]),
            ("GenieDialogConfig_free", [ctypes.c_void_p]),
            ("GenieDialog_create", [ctypes.c_void_p, handle_p]),
            ("GenieDialog_free", [ctypes.c_void_p]),
            ("GenieDialog_reset", [ctypes.c_void_p]),
            ("GenieDialog_query", [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
                                   self._Callback, ctypes.c_void_p]),
            ("GenieDialog_signal", [ctypes.c_void_p, ctypes.c_int]),
        ):
            fn = getattr(L, name)
            fn.restype = ctypes.c_int
            fn.argtypes = args
        L.Genie_getApiMajorVersion.restype = ctypes.c_uint32
        L.Genie_getApiMinorVersion.restype = ctypes.c_uint32
        self.version = f"{L.Genie_getApiMajorVersion()}.{L.Genie_getApiMinorVersion()}"

        self._config = ctypes.c_void_p()
        self._dialog = ctypes.c_void_p()
        self._lock = threading.Lock()
        with open(GENIE_CONFIG) as f:
            cfg = f.read()
        # The config names the model files relative to the bundle
        prev = os.getcwd()
        os.chdir(GENIE_DIR)
        try:
            self._check(L.GenieDialogConfig_createFromJson(cfg.encode(), ctypes.byref(self._config)),
                        "GenieDialogConfig_createFromJson")
            self._check(L.GenieDialog_create(self._config, ctypes.byref(self._dialog)),
                        "GenieDialog_create")
        finally:
            os.chdir(prev)

    @staticmethod
    def _check(status, what):
        if status != GENIE_STATUS_SUCCESS:
            raise RuntimeError(f"{what} failed: status={status}")

    def generate(self, prompt, stop=()):
        """The answer to one prompt, ended at the first stop string (kept).
        One query at a time on the NPU; Genie calls back with the text from
        inside GenieDialog_query, and a stop aborts the query from another
        thread, as Genie expects."""
        chunks = []
        aborted = threading.Event()

        def on_text(text, _code, _user):
            if not text:
                return
            chunks.append(text.decode("utf-8", errors="replace"))
            if stop and not aborted.is_set() and any(s in "".join(chunks) for s in stop):
                aborted.set()
                threading.Thread(target=self._lib.GenieDialog_signal,
                                 args=(self._dialog, _ACTION_ABORT), daemon=True).start()

        callback = self._Callback(on_text)
        with self._lock:
            self._lib.GenieDialog_reset(self._dialog)
            status = self._lib.GenieDialog_query(self._dialog, prompt.encode("utf-8"),
                                                 _SENTENCE_COMPLETE, callback, None)
        if status == _STATUS_CONTEXT_FULL:
            raise OverflowError("prompt does not fit the context")
        if not aborted.is_set():
            self._check(status, "GenieDialog_query")
        text = "".join(chunks)
        for s in stop:
            at = text.find(s)
            if at >= 0:
                text = text[:at + len(s)]
        return text.strip()

    def close(self):
        """Free the model on the NPU. Without this the kernel keeps the
        model's DMA buffers (1.7 GB) after the process ends, until reboot."""
        with self._lock:
            if self._dialog:
                self._lib.GenieDialog_free(self._dialog)
                self._dialog = ctypes.c_void_p()
            if self._config:
                self._lib.GenieDialogConfig_free(self._config)
                self._config = ctypes.c_void_p()


genie = None


class Handler(BaseHTTPRequestHandler):
    def _reply(self, code, body):
        payload = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path != "/":
            self.send_error(404)
        elif genie is None:
            self._reply(503, {"status": "loading"})
        else:
            self._reply(200, {"status": "ok", "backend": "npu", "genie": genie.version})

    def do_POST(self):
        if self.path not in ("/api/generate", "/api/chat"):
            self.send_error(404)
            return
        if genie is None:
            self._reply(503, {"error": "the model is still loading"})
            return
        length = int(self.headers.get("Content-Length", 0))
        try:
            body = json.loads(self.rfile.read(length)) if length else {}
        except json.JSONDecodeError:
            self.send_error(400, "not JSON")
            return
        prefill = str(body.get("prefill", ""))
        stop = [str(s) for s in body.get("stop", []) if s]
        if self.path == "/api/chat":
            messages = body.get("messages")
            if not isinstance(messages, list) or not messages:
                self.send_error(400, "missing messages")
                return
            prompt = build_chat(messages, prefill)
        else:
            if not body.get("prompt"):
                self.send_error(400, "missing prompt")
                return
            prompt = build_prompt(body.get("system", ""), body["prompt"], prefill)
        start = time.monotonic()
        try:
            text = prefill + genie.generate(prompt, stop)
        except OverflowError as e:
            self._reply(413, {"error": str(e)})
            return
        except RuntimeError as e:
            log(f"generate failed: {e}")
            self._reply(500, {"error": str(e)})
            return
        took = time.monotonic() - start
        log(f"answered in {took:.2f}s: {text[:120]!r}")
        self._reply(200, {"model": MODEL_LABEL, "response": text, "done": True,
                          "total_duration": int(took * 1e9)})

    def log_message(self, fmt, *args):
        pass


class Server(HTTPServer):
    allow_reuse_address = True


def main():
    global genie
    # systemd stops the service with SIGTERM: free the model first (close())
    stop = threading.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda *_: stop.set())
    # Reachable at once; GET / says "loading" until the model is on the NPU
    server = Server((HOST, PORT), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    log(f"listening on {HOST}:{PORT}, loading {GENIE_DIR}")
    start = time.monotonic()
    g = None
    try:
        g = Genie()
        g.generate(build_prompt("Reply with one word.", "hi"))
    except Exception as e:
        log(f"cannot load the model: {e!r}")
        if g is not None:
            g.close()
        server.shutdown()
        sys.exit(1)
    genie = g
    log(f"ready on the NPU (Genie {g.version}) after {time.monotonic() - start:.1f}s")
    stop.wait()
    log("stopping: freeing the model on the NPU")
    server.shutdown()
    genie.close()


if __name__ == "__main__":
    main()
