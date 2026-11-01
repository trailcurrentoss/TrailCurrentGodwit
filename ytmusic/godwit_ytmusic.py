#!/usr/bin/env python3
"""YouTube Music for the Godwit HMI.

The HMI runs this as a child process and talks to it over stdin and stdout,
one JSON object per line. Each request carries an "id", and its reply
carries the same "id", with "ok" set to true or false and "error" when it
is false. Requests run on worker threads, so a slow stream lookup does not
hold up a search.

  {"cmd": "status"}                     -> {"signedIn", "account"}
  {"cmd": "search", "q"}                -> {"items": [track]}
  {"cmd": "radio", "ref": videoId}       -> {"tracks": [track]}  the song, then songs like it
  {"cmd": "lists"}                      -> {"items": [{id, title, sub, icon}]}  the account's playlists
  {"cmd": "tracks", "ref": playlistId}   -> {"title", "tracks": [track]}
  {"cmd": "stream", "ref": videoId}      -> {"url"}  the audio stream, valid for a few hours
  {"cmd": "signin", "clientId", "clientSecret"} -> {"url", "code"}
  {"cmd": "signout"}

where track is {id, title, artist, album, dur (seconds), art}.

Signing in follows Google's device flow. After "signin" replies, the helper
waits in the background for the code to be entered, then sends an event
line: {"event": "signedIn", "account"} or {"event": "signInFailed", "error"}.

ytmusicapi does the searching and listing, without an account unless one
is signed in. yt-dlp finds the audio streams. YouTube's player now needs a
JavaScript runtime to do that; Node.js is used.

The token, and the OAuth client it belongs to, are kept in --state, which
only the HMI can read.
"""

import argparse
import json
import os
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor

from ytmusicapi import OAuthCredentials, YTMusic

out_lock = threading.Lock()


def send(obj):
    with out_lock:
        sys.stdout.write(json.dumps(obj, separators=(",", ":")) + "\n")
        sys.stdout.flush()


def log(*args):
    print(*args, file=sys.stderr, flush=True)


def seconds(text):
    """'3:45' or '1:02:03' -> seconds"""
    try:
        n = 0
        for part in (text or "").split(":"):
            n = n * 60 + int(part)
        return n
    except ValueError:
        return 0


def art(item):
    thumbs = item.get("thumbnails") or item.get("thumbnail") or []
    return thumbs[-1]["url"] if thumbs else ""


def track(item):
    album = item.get("album") or {}
    return {
        "id": item.get("videoId") or "",
        "title": item.get("title") or "",
        "artist": ", ".join(a["name"] for a in item.get("artists") or [] if a.get("name")),
        "album": album.get("name", "") if isinstance(album, dict) else str(album),
        "dur": item.get("duration_seconds") or seconds(item.get("length") or item.get("duration")),
        "art": art(item),
    }


def tracks(items):
    return [t for t in (track(i) for i in items or []) if t["id"]]


class Service:
    def __init__(self, state):
        self.state = state
        self.token_path = os.path.join(state, "oauth.json")
        self.client_path = os.path.join(state, "client.json")
        self.lock = threading.Lock()
        self.yt = None
        self.signed_in = False
        self.account = ""
        self.pairing = 0  # bumps to cancel a sign-in still waiting
        self.connect()

    # ------------------------------------------------------------ account

    def connect(self):
        with self.lock:
            self.signed_in = False
            self.account = ""
            client = self.read_json(self.client_path)
            if client and os.path.exists(self.token_path):
                try:
                    creds = OAuthCredentials(client["clientId"], client["clientSecret"])
                    self.yt = YTMusic(self.token_path, oauth_credentials=creds)
                    self.signed_in = True
                except Exception as e:  # a stale or broken token: carry on without it
                    log("signed-in session failed:", e)
            if not self.signed_in:
                self.yt = YTMusic()
        if self.signed_in:
            try:
                self.account = self.yt.get_account_info().get("accountName", "")
            except Exception as e:
                log("account info failed:", e)

    @staticmethod
    def read_json(path):
        try:
            with open(path, encoding="utf8") as f:
                return json.load(f)
        except (OSError, ValueError):
            return None

    def write_private(self, path, obj):
        tmp = path + ".tmp"
        fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w", encoding="utf8") as f:
            json.dump(obj, f)
        os.replace(tmp, path)

    def signin(self, client_id, client_secret):
        if not client_id or not client_secret:
            raise ValueError("Enter the client ID and secret first")
        creds = OAuthCredentials(client_id, client_secret)
        code = creds.get_code()
        self.pairing += 1
        threading.Thread(target=self.wait_for_code, daemon=True,
                         args=(creds, code, client_id, client_secret, self.pairing)).start()
        return {"url": code.get("verification_url", "https://www.google.com/device"), "code": code.get("user_code", "")}

    def wait_for_code(self, creds, code, client_id, client_secret, pairing):
        interval = int(code.get("interval") or 5)
        deadline = time.time() + int(code.get("expires_in") or 1800)
        while time.time() < deadline and pairing == self.pairing:
            time.sleep(interval)
            try:
                raw = creds.token_from_code(code["device_code"])
            except Exception as e:
                send({"event": "signInFailed", "error": str(e)})
                return
            error = raw.get("error")
            if error == "authorization_pending":
                continue
            if error == "slow_down":
                interval += 5
                continue
            if error:
                send({"event": "signInFailed", "error": raw.get("error_description") or error})
                return
            # The fields ytmusicapi's token file holds
            token = {k: raw.get(k) for k in ("scope", "token_type", "access_token", "refresh_token", "expires_in")}
            token["expires_at"] = int(time.time()) + int(raw.get("expires_in", 0))
            self.write_private(self.client_path, {"clientId": client_id, "clientSecret": client_secret})
            self.write_private(self.token_path, token)
            self.connect()
            if self.signed_in:
                send({"event": "signedIn", "account": self.account})
            else:
                send({"event": "signInFailed", "error": "YouTube Music did not accept the sign-in"})
            return
        if pairing == self.pairing:
            send({"event": "signInFailed", "error": "The code expired"})

    def signout(self):
        self.pairing += 1
        for p in (self.token_path, self.client_path):
            try:
                os.remove(p)
            except FileNotFoundError:
                pass
        self.connect()

    # ------------------------------------------------------------ music

    def search(self, q):
        return tracks(self.yt.search(q, filter="songs", limit=20))

    def radio(self, video_id):
        return tracks(self.yt.get_watch_playlist(videoId=video_id, limit=40).get("tracks"))

    def lists(self):
        if not self.signed_in:
            return []
        items = [{"id": "LM", "title": "Liked Music", "sub": "Your library", "icon": "heart"}]
        for p in self.yt.get_library_playlists(limit=100):
            if p.get("playlistId") in (None, "LM"):
                continue
            count = p.get("count")
            items.append({"id": p["playlistId"], "title": p.get("title", ""),
                          "sub": "Playlist" + (f" · {count} songs" if count else ""), "icon": "list"})
        return items

    def playlist(self, playlist_id):
        p = self.yt.get_playlist(playlist_id, limit=300)
        return p.get("title", ""), tracks(p.get("tracks"))

    def stream(self, video_id):
        import yt_dlp
        opts = {
            "format": "bestaudio[ext=m4a]/bestaudio",
            "quiet": True,
            "no_warnings": True,
            "noplaylist": True,
            "skip_download": True,
            "js_runtimes": {"node": {}},
        }
        with yt_dlp.YoutubeDL(opts) as ydl:
            info = ydl.extract_info("https://music.youtube.com/watch?v=" + video_id, download=False)
        url = info.get("url")
        if not url:
            raise RuntimeError("no audio stream")
        return url

    # ------------------------------------------------------------ requests

    def handle(self, req):
        cmd = req.get("cmd")
        if cmd == "status":
            return {"signedIn": self.signed_in, "account": self.account}
        if cmd == "search":
            return {"items": self.search(req.get("q", ""))}
        if cmd == "radio":
            return {"tracks": self.radio(req["ref"])}
        if cmd == "lists":
            return {"items": self.lists()}
        if cmd == "tracks":
            title, t = self.playlist(req["ref"])
            return {"title": title, "tracks": t}
        if cmd == "stream":
            return {"url": self.stream(req["ref"])}
        if cmd == "signin":
            return self.signin(req.get("clientId", "").strip(), req.get("clientSecret", "").strip())
        if cmd == "signout":
            self.signout()
            return {}
        raise ValueError(f"unknown command {cmd!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--state", required=True, help="folder for the sign-in token")
    args = parser.parse_args()
    os.makedirs(args.state, mode=0o700, exist_ok=True)
    os.umask(0o077)

    service = Service(args.state)
    pool = ThreadPoolExecutor(max_workers=4)

    def run(req):
        try:
            reply = service.handle(req)
            reply.update({"id": req.get("id"), "ok": True})
        except Exception as e:
            log(f"{req.get('cmd')} failed: {e}")
            reply = {"id": req.get("id"), "ok": False, "error": str(e)}
        send(reply)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except ValueError:
            log("not JSON:", line[:200])
            continue
        pool.submit(run, req)
    pool.shutdown(wait=False, cancel_futures=True)


if __name__ == "__main__":
    main()
