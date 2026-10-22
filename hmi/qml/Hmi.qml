pragma Singleton
import QtQuick
import Godwit
import "Ring.js" as Ring
import "DemoData.js" as D

// Head unit state, knob arbitration and knob actions. Ported from the design
// mockup (Dual Encoder Head Unit v2.dc.html under DOCS/) so the two
// can be compared function by function: owner(), act(), the o_*/a_* handlers,
// encs() and rows() keep their names.
//
// State objects are replaced, never mutated, so bindings see every change.
QtObject {
    id: hmi

    // ---------------------------------------------------------------- state
    // Park until the vehicle CAN bus reports the real gear
    property string gear: "P"
    property string prevApp: ""
    property int gvol: 60
    property double guideUntil: 0
    property string guideTxt: ""
    property bool resume: false
    property string app: "nav"
    property bool launcher: false
    property int lsel: 0
    property var rot: [0, 0]
    property var flash: [false, false]
    property string toastMsg: ""
    property var alert: null              // {msg, until}
    property string audio: "media"         // media | radio
    property bool edit: false
    property var rsel: ({ energy: 0, water: 0, lights: 0, settings: 0 })
    property int czone: 0
    property var clim: ({ drv: { t: 70, fan: 3, seat: 2, air: "face" }, pas: { t: 74, fan: 3, seat: 0, air: "bi" },
                          mode: [0, 0], auto: false, ac: true, recirc: false, defF: false, defR: false,
                          sync: false, cabin: 68, out: 54 })
    property int vol: 45
    property bool muted: false
    property var radio: ({ band: "FM", fm: 101.9, am: 880, ptr: 0,
                           presets: [{ b: "FM", f: 88.5 }, { b: "FM", f: 91.3 }, { b: "FM", f: 94.7 },
                                     { b: "FM", f: 101.9 }, { b: "FM", f: 106.7 }, { b: "AM", f: 880 }] })
    property var media: ({ playing: true, idx: 2, sel: 3, pos: 74, src: "Bluetooth", shuffle: false })
    property var nav: ({ zoom: 15, step: 0, dist: 0.6, voice: true, view: "2D" })
    // The active route: its turn-by-turn steps and destination
    property bool routeActive: true
    property var steps: D.STEPS
    property var dest: ({ n: "Castle Valley", a: "Castle Valley, UT", icon: "flag-outline", mi: 16.9 })
    // Destination search, open over the map: {q, sel, preview}
    property var navSearch: null
    property var phone: ({ sel: 0, call: null, mic: true, silent: false })
    property var energy: ({ soc: 84, limit: 40, inverter: true, shore: false,
                            circuits: [{ n: "Fridge", on: true, w: 65 }, { n: "Water Pump", on: false, w: 48 },
                                       { n: "Furnace Fan", on: true, w: 42 }, { n: "Roof Fan", on: false, w: 30 },
                                       { n: "USB / 12V", on: true, w: 18 }] })
    property var water: ({ pump: false, heater: true, set: 120,
                           tanks: [{ n: "Fresh", p: 72, cap: 40 }, { n: "Grey", p: 41, cap: 30 }, { n: "Black", p: 88, cap: 30 }] })
    property var level: ({ pitch: 1.4, roll: -0.8, auto: false, axis: "pitch", jacks: "Up", rt: 0 })
    property var lights: ({ zones: [{ n: "Kitchen", on: true, b: 60 }, { n: "Living", on: true, b: 40 },
                                    { n: "Bedroom", on: false, b: 30 }, { n: "Bath", on: false, b: 80 },
                                    { n: "Porch", on: false, b: 100 }, { n: "Awning", on: true, b: 70 }] })
    property var cams: ({ sel: 0, guides: true })
    property var prefs: ({ bright: 100, ring: 90, autodim: true, appearance: "Light",
                           phoneVol: 60, bal: 0, fader: 0, bass: 2, mid: 0, treble: 1, touchSound: true,
                           avoidHwy: false, avoidTolls: false,
                           tempUnit: "F", distUnit: "mi", volUnit: "gal", clock24: false, autoTime: true, tz: "Mountain" })

    // Settings: connectivity state (demo until ConnMan / BlueZ are connected)
    // Wi-Fi: live from ConnMan on the head unit, demo networks elsewhere.
    // Change it only through the wifi* functions below.
    property var demoWifi: ({ on: true, ssid: "Campground Guest", nets: D.WIFI_NETS, noAutoJoin: [] })
    readonly property var wifi: Net.available
        ? { on: Net.wifiPowered, ssid: Net.connectedSsid, nets: Net.networks, scanning: Net.scanning,
            noAutoJoin: Net.networks.filter(function (n) { return n.saved && !n.autoConnect; }).map(function (n) { return n.ssid; }) }
        : demoWifi
    property var bt: ({ on: true, phones: D.PHONES })
    // Settings navigation: a stack of pages, the selected row on each, and
    // the overlays a page can raise
    property var setStack: ["root"]
    property var setArgs: [null]
    property var setSel: ({})
    property var confirmBox: null     // {title, body, ok, act}
    property var join: null           // {ssid, sec, pw, show, busy}
    property var pairing: null        // {stage: "search" | "confirm" | "busy", phone, code}
    readonly property string setPage: setStack[setStack.length - 1]
    readonly property var setArg: setArgs[setArgs.length - 1]

    // Wall clock that bindings depend on. Advanced by the 1 s tick and after
    // every input, so time-based rules (recent turn, guidance window) update.
    property double clock: Date.now()
    property double lastInput: Date.now()
    property var lastTurn: [0, 0]
    readonly property double t0: Date.now()
    function secs() { return (Date.now() - t0) / 1000; }

    // Per-ring transient effects (press flash, detent, end bump, hold sweep).
    // Read by the ring frame loop only, so mutated in place.
    property var fx: [({}), ({})]
    property var hover: [false, false]
    // Touch stand-in for a knob, opened from the bottom bar: -1 closed, 0 left, 1 right
    property int knobPanel: -1

    // Ring output: two arrays of 5 premultiplied {r,g,b}, refreshed at 30 Hz
    property var ring: [[], []]

    // --------------------------------------------------------------- helpers
    function upd(key, patch) {
        var cur = hmi[key];
        var p = typeof patch === "function" ? patch(cur) : patch;
        hmi[key] = Object.assign({}, cur, p);
    }
    function toast(msg) { toastMsg = msg; toastTimer.restart(); }
    readonly property Timer toastTimer: Timer { interval: 1700; onTriggered: hmi.toastMsg = "" }

    // Distances arrive in miles, temperatures in °F and liquids in gallons;
    // these format them in the units chosen in Settings
    function distParts(mi) {
        if (prefs.distUnit === "km") {
            var km = mi * 1.609344;
            return km < 0.16 ? { v: Math.round(km * 1000 / 50) * 50, u: " m" } : { v: km.toFixed(1), u: " km" };
        }
        return mi < 0.1 ? { v: Math.round(mi * 5280 / 50) * 50, u: " ft" } : { v: mi.toFixed(1), u: " mi" };
    }
    function fd(mi) { var p = distParts(mi); return p.v + p.u; }
    function tempNum(f) {
        if (prefs.tempUnit !== "C") return String(f);
        var c = Math.round((f - 32) * 5 / 9 * 2) / 2;
        return c % 1 ? c.toFixed(1) : String(c);
    }
    function tempStr(f) { return tempNum(f) + "°" + prefs.tempUnit; }
    function volNum(gal) { return prefs.volUnit === "L" ? Math.round(gal * 3.785) : Math.round(gal); }
    function volUnit() { return prefs.volUnit === "L" ? "L" : "gal"; }
    function fmtTime(d, ampm) {
        var m = ("0" + d.getMinutes()).slice(-2);
        if (prefs.clock24) return ("0" + d.getHours()).slice(-2) + ":" + m;
        return (d.getHours() % 12 || 12) + ":" + m + (ampm ? (d.getHours() < 12 ? " AM" : " PM") : "");
    }
    function sig() {
        var R = radio, fm = R.band === "FM", f = fm ? R.fm : R.am, L = fm ? D.FM : D.AM;
        var best = 0, stn = null;
        L.forEach(function (x) {
            var df = Math.abs(x.f - f);
            var v = Math.pow(Ring.clamp(1 - df / (fm ? 0.5 : 40), 0, 1), 0.7) * x.s;
            if (v > best) { best = v; stn = x; }
        });
        return { sig: best, stn: best > 0.35 ? stn : null };
    }
    function urg() {
        var cur = steps[nav.step];
        return !routeActive || cur.dir === "arrive" ? 0 : Ring.clamp(1 - nav.dist / 0.6, 0, 1);
    }
    function interlock() { return gear !== "P" && level.jacks !== "Up"; }
    function guideOn() { return clock < guideUntil && !phone.call && gear === "D"; }
    function recent(s) { return clock - lastTurn[s] < 2000; }
    readonly property bool inCall: !!phone.call
    readonly property bool incoming: inCall && phone.call.state === "incoming"
    readonly property bool lowBattery: energy.soc < 20

    // ------------------------------------------------------------ app flow
    function openApp(id) {
        if (gear === "R" && id !== "cams") { toast("Rear camera stays on in Reverse"); return; }
        var call = !!phone.call;
        app = id; launcher = false; edit = false;
        if (id === "settings") resetSet();
        if (id === "radio") audio = "radio";
        if (id === "media") audio = "media";
        if (call && (id === "radio" || id === "media")) resume = true;
        if (id === "radio") upd("media", { playing: false });
        if (call) return;
        if (id === "media") upd("media", { playing: true });
        if (id === "radio") muted = false;
    }
    function toggleLauncher() {
        if (gear === "R") { toast("Rear camera stays on in Reverse"); return; }
        var i = D.APPS.findIndex(function (a) { return a.id === app; });
        lsel = Math.max(0, i);
        launcher = !launcher;
    }
    function pauseForCall() {
        resume = resume || (audio === "media" ? media.playing : !muted);
        upd("media", { playing: false });
    }
    function answerCall() { upd("phone", function (p) { return { call: Object.assign({}, p.call, { state: "active", t: 0 }) }; }); }
    function startCall(c) { pauseForCall(); upd("phone", { call: { who: c, state: "active", t: 0 }, mic: true }); }
    function endCall(msg) {
        var r = resume, src = audio;
        resume = false;
        upd("phone", { call: null, mic: true, silent: false });
        if (r && src === "media") upd("media", { playing: true });
        if (r && src === "radio") muted = false;
        toast(msg + (r ? " · " + D.SRC[src].n + " resumed" : ""));
    }
    function simIncoming() {
        if (phone.call) return;
        pauseForCall();
        upd("phone", { silent: false, call: { who: 3, state: "incoming", t: 0 } });
    }
    function simBattery() {
        if (lowBattery) { upd("energy", { soc: 84 }); alert = null; }
        else { upd("energy", { soc: 14 }); alert = { msg: "Battery low · 14%", until: Date.now() + 4000 }; }
    }
    function retract() { upd("level", { jacks: "Retracting", auto: false, rt: 3 }); toast("Retracting jacks"); }
    function setGear(g) {
        if (g === gear) return;
        if (g === "R") {
            prevApp = app === "cams" ? prevApp : app;
            app = "cams"; launcher = false; edit = false;
            upd("cams", { sel: 0 });
        } else if (gear === "R" && prevApp) {
            app = prevApp; prevApp = "";
        }
        if (g !== "P" && level.auto) upd("level", { auto: false });
        gear = g;
        toast({ P: "Park", R: "Reverse · rear camera on", D: "Drive" }[g]);
    }
    // Next station up, or down with dir -1
    function seek(dir) {
        var R = radio, fm = R.band === "FM", cur = fm ? R.fm : R.am, L = fm ? D.FM : D.AM;
        var nx = dir < 0 ? (L.slice().reverse().find(function (x) { return x.f < cur - 0.001; }) || L[L.length - 1])
                         : (L.find(function (x) { return x.f > cur + 0.001; }) || L[0]);
        upd("radio", fm ? { fm: nx.f } : { am: nx.f });
        muted = false;
        toast("Seek · " + nx.n);
    }
    function tuneTo(b, f) {
        upd("radio", b === "FM" ? { band: b, fm: f } : { band: b, am: f });
        upd("media", { playing: false });
        audio = "radio";
        if (inCall) resume = true; else muted = false;
    }
    function playTrack(i) {
        upd("media", { sel: i, idx: i, pos: 0, playing: !inCall });
        if (inCall) { resume = true; audio = "media"; }
    }
    function toggleTheme() { upd("prefs", { appearance: Theme.dark ? "Light" : "Dark" }); applyAppearance(); }
    // Auto follows the time of day until a light sensor is fitted
    function applyAppearance() {
        var h = new Date().getHours();
        Theme.dark = prefs.appearance === "Dark" || (prefs.appearance === "Auto" && (h >= 19 || h < 7));
    }

    // ------------------------------------------------- knob arbitration
    // Who owns each knob right now: [left, right]
    function owner() {
        var call = phone.call, inc = call && call.state === "incoming";
        if (gear === "R") return [inc ? "silence" : call ? "call" : "audio", "cams"];
        var L = call ? (inc ? "incoming" : "call") : guideOn() ? "guide" : "audio";
        if (interlock()) return [L, "interlock"];
        if (inc) return ["incoming", "incoming"];
        if (launcher) return [L, "launcher"];
        if (app === "climate") return call ? ["call", "climateR"] : ["climate", "climate"];
        if (call && (app === "media" || app === "radio")) return ["call", "endcall"];
        return [L, app];
    }

    function encKey(s) { var e = encs[s]; return e.value + "|" + e.sub + "|" + e.label; }

    // Entry point for all knob input: s = 0 left / 1 right, kind = turn|press|hold
    function act(s, kind, d) {
        var T = secs();
        lastInput = Date.now();
        if (kind === "turn") {
            var before = encKey(s);
            var r = rot.slice(); r[s] += d * 15; rot = r;
            var na = ((r[s] % 360) + 540) % 360 - 180, nk = 0, bd = 999;
            Ring.POS.forEach(function (p, k) { var q = Ring.ang(p, na); if (q < bd) { bd = q; nk = k; } });
            fx[s].det = { k: nk, t: T };
            var lt = lastTurn.slice(); lt[s] = Date.now(); lastTurn = lt;
            recencyTimer.restart();
            Qt.callLater(function () { if (encKey(s) === before) fx[s].bump = { t: secs(), d: d }; });
        } else {
            fx[s].press = T;
            var f = flash.slice(); f[s] = true; flash = f;
            flashTimer.restart();
        }
        var own = owner()[s];
        var h = hmi["o_" + own] || hmi["a_" + own];
        if (h) h(s, kind, d || 0);
        clock = Date.now();
    }
    readonly property Timer flashTimer: Timer { interval: 180; onTriggered: hmi.flash = [false, false] }
    readonly property Timer recencyTimer: Timer { interval: 2100; onTriggered: hmi.clock = Date.now() }

    // Hold-progress sweep: starts when a knob goes down on a control that has a hold action
    function holdStart(s) { var c = encs[s].holdC; if (c) fx[s].hold = { t: secs() + 0.12, c: c }; }
    function holdCancel(s) { fx[s].hold = null; }

    // ------------------------------------------------- knob handlers
    function o_incoming(s, k, d) {
        if (s === 0) {
            if (k === "turn") { if (!phone.silent) { upd("phone", { silent: true }); toast("Ringer silenced"); } }
            else answerCall();
        } else if (k !== "turn") endCall("Call declined");
    }
    function o_silence(s, k, d) {
        if (!phone.silent) { upd("phone", { silent: true }); toast("Ringer silenced · leave Reverse to answer"); }
    }
    function o_guide(s, k, d) {
        if (k === "turn") { gvol = Ring.clamp(gvol + d * 2, 0, 100); guideUntil = Math.max(guideUntil, Date.now() + 1500); guideTimer.restart(); }
        if (k === "press") guideUntil = 0;
        if (k === "hold") { upd("nav", { voice: false }); guideUntil = 0; toast("Voice guidance off"); }
    }
    function o_interlock(s, k, d) { if (k !== "turn" && level.jacks === "Down") retract(); }
    function o_endcall(s, k, d) { if (k !== "turn") endCall("Call ended"); else toast("Audio paused during call"); }
    function o_call(s, k, d) {
        if (k === "turn") vol = Ring.clamp(vol + d * 2, 0, 100);
        if (k === "press") upd("phone", function (p) { return { mic: !p.mic }; });
        if (k === "hold") endCall("Call ended");
    }
    function o_audio(s, k, d) {
        var src = audio;
        if (k === "turn") { vol = Ring.clamp(vol + d * 2, 0, 100); muted = false; }
        if (k === "press") {
            if (src === "media") upd("media", function (m) { return { playing: !m.playing }; });
            else muted = !muted;
        }
        if (k === "hold") {
            if (src === "media") {
                var i = (media.idx + 1) % D.TRACKS.length;
                upd("media", { idx: i, sel: i, pos: 0, playing: true });
                toast("Next · " + D.TRACKS[i][0]);
            } else seek();
        }
    }
    function o_launcher(s, k, d) {
        if (k === "turn") lsel = Ring.clamp(lsel + d, 0, D.APPS.length - 1);
        if (k === "press") openApp(D.APPS[lsel].id);
        if (k === "hold") launcher = false;
    }
    function o_climate(s, k, d) { climTurn(s ? "pas" : "drv", s, k, d, clim.mode[s]); }
    function o_climateR(s, k, d) {
        var z = czone ? "pas" : "drv";
        if (k === "press") { czone = czone ? 0 : 1; return; }
        if (k === "turn") climTurn(z, czone, k, d, 0);
    }
    function climTurn(z, s, k, d, m) {
        if (k === "turn") {
            var C = clim, Z = Object.assign({}, C[z]), sync = C.sync, auto = C.auto;
            if (m === 0) { Z.t = Ring.clamp(Z.t + d, 60, 84); if (z === "pas") sync = false; }
            if (m === 1) { Z.fan = Ring.clamp(Z.fan + d, 0, 7); auto = false; }
            if (m === 2) Z.seat = Ring.clamp(Z.seat + d, -3, 3);
            var nc = Object.assign({}, C, { sync: sync, auto: auto });
            nc[z] = Z;
            if (m === 0 && z === "drv" && sync) nc.pas = Object.assign({}, C.pas, { t: Z.t });
            clim = nc;
        }
        if (k === "press") upd("clim", function (c) { return { mode: c.mode.map(function (v, i) { return i === s ? (v + 1) % 3 : v; }) }; });
        if (k === "hold") {
            if (s === 0) { var a = clim.auto; upd("clim", { auto: !a }); toast(a ? "Auto off" : "Auto on"); }
            else {
                var sy = clim.sync;
                upd("clim", function (c) { return { sync: !c.sync, pas: !c.sync ? Object.assign({}, c.pas, { t: c.drv.t }) : c.pas }; });
                toast(sy ? "Sync off" : "Passenger synced to driver");
            }
        }
    }
    // Touch: the same temperature step the zone's knob makes, sync included
    function nudgeTemp(z, d) { climTurn(z, z === "pas" ? 1 : 0, "turn", d, 0); }
    function setZone(z, patch) { upd("clim", function (c) { var o = {}; o[z] = Object.assign({}, c[z], patch); return o; }); }
    function toggleClim(k) {
        upd("clim", function (c) {
            var n = {}; n[k] = !c[k];
            if (k === "sync" && !c.sync) n.pas = Object.assign({}, c.pas, { t: c.drv.t });
            return n;
        });
    }
    function a_radio(s, k, d) {
        if (k === "turn") radioTune(d);
        if (k === "press") seek();
        if (k === "hold") savePreset(radio.ptr);
    }
    // Radio actions, shared by the right knob and the screen
    function radioTune(d) {
        upd("radio", function (r) {
            return r.band === "FM" ? { fm: Math.round(Ring.clamp(r.fm + d * 0.2, 87.9, 107.9) * 10) / 10 }
                                   : { am: Ring.clamp(r.am + d * 10, 530, 1700) };
        });
    }
    function savePreset(i) {
        var R = radio, p = R.presets.slice();
        p[i] = { b: R.band, f: R.band === "FM" ? R.fm : R.am };
        upd("radio", { presets: p, ptr: (i + 1) % 6 });
        toast("Saved to preset " + (i + 1));
    }
    function a_media(s, k, d) {
        if (k === "turn") upd("media", function (m) { return { sel: Ring.clamp(m.sel + d, 0, D.TRACKS.length - 1) }; });
        if (k === "press") upd("media", function (m) { return { idx: m.sel, pos: 0, playing: true }; });
        if (k === "hold") { var sh = media.shuffle; upd("media", { shuffle: !sh }); toast(sh ? "Shuffle off" : "Shuffle on"); }
    }
    // The map view listens to these; it owns the camera
    signal mapEase(var camera, int duration)
    // ---------------------------------------------------- destination search
    function openSearch() { navSearch = { q: "", sel: 0, preview: null }; }
    function closeSearch() { navSearch = null; }
    function searchSet(patch) { navSearch = Object.assign({}, navSearch, patch); }
    function searchKey(ch) { if (navSearch && gear === "P") searchSet({ q: navSearch.q + ch, sel: 0, preview: null }); }
    function searchBackspace() { if (navSearch) searchSet({ q: navSearch.q.slice(0, -1), sel: 0, preview: null }); }
    function searchClear() { if (navSearch) searchSet({ q: "", sel: 0, preview: null }); }
    function findPlace(n) { return D.PLACES.find(function (p) { return p.n === n; }); }
    // With no query: saved places then recents. Otherwise matching places, nearest first.
    function searchRows() {
        if (!navSearch) return [];
        var q = navSearch.q.trim().toLowerCase();
        if (q === "")
            return D.SAVED_PLACES.map(function (p) { return Object.assign({ group: "Saved" }, p); })
                .concat(D.RECENT_DEST.map(function (n) { return Object.assign({ group: "Recent", icon: "time-outline" }, findPlace(n)); }));
        return D.PLACES.filter(function (p) { return (p.n + " " + p.a).toLowerCase().indexOf(q) >= 0; })
                       .sort(function (a, b) { return a.mi - b.mi; }).slice(0, 7);
    }
    function searchPick(i) {
        var r = searchRows()[i];
        if (r) searchSet({ sel: i, preview: r });
    }
    function startRoute(place) {
        var st = D.STEPS.map(function (x) { return Object.assign({}, x); });
        st[st.length - 1].st = place.n;
        steps = st;
        dest = place;
        routeActive = true;
        upd("nav", { step: 0, dist: st[0].d });
        mapEase({ center: D.MAPC }, 400);
        closeSearch();
        toast("Route to " + place.n);
    }
    function endRoute() { routeActive = false; guideUntil = 0; toast("Route ended"); }
    function routeMiles() { return nav.dist + steps.slice(nav.step + 1).reduce(function (a, x) { return a + x.d; }, 0); }

    function a_nav(s, k, d) {
        if (navSearch) {
            var R = searchRows();
            if (navSearch.preview) {
                if (k === "press") startRoute(navSearch.preview);
                if (k === "hold") searchSet({ preview: null });
                return;
            }
            if (k === "turn") searchSet({ sel: Ring.clamp(navSearch.sel + d, 0, Math.max(0, R.length - 1)) });
            if (k === "press") searchPick(navSearch.sel);
            if (k === "hold") closeSearch();
            return;
        }
        if (k === "turn") navZoom(d);
        if (k === "press") navRecenter();
        if (k === "hold") navToggleView();
    }
    // Map actions, shared by the right knob and the on-screen buttons
    function navZoom(d) { var z = Ring.clamp(nav.zoom + d, 10, 18); upd("nav", { zoom: z }); mapEase({ zoom: z }, 200); }
    function navRecenter() { mapEase({ center: D.MAPC }, 300); toast("Recentered"); }
    function navToggleView() {
        var v = nav.view === "2D" ? "3D" : "2D";
        upd("nav", { view: v }); mapEase({ pitch: v === "3D" ? 55 : 0 }, 300); toast(v + " view");
    }
    function phoneList() {
        return D.FAVS.map(function (c) { return { c: c, fav: true }; })
            .concat(D.RECENTS.map(function (r) { return { c: r.c, fav: false }; }));
    }
    function a_phone(s, k, d) {
        if (phone.call) { if (k !== "turn") endCall("Call ended"); return; }
        var L = phoneList();
        if (k === "turn") upd("phone", function (p) { return { sel: Ring.clamp(p.sel + d, 0, L.length - 1) }; });
        if (k === "press") startCall(L[phone.sel].c);
    }
    function a_level(s, k, d) {
        if (gear !== "P") { if (k !== "turn") toast("Shift to Park to level"); return; }
        if (k === "turn") levelTrim(level.axis, d);
        if (k === "press") { upd("level", { auto: true, jacks: "Down" }); toast("Auto leveling · jacks down"); }
        if (k === "hold") upd("level", function (L) { return { axis: L.axis === "pitch" ? "roll" : "pitch" }; });
    }
    function levelTrim(ax, d) {
        if (gear !== "P") { toast("Shift to Park to level"); return; }
        upd("level", function (L) { var o = { auto: false, axis: ax }; o[ax] = Math.round(Ring.clamp(L[ax] + d * 0.1, -5, 5) * 10) / 10; return o; });
    }
    function autoLevel() {
        if (gear !== "P") { toast("Shift to Park to level"); return; }
        upd("level", { auto: true, jacks: "Down" });
    }
    function a_cams(s, k, d) {
        if (k === "turn") upd("cams", function (c) { return { sel: Ring.clamp(c.sel + d, 0, 3) }; });
        if (k === "press") upd("cams", function (c) { return { guides: !c.guides }; });
    }

    // ------------------------------------- generic list + edit (vehicle screens)
    function rows(a) {
        if (a === "energy") {
            var E = energy;
            return E.circuits.map(function (c, i) {
                return { n: c.n, kind: "sw", on: c.on, v: c.w + " W",
                         toggle: function () { upd("energy", function (e) { return { circuits: e.circuits.map(function (x, j) { return j === i ? Object.assign({}, x, { on: !x.on }) : x; }) }; }); } };
            }).concat([
                { n: "Charge Limit", kind: "val", v: E.limit + " A", frac: (E.limit - 10) / 50, c: Ring.AMBER,
                  adj: function (d) { upd("energy", function (e) { return { limit: Ring.clamp(e.limit + d * 2, 10, 60) }; }); } },
                { n: "Inverter", kind: "sw", on: E.inverter, v: "", toggle: function () { upd("energy", function (e) { return { inverter: !e.inverter }; }); } },
                { n: "Shore Power", kind: "sw", on: E.shore, v: "", toggle: function () { upd("energy", function (e) { return { shore: !e.shore }; }); } }
            ]);
        }
        if (a === "water") {
            var W = water;
            return [
                { n: "Water Pump", kind: "sw", on: W.pump, v: "", toggle: function () { upd("water", function (w) { return { pump: !w.pump }; }); } },
                { n: "Water Heater", kind: "sw", on: W.heater, v: "", toggle: function () { upd("water", function (w) { return { heater: !w.heater }; }); } },
                { n: "Heater Setpoint", kind: "val", v: W.set + "°F", frac: (W.set - 100) / 40, c: Ring.RED,
                  adj: function (d) { upd("water", function (w) { return { set: Ring.clamp(w.set + d * 2, 100, 140) }; }); } }
            ];
        }
        if (a === "lights") {
            return lights.zones.map(function (z, i) {
                return { n: z.n, kind: "swval", on: z.on, v: z.on ? z.b + "%" : "Off", frac: z.on ? z.b / 100 : 0, c: Ring.WARMW,
                         toggle: function () { toggleZone(i); },
                         adj: function (d) {
                             upd("lights", function (L) {
                                 return { zones: L.zones.map(function (q, j) {
                                     if (j !== i) return q;
                                     var b = Ring.clamp((q.on ? q.b : 0) + d * 5, 0, 100);
                                     return Object.assign({}, q, { b: b || q.b, on: b > 0 });
                                 }) };
                             });
                         } };
            });
        }
        return [];
    }
    function toggleZone(i) {
        upd("lights", function (L) { return { zones: L.zones.map(function (q, j) { return j === i ? Object.assign({}, q, { on: !q.on }) : q; }) }; });
    }
    function selectRow(a, j) { var o = Object.assign({}, rsel); o[a] = j; rsel = o; edit = false; }
    function listAct(a, k, d) {
        var R = rows(a), i = rsel[a] || 0, r = R[i];
        if (edit) { if (k === "turn") r.adj(d); else edit = false; return; }
        if (k === "turn") { selectRow(a, Ring.clamp(i + d, 0, R.length - 1)); return; }
        if (k === "press") { if (r.kind === "val") edit = true; else r.toggle(); }
        if (k === "hold" && r.kind === "swval") edit = true;
    }
    function a_energy(s, k, d) { listAct("energy", k, d); }
    function a_water(s, k, d) { listAct("water", k, d); }
    function a_lights(s, k, d) { listAct("lights", k, d); }
    // ------------------------------------------------------------- Settings
    // Settings is a stack of pages. Each page is a list of rows; the right
    // knob turns through them, presses to act on one and holds to go back.
    // Row kinds: page (opens another page), sw (toggle), val and choice
    // (press, then turn to adjust), action, net (Wi-Fi network), phone, info.
    // CAN 1 / CAN 2 port settings
    property var can: [
        { role: "Vehicle", fd: false, rate: 500000, drate: 2000000, listen: true },
        { role: "TrailCurrent", fd: false, rate: 500000, drate: 2000000, listen: false }
    ]

    function resetSet() { setStack = ["root"]; setArgs = [null]; confirmBox = null; join = null; pairing = null; }
    // The argument list changes before the page stack on the way in, and after
    // it on the way out, so anything reading setPage always has its argument
    function openSet(page, arg) {
        setArgs = setArgs.concat([arg === undefined ? null : arg]);
        setStack = setStack.concat([page]);
        selSet(0);
        if (page === "wifiJoin") {
            var n = wifi.nets.find(function (x) { return x.ssid === arg; });
            join = { ssid: arg, sec: n ? n.sec : "WPA2", pw: "", show: false, busy: false };
        }
        if (page === "btPair") startPairing();
        if (page === "wifi" && Net.available && wifi.on) Net.scan();
    }
    function backSet() {
        if (setStack.length < 2) return;
        if (setPage === "wifiJoin") join = null;
        if (setPage === "btPair") { pairing = null; pairTimer.stop(); }
        setStack = setStack.slice(0, -1);
        setArgs = setArgs.slice(0, -1);
        edit = false;
    }
    function selSet(i) { var o = Object.assign({}, setSel); o[setPage] = i; setSel = o; edit = false; }
    function setTitle(page, arg) {
        if (page === "wifiNet") return arg;
        if (page === "btPhone") return bt.phones[arg] ? bt.phones[arg].name : "Phone";
        if (page === "canPort") return "CAN " + (arg + 1);
        return { root: "Settings", wifi: "Wi-Fi", wifiJoin: "Join Network", bt: "Bluetooth & Phones", btPair: "Pair New Phone",
                 can: "CAN Buses", display: "Display", sound: "Sound", navset: "Navigation", units: "Units, Date & Time",
                 system: "System" }[page] || "Settings";
    }

    // Row builders
    function rowVal(n, key, min, max, step, fmt, owner) {
        var x = owner ? hmi[owner] : prefs[key];
        return { kind: "val", n: n, v: fmt(x), frac: (x - min) / (max - min), c: Ring.GREEN,
                 adj: function (d) {
                     var y = Ring.clamp((owner ? hmi[owner] : prefs[key]) + d * step, min, max);
                     if (owner) hmi[owner] = y; else { var o = {}; o[key] = y; upd("prefs", o); }
                 } };
    }
    function rowSw(n, on, set, desc) { return { kind: "sw", n: n, on: on, v: on ? "On" : "Off", desc: desc || "", toggle: function () { set(!on); } }; }
    function rowChoice(n, opts, cur, set, label) {
        var i = Math.max(0, opts.indexOf(cur));
        return { kind: "choice", n: n, v: label ? label(cur) : String(cur), frac: opts.length > 1 ? i / (opts.length - 1) : 0, c: Ring.GREEN,
                 opts: opts.map(function (o) { return label ? label(o) : String(o); }), idx: i,
                 adj: function (d) { set(opts[(i + d + opts.length) % opts.length]); } };
    }
    function pref(key) { return function (v) { var o = {}; o[key] = v; upd("prefs", o); }; }
    function signed(x) { return (x > 0 ? "+" : "") + x; }

    function setRows(page, arg) {
        var rows = [];
        switch (page) {
        case "root":
            return D.SET_SECTIONS.map(function (sec) { return { kind: "page", n: sec.n, icon: sec.icon, page: sec.page, v: setSummary(sec.page) }; });
        case "wifi":
            rows.push(rowSw("Wi-Fi", wifi.on, function (v) { wifiSetOn(v); }));
            if (!wifi.on) return rows;
            wifi.nets.slice().sort(function (a, b) {
                return (b.ssid === wifi.ssid) - (a.ssid === wifi.ssid) || (b.saved - a.saved) || (b.sig - a.sig);
            }).forEach(function (n) {
                var connected = n.ssid === wifi.ssid;
                rows.push({ kind: "net", n: n.ssid, sig: n.sig, sec: n.sec, connected: connected, saved: n.saved,
                            v: connected ? "Connected" : n.saved ? "Saved" : n.sec === "Open" ? "Open" : n.sec,
                            act: function () {
                                if (connected || n.saved) openSet("wifiNet", n.ssid);
                                else if (n.sec === "Open") wifiConnect(n.ssid);
                                else openSet("wifiJoin", n.ssid);
                            } });
            });
            rows.push({ kind: "action", n: wifi.scanning ? "Scanning…" : "Scan Again", icon: "refresh-outline", v: "", act: function () { wifiScan(); } });
            return rows;
        case "wifiNet": {
            var net = wifi.nets.find(function (x) { return x.ssid === arg; }) || { ssid: arg, sig: 0, sec: "", saved: false };
            var here = net.ssid === wifi.ssid, auto = wifi.noAutoJoin.indexOf(net.ssid) < 0;
            rows.push({ kind: "info", n: "Status", v: here ? "Connected" : "Not connected" });
            rows.push({ kind: "info", n: "Signal", v: Math.round(net.sig * 100) + "%" });
            rows.push({ kind: "info", n: "Security", v: net.sec });
            rows.push(rowSw("Join Automatically", auto, function (v) {
                wifiSetAutoJoin(net.ssid, v);
            }));
            rows.push(here ? { kind: "action", n: "Disconnect", v: "", act: function () { wifiDisconnect(net.ssid); } }
                           : { kind: "action", n: "Join", v: "", act: function () { wifiConnect(net.ssid); } });
            if (net.saved)
                rows.push({ kind: "action", n: "Forget Network", danger: true, v: "", act: function () {
                    askConfirm("Forget " + net.ssid + "?", "Godwit will no longer join this network automatically.", "Forget", function () {
                        wifiForget(net.ssid);
                        backSet();
                        toast("Forgot " + net.ssid);
                    });
                } });
            return rows;
        }
        case "bt":
            rows.push(rowSw("Bluetooth", bt.on, function (v) { upd("bt", { on: v }); toast(v ? "Bluetooth on" : "Bluetooth off"); }));
            if (!bt.on) return rows;
            bt.phones.forEach(function (ph, i) {
                rows.push({ kind: "phone", n: ph.name, v: (ph.connected ? "Connected" : "Paired") + (ph.primary ? " · Primary" : ""),
                            connected: ph.connected, primary: ph.primary, act: function () { openSet("btPhone", i); } });
            });
            rows.push({ kind: "page", n: "Pair New Phone", icon: "phone-portrait-outline", page: "btPair", v: "" });
            rows.push({ kind: "info", n: "Visible As", v: "Godwit" });
            return rows;
        case "btPhone": {
            var ph = bt.phones[arg];
            if (!ph) return [{ kind: "info", n: "Phone", v: "Removed" }];
            var setPh = function (patch) { upd("bt", { phones: bt.phones.map(function (x, j) { return j === arg ? Object.assign({}, x, patch) : x; }) }); };
            rows.push({ kind: "action", n: ph.connected ? "Disconnect" : "Connect", v: "", act: function () {
                setPh({ connected: !ph.connected }); toast((ph.connected ? "Disconnected " : "Connected ") + ph.name);
            } });
            rows.push(rowSw("Primary Phone", ph.primary, function (v) {
                upd("bt", { phones: bt.phones.map(function (x, j) { return Object.assign({}, x, { primary: j === arg ? v : (v ? false : x.primary) }); }) });
            }, "Calls and messages go to this phone when two are connected"));
            rows.push(rowSw("Sync Contacts", ph.contacts, function (v) { setPh({ contacts: v }); }));
            rows.push({ kind: "info", n: "Model", v: ph.model });
            rows.push({ kind: "action", n: "Forget Phone", danger: true, v: "", act: function () {
                askConfirm("Forget " + ph.name + "?", "You will need to pair it again to use it in the vehicle.", "Forget", function () {
                    upd("bt", { phones: bt.phones.filter(function (x, j) { return j !== arg; }) });
                    backSet();
                    toast("Forgot " + ph.name);
                });
            } });
            return rows;
        }
        case "can":
            return can.map(function (c, i) { return { kind: "page", n: "CAN " + (i + 1), icon: "git-network-outline", page: "canPort", arg: i, v: canSummary(c) }; });
        case "canPort": {
            var cp = can[arg];
            if (!cp) return rows;
            rows.push(rowChoice("Connected To", D.CAN_ROLES, cp.role, function (v) { setCanRole(arg, v); },
                                function (r) { return r === "Vehicle" ? "Vehicle bus" : r === "TrailCurrent" ? "TrailCurrent bus" : "Not used"; }));
            if (cp.role === "Off") return rows;
            rows.push(rowChoice("Protocol", [false, true], cp.fd, function (v) { setCan(arg, { fd: v }); },
                                function (fd) { return fd ? "CAN FD" : "Classic CAN"; }));
            rows.push(rowChoice(cp.fd ? "Arbitration Rate" : "Bit Rate", D.CAN_RATES, cp.rate, function (v) { setCan(arg, { rate: v }); }, D.fmtRate));
            if (cp.fd)
                rows.push(rowChoice("Data Rate", D.CAN_DATA_RATES, cp.drate, function (v) { setCan(arg, { drate: v }); }, D.fmtRate));
            rows.push(rowSw("Listen Only", cp.listen, function (v) { setCan(arg, { listen: v }); },
                            "Receive without ever transmitting. Recommended on the vehicle bus."));
            rows.push({ kind: "info", n: "Status", v: "Active · " + (cp.role === "Vehicle" ? "1,240" : "180") + " frames/s" });
            return rows;
        }
        case "display":
            return [
                rowVal("Screen Brightness", "bright", 10, 100, 5, function (x) { return x + "%"; }),
                rowVal("Ring Brightness", "ring", 10, 100, 5, function (x) { return x + "%"; }),
                rowChoice("Appearance", ["Light", "Dark", "Auto"], prefs.appearance, function (v) { upd("prefs", { appearance: v }); applyAppearance(); }),
                rowSw("Auto Dim at Night", prefs.autodim, pref("autodim"))
            ];
        case "sound":
            return [
                rowVal("Media Volume", "", 0, 100, 2, function (x) { return x + "%"; }, "vol"),
                rowVal("Navigation Volume", "", 0, 100, 2, function (x) { return x + "%"; }, "gvol"),
                rowVal("Phone Volume", "phoneVol", 0, 100, 2, function (x) { return x + "%"; }),
                rowVal("Balance", "bal", -10, 10, 1, function (x) { return x === 0 ? "Center" : (x < 0 ? "L " : "R ") + Math.abs(x); }),
                rowVal("Fader", "fader", -10, 10, 1, function (x) { return x === 0 ? "Center" : (x < 0 ? "Front " : "Rear ") + Math.abs(x); }),
                rowVal("Bass", "bass", -6, 6, 1, signed),
                rowVal("Mid", "mid", -6, 6, 1, signed),
                rowVal("Treble", "treble", -6, 6, 1, signed),
                rowSw("Touch Sounds", prefs.touchSound, pref("touchSound"))
            ];
        case "navset":
            return [
                rowSw("Voice Guidance", nav.voice, function (v) { upd("nav", { voice: v }); }),
                rowSw("Avoid Highways", prefs.avoidHwy, pref("avoidHwy")),
                rowSw("Avoid Tolls", prefs.avoidTolls, pref("avoidTolls")),
                { kind: "info", n: "Map Region", v: Config.mapInstalled ? "Installed" : "None" },
                { kind: "action", n: "Update Maps from USB", icon: "download-outline", v: "",
                  act: function () { toast("No map bundle found on USB"); } }
            ];
        case "units":
            return [
                rowChoice("Temperature", ["F", "C"], prefs.tempUnit, pref("tempUnit"), function (u) { return "°" + u; }),
                rowChoice("Distance", ["mi", "km"], prefs.distUnit, pref("distUnit"), function (u) { return u === "mi" ? "Miles" : "Kilometres"; }),
                rowChoice("Liquids", ["gal", "L"], prefs.volUnit, pref("volUnit"), function (u) { return u === "gal" ? "Gallons" : "Litres"; }),
                rowChoice("Clock", [false, true], prefs.clock24, pref("clock24"), function (b) { return b ? "24-hour" : "12-hour"; }),
                rowSw("Set Time Automatically", prefs.autoTime, pref("autoTime"), "From GPS and network time"),
                rowChoice("Time Zone", D.TIMEZONES, prefs.tz, pref("tz"))
            ];
        case "system":
            return [
                { kind: "info", n: "Software", v: "Godwit 0.1.0" },
                { kind: "info", n: "Platform", v: "AGL 22.0.0" },
                { kind: "action", n: "Check for Updates", icon: "download-outline", v: "", act: function () { toast("Godwit is up to date"); } },
                { kind: "info", n: "Device Name", v: "Godwit" },
                { kind: "action", n: "Restart", icon: "power-outline", v: "", act: function () {
                    askConfirm("Restart Godwit?", "The screen goes dark for a few seconds.", "Restart", function () { toast("Restarting"); });
                } },
                { kind: "action", n: "Factory Reset", danger: true, v: "", act: function () {
                    askConfirm("Reset to factory settings?", "Removes Wi-Fi networks, paired phones and all settings. Maps stay installed.", "Reset",
                               function () { toast("Settings reset"); });
                } }
            ];
        }
        return rows;
    }
    function setSummary(page) {
        switch (page) {
        case "wifi": return !wifi.on ? "Off" : wifi.ssid || "Not connected";
        case "bt": {
            if (!bt.on) return "Off";
            var c = bt.phones.find(function (p) { return p.connected; });
            return c ? c.name : bt.phones.length + " paired";
        }
        case "can": return can.map(function (c, i) { return (i + 1) + ": " + (c.role === "Off" ? "Off" : c.role); }).join(" · ");
        case "display": return prefs.appearance;
        case "sound": return "Volume " + vol + "%";
        case "navset": return nav.voice ? "Voice on" : "Voice off";
        case "units": return "°" + prefs.tempUnit + " · " + (prefs.distUnit === "mi" ? "Miles" : "Km") + " · " + (prefs.clock24 ? "24h" : "12h");
        case "system": return "Godwit 0.1.0";
        }
        return "";
    }
    function canSummary(c) {
        if (c.role === "Off") return "Not used";
        return c.role + " · " + (c.fd ? "FD " + D.fmtRate(c.rate) + " / " + D.fmtRate(c.drate) : D.fmtRate(c.rate)) + (c.listen ? " · Listen only" : "");
    }
    function setCan(i, patch) {
        can = can.map(function (c, j) { return j === i ? Object.assign({}, c, patch) : c; });
        if (!("listen" in patch)) toast("CAN " + (i + 1) + " restarted · " + canSummary(can[i]));
    }
    // A bus can only be on one port: picking a role the other port has swaps them
    function setCanRole(i, role) {
        var other = 1 - i, swapped = role !== "Off" && can[other].role === role;
        can = can.map(function (c, j) {
            if (j === i) return Object.assign({}, c, { role: role, listen: role === "Vehicle" ? true : c.listen });
            if (j === other && swapped) return Object.assign({}, c, { role: can[i].role, listen: can[i].role === "Vehicle" });
            return c;
        });
        toast(swapped ? "Swapped: CAN " + (i + 1) + " is " + role + ", CAN " + (other + 1) + " is " + can[other].role
                      : "CAN " + (i + 1) + " set to " + role);
    }

    function askConfirm(title, body, ok, act) { confirmBox = { title: title, body: body, ok: ok, act: act }; }
    function doConfirm() { var c = confirmBox; confirmBox = null; if (c) c.act(); }

    // Wi-Fi actions: ConnMan when it is there, the demo networks otherwise
    function wifiSetOn(v) {
        console.info("Wi-Fi", v ? "on" : "off", Net.available ? "" : "(demo, ConnMan not reachable)");
        if (Net.available) Net.setWifiPowered(v); else upd("demoWifi", { on: v });
        toast(v ? "Wi-Fi on" : "Wi-Fi off");
    }
    function wifiScan() {
        if (Net.available) Net.scan();
        toast("Scanning for networks");
    }
    function wifiConnect(ssid, passphrase) {
        toast("Connecting to " + ssid);
        if (Net.available) { Net.connectTo(ssid, passphrase || ""); return; }
        connectTimer.ssid = ssid;
        connectTimer.restart();
    }
    function wifiDisconnect(ssid) {
        if (Net.available) Net.disconnectFrom(ssid); else upd("demoWifi", { ssid: "" });
        toast("Disconnected from " + ssid);
    }
    function wifiForget(ssid) {
        if (Net.available) { Net.forget(ssid); return; }
        upd("demoWifi", { ssid: demoWifi.ssid === ssid ? "" : demoWifi.ssid,
                          nets: demoWifi.nets.map(function (x) { return x.ssid === ssid ? Object.assign({}, x, { saved: false }) : x; }) });
    }
    function wifiSetAutoJoin(ssid, on) {
        if (Net.available) { Net.setAutoConnect(ssid, on); return; }
        var off = demoWifi.noAutoJoin;
        upd("demoWifi", { noAutoJoin: on ? off.filter(function (x) { return x !== ssid; }) : off.concat([ssid]) });
    }
    function wifiConnected(ssid) {
        if (setPage === "wifiJoin") backSet();
        join = null;
        toast("Connected to " + ssid);
    }
    function wifiFailed(ssid, reason) {
        if (join) join = Object.assign({}, join, { busy: false, pw: reason === "invalid-key" ? "" : join.pw });
        toast(reason === "invalid-key" ? "Wrong password for " + ssid : "Couldn't connect to " + ssid);
    }
    readonly property Connections netEvents: Connections {
        target: Net
        function onConnected(ssid) { hmi.wifiConnected(ssid); }
        function onFailed(ssid, reason) { hmi.wifiFailed(ssid, reason); }
    }
    // Demo: a join that always succeeds after a moment
    readonly property Timer connectTimer: Timer {
        property string ssid
        interval: 1500
        onTriggered: {
            hmi.upd("demoWifi", { ssid: ssid, nets: hmi.demoWifi.nets.map(function (x) { return x.ssid === ssid ? Object.assign({}, x, { saved: true }) : x; }) });
            hmi.wifiConnected(ssid);
        }
    }
    function joinKey(ch) { if (join && !join.busy) join = Object.assign({}, join, { pw: join.pw + ch }); }
    function joinBackspace() { if (join && !join.busy) join = Object.assign({}, join, { pw: join.pw.slice(0, -1) }); }
    function joinCanConnect() { return !!join && !join.busy && gear === "P" && join.pw.length >= 8; }
    function joinConnect() {
        if (gear !== "P") { toast("Shift to Park to enter a password"); return; }
        if (!joinCanConnect()) { toast("Password needs at least 8 characters"); return; }
        join = Object.assign({}, join, { busy: true });
        wifiConnect(join.ssid, join.pw);
    }

    // Phone pairing: visible, then a phone asks, then both sides confirm a code
    function startPairing() { pairing = { stage: "search" }; pairTimer.restart(); }
    readonly property Timer pairTimer: Timer {
        interval: 3000
        onTriggered: {
            var code = String(100000 + Math.floor(Math.random() * 900000));
            hmi.pairing = { stage: "confirm", phone: D.PAIRING_PHONE, code: code.slice(0, 3) + " " + code.slice(3) };
        }
    }
    function pairAccept() {
        if (!pairing || pairing.stage !== "confirm") return;
        var ph = pairing.phone;
        pairing = Object.assign({}, pairing, { stage: "busy" });
        pairDone.phone = ph;
        pairDone.restart();
    }
    readonly property Timer pairDone: Timer {
        property var phone
        interval: 1200
        onTriggered: {
            var first = hmi.bt.phones.length === 0;
            hmi.upd("bt", { phones: hmi.bt.phones.concat([{ name: phone.name, model: phone.model, connected: true, primary: first, contacts: true }]) });
            hmi.pairing = null;
            hmi.backSet();
            hmi.toast("Paired with " + phone.name);
        }
    }

    function setActivate(r) {
        switch (r.kind) {
        case "page": openSet(r.page, r.arg); break;
        case "sw": r.toggle(); break;
        case "val": case "choice": edit = true; break;
        case "action": case "net": case "phone": r.act(); break;
        }
    }
    function a_settings(s, k, d) {
        if (confirmBox) { if (k === "press") doConfirm(); else if (k === "hold") confirmBox = null; return; }
        if (setPage === "wifiJoin") { if (k === "press") joinConnect(); else if (k === "hold") backSet(); return; }
        if (setPage === "btPair") { if (k === "press") pairAccept(); else if (k === "hold") backSet(); return; }
        var R = setRows(setPage, setArg), i = Math.min(setSel[setPage] || 0, R.length - 1), r = R[i];
        if (edit) { if (k === "turn") r.adj(d); else edit = false; return; }
        if (k === "turn") { selSet(Ring.clamp(i + d, 0, R.length - 1)); return; }
        if (k === "press") setActivate(r);
        if (k === "hold") backSet();
    }
    // Touch: the first tap selects a row, a second tap acts on it
    function setTap(i) {
        var R = setRows(setPage, setArg), cur = Math.min(setSel[setPage] || 0, R.length - 1);
        if (i !== cur) { selSet(i); return; }
        var r = R[i];
        if (r.kind === "val") edit = !edit;
        else if (r.kind === "choice") r.adj(1);
        else setActivate(r);
    }

    // ------------------------------------------------------------ 1 s tick
    readonly property Timer tickTimer: Timer {
        interval: 1000; repeat: true; running: true
        onTriggered: hmi.tick()
    }
    // Wakes bindings when a spoken prompt's knob borrow ends
    readonly property Timer guideTimer: Timer { interval: 3600; onTriggered: hmi.clock = Date.now() }
    function tick() {
        if (media.playing) {
            var dur = D.TRACKS[media.idx][2], pos = media.pos + 1, idx = media.idx;
            if (pos >= dur) { pos = 0; idx = (idx + 1) % D.TRACKS.length; }
            upd("media", { pos: pos, idx: idx });
        }
        if (gear === "D" && routeActive) {
            var N = nav, dist = Math.round((N.dist - 0.02) * 100) / 100, step = N.step;
            var cross = function (t) { return N.dist > t && dist <= t; };
            if (N.voice && steps[step].dir !== "arrive" && (cross(0.5) || cross(0.1)) && !phone.call) {
                var Dr = D.DIRS[steps[step].dir];
                guideUntil = Date.now() + 3500; guideTimer.restart();
                guideTxt = Dr.l + (cross(0.1) ? " now" : " in " + fd(dist)) + " · " + steps[step].st;
            }
            if (dist <= 0) { step = step + 1; if (step >= steps.length - 1) step = 0; dist = steps[step].d; }
            upd("nav", { dist: dist, step: step });
        }
        if (phone.call && phone.call.state === "active")
            upd("phone", { call: Object.assign({}, phone.call, { t: phone.call.t + 1 }) });
        var L = level;
        if (L.auto) {
            var f = function (v) { return Math.abs(v) <= 0.15 ? 0 : Math.round((v - Math.sign(v) * 0.15) * 10) / 10; };
            var p = f(L.pitch), r = f(L.roll);
            L = Object.assign({}, L, { pitch: p, roll: r, auto: !(p === 0 && r === 0) });
        }
        if (L.jacks === "Retracting") {
            var rt = (L.rt || 1) - 1;
            L = Object.assign({}, L, { rt: rt, jacks: rt <= 0 ? "Up" : "Retracting" });
        }
        if (L !== level) level = L;
        if (alert && Date.now() > alert.until) alert = null;
        if (edit && Date.now() - lastInput > 6000) edit = false;
        if (prefs.appearance === "Auto") applyAppearance();
        clock = Date.now();
    }

    // ------------------------------------------------- encoder descriptors
    // What each knob's round display shows and what its ring does. mkEnc() fills
    // defaults; spec is the ring pattern.
    function mkEnc(o) {
        var v = o.value === undefined || o.value === null ? "" : String(o.value);
        return {
            label: o.label || "", icon: o.icon || "", iconSize: v ? 40 : 84,
            value: v, unit: o.unit || "", sub: o.sub || "",
            color: o.color || "tx", small: !!o.small,
            vSize: o.small ? (v.length > 9 ? 26 : v.length > 6 ? 32 : 38) : (v.length > 4 ? 60 : 88),
            pager: o.pager || [], spec: o.spec || null, note: o.note || "",
            idc: o.idc || Ring.GREEN, holdC: o.hold ? (o.holdC || Ring.WHITE) : null,
            name: o.name || o.label || "",
            hint: [o.press ? "Press · " + o.press : "", o.hold ? "Hold · " + o.hold : ""].filter(Boolean).join(" / ")
        };
    }

    function navCue() {
        var N = nav, cur = steps[N.step], Dr = D.DIRS[cur.dir], u = urg(), col = Ring.urgCol(u);
        var spec = function (k, T) {
            if (u > 0.75 && Dr.seq.length) {
                var step = Math.floor((T * 3.2) % (Dr.seq.length + 1)), idx = Dr.seq.indexOf(k);
                if (idx >= 0 && idx < step) return { c: Ring.AMBER, a: 1 };
            }
            if (cur.dir === "arrive") return { c: Ring.LIME, a: 0.3 + 0.5 * Ring.pulse(T, 0.5) };
            var p = u > 0.55 ? 0.5 + 0.5 * Math.abs(Math.sin(T * Math.PI * (1 + u * 3))) : 1;
            var a = Math.max(0, 1 - Ring.ang(Ring.POS[k], Dr.a) / 72);
            return a > 0.02 ? { c: col, a: a * p } : { c: col, a: 0.04 };
        };
        return { D: Dr, u: u, col: col, spec: spec, cur: cur, N: N };
    }

    function clim1(s, z, callMode) {
        var C = clim, cab = C.cabin, Z = C[z], m = callMode ? 0 : C.mode[s], nm = z === "pas" ? "Passenger" : "Driver";
        var pager = callMode ? [] : D.CMODES.map(function (_, i) { return { on: i === m }; });
        var heat = Z.t > cab, cool = Z.t < cab;
        var hold = callMode ? "" : (s ? "Sync to Driver" : "Auto"), press = callMode ? "Switch Zone" : D.CMODES[(m + 1) % 3];
        if (m === 0) {
            var delta = Ring.clamp((Z.t - cab) / 12, -1, 1);
            return mkEnc({ label: nm, value: tempNum(Z.t), unit: "°", sub: heat ? "Heating" : cool ? "Cooling" : "Holding",
                       color: heat ? "heat" : cool ? "cool" : "tx", pager: pager,
                       idc: heat ? Ring.WARM : cool ? Ring.COOL : Ring.NEU, name: nm + " Temp", press: press, hold: hold,
                       note: "Top = cabin " + tempNum(cab) + "° · right LEDs warmer · left cooler",
                       spec: function (k, T) {
                           var c = Ring.gradT(k / 4);
                           if (k === 2) return { c: c, a: 0.45 };
                           var side = k > 2 ? 1 : -1, rank = Math.abs(k - 2);
                           if (Math.sign(delta) !== side) return { c: c, a: 0.06 };
                           var a = Ring.clamp(Math.abs(delta) * 2 - (rank - 1), 0, 1);
                           return { c: c, a: Math.max(0.06, a * (0.7 + 0.3 * Ring.pulse(T, 0.35))) };
                       } });
        }
        if (m === 1)
            return mkEnc({ label: nm + " Fan", value: Z.fan || "Off", sub: Z.fan ? "of 7" : "--", pager: pager,
                       idc: Ring.GREEN, name: nm + " Fan", press: press, hold: hold,
                       note: "Fill = fan speed · sweep speed = airflow",
                       spec: function (k, T) {
                           var f = Ring.fill5(Z.fan / 7, Ring.GREEN)(k);
                           return f.a > 0.1 ? { c: Ring.GREEN, a: f.a * (0.45 + 0.55 * Ring.comet5(k, T, 0.12 + Z.fan * 0.1)) } : f;
                       } });
        var ds = Z.seat / 3;
        return mkEnc({ label: nm + " Seat", value: Z.seat ? Math.abs(Z.seat) : "Off", sub: Z.seat > 0 ? "Heat" : Z.seat < 0 ? "Vent" : "--",
                   color: Z.seat > 0 ? "heat" : Z.seat < 0 ? "cool" : "tx", pager: pager,
                   idc: Z.seat > 0 ? Ring.WARM : Z.seat < 0 ? Ring.COOL : Ring.NEU, name: nm + " Seat", press: press, hold: hold,
                   note: "Right LEDs = heat · left = vent · flicker = active",
                   spec: function (k, T) {
                       var c = Ring.gradT(k / 4);
                       if (k === 2) return { c: Ring.NEU, a: 0.2 };
                       var side = k > 2 ? 1 : -1, rank = Math.abs(k - 2);
                       if (Math.sign(ds) !== side) return { c: c, a: 0.05 };
                       var a = Ring.clamp(Math.abs(ds) * 2 - (rank - 1), 0, 1);
                       return { c: c, a: Math.max(0.05, a * (0.6 + 0.4 * Math.abs(Math.sin(T * 5 + k)))) };
                   } });
    }

    function leftEnc(own, route) {
        var Ph = phone, c;
        switch (own) {
        case "incoming":
            c = D.CONTACTS[Ph.call.who];
            return mkEnc({ label: "Answer", icon: "call", sub: c.n, color: "acc", idc: Ring.LIME, name: "Answer Call", press: "Answer",
                       note: Ph.silent ? "Silenced · press to answer" : "Spinning green = incoming call · turn to silence",
                       spec: function (k, T) { return { c: Ring.LIME, a: 0.08 + 0.92 * Ring.comet5(k, T, Ph.silent ? 0.4 : 1.1) * (Ph.silent ? 0.5 : 1) }; } });
        case "call": {
            c = D.CONTACTS[Ph.call.who];
            var mic = Ph.mic, v = vol;
            return mkEnc({ label: mic ? "Call" : "Mic Muted", icon: mic ? "call" : "mic-off", value: v, sub: c.n,
                       color: mic ? "tx" : "heat", idc: mic ? Ring.LIME : Ring.RED, name: "Call Volume",
                       press: mic ? "Mute Mic" : "Unmute", hold: "End Call", holdC: Ring.RED,
                       note: "Lime fill = call volume · flicker = their voice · red = mic muted",
                       spec: function (k, T) {
                           if (!mic) return { c: Ring.RED, a: k === 2 ? 1 : 0.35 };
                           var f = Ring.fill5(v / 100, Ring.LIME)(k);
                           return k === Math.min(4, Math.floor(v / 20)) ? { c: Ring.LIME, a: f.a * (0.5 + 0.5 * Ring.lvl(T)) } : f;
                       } });
        }
        case "climate": return clim1(0, "drv", false);
        case "silence":
            c = D.CONTACTS[Ph.call.who];
            return mkEnc({ label: "Ringing", icon: "notifications-off", sub: Ph.silent ? "Silenced" : c.n, name: "Silence Ringer",
                       press: "Silence", note: "Answering waits until you leave Reverse" });
        case "guide": {
            var gv = gvol;
            return mkEnc({ label: "Guidance", icon: "navigate", value: gv, sub: D.SRC[audio].n + " ducked", idc: Ring.CYAN,
                       name: "Guidance Volume", press: "Skip Prompt", hold: "Voice Off",
                       note: "Cyan = spoken guidance · fill = guidance volume",
                       spec: function (k, T) {
                           var f = Ring.fill5(gv / 100, Ring.CYAN)(k);
                           return f.a > 0.1 ? { c: Ring.CYAN, a: f.a * (0.6 + 0.4 * Ring.lvl(T)) } : f;
                       } });
        }
        default: {
            var src = audio, S = D.SRC[src], m = muted && src === "radio", playing = src === "media" ? media.playing : !m, vv = vol;
            var volSpec = function (k, T) {
                if (m) return { c: Ring.RED, a: k === 2 ? 0.55 + 0.45 * Ring.pulse(T, 1) : 0.06 };
                var f = Ring.fill5(vv / 100, S.c)(k), lead = Math.min(4, Math.floor(vv / 20));
                return (playing && k === lead) ? { c: S.c, a: f.a * (0.55 + 0.45 * Ring.lvl(T)) }
                                               : (playing ? f : { c: S.c, a: Math.min(f.a, 0.35) });
            };
            var st = sig();
            var nowTxt = src === "media" ? D.TRACKS[media.idx][0] : (st.stn || { n: "--" }).n;
            var base = { label: S.n + " Volume", icon: m ? "volume-mute" : (!playing ? "pause" : ""), value: m ? "" : vv,
                         sub: m ? "Muted" : (!playing ? "Paused" : nowTxt), color: m ? "heat" : "tx", idc: m ? Ring.RED : S.c,
                         name: S.n + " Volume", press: src === "media" ? (media.playing ? "Pause" : "Play") : (m ? "Unmute" : "Mute"),
                         hold: src === "media" ? "Next Track" : "Next Station",
                         note: "Fill = volume in source color · lead LED flickers with audio" };
            var cueActive = (app === "nav" && routeActive) || route.u > 0.5;
            if (cueActive && !recent(0)) {
                var dp = distParts(route.N.dist), arrive = route.cur.dir === "arrive";
                return mkEnc(Object.assign(base, {
                    label: route.D.l, icon: route.D.i,
                    value: arrive ? "" : dp.v, unit: arrive ? "" : dp.u, sub: route.cur.st,
                    color: route.u > 0.6 ? "warn" : "tx", idc: route.col,
                    note: "Ring = next turn · cyan far → amber near · knob = " + S.n.toLowerCase() + " volume",
                    spec: route.spec }));
            }
            return mkEnc(Object.assign(base, { spec: volSpec }));
        }
        }
    }

    function rightEnc(own, route) {
        var Ph = phone, c;
        switch (own) {
        case "incoming":
            c = D.CONTACTS[Ph.call.who];
            return mkEnc({ label: "Decline", icon: "close", sub: c.n, color: "heat", idc: Ring.RED, name: "Decline", press: "Decline",
                       note: "Spinning red = decline",
                       spec: function (k, T) { return { c: Ring.RED, a: 0.06 + 0.6 * Ring.comet5(k, T, -0.7) }; } });
        case "interlock": {
            var j = level.jacks;
            return mkEnc({ label: "Jacks " + j, icon: j === "Retracting" ? "sync" : "warning", sub: j === "Retracting" ? "Stay stopped" : "Retract to drive",
                       color: "heat", idc: Ring.RED, name: "Retract Jacks", press: j === "Down" ? "Retract Jacks" : "",
                       note: "Red pulse = vehicle interlock · amber sweep = retracting" });
        }
        case "launcher": {
            var n = D.APPS.length, a = D.APPS[lsel];
            return mkEnc({ label: "Apps", icon: a.icon, value: a.name, small: true, idc: Ring.WHITE, name: "Browse Apps",
                       press: "Open", hold: "Close", note: "Bright LED = position in the app grid",
                       spec: Ring.mark5(lsel / (n - 1), Ring.WHITE) });
        }
        case "climate": return clim1(1, "pas", false);
        case "climateR": return clim1(czone, czone ? "pas" : "drv", true);
        case "nav": {
            if (navSearch) {
                var SR = searchRows(), sp = navSearch.preview, ssel = Math.min(navSearch.sel, Math.max(0, SR.length - 1));
                if (sp)
                    return mkEnc({ label: "Start Route", value: sp.n, small: true, sub: fd(sp.mi) + " away", idc: Ring.CYAN,
                                   name: "Start Route", press: "Start", hold: "Back", note: "Press to start · hold for the list",
                                   spec: Ring.allC(Ring.CYAN, 0.8) });
                return mkEnc({ label: navSearch.q ? "Results" : "Destinations", value: SR.length ? SR[ssel].n : "No matches", small: true,
                               sub: SR.length ? SR[ssel].a : "Try another search", idc: Ring.WHITE, name: "Choose Destination",
                               press: SR.length ? "Select" : "", hold: "Close", note: "Bright LED = position in the list",
                               spec: Ring.mark5(SR.length > 1 ? ssel / (SR.length - 1) : 0, Ring.WHITE) });
            }
            var N = nav, near = route.u > 0.75 && route.D.seq.length, rD = route.D;
            return mkEnc({ label: "Zoom", value: N.zoom, sub: N.view + " View", idc: Ring.CYAN, name: "Map Zoom",
                       press: "Recenter", hold: "2D / 3D", note: "Fill = zoom · amber sequence = turn signal near a turn",
                       spec: function (k, T) {
                           if (near) {
                               var step = Math.floor((T * 3.2) % (rD.seq.length + 1)), idx = rD.seq.indexOf(k);
                               if (idx >= 0 && idx < step) return { c: Ring.AMBER, a: 1 };
                           }
                           return Ring.fill5((N.zoom - 10) / 8, Ring.CYAN)(k);
                       } });
        }
        case "endcall":
            c = D.CONTACTS[Ph.call.who];
            return mkEnc({ label: "End Call", icon: "close", value: D.fmtT(Ph.call.t), small: true, sub: c.n, color: "heat", idc: Ring.RED,
                       name: "End Call", press: "End Call", note: "Audio is paused for the call · red = end call",
                       spec: function (k, T) { return { c: Ring.RED, a: k === 2 ? 0.9 : 0.25 + 0.15 * Ring.pulse(T, 0.5) }; } });
        case "radio": {
            var Rr = radio, fm = Rr.band === "FM", f = fm ? Rr.fm : Rr.am, S = sig(), pos = fm ? (f - 87.9) / 20 : (f - 530) / 1170;
            var sg = S.sig, col = Ring.sigCol(sg);
            return mkEnc({ label: Rr.band + (fm ? " MHz" : " kHz"), value: fm ? f.toFixed(1) : f, sub: S.stn ? S.stn.n + " · " + S.stn.g : "Searching",
                       idc: col, name: "Tune", press: "Seek", hold: "Save Preset",
                       note: "Crossfade = frequency · color = signal · white flicker = static",
                       spec: function (k, T) {
                           var a = Math.max(0, 1 - Math.abs(pos * 4 - k));
                           if (a > 0.02) return { c: col, a: a * (0.5 + 0.5 * sg) };
                           var nz = Math.max(0, Math.sin(T * 23 + k * 7.1) * Math.sin(T * 17.3 + k * 3.7));
                           return { c: Ring.WHITE, a: (1 - sg) * (0.03 + 0.3 * nz) };
                       } });
        }
        case "media": {
            var M = media, nt = D.TRACKS.length;
            return mkEnc({ label: "Up Next", value: M.sel + 1, unit: "/" + nt, sub: D.TRACKS[M.sel][0], idc: Ring.WHITE,
                       name: "Browse Queue", press: "Play", hold: "Shuffle", note: "Bright LED = position in the queue",
                       spec: Ring.mark5(M.sel / (nt - 1), Ring.WHITE) });
        }
        case "phone": {
            if (Ph.call)
                return mkEnc({ label: "End Call", icon: "close", sub: D.fmtT(Ph.call.t), color: "heat", idc: Ring.RED, name: "End Call",
                           press: "End Call", note: "Red = end call",
                           spec: function (k, T) { return { c: Ring.RED, a: 0.25 + 0.15 * Ring.pulse(T, 0.5) }; } });
            var Ls = phoneList(), it = Ls[Ph.sel];
            c = D.CONTACTS[it.c];
            return mkEnc({ label: it.fav ? "Favorites" : "Recents", value: c.ini, small: true, sub: c.n, idc: Ring.LIME, name: "Contacts",
                       press: "Call " + c.n.split(" ")[0], note: "Bright LED = position in the contact list",
                       spec: Ring.mark5(Ph.sel / (Ls.length - 1), Ring.LIME) });
        }
        case "level": {
            if (gear !== "P")
                return mkEnc({ label: "Leveling", icon: "lock-closed", sub: "Park to level", idc: Ring.NEU, name: "Leveling",
                           note: "Leveling only works in Park", spec: Ring.allC(Ring.NEU, 0.06) });
            var Lv = level, ax = Lv.axis, lv = Lv[ax], ok = Math.abs(lv) < 0.2;
            var okAll = Math.abs(Lv.pitch) < 0.2 && Math.abs(Lv.roll) < 0.2;
            var dir = ax === "pitch" ? (lv > 0 ? 180 : 0) : (lv > 0 ? -90 : 90);
            var lcol = Math.abs(lv) > 2 ? Ring.mix(Ring.AMBER, Ring.RED, 0.5) : Ring.AMBER, lauto = Lv.auto;
            return mkEnc({ label: ax === "pitch" ? "Pitch" : "Roll", value: (lv > 0 ? "+" : "") + lv.toFixed(1), unit: "°",
                       sub: ok ? "Level" : (ax === "pitch" ? (lv > 0 ? "Nose up" : "Nose down") : (lv > 0 ? "Tilted right" : "Tilted left")),
                       color: ok ? "acc" : "warn", idc: ok ? Ring.GREEN : Ring.AMBER, name: "Trim " + (ax === "pitch" ? "Pitch" : "Roll"),
                       press: "Auto Level", hold: "Switch Axis",
                       note: lauto ? "Spinning = auto leveling" : okAll ? "Solid green = level" : "Lit side = low side · brighter = more tilt",
                       spec: function (k, T) {
                           if (lauto) return { c: Ring.AMBER, a: 0.08 + 0.92 * Ring.comet5(k, T, 0.8) };
                           if (okAll) return { c: Ring.GREEN, a: 0.7 + 0.3 * Ring.pulse(T, 0.3) };
                           if (ok) return { c: Ring.GREEN, a: 0.25 };
                           var a = Math.max(0, 1 - Ring.ang(Ring.POS[k], dir) / 90) * Ring.clamp(0.4 + Math.abs(lv) / 3, 0, 1);
                           return a > 0.02 ? { c: lcol, a: a } : { c: lcol, a: 0.04 };
                       } });
        }
        case "cams": {
            var Cm = cams, cm = D.CAMS[Cm.sel];
            return mkEnc({ label: "Camera", value: cm.n, small: true, sub: Cm.guides ? "Guides on" : "Guides off", idc: Ring.WHITE,
                       name: "Camera", press: "Guides", note: "Lit LED = where the camera looks", spec: Ring.dir5(cm.dir, Ring.WHITE) });
        }
        case "settings": {
            var pg = setPage, title = setTitle(pg, setArg);
            if (confirmBox)
                return mkEnc({ label: "Confirm", value: confirmBox.ok, small: true, sub: "Hold to cancel", color: "heat", idc: Ring.AMBER,
                               name: confirmBox.ok, press: confirmBox.ok, hold: "Cancel", note: "Press to confirm · hold to cancel",
                               spec: function (k, T) { return { c: Ring.AMBER, a: k === 2 ? 0.9 : 0.15 + 0.2 * Ring.pulse(T, 0.8) }; } });
            if (pg === "wifiJoin") {
                var ok = joinCanConnect();
                return mkEnc({ label: "Wi-Fi", value: join ? join.ssid : "", small: true,
                               sub: gear !== "P" ? "Park to type" : ok ? "Ready to connect" : "Type password on screen",
                               idc: Ring.CYAN, name: "Join Network", press: "Connect", hold: "Cancel",
                               note: "Passwords are typed on the touchscreen", spec: Ring.allC(Ring.CYAN, ok ? 0.8 : 0.2) });
            }
            if (pg === "btPair") {
                var Pp = pairing || {};
                var conf = Pp.stage === "confirm";
                return mkEnc({ label: "Pair Phone", value: conf ? Pp.code : "", icon: conf ? "" : "bluetooth", small: true,
                               sub: conf ? Pp.phone.name : Pp.stage === "busy" ? "Pairing" : "Waiting for phone",
                               idc: Ring.CYAN, name: "Pair New Phone", press: conf ? "Pair" : "", hold: "Cancel",
                               note: "Spinning blue = visible for pairing",
                               spec: function (k, T) { return { c: Ring.CYAN, a: conf ? (k === 2 ? 1 : 0.3) : 0.08 + 0.92 * Ring.comet5(k, T, 0.6) }; } });
            }
            var Rs2 = setRows(pg, setArg), si = Math.min(setSel[pg] || 0, Rs2.length - 1), sr = Rs2[si];
            if (edit)
                return mkEnc({ label: sr.n, value: sr.v, small: sr.v.length > 5, idc: sr.c || Ring.GREEN, name: "Adjust " + sr.n,
                               press: "Done", note: "Fill = value · press to finish", spec: Ring.fill5(sr.frac, sr.c || Ring.GREEN) });
            var pressTxt = { page: "Open", sw: "Toggle", val: "Adjust", choice: "Change", action: sr.n,
                             net: sr.connected || sr.saved ? "Details" : "Connect", phone: "Options", info: "" }[sr.kind];
            return mkEnc({ label: title, value: sr.n, small: true, sub: sr.v || "", idc: Ring.WHITE, name: title,
                           press: pressTxt, hold: setStack.length > 1 ? "Back" : "",
                           note: "Bright LED = position in the list",
                           spec: Ring.mark5(Rs2.length > 1 ? si / (Rs2.length - 1) : 0, Ring.WHITE) });
        }
        case "energy": case "water": case "lights": {
            var Rs = rows(own), i = rsel[own] || 0, r = Rs[i];
            if (edit)
                return mkEnc({ label: r.n, value: r.v, small: r.v.length > 5, idc: r.c || Ring.GREEN, name: "Adjust " + r.n,
                           press: "Done", note: "Fill = value · press to finish", spec: Ring.fill5(r.frac, r.c || Ring.GREEN) });
            return mkEnc({ label: "Select", value: r.n, small: true,
                       sub: r.kind === "val" ? r.v : (r.on ? "On" : "Off") + (r.v && r.kind !== "sw" ? " · " + r.v : ""),
                       idc: Ring.WHITE, name: D.APPS.find(function (x) { return x.id === own; }).name,
                       press: r.kind === "val" ? "Edit" : "Toggle", hold: r.kind === "swval" ? "Brightness" : "",
                       note: "Bright LED = position in the list",
                       spec: Ring.mark5(Rs.length > 1 ? i / (Rs.length - 1) : 0, Ring.WHITE) });
        }
        }
        return mkEnc({});
    }

    // [left, right] encoder descriptors; re-evaluated whenever state they read changes
    readonly property var encs: {
        void clock;
        var own = owner(), route = navCue();
        var L = leftEnc(own[0], route), R = rightEnc(own[1], route);
        if (gear === "R") {
            var Tn = secs();
            [L, R].forEach(function (e, s) {
                var fn = s ? D.camDistR : D.camDist, side = s ? "right" : "left";
                e.spec = function (k, T) {
                    var dd = fn(T), q = Ring.clamp(1 - (dd - 0.3) / 2.7, 0, 1);
                    var col = q < 0.5 ? Ring.mix(Ring.LIME, Ring.AMBER, q * 2) : Ring.mix(Ring.AMBER, Ring.RED, (q - 0.5) * 2);
                    var pr = q > 0.6 ? 0.5 + 0.5 * Math.abs(Math.sin(T * Math.PI * (1 + q * 5))) : 1;
                    var rank = [0, 1, 2, 1, 0][k], a = Ring.clamp(q * 3 - rank, 0, 1);
                    return a > 0.02 ? { c: col, a: a * pr } : { c: col, a: 0.04 };
                };
                e.note = "Ring = rear " + side + " sensor · fills inward as an obstacle nears";
                if (recent(s) || (s === 0 && own[0] === "silence" && !phone.silent)) return;
                var dist = fn(Tn);
                Object.assign(e, { label: "Rear " + (s ? "Right" : "Left"), icon: "", value: dist.toFixed(1), unit: " m", vSize: 88, small: false,
                                   sub: dist < 0.8 ? "Stop" : dist < 1.6 ? "Close" : "Clear",
                                   color: dist < 0.8 ? "heat" : dist < 1.6 ? "warn" : "acc",
                                   idc: dist < 0.8 ? Ring.RED : dist < 1.6 ? Ring.AMBER : Ring.LIME });
            });
        }
        if (gear !== "R" && interlock()) {
            var j = level.jacks;
            var ls = function (k, T) {
                return j === "Retracting" ? { c: Ring.AMBER, a: 0.08 + 0.92 * Ring.comet5(k, T, 0.9) }
                                          : { c: Ring.RED, a: 0.3 + 0.7 * Ring.pulse(T, 1.2) };
            };
            R.spec = ls;
            if (own[0] !== "incoming" && own[0] !== "call") L.spec = ls;
        }
        return [L, R];
    }

    // What the encoder modules' round displays show: KnobFace's fields of each
    // descriptor, plus the theme and brightness. Knobs sends a module only
    // what changed.
    readonly property var faces: encs.map(function (e) {
        return { label: e.label, icon: e.icon, value: e.value, unit: e.unit, sub: e.sub,
                 color: e.color, small: e.small, vSize: e.vSize, iconSize: e.iconSize,
                 pagerCount: e.pager.length,
                 pagerOn: e.pager.findIndex(function (p) { return p.on; }),
                 dark: Theme.dark, bright: prefs.bright };
    })
    onFacesChanged: { Knobs.setFace(0, faces[0]); Knobs.setFace(1, faces[1]); }

    // A critical alert flashes both rings red, except in Reverse or while ringing
    readonly property bool flashRings: !!alert && gear !== "R" && !incoming

    // ------------------------------------------------------- ring frame loop
    readonly property Timer ringTimer: Timer {
        interval: 33; repeat: true; running: true
        onTriggered: hmi.ringFrame()
    }
    function ringFrame() {
        var T = secs(), idle = Date.now() - lastInput > 12000, out = [];
        var redFlash = function (k, T) { return { c: Ring.RED, a: 0.25 + 0.75 * Math.abs(Math.sin(T * Math.PI * 1.5)) }; };
        for (var s = 0; s < 2; s++) {
            var gain = (prefs.ring / 100) * (idle && !hover[s] ? 0.55 : 1);
            var spec = flashRings ? redFlash : encs[s].spec;
            var px = Ring.frame(spec, fx[s], T, gain);
            out.push(px);
            Knobs.setRing(s, px);
        }
        ring = out;
    }

    Component.onCompleted: {
        Knobs.turned.connect(function (s, d) { hmi.act(s, "turn", d); });
        Knobs.pressed.connect(function (s) { hmi.act(s, "press", 0); });
        Knobs.held.connect(function (s) { hmi.act(s, "hold", 0); });
        Knobs.holdStarted.connect(function (s) { hmi.holdStart(s); });
        Knobs.holdCancelled.connect(function (s) { hmi.holdCancel(s); });
    }
}
