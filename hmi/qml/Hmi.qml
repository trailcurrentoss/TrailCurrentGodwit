pragma Singleton
import QtQuick
import Godwit
import "Ring.js" as Ring
import "DemoData.js" as D
import "Legal.js" as L
import "Intents.js" as Intents

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
    // What is heard: the radio, the phone over Bluetooth, or one of the
    // Media app's sources (Music: local, usb).
    // Only one plays; choosing another pauses the rest.
    property string audio: "local"
    readonly property string audioSrc: audio
    // The Media app's source while the radio plays, to come back to
    property string lastMusic: "local"
    onAudioChanged: if (audio !== "radio") lastMusic = audio
    // The Music source being heard, or null for the radio and the phone
    readonly property var msrc: audio === "radio" || audio === "bt" ? null : Music.source(audio)
    // The connected phone's media player (AVRCP). The phone becomes the
    // source when it starts playing and stays it while paused; tuning the
    // radio takes over and pauses the phone.
    readonly property bool btPlayer: !!Bt.player.path
    readonly property bool btPlaying: btPlayer && Bt.player.status === "playing"
    // A phone connected over Bluetooth, music or not
    readonly property bool btPhone: Bt.phones.some(function (p) { return p.connected; })
    onBtPlayingChanged: if (btPlaying) takeSource("bt")
    function btPlayPause() { if (btPlaying) Bt.pause(); else Bt.play(); }
    // Whether the source being heard is playing
    readonly property bool srcPlaying: audio === "bt" ? btPlaying : audio === "radio" ? !muted : !!msrc && msrc.playing
    function srcToggle() {
        // Muted: this press brings the sound back, and starts a paused source
        if (muted) {
            muted = false;
            if (srcPlaying) return;
        }
        if (audio === "bt") btPlayPause();
        else if (msrc) msrc.toggle();
        else muted = !muted;
    }
    // Makes id the source heard, pausing the others
    function takeSource(id) {
        console.debug(lcUi, "source", id);
        if (id !== "bt" && btPlaying) Bt.pause();
        ["local", "usb"].forEach(function (k) {
            var s = Music.source(k);
            if (k !== id && s.playing) s.pause();
        });
        audio = id;
    }
    // Switch to the radio, pausing the music
    function takeRadio() {
        // There is one tuner and this app has it: asking for the radio hands it back
        if (tracking || weatherOpen) openApp("radio");
        takeSource("radio");
    }
    readonly property list<QtObject> musicEvents: [
        Connections { target: Music.local; function onMessage(t) { hmi.toast(t); } },
        Connections { target: Music.usb; function onMessage(t) { hmi.toast(t); } }
    ]

    // Volumes into the system mixer: one role each, so guidance and calls
    // keep their own levels
    onVolChanged: Mixer.setVolume("multimedia", vol)
    onGvolChanged: Mixer.setVolume("navigation", gvol)
    onMutedChanged: Mixer.setMuted("multimedia", muted || micOpen)
    readonly property int phoneVolume: prefs.phoneVol
    onPhoneVolumeChanged: Mixer.setVolume("communication", phoneVolume)
    property bool edit: false
    property var rsel: ({ energy: 0, water: 0, lights: 0, settings: 0 })
    property int czone: 0
    property var clim: ({ drv: { t: 70, fan: 4, seat: 2, air: "face" }, pas: { t: 74, fan: 4, seat: 0, air: "bi" },
                          mode: [0, 0], auto: false, ac: true, recirc: false, defF: false, defR: false,
                          sync: false, cabin: 68, out: 54 })
    // Media volume at start, and after a deploy restarts the audio: low, so
    // the cabin is never loud on power-on
    readonly property int startVol: 10
    property int vol: startVol
    property bool muted: false
    // On the head unit, the real tuner's; in the simulator, demo stations
    property var radio: Radio.available
        ? ({ band: "FM", fm: Radio.frequency, am: 880, ptr: 0, presets: Radio.presets })
        : ({ band: "FM", fm: 101.9, am: 880, ptr: 0,
             presets: [{ b: "FM", f: 88.5 }, { b: "FM", f: 91.3 }, { b: "FM", f: 94.7 },
                       { b: "FM", f: 101.9 }, { b: "FM", f: 106.7 }, { b: "AM", f: 880 }] })
    // The tuner runs only while the radio is what is heard
    // (RestoreNone: a Binding otherwise puts the old value back as it is
    // destroyed, and the frequency saved at shutdown would be the first one)
    // The dongle takes a moment to let go of, so each waits for the other's
    // process to have ended, or it would find no tuner to open
    readonly property Binding radioOn: Binding { target: Radio; property: "active"; restoreMode: Binding.RestoreNone; value: hmi.audio === "radio" && !hmi.muted && !hmi.inCall && !hmi.tracking && !hmi.weatherOpen && !Aircraft.tunerHeld && !Ships.tunerHeld && !WeatherRadio.tunerHeld }
    readonly property Binding aircraftOn: Binding { target: Aircraft; property: "active"; restoreMode: Binding.RestoreNone; value: hmi.trafficAllowed && hmi.app === "aircraft" && !Radio.tunerHeld && !Ships.tunerHeld && !WeatherRadio.tunerHeld }
    readonly property Binding shipsOn: Binding { target: Ships; property: "active"; restoreMode: Binding.RestoreNone; value: hmi.trafficAllowed && hmi.app === "ships" && !Radio.tunerHeld && !Aircraft.tunerHeld && !WeatherRadio.tunerHeld }
    readonly property Binding weatherOn: Binding { target: WeatherRadio; property: "active"; restoreMode: Binding.RestoreNone; value: hmi.weatherAllowed && hmi.weatherOpen && !hmi.inCall && !hmi.wxMusicHeard && !Radio.tunerHeld && !Aircraft.tunerHeld && !Ships.tunerHeld }
    readonly property Binding radioFreq: Binding { target: Radio; property: "frequency"; restoreMode: Binding.RestoreNone; value: hmi.radio.fm }
    readonly property Connections radioEvents: Connections {
        target: Radio
        function onTunedTo(mhz) { if (hmi.radio.fm !== mhz) hmi.upd("radio", { fm: mhz }); }
        function onPresetsChanged() { hmi.upd("radio", { presets: Radio.presets }); }
    }
    // ------------------------------------------------- aircraft and ships
    // Offered only where the head unit is set to the US, which is the only place
    // their legal basis has been reviewed. Not set counts as not allowed. The
    // simulator only ever shows demo data, so it has them regardless.
    // On the head unit, a missing decoder hides the apps too: their demo traffic is
    // for the simulator, and must never look like real aircraft on a real screen
    // What the screen is doing, in verbose logging
    readonly property LoggingCategory lcUi: LoggingCategory { name: "godwit.ui" }
    readonly property Connections powerEvents: Connections {
        target: Power
        function onRestartFailed() { hmi.toast("Could not do that. Restart the head unit and try again."); }
    }
    readonly property bool trafficAllowed: Qt.platform.pluginName === "eglfs"
        ? Config.country === "US" && Aircraft.available && Ships.available
        : Config.country === "US" || !Aircraft.available
    readonly property var apps: D.APPS.filter(function (a) {
        if (a.id === "aircraft" || a.id === "ships") return trafficAllowed;
        return a.id !== "weather" || weatherAllowed;
    })
    // The apps that show traffic on a scope. Each runs only while it is open and
    // the radio goes silent meanwhile.
    readonly property bool tracking: app === "aircraft" || app === "ships"
    // id: the one chosen, which stays chosen as the list reorders.
    // list: Park's full list is showing.
    property var air: ({ range: 25, id: "", list: false })
    property var ships: ({ range: 10, id: "", list: false })
    readonly property string trackKey: app === "ships" ? "ships" : "air"
    readonly property var track: hmi[trackKey]
    readonly property var trackRx: app === "ships" ? Ships : Aircraft
    readonly property bool trackListening: !trackRx.available || trackRx.listening
    // Distance and bearing need to know where the vehicle is
    readonly property bool trackLocated: !trackRx.available || Gnss.fix
    // What is heard, the nearest first, what has sent no position yet last. Each
    // { id, name, sub, dist, brg, at } and what its app shows besides.
    readonly property var trackRows: {
        if (!tracking) return [];
        var rx = trackRx, isShip = app === "ships";
        if (!rx.available) return isShip ? D.SHIPS : D.AIRCRAFT;
        var fix = Gnss.fix, lat = Gnss.lat, lon = Gnss.lon;
        return rx.list.map(function (a) {
            var r = isShip ? shipRow(a) : airRow(a);
            r.at = a.at;
            if (fix && a.lat !== undefined) {
                var w = trackVector(lat, lon, a.lat, a.lon);
                r.dist = w.dist;
                r.brg = w.brg;
            }
            return r;
        }).sort(function (x, y) {
            var dx = x.dist === undefined ? Infinity : x.dist, dy = y.dist === undefined ? Infinity : y.dist;
            return dx !== dy ? dx - dy : (x.id < y.id ? -1 : 1);
        });
    }
    readonly property int trackSel: {
        var id = track.id, i = trackRows.findIndex(function (r) { return r.id === id; });
        return Math.max(0, i);
    }
    // Great circle from here to there: nautical miles, and degrees from north
    function trackVector(lat1, lon1, lat2, lon2) {
        var k = Math.PI / 180, p1 = lat1 * k, p2 = lat2 * k, dl = (lon2 - lon1) * k;
        var h = Math.pow(Math.sin((p2 - p1) / 2), 2) + Math.cos(p1) * Math.cos(p2) * Math.pow(Math.sin(dl / 2), 2);
        var b = Math.atan2(Math.sin(dl) * Math.cos(p2), Math.cos(p1) * Math.sin(p2) - Math.sin(p1) * Math.cos(p2) * Math.cos(dl));
        return { dist: 2 * Math.asin(Math.sqrt(h)) * 3440.065, brg: (b / k + 360) % 360 };
    }
    // "B737 · PHX → DEN". A flight's stops may be several; it is the ends that
    // say where it is going.
    function airRow(a) {
        var kind = a.type || D.AIR_KIND[a.category] || "";
        var route = a.route ? a.route[0] + " → " + a.route[a.route.length - 1] : "";
        return { id: a.hex, name: a.call || a.reg || a.hex, alt: a.alt, speed: a.speed,
                 sub: [kind, route].filter(Boolean).join(" · ") || "ICAO " + a.hex };
    }
    // "Cargo · to Long Beach". Names and places come in capitals.
    function shipRow(a) {
        var kind = a.type ? D.shipKind(a.type) : "", to = a.destination ? "to " + titled(a.destination) : "";
        return { id: a.mmsi, name: a.name ? titled(a.name) : "MMSI " + a.mmsi, speed: a.speed, course: a.course,
                 sub: [kind, to].filter(Boolean).join(" · ") || (a.name ? "MMSI " + a.mmsi : "Name not sent yet") };
    }
    // A port's code stays as it is ("US LGB"), as does a short first word ("MV",
    // "MT"), which tends to be a prefix
    function titled(text) {
        var t = String(text);
        if (/^[A-Z]{2} ?[A-Z]{3}$/.test(t)) return t;
        return t.split(" ").map(function (w, i) {
            if (w.length < 3) return i ? w.toLowerCase() : w;
            return w.charAt(0) + w.slice(1).toLowerCase();
        }).join(" ");
    }
    function trackStatus() {
        var n = trackRows.length, rx = trackRx;
        if (rx.available) {
            if (!rx.tunerFound) return "Tuner not connected";
            if (!rx.listening) return "Starting tuner";
            if (!n) return "Listening";
            if (!Gnss.fix) return n + " heard · waiting for GPS";
        }
        var range = track.range;
        return trackRows.filter(function (r) { return r.dist !== undefined && r.dist <= range; }).length + " of " + n + " in range";
    }
    function trackHint() {
        if (trackRx.available && !trackRx.tunerFound) return "Plug in the tuner";
        if (!trackListening) return "Waiting for the tuner";
        return "Listening on " + (app === "ships" ? "162 MHz" : "1090 MHz");
    }
    function trackSelect(i) { if (trackRows[i]) upd(trackKey, { id: trackRows[i].id }); }
    function trackNextRange() {
        var R = app === "ships" ? D.SHIP_RANGES : D.AIR_RANGES;
        upd(trackKey, { range: R[(R.indexOf(track.range) + 1) % R.length] });
    }
    function trackKnob(k, d) {
        var n = trackRows.length;
        if (k === "turn" && n) trackSelect((trackSel + d % n + n) % n);
        if (k === "press") trackNextRange();
    }
    // The full list is showing, which it only does in Park
    readonly property bool trackListing: track.list && gear === "P"
    // The left knob in Aircraft and Ships, as Climate has it: the app's unless
    // something more important takes it (owner()). Turn moves through the
    // aircraft, press opens the list, and press again keeps the one it is on and
    // goes back to its data.
    function o_track(s, k, d) {
        if (k === "turn" && trackRows.length) trackSelect(Ring.clamp(trackSel + d, 0, trackRows.length - 1));
        if (k !== "press") return;
        if (trackListing) upd(trackKey, { list: false });
        else if (gear !== "P") toast("Full list in Park");
        else if (trackRows.length) upd(trackKey, { list: true });
    }
    function a_aircraft(s, k, d) { trackKnob(k, d); }
    function a_ships(s, k, d) { trackKnob(k, d); }
    // ------------------------------------------------- weather radio
    // Spoken forecasts from NOAA. Offered only where the head unit is set to the US,
    // which is the only place its legal basis has been reviewed: the channels may
    // carry other traffic abroad. As with Aircraft and Ships, the simulator only ever
    // shows demo data, and a missing receiver hides the app on the head unit.
    readonly property bool weatherAllowed: Qt.platform.pluginName === "eglfs"
        ? Config.country === "US" && WeatherRadio.available
        : Config.country === "US" || !WeatherRadio.available
    readonly property bool weatherOpen: app === "weather"
    // It runs only while its app is open and the radio goes silent meanwhile. Music
    // makes its own sound, so what started on its own stops the broadcast: they are
    // never heard together.
    readonly property bool wxMusicHeard: btPlaying || (!!msrc && msrc.playing)
    property var wx: ({ sel: 6 })
    // The seven channels in order, each { mhz, f, n, c, st, mi, s }. n, c, st and mi
    // are the nearest transmitter, from the table on the head unit, and need a fix.
    // s is its strength, 0..1.
    readonly property var wxRows: {
        if (!weatherOpen) return [];
        if (!WeatherRadio.available) return D.WX;
        var L = WeatherRadio.levels;
        return WeatherRadio.channels(Gnss.fix, Gnss.lat, Gnss.lon).map(function (r, i) {
            r.s = L[i];
            return r;
        });
    }
    readonly property int wxSel: {
        if (!WeatherRadio.available) return wx.sel;
        var f = WeatherRadio.frequency;
        return Math.max(0, wxRows.findIndex(function (r) { return Math.abs(r.mhz - f) < 0.001; }));
    }
    readonly property var wxCur: wxRows[wxSel] || ({})
    readonly property bool wxHeard: WeatherRadio.available ? WeatherRadio.receiving : wxCur.s > 0
    // A bar, so something is there, but nothing that can be played: too faint, or a
    // carrier with no speech on it. Never called empty.
    readonly property bool wxFaint: !wxHeard && Math.round((wxCur.s || 0) * 5) >= 1
    function wxDist(mi) {
        return prefs.distUnit === "km" ? Math.round(mi * 1.609344) + " km" : Math.round(mi) + " mi";
    }
    function wxStatus() {
        if (inCall) return "Paused for call";
        if (muted) return "Muted";
        if (WeatherRadio.available) {
            if (!WeatherRadio.tunerFound) return "Tuner not connected";
            if (wxMusicHeard) return "Paused for music";
            if (!WeatherRadio.listening) return "Starting tuner";
        }
        return wxHeard ? "Playing" : wxFaint ? "No clear speech" : "No station heard";
    }
    function wxTitle() {
        if (wxHeard) {
            if (wxCur.n) return wxCur.n + " · " + wxCur.c;
            return Gnss.fix ? "Station on this channel" : "Waiting for GPS to name the station";
        }
        if (WeatherRadio.available && !WeatherRadio.tunerFound) return "Plug in the tuner";
        if (wxFaint) return "Signal, but no clear speech";
        return "Nothing heard on this channel";
    }
    function wxHint() {
        if (wxHeard) return wxCur.mi !== undefined ? "Nearest transmitter on this channel · " + wxDist(wxCur.mi)
                                                   : "Weather broadcast on this channel";
        if (WeatherRadio.available && !WeatherRadio.tunerFound) return "Weather Radio is received with the USB tuner.";
        if (wxFaint) return "Something is on this channel, but not clearly enough to play. A better antenna may help.";
        return "Stations reach about 40 miles, less among hills and trees. An outdoor antenna helps.";
    }
    function wxTune(i) {
        if (!wxRows[i]) return;
        if (WeatherRadio.available) WeatherRadio.frequency = wxRows[i].mhz;
        else upd("wx", { sel: i });
    }
    function wxStrongest() {
        if (WeatherRadio.available) {
            if (!WeatherRadio.tuneStrongest()) toast("No station heard");
            return;
        }
        var best = 0;
        wxRows.forEach(function (r, i) { if (r.s > wxRows[best].s) best = i; });
        upd("wx", { sel: best });
    }
    function a_weather(s, k, d) {
        var n = wxRows.length;
        if (k === "turn" && n) wxTune((wxSel + d % n + n) % n);
        if (k === "press") wxStrongest();
    }
    // Its sound replaces what was heard, and what was heard comes back after. It is
    // on the same volume and mute as music, so the mute it found is put back too.
    property bool wxWasOpen: false
    property bool wxResume: false
    property bool wxMuted: false
    onAppChanged: {
        // Not weatherOpen, which may not have heard of this yet
        const open = app === "weather";
        if (wxWasOpen === open) return;
        wxWasOpen = open;
        if (open) {
            wxResume = wxMusicHeard;
            wxMuted = muted;
            if (btPlaying) Bt.pause();
            else if (msrc && msrc.playing) msrc.pause();
            if (audio === "radio" && !muted) toast("Radio is off while Weather Radio is open");
            muted = false;
            return;
        }
        muted = wxMuted;
        if (!wxResume) return;
        wxResume = false;
        if (inCall) resume = true;
        else if (audio === "bt") Bt.play();
        else if (msrc) msrc.play();
    }
    // Everything a station can be asked for by name
    function radioStations() { return Radio.available ? Radio.known : D.FM.concat(D.AM); }
    // The Media app: the queue row the right knob is on, which panel the right
    // card shows (queue | browse) and the Browse row it is on
    property var media: ({ sel: 0, panel: "queue", bsel: 0 })
    // The queue row follows the playing track when it was on it, so skipping
    // from the dial or a song ending moves it too; a row the driver turned to
    // stays put
    // Set only here, never bound: a binding could update before the handler
    property int playingIdx: -1
    readonly property Connections queueEvents: Connections {
        target: hmi.msrc
        ignoreUnknownSignals: true
        function onQueueChanged() {
            var i = hmi.msrc.queueIndex;
            if (i >= 0 && i !== hmi.playingIdx && hmi.media.sel === hmi.playingIdx) hmi.upd("media", { sel: i });
            hmi.playingIdx = i;
        }
    }
    onMsrcChanged: playingIdx = msrc ? msrc.queueIndex : -1
    // up: "north" keeps north at the top; "heading" turns the map with the vehicle
    property var nav: ({ zoom: 15, step: 0, dist: 0, voice: true, view: "2D", up: "north" })
    // The active route: its turn-by-turn steps and destination. None until a
    // destination is chosen.
    property bool routeActive: false
    property var steps: []
    property var dest: null
    // Its line on the map, [[lon, lat], ...]
    property var routeShape: []

    // ------------------------------------------------------------- position
    // The vehicle, from the GNSS receiver; the fixed demo position until it
    // has a fix
    function pos() { return Gnss.fix ? [Gnss.lat, Gnss.lon] : D.MAPC; }
    // The map keeps the vehicle centred until it is dragged; Recenter resumes
    property bool follow: true
    // Following the route: the nearest point on its shape, fixes off it, and
    // whether a new route is being found from here
    property int navIdx: 0
    property int offRoute: 0
    property bool rerouting: false
    property double lastClockSync: 0
    readonly property Connections gnssEvents: Connections {
        target: Gnss
        function onUpdated() {
            if (!Gnss.fix) return;
            if (Trails.recording) Trails.addFix(Gnss.lat, Gnss.lon, Gnss.altitude, Gnss.utc);
            if (hmi.follow) {
                var cam = { center: [Gnss.lat, Gnss.lon] };
                // The course is noise at a standstill: keep the last heading
                if (hmi.nav.up === "heading" && Gnss.speed > 3) cam.bearing = Gnss.course;
                hmi.mapEase(cam, 900);
            }
            if (hmi.routeActive) hmi.navTrack(Gnss.lat, Gnss.lon);
            hmi.syncClock();
        }
    }
    // Set Time Automatically: the receiver's UTC time, when the clock is out
    // by more than two seconds, at most once a minute
    function syncClock() {
        if (!prefs.autoTime || !Clock.available || !Gnss.utc || Date.now() - lastClockSync < 60000) return;
        var off = (Gnss.utc.getTime() - Date.now()) / 1000;
        if (Math.abs(off) > 2) { Clock.adjust(Math.round(off)); lastClockSync = Date.now(); }
    }
    // Metres between [lat, lon] and a shape point [lon, lat] (equirectangular;
    // plenty at these distances)
    function metres(lat, lon, p) {
        var k = Math.PI / 180, x = (p[0] - lon) * k * Math.cos(lat * k), y = (p[1] - lat) * k;
        return 6371000 * Math.sqrt(x * x + y * y);
    }
    // Progress along the active route from a fix
    function navTrack(lat, lon) {
        var S = routeShape;
        if (!S.length || rerouting) return;
        // The nearest shape point, looking a little behind and well ahead
        var best = navIdx, bd = 1e12;
        for (var i = Math.max(0, navIdx - 5); i < Math.min(S.length, navIdx + 400); i++) {
            var m = metres(lat, lon, S[i]);
            if (m < bd) { bd = m; best = i; }
        }
        if (bd > 80) {
            if (++offRoute >= 3) reroute(lat, lon);
            return;
        }
        offRoute = 0;
        navIdx = best;
        var step = nav.step;
        while (step < steps.length - 1 && best >= steps[step].si) step++;
        // Along the shape to the maneuver
        var left = 0, to = Math.min(steps[step].si, S.length - 1);
        for (var j = best; j < to; j++) left += metres(S[j][1], S[j][0], S[j + 1]);
        var dist = Math.round(left / 1609.344 * 100) / 100, prev = step === nav.step ? nav.dist : 1e9;
        var cur = steps[step];
        if (cur.dir === "arrive" && left < 30) { speak(cur.vp); toast("You have arrived"); endRoute(true); return; }
        var cross = function (t) { return prev > t && dist <= t; };
        if (cur.dir === "arrive") {
            if (cross(0.1)) speak(cur.vp);
        } else if (cross(0.1)) {
            prompt(cur, D.DIRS[cur.dir].l + " now" + (cur.st ? " · " + cur.st : ""), cur.vp);
        } else if (cross(0.5) || (cross(2) && cur.d > 3)) {
            var ahead = cross(0.5) ? "half a mile" : "2 miles";
            prompt(cur, D.DIRS[cur.dir].l + " in " + fd(dist) + (cur.st ? " · " + cur.st : ""),
                   "In " + ahead + ", " + lowerFirst(cur.va || cur.vp));
        }
        upd("nav", { step: step, dist: dist });
    }
    // A guidance prompt: shown on the status bar and knob, and spoken
    function prompt(step, shown, spoken) {
        if (phone.call) return;
        guideUntil = Date.now() + 3500; guideTimer.restart();
        guideTxt = shown;
        speak(spoken);
    }
    function speak(text) { if (nav.voice && !phone.call && !dictating && text) Voice.say(text); }
    function lowerFirst(s) { return s ? s.charAt(0).toLowerCase() + s.slice(1) : ""; }
    // Off the route: a new one from here to the same place
    function reroute(lat, lon) {
        offRoute = 0;
        if (!Router.available || !dest) return;
        rerouting = true;
        Router.plan(lat, lon, dest.lat, dest.lon);
        toast("Finding a new route");
    }
    readonly property Connections routerEvents: Connections {
        target: Router
        function onRouteChanged() {
            if (!hmi.rerouting || Router.busy) return;
            hmi.rerouting = false;
            var R = Router.route;
            if (!R.steps || !R.steps.length) { hmi.toast("No route from here"); return; }
            var st = R.steps.map(function (x) { return Object.assign({}, x); });
            var say = String(R.say || ""), shape = R.shape.slice();
            st[st.length - 1].st = hmi.dest.n;
            hmi.steps = st;
            hmi.routeShape = shape;
            hmi.navIdx = 0;
            hmi.upd("nav", { step: 0, dist: st[0].d });
            Router.clear();
            hmi.speak("Route updated. " + say);
        }
    }
    // Destination search, open over the map: {q, sel, preview}
    property var navSearch: null
    // call: {n, num, ini, state: "incoming" | "dialing" | "active", t}
    property var phone: ({ sel: 0, tab: "fav", call: null, mic: true, silent: false })

    // On the head unit, the connected phone's contacts, recent calls and
    // calls (PBAP and oFono); in the simulator, the demo ones
    readonly property bool phoneReal: Phonebook.available
    readonly property var contacts: phoneReal ? Phonebook.contacts : D.CONTACTS
    readonly property var recents: phoneReal
        ? Phonebook.recents.map(function (r) { return Object.assign({}, r, { t: recentTime(r.when) }); })
        : D.RECENTS.map(function (r) { return Object.assign({}, D.CONTACTS[r.c], { type: r.type, t: r.t }); })
    // The people called most, as phones do not share their favorites
    readonly property var favs: {
        if (!phoneReal) return D.FAVS.map(function (i) { return D.CONTACTS[i]; });
        var count = {}, first = {};
        Phonebook.recents.forEach(function (r) {
            if (r.n === r.num) return;  // not a contact
            count[r.num] = (count[r.num] || 0) + 1;
            if (!first[r.num]) first[r.num] = r;
        });
        return Object.keys(count).sort(function (a, b) { return count[b] - count[a]; }).slice(0, 6)
            .map(function (k) { return { n: first[k].n, num: first[k].num, ini: first[k].ini }; });
    }
    function recentTime(when) {
        var d = new Date(when), now = new Date(), days = Math.floor((new Date(now.getFullYear(), now.getMonth(), now.getDate()) - new Date(d.getFullYear(), d.getMonth(), d.getDate())) / 86400000);
        if (days <= 0) return fmtTime(d, true);
        if (days === 1) return "Yesterday";
        if (days < 7) return ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"][d.getDay()];
        return (d.getMonth() + 1) + "/" + d.getDate();
    }
    function initials(name) {
        var w = String(name).split(" ").filter(function (x) { return x; });
        if (!w.length || !/[A-Za-z]/.test(w[0][0])) return "#";
        return (w[0][0] + (w.length > 1 && /[A-Za-z]/.test(w[w.length - 1][0]) ? w[w.length - 1][0] : "")).toUpperCase();
    }
    // Read the connected phone's contacts, unless its Sync Contacts is off.
    // They stay through a dropped connection; another phone replaces them.
    function syncPhonebook(recentsOnly) {
        if (!Calls.available) return;
        var a = Calls.address;
        Phonebook.sync(btNoContacts.indexOf(a) < 0 ? a : "", !!recentsOnly);
    }
    onBtNoContactsChanged: syncPhonebook(false)

    // ------------------------------------------------------------- messages
    // A text message on screen: {id, n, num, ini, stage, until, reply,
    // compose}; compose is a new text started by voice, which begins at
    // "prompt" rather than "new". It moves through the stages below; Read and Reply sit above the left knob,
    // Dismiss and Cancel above the right. One arriving during a call waits for
    // the call to end. The texts themselves are only ever spoken.
    property var msgNote: null
    property var msgHeld: null
    // Per stage: the line under the sender, the left and right actions
    // [label, icon] (null for none), and how long it waits for an answer (ms)
    readonly property var msgStages: ({
        "new":          { sub: "New message", l: ["Read", "volume-high"], r: ["Dismiss", "close"], wait: 12000 },
        "fetching":     { sub: "Getting the message…", l: null, r: ["Stop", "close"] },
        "reading":      { sub: "Reading…", l: ["Skip", "play-skip-forward"], r: ["Stop", "close"] },
        "offer":        { sub: "Reply by voice?", l: ["Reply", "mic"], r: ["Done", "checkmark"], wait: 10000 },
        "offerAnswer":  { sub: "Say yes to reply, or no · or press Reply", l: ["Reply", "mic"], r: ["Done", "checkmark"] },
        "prompt":       { sub: "Say your message after the tone", l: null, r: ["Cancel", "close"] },
        "listening":    { sub: "Listening · say your message", l: ["Done Talking", "checkmark"], r: ["Cancel", "close"] },
        "transcribing": { sub: "Getting your words…", l: null, r: ["Cancel", "close"] },
        "answer":       { sub: "Say yes to send, or no · or press Send", l: ["Send", "send"], r: ["Cancel", "close"] },
        "confirm":      { sub: "Send this message? · hold Send to say it again", l: ["Send", "send"], r: ["Cancel", "close"], wait: 20000 },
        "sending":      { sub: "Sending…", l: null, r: null }
    })
    readonly property var msgStage: msgNote && msgStages[msgNote.stage] || null
    readonly property bool dictating: !!msgNote && (msgNote.stage === "listening" || msgNote.stage === "transcribing" || msgNote.stage === "answer" || msgNote.stage === "offerAnswer")
    function msgTo(stage, patch) {
        console.debug(lcUi, "message", stage);
        var w = msgStages[stage].wait;
        msgNote = Object.assign({}, msgNote, patch || {}, { stage: stage, until: w ? Date.now() + w : 0 });
    }
    // Music is muted while listening, so the microphone hears only the driver
    onMicOpenChanged: Mixer.setMuted("multimedia", micOpen || muted)

    // Take the connected phone's messages, unless its Message Notifications is off
    function syncMessages() {
        var a = Calls.available ? Calls.address : "";
        Messages.connectPhone(btNoMessages.indexOf(a) < 0 ? a : "");
    }
    onBtNoMessagesChanged: syncMessages()
    readonly property Connections msgEvents: Connections {
        target: Messages
        function onReceived(m) {
            var name = Phonebook.nameFor(m.num) || m.n || m.num || "Unknown sender";
            hmi.showMessage({ id: m.id, n: name, num: m.num, ini: hmi.initials(name) });
        }
        function onTextReady(id, name, number, text) {
            if (!hmi.msgNote || hmi.msgNote.id !== id || hmi.msgNote.stage !== "fetching") return;
            hmi.sayMessage(text, number);
        }
        function onTextFailed(id) {
            if (!hmi.msgNote || hmi.msgNote.id !== id) return;
            hmi.msgNote = null;
            hmi.toast("Could not read the message");
        }
        function onSent(number) {
            var what = hmi.msgNote && hmi.msgNote.compose ? "Message" : "Reply";
            hmi.msgNote = null;
            hmi.toast(what + " sent");
            Voice.say(what + " sent");
        }
        function onSendFailed(number) {
            if (!hmi.msgNote || hmi.msgNote.stage !== "sending") return;
            var what = hmi.msgNote.compose ? "message" : "reply";
            hmi.msgNote = null;
            hmi.toast("Could not send the " + what);
            Voice.say("The " + what + " could not be sent");
        }
    }
    // Reading done: offer to reply
    readonly property Connections msgVoice: Connections {
        target: Voice
        function onSpeakingChanged() {
            if (Voice.speaking || !hmi.msgNote) return;
            if (hmi.msgNote.stage === "prompt")
                hmi.chimeThen(function () { if (hmi.msgNote && hmi.msgNote.stage === "prompt") hmi.startReply(); });
            else if (hmi.msgNote.stage === "reading") hmi.offerReply();
            // "Do you want to reply?" asked: listen for the answer, the knobs still work
            else if (hmi.msgNote.stage === "offer" && hmi.msgNote.asked && Dictation.available)
                hmi.chimeThen(function () {
                    if (!hmi.msgNote || hmi.msgNote.stage !== "offer") return;
                    hmi.msgTo("offerAnswer", { asked: false });
                    Dictation.listen();
                });
            // "Send it?" asked: listen for the answer, the knobs still work
            else if (hmi.msgNote.stage === "confirm" && Dictation.available)
                hmi.chimeThen(function () {
                    if (!hmi.msgNote || hmi.msgNote.stage !== "confirm") return;
                    hmi.msgTo("answer");
                    Dictation.listen();
                });
            else if (hmi.msgNote.stage === "confirm") hmi.msgTo("confirm");  // its wait starts now
        }
    }
    readonly property Connections msgDictation: Connections {
        target: Dictation
        function onStateChanged() {
            if (Dictation.state !== "transcribing") return;
            if (hmi.msgNote && hmi.msgNote.stage === "listening") hmi.msgTo("transcribing");
            else if (hmi.asking && hmi.ask.stage === "listening") hmi.askTo("transcribing");
        }
        function onHeard(text) {
            if (hmi.asking && hmi.ask.stage === "confirm") hmi.endRouteAsked(Intents.yesNo(text) === "yes");
            else if (hmi.msgNote && hmi.msgNote.stage === "offerAnswer") hmi.answerOffer(text);
            else if (hmi.msgNote && hmi.msgNote.stage === "answer") hmi.answerSend(text);
            else if (hmi.dictating) hmi.confirmReply(text);
            else if (hmi.asking && (hmi.ask.stage === "listening" || hmi.ask.stage === "transcribing")) hmi.command(text);
        }
        function onFailed(reason) {
            var said = reason === "silence" ? "Didn't hear anything" : "The microphone is not working";
            // No answer to "Cancel the route?": the route stays
            if (hmi.asking && hmi.ask.stage === "confirm") { hmi.endAsk(); hmi.toast("Route kept"); return; }
            if (hmi.asking && !hmi.dictating) { hmi.endAsk(); hmi.toast(said); return; }
            if (!hmi.dictating) return;
            // No answer to "Send it?": wait for a press instead
            if (hmi.msgNote.stage === "offerAnswer") { hmi.msgTo("offer"); return; }
            if (hmi.msgNote.stage === "answer") { hmi.msgTo("confirm"); return; }
            if (hmi.msgNote.compose) { hmi.msgNote = null; hmi.toast(said); return; }
            hmi.msgTo("offer");
            hmi.toast(reason === "silence" ? "Didn't hear a reply" : said);
        }
    }
    function showMessage(note) {
        if (phone.call) { msgHeld = note; return; }
        // A reply under way is not interrupted
        if (msgNote && msgNote.stage !== "new") return;
        msgNote = Object.assign({}, note, { stage: "new", until: Date.now() + msgStages["new"].wait });
        Voice.say("Message from " + Intents.sayName(note.n));
    }
    function readMessage() {
        var m = msgNote;
        if (!m) return;
        if (m.text !== undefined) { sayMessage(m.text, m.num); return; }  // the simulator's
        msgTo("fetching");
        Messages.fetch(m.id);
    }
    function closeMessage() {
        if (!msgNote) return;
        var st = msgNote.stage;
        if (st === "listening" || st === "transcribing" || st === "answer" || st === "offerAnswer") Dictation.cancel();
        // Cleared first: stopping the voice must not count as having finished reading
        msgNote = null;
        if (st === "reading" || st === "offer" || st === "confirm" || st === "prompt") Voice.stop();
    }
    function offerReply() {
        msgTo("offer", { asked: true });
        if (Voice.available) Voice.say("Do you want to reply?");
    }
    // The spoken answer to "Do you want to reply?"
    function answerOffer(text) {
        var a = Intents.yesNo(text);
        if (a === "yes") promptReply();
        else if (a === "no") closeMessage();
        else msgTo("offer");
    }
    // Read aloud on the head unit; shown in the simulator, which has no voice
    function sayMessage(text, number) {
        if (phone.call) { msgNote = null; return; }
        msgTo("reading", number ? { num: number } : {});
        if (Voice.available) Voice.say(Intents.sayName(msgNote.n) + " says: " + Intents.sayText(text));
        else { toast(msgNote.n + ": " + text); msgTo("offer"); }
    }
    // Ask for the words, then the tone, then listen: never a silent microphone
    function promptReply() {
        if (!msgNote.num || !Voice.available) { startReply(); return; }
        // Off "offer" while the voice stops, so that stopping it is not an answer's cue
        msgTo("offer", { asked: false });
        Voice.stop();
        msgTo("prompt");
        Voice.say("What's your message?");
    }
    function startReply() {
        Voice.stop();
        if (!msgNote.num) { toast("No number to reply to"); msgNote = null; return; }
        msgTo("listening");
        if (Dictation.available) Dictation.listen();
        else simHeardTimer.restart();
    }
    readonly property Timer simHeardTimer: Timer { interval: 2000; onTriggered: if (hmi.dictating) hmi.confirmReply("Sounds good, see you there") }
    function confirmReply(text) {
        // "Cancel" said instead of a message
        if (text.split(/\s+/).length <= 3 && Intents.yesNo(text) === "no") {
            closeMessage();
            toast("Message cancelled");
            if (Voice.available) Voice.say("Okay, cancelled.");
            return;
        }
        msgTo("confirm", { reply: text });
        if (Voice.available) Voice.say((msgNote.compose ? "Your message to " : "Your reply to ") + Intents.sayName(msgNote.n) + ": " + text + (/[.!?]$/.test(text) ? "" : ".") + " Send it?");
        else toast((msgNote.compose ? "Message: " : "Reply: ") + text);
    }
    // The spoken answer to "Send it?"
    function answerSend(text) {
        var a = Intents.yesNo(text);
        if (a === "yes") sendReply();
        else if (a === "no") { closeMessage(); toast("Message not sent"); if (Voice.available) Voice.say("Okay, I won't send it."); }
        else if (a === "again") promptReply();
        else msgTo("confirm");
    }
    function sendReply() {
        var m = msgNote;
        Voice.stop();
        if (!Messages.available) { msgNote = null; toast("Reply sent"); return; }  // the simulator
        msgTo("sending");
        Messages.send(m.num, m.reply);
    }
    // Presses answer the message; turns still do what they would without it
    function o_message(s, k, d) {
        if (k !== "turn") { msgAct(s, k); return; }
        // The left knob sets the voice's volume while a message is spoken
        if (s === 0 && Voice.speaking) { gvol = Ring.clamp(gvol + d * 2, 0, 100); return; }
        var own = owner(true)[s], h = hmi["o_" + own] || hmi["a_" + own];
        if (h) h(s, k, d);
    }
    // The left (0) or right (1) action, from a knob or the banner
    function msgAct(s, k) {
        if (!msgNote) return;
        var st = msgNote.stage;
        if (s === 1) { if (msgStage.r) closeMessage(); return; }
        if (st === "new") readMessage();
        else if (st === "reading") { msgTo("offer"); Voice.stop(); offerReply(); }
        else if (st === "offer") promptReply();
        else if (st === "offerAnswer") { Dictation.cancel(); promptReply(); }
        else if (st === "listening") {
            if (Dictation.available) Dictation.finish();
            else { simHeardTimer.stop(); confirmReply("Sounds good, see you there"); }
        }
        else if (st === "confirm" || st === "answer") {
            if (st === "answer") Dictation.cancel();
            if (k === "hold") promptReply(); else sendReply();
        }
    }
    function simMessage() {
        var c = D.CONTACTS[1];
        showMessage({ id: "", n: c.n, num: c.num, ini: c.ini, text: "Running ten minutes late, save me a spot by the fire." });
    }
    // ------------------------------------------------------- voice assistant
    // "Hey Peregrine", then one of four commands (Intents.js): directions, a
    // call, a text or the radio. Patterns understand most requests at once;
    // the rest go to the language model on the NPU (Llm). ask is the request
    // under way: {stage, heard, place, sent, found}. It closes once the
    // command is carried out or answered.
    property var ask: null
    readonly property bool asking: !!ask
    readonly property var askStages: ({
        listening:    { sub: "Listening · say what you need" },
        transcribing: { sub: "Getting your words…" },
        thinking:     { sub: "Working it out…" },
        contacts:     { sub: "Checking your phone's contacts…" },
        confirm:      { sub: "Say yes to end the route, or no" },
        finding:      { sub: "Searching…" },
        routing:      { sub: "Finding a route…" }
    })
    readonly property var askStage: ask && askStages[ask.stage] || null
    // The microphone is open for the driver's words
    readonly property bool micOpen: dictating || (asking && (ask.stage === "listening" || (ask.stage === "confirm" && !!ask.mic)))
    function askTo(stage, patch) { ask = Object.assign({}, ask, patch || {}, { stage: stage }); }
    // Listening starts after the chime, so the chime is not taken for speech
    property var afterChime: null
    readonly property Timer chimeTimer: Timer {
        interval: 400
        onTriggered: { var f = hmi.afterChime; hmi.afterChime = null; if (f) f(); }
    }
    function chimeThen(f) { Voice.chime(); afterChime = f; chimeTimer.restart(); }

    // Listening for the wake word, except during a call
    readonly property Binding wakeOn: Binding { target: Wake; property: "enabled"; value: hmi.prefs.wake && !hmi.inCall }
    readonly property Connections wakeEvents: Connections {
        target: Wake
        function onDetected(score) { hmi.wake(); }
    }
    function wake() {
        // Not over a call, a message, the safety notice or a request under way
        if (inCall || msgNote || noticeUp || asking) return;
        Voice.stop();
        ask = { stage: "listening", heard: "" };
        if (Dictation.available) chimeThen(function () { if (hmi.asking && hmi.ask.stage === "listening") Dictation.listen(); });
        else simAskTimer.restart();
    }
    // The simulator has no microphone: it pretends to hear each of these in turn
    property int simAsk: 0
    readonly property Timer simAskTimer: Timer { interval: 1500; onTriggered: hmi.simHeard() }
    function simHeard() {
        if (!asking || ask.stage !== "listening") return;
        var says = ["Get directions to Arches National Park", "Call " + D.CONTACTS[0].n.split(" ")[0],
                    "Send a text to " + D.CONTACTS[1].n.split(" ")[0] + " saying I'm on my way", "Set the radio to 94.7"];
        command(says[simAsk++ % says.length]);
    }
    function endAsk() {
        if (!ask) return;
        if (ask.stage === "listening" || ask.stage === "transcribing" || (ask.stage === "confirm" && ask.mic)) Dictation.cancel();
        Llm.cancel();
        chimeTimer.stop();
        afterChime = null;
        simAskTimer.stop();
        ask = null;
    }
    // Shown and spoken; the assistant answers even with voice guidance off,
    // but never over a call
    function answer(text) {
        toast(text);
        if (!inCall && Voice.available) Voice.say(text);
    }
    function command(text) {
        if (!ask) return;
        // Only "Hey Peregrine" again: the driver missed that it was already
        // listening, so listen once more rather than answer
        if (!Intents.clean(text) && !ask.again && Dictation.available) {
            askTo("listening", { heard: "", again: true });
            chimeThen(function () { if (hmi.asking && hmi.ask.stage === "listening") Dictation.listen(); });
            return;
        }
        var c = Intents.match(text, callable().map(function (x) { return x.n; }), radioStations());
        askTo(ask.stage, { heard: text });
        if (c) { doCommand(c); return; }
        if (!Llm.available) { endAsk(); answer(Intents.HELP); return; }
        askTo("thinking");
        Llm.interpret(text);
    }
    readonly property Connections llmEvents: Connections {
        target: Llm
        function onUnderstood(cmd) {
            if (!hmi.asking || hmi.ask.stage !== "thinking") return;
            var c = Intents.fromModel(cmd, hmi.radioStations());
            if (c) hmi.doCommand(c, true);
            else { hmi.endAsk(); hmi.answer(Intents.HELP); }
        }
        function onFailed(reason) {
            if (!hmi.asking || hmi.ask.stage !== "thinking") return;
            hmi.endAsk();
            hmi.answer(reason === "timeout" ? "Sorry, that took too long." : Intents.HELP);
        }
    }
    // guessed: the language model's reading, which can take a place or a
    // station for a name, so a name nobody has gets the help answer
    function doCommand(c, guessed) {
        if (c.action === "directions") { findPlace(c.place); return; }
        if (c.action === "endroute") { confirmEndRoute(); return; }
        if (c.action === "mute" || c.action === "unmute") {
            muted = c.action === "mute";
            endAsk();
            toast(muted ? "Muted" : "Sound on");
            return;
        }
        if (c.action === "radio") { endAsk(); tuneByVoice(c); return; }
        if (c.action === "music") { endAsk(); playMusic(c.words, c.source, c.shuffle); return; }
        var who = Intents.findContact(c.contact, callable());
        // Not found: the phone may have a contact added since it was last
        // read, so read it again and try once more
        if (!who && !c.reread && phoneReal && Calls.available && !Phonebook.syncing) {
            askTo("contacts", { pending: Object.assign({}, c, { reread: true }), guessed: !!guessed });
            Phonebook.refresh();
            if (!Phonebook.syncing) doCommand(ask.pending, guessed);
            return;
        }
        endAsk();
        if (!who && !guessed && phoneReal && !contacts.length && !/^[+\d\s]+$/.test(c.contact)) {
            answer("I can't see your phone's contacts. Allow contact sharing for Godwit in your phone's Bluetooth settings.");
            return;
        }
        if (!who) { answer(guessed ? Intents.HELP : "I couldn't find " + c.contact + " in your contacts."); return; }
        if (phoneReal && !Calls.available) { answer(c.action === "call" ? "Connect a phone to make calls." : "Connect a phone to send texts."); return; }
        if (c.action === "call") { toast("Calling " + who.n); startCall(who); return; }
        composeText(who, c.message);
    }
    // The contacts, then anyone else in the recent calls with a name: a phone
    // that does not share its contacts still shares its call history
    function callable() {
        var seen = {};
        contacts.forEach(function (c) { seen[c.num] = true; });
        return contacts.concat(recents.filter(function (r) {
            if (!r.n || r.n === r.num || seen[r.num]) return false;
            seen[r.num] = true;
            return true;
        }));
    }
    // Ending the route asks first: "Cancel the route to Denver?", then a yes
    // or no by voice, or End Route / Keep Route on the knobs
    function confirmEndRoute() {
        if (!routeActive) { endAsk(); answer("There's no route to cancel."); return; }
        askTo("confirm", { heard: "", mic: false });
        var q = "Cancel the route to " + (dest && dest.n ? dest.n : "your destination") + "?";
        if (Voice.available && !inCall) Voice.say(q);
        else toast(q);
    }
    readonly property Connections askVoice: Connections {
        target: Voice
        function onSpeakingChanged() {
            if (Voice.speaking || !hmi.asking || hmi.ask.stage !== "confirm" || hmi.ask.mic || !Dictation.available) return;
            hmi.chimeThen(function () {
                if (!hmi.asking || hmi.ask.stage !== "confirm") return;
                hmi.askTo("confirm", { mic: true });
                Dictation.listen();
            });
        }
    }
    function endRouteAsked(yes) {
        endAsk();
        if (!yes) { answer("Okay, keeping the route."); return; }
        endRoute(false);
        answer("Route cancelled.");
    }
    // A new text: ask for the words unless they were already said, then
    // the reply flow's confirm and send
    function composeText(who, message) {
        msgNote = { id: "", n: who.n, num: who.num, ini: who.ini || initials(who.n), compose: true };
        if (message) { confirmReply(message); return; }
        msgTo("prompt");
        if (Voice.available) Voice.say("What's your message to " + (who.ini === "#" ? Intents.sayName(who.n) : String(who.n).split(" ")[0]) + "?");
        else startReply();
    }
    function tuneByVoice(c) {
        var R = radio, b = c.band || R.band, f;
        if (c.preset) {
            var p = R.presets[c.preset - 1];
            if (!p) { answer("There is no preset " + c.preset + "."); return; }
            if (!p.f) { answer("Preset " + c.preset + " is empty."); return; }
            b = p.b; f = p.f;
        } else if (c.station) {
            var L = radioStations(), hit = null;
            for (var i = 0; i < L.length; ++i)
                if (L[i].n.toLowerCase() === c.station.toLowerCase()) { hit = L[i]; break; }
            if (!hit) { answer("I couldn't find the station " + c.station + "."); return; }
            b = hit.f < 200 ? "FM" : "AM"; f = hit.f;
        } else {
            f = c.freq || (b === "FM" ? R.fm : R.am);
        }
        if (Radio.available && b === "AM") { answer("This tuner only receives FM."); return; }
        tuneTo(b, f);
        toast(b + " " + (b === "FM" ? Number(f).toFixed(1) : f));
    }
    // Directions: a contact's address when the place is a contact who has
    // one on the phone ("Maya", "Maya's house"), otherwise the place itself.
    // The search's first match, then its route preview; the driver presses
    // to start, as with a place picked by hand.
    function findPlace(place) {
        if (gear === "R") { endAsk(); answer("Directions are not available in reverse."); return; }
        var who = Intents.contactPlace(place, callable()), q = who ? who.adr : place;
        openApp("nav");
        navSearch = { q: q, sel: 0, preview: null };
        askTo("finding", { place: q, who: who ? who.n : "", sent: false });
        var p = pos();
        Places.search(q, p[0], p[1]);
    }
    readonly property Connections askPlaces: Connections {
        target: Places
        function onBusyChanged() { if (Places.busy && hmi.asking && hmi.ask.stage === "finding") hmi.askTo("finding", { sent: true }); }
        function onResultsChanged() {
            if (!hmi.asking || hmi.ask.stage !== "finding" || !hmi.ask.sent || Places.busy) return;
            var R = Places.results, q = hmi.ask.place;
            if (!R.length) { hmi.endAsk(); hmi.answer(Places.available ? "I couldn't find " + q + "." : "Search is still starting up."); return; }
            hmi.askTo("routing", { found: R[0] });
            hmi.searchPick(0);
            if (!Router.available) hmi.offerRoute(R[0], null);
        }
    }
    // Planning ends when the router goes idle; plan() clears the old route
    // just before it turns busy, which is not an answer
    readonly property Connections askContacts: Connections {
        target: Phonebook
        function onSynced() {
            if (hmi.asking && hmi.ask.stage === "contacts") hmi.doCommand(hmi.ask.pending, hmi.ask.guessed);
        }
    }
    readonly property Connections askRouter: Connections {
        target: Router
        function onBusyChanged() {
            if (Router.busy || !hmi.asking || hmi.ask.stage !== "routing") return;
            // The search was closed or another place picked while it planned.
            // Each read of the results is a fresh copy, so compare where they are.
            var pv = hmi.navSearch && hmi.navSearch.preview, f = hmi.ask.found;
            if (!pv || pv.lat !== f.lat || pv.lon !== f.lon) { hmi.endAsk(); return; }
            hmi.offerRoute(hmi.ask.found, Router.route);
        }
    }
    function offerRoute(place, R) {
        var name = ask && ask.who ? ask.who + "'s address" : place.n;
        endAsk();
        if (R && (!R.steps || !R.steps.length)) { answer("I couldn't find a route to " + name + "."); return; }
        answer(name + (R ? ", " + sayDist(R.mi) + ", " + sayMins(R.mins) : "") + ". Press to start.");
    }
    function sayDist(mi) {
        var km = prefs.distUnit === "km", v = km ? mi * 1.609344 : mi;
        v = v < 10 ? Math.round(v * 10) / 10 : Math.round(v);
        return v + (km ? (v === 1 ? " kilometre" : " kilometres") : (v === 1 ? " mile" : " miles"));
    }
    function sayMins(m) {
        m = Math.max(1, Math.round(m));
        var h = Math.floor(m / 60), r = m % 60;
        var mins = r + (r === 1 ? " minute" : " minutes");
        return h ? h + (h === 1 ? " hour" : " hours") + (r ? " " + mins : "") : mins;
    }
    // Presses answer the request; turns still do what they would without it
    function o_ask(s, k, d) {
        if (k === "turn") {
            var own = owner(true)[s], h = hmi["o_" + own] || hmi["a_" + own];
            if (h) h(s, k, d);
            return;
        }
        if (ask.stage === "confirm") { endRouteAsked(s === 0); return; }
        if (s === 1) { endAsk(); return; }
        if (ask.stage !== "listening") return;
        if (Dictation.available) Dictation.finish();
        else { simAskTimer.stop(); simHeard(); }
    }

    // The phone's calls, including ones made or answered on the phone itself
    function syncCall() {
        var L = Calls.calls;
        var pick = L.find(function (x) { return ["active", "held", "dialing", "alerting"].indexOf(x.state) >= 0; })
                || L.find(function (x) { return x.state === "incoming" || x.state === "waiting"; });
        if (!pick) {
            if (phone.call) {
                finishCall(phone.call.state === "incoming" ? "Missed call" : "Call ended");
                syncPhonebook(true);
            }
            return;
        }
        var st = pick.state === "incoming" || pick.state === "waiting" ? "incoming"
               : pick.state === "dialing" || pick.state === "alerting" ? "dialing" : "active";
        var name = pick.name || Phonebook.nameFor(pick.number) || pick.number || "Unknown caller";
        var prev = phone.call;
        if (!prev) pauseForCall();
        upd("phone", { silent: prev ? phone.silent : false,
                       call: { n: name, num: pick.number, ini: initials(name), state: st,
                               t: prev && prev.state === "active" && st === "active" ? prev.t : 0 } });
    }
    // The system clock and zone (timedated). The zone setting shows the
    // system's, and changing either redraws every clock on screen.
    readonly property Connections clockEvents: Connections {
        target: Clock
        function onTimezoneChanged() {
            for (var k in D.ZONES) if (D.ZONES[k] === Clock.timezone && hmi.prefs.tz !== k) hmi.upd("prefs", { tz: k });
        }
        function onChanged() { Date.timezoneUpdated(); hmi.clock = Date.now(); }
    }
    function setTimeZone(label) { upd("prefs", { tz: label }); Clock.setTimezone(D.ZONES[label]); }
    // Manual clock rows, when not set automatically: each step moves the system clock
    function rowClock(n, unit, part, max, fmt) {
        void clock;
        var d = new Date();
        return { kind: "val", n: n, v: fmt(d), frac: part(d) / max, c: Ring.GREEN,
                 adj: function (s) {
                     if (!Clock.available) { toast("The clock is set on the head unit"); return; }
                     Clock.adjust(s * unit);
                 } };
    }

    readonly property Connections callEvents: Connections {
        target: Calls
        function onCallsChanged() { hmi.syncCall(); }
        function onAvailableChanged() {
            hmi.syncPhonebook(false);
            hmi.syncMessages();
            if (!Calls.available && hmi.phone.call) hmi.finishCall("Phone disconnected");
        }
        function onMutedChanged() { hmi.upd("phone", { mic: !Calls.muted }); }
        function onDialFailed(number, reason) { hmi.toast("Could not call " + number); }
    }
    function toggleMic() {
        if (phoneReal) { if (Calls.available) Calls.setMuted(phone.mic); return; }
        upd("phone", function (p) { return { mic: !p.mic }; });
    }
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
    // Pictures: the selected photo, whether it fills the page, slideshow
    property var pics: ({ sel: 0, view: false, play: false })
    // The selection, kept inside the library as photos come and go
    function picSel() { return Math.min(pics.sel, Math.max(0, Photos.items.length - 1)); }
    // Trails: the selected track, by file as the list sorts by name, and whether
    // the map's panel of them is open
    property var trails: ({ file: "", picker: false })
    readonly property var trailRows: Trails.items.slice().sort(function (a, b) { return a.n.localeCompare(b.n); })
    function trailIdx() { var f = trails.file; return Math.max(0, trailRows.findIndex(function (r) { return r.file === f; })); }
    // Metres of climb in the units chosen in Settings
    function fmtClimb(m) { return prefs.distUnit === "km" ? Math.round(m).toLocaleString(Qt.locale(), "f", 0) + " m"
                                                          : Math.round(m * 3.28084).toLocaleString(Qt.locale(), "f", 0) + " ft"; }
    // What a row says under the name. A file's alternates and spurs are sections
    // like any other, so the distance is what its sections add up to.
    function trailSub(r) {
        if (!r || !r.file) return "";
        if (!r.ready) return "Reading";
        if (r.bad) return "No track in this file";
        var parts = [fd(r.mi) + (r.parts > 1 ? " in " + r.parts + " sections" : "")];
        if (r.gainM > 0) parts.push(fmtClimb(r.gainM) + " climb");
        return parts.join(" · ");
    }
    // Where a new map should look; a map on screen is moved by mapEase
    property var mapTarget: null
    // Opens the map on the whole of a track and stops it following the vehicle
    function trailShow(r) {
        if (!r || !r.ready || r.bad) return;
        var my = function (lat) { return Math.log(Math.tan(Math.PI / 4 + lat * Math.PI / 360)); };
        var y0 = my(r.south), y1 = my(r.north);
        var spanX = Math.max(r.east - r.west, 1e-4) / 360 * 512;
        var spanY = Math.max(y1 - y0, 1e-4) / (2 * Math.PI) * 512;
        // The room between the map's buttons and between the bars, either side
        // of the map's centre, against the track's size in pixels at zoom 0
        var z = Math.min(Math.log(700 / spanX), Math.log(420 / spanY)) / Math.LN2;
        z = Ring.clamp(z, navZoomMin, navZoomMax);
        var lat = (2 * Math.atan(Math.exp((y0 + y1) / 2)) - Math.PI / 2) * 180 / Math.PI;
        var cam = { center: [lat, (r.west + r.east) / 2], zoom: z, bearing: 0 };
        follow = false;
        mapTarget = cam;
        upd("nav", { zoom: Math.round(z) });
        upd("trails", { picker: false });
        if (app !== "nav") openApp("nav");
        launcher = false;
        mapEase(cam, 700);
    }
    function trailToggle(r) {
        if (!r || !r.ready || r.bad) return;
        Trails.setOn(r.file, !r.on);
        toast((r.on ? "Hidden: " : "On the map: ") + r.n);
    }
    // Guidance to the closest point of the track, through the destination preview
    function trailRoute(r) {
        if (!r || !r.ready || r.bad) return;
        var p = pos(), near = Trails.nearest(r.file, p[0], p[1]);
        if (near.lat === undefined) return;
        upd("trails", { picker: false });
        navSearch = { q: "", sel: 0, preview: { n: r.n, a: "Closest point of the trail", lat: near.lat, lon: near.lon, mi: near.mi } };
        if (Router.available) Router.plan(p[0], p[1], near.lat, near.lon);
        if (app !== "nav") openApp("nav");
    }
    function trailDelete(r) {
        if (!r) return;
        if (gear !== "P") { toast("Shift to Park to delete a trail"); return; }
        askConfirm("Delete " + r.n + "?", "The track is removed from the head unit.", "Delete", function () { Trails.remove(r.file); });
    }
    // Naming: {file, edit, title, note, placeholder, keep}. keep: backing out
    // leaves the name the recording was saved under.
    property var trailName: null
    readonly property int trailNameMax: 40
    readonly property bool trailNaming: !!trailName && gear === "P"
    function trailRename(r) {
        if (!r) return;
        if (gear !== "P") { toast("Shift to Park to rename a trail"); return; }
        trailName = { file: r.file, edit: r.n, title: "Rename Trail", note: "What this track is called in the list and on the map" };
    }
    function trailNameKey(ch) { if (trailName && trailName.edit.length < trailNameMax) upd("trailName", { edit: trailName.edit + ch }); }
    function trailNameBackspace() { if (trailName) upd("trailName", { edit: trailName.edit.slice(0, -1) }); }
    function trailNameCanSave() { return !!trailName && trailName.edit.trim() !== ""; }
    function trailNameSave() {
        if (!trailNameCanSave()) { toast("Type a name first"); return; }
        Trails.rename(trailName.file, trailName.edit);
        toast("Saved: " + trailName.edit.trim());
        trailName = null;
    }
    function trailNameCancel() {
        if (trailName && trailName.keep) toast("Kept as " + trailName.placeholder);
        trailName = null;
    }
    // The record button. What has been recorded is on disk from the start, so a
    // track is saved at once and naming it is only a matter of what it is called.
    function recToggle() {
        if (!Trails.recording) {
            Trails.startRecording();
            toast(Trails.recording ? "Recording your route" : "Could not start recording");
            return;
        }
        var now = new Date(), name = "Track " + Qt.formatDate(now, "MMM d") + ", " + fmtTime(now, true);
        var file = Trails.stopRecording(name);
        if (!file) { toast("Nothing to save: the vehicle has not moved"); return; }
        trailName = { file: file, edit: "", title: "Name This Track", placeholder: name, keep: true, note: "Saved. Give it a name, or keep the date." };
        if (gear !== "P") toast("Track saved. Name it when you park.");
    }
    // The simulator has no receiver: drives a wandering line so recording can be
    // tried. The head unit only ever records real fixes.
    readonly property Timer recSim: Timer {
        property real t: 0
        interval: 1000
        repeat: true
        running: Trails.recording && !Gnss.available && Qt.platform.pluginName !== "eglfs"
        onTriggered: {
            t += 1;
            var c = D.MAPC;
            Trails.addFix(c[0] + t * 0.0003 + 0.001 * Math.sin(t / 7), c[1] + t * 0.0004 + 0.001 * Math.cos(t / 11), 1300, new Date());
        }
    }
    // Videos: the selected video, whether the player is open, playing, and
    // across the whole screen
    property var vids: ({ sel: 0, view: false, play: false, full: false })
    function vidSel() { return Math.min(vids.sel, Math.max(0, Videos.items.length - 1)); }
    // Where the player is and how long the video runs, in seconds, for the knob
    property int vidPos: 0
    property int vidDur: 0
    // The knob skipping through the video; the page moves the player
    signal vidSeek(int secs)
    readonly property bool vidFull: vids.full && vids.view && app === "videos" && !launcher && gear === "P"
    // Changes vids; a video starting pauses the phone's music
    function setVid(patch) {
        if (patch.play && btPlaying) Bt.pause();
        upd("vids", patch);
    }
    // Which USB camera feeds each view: view name -> CameraList id, or "off".
    // Views without one take the remaining cameras in order.
    property var camAssign: ({})
    function camSource(view) {
        var ids = CameraList.devices.map(function (d) { return d.id; });
        var a = camAssign[view];
        if (a === "off") return "";
        if (a) return ids.indexOf(a) >= 0 ? a : "";
        var taken = Object.keys(camAssign).map(function (k) { return camAssign[k]; });
        var free = ids.filter(function (id) { return taken.indexOf(id) < 0; });
        var autoViews = D.CAMS.map(function (c) { return c.n; }).filter(function (n) { return !camAssign[n]; });
        var k = autoViews.indexOf(view);
        return k >= 0 && k < free.length ? free[k] : "";
    }
    function camLabel(id) {
        var L = CameraList.devices, i = L.findIndex(function (x) { return x.id === id; });
        return i < 0 ? "Not connected" : "Camera " + (i + 1) + " · port " + L[i].port + " · " + L[i].name;
    }
    property var prefs: ({ bright: 100, ring: 90, autodim: true, appearance: "Light",
                           phoneVol: 60, bal: 0, fader: 0, bass: 2, mid: 0, treble: 1, touchSound: true, wake: true,
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
    // Bluetooth through BlueZ on the head unit; the demo phones elsewhere.
    // Primary phone and contact sync are kept here, by phone address.
    property var demoBt: ({ on: true, phones: D.PHONES })
    property string btPrimary: ""
    property var btNoContacts: []
    property var btNoMessages: []
    readonly property var bt: Bt.available
        ? { on: Bt.powered, live: true,
            phones: Bt.phones.map(function (p, i) {
                return Object.assign({}, p, { primary: btPrimary ? p.address === btPrimary : i === 0,
                                              contacts: btNoContacts.indexOf(p.address) < 0,
                                              messages: btNoMessages.indexOf(p.address) < 0 });
            }) }
        : demoBt
    // Settings navigation: a stack of pages, the selected row on each, and
    // the overlays a page can raise
    property var setStack: ["root"]
    // The driver safety notice is accepted once per boot, before anything
    // else, and covers every function. The cameras never wait behind it:
    // Reverse, or the notice's own Cameras button, shows them first.
    property bool noticeAccepted: Config.noticeAccepted()
    function acceptNotice() { noticeAccepted = true; Config.acceptNotice(); }
    readonly property bool noticeUp: !noticeAccepted && gear !== "R" && app !== "cams"
    property var setArgs: [null]
    property var setSel: ({})
    property var confirmBox: null     // {title, body, ok, act}
    property var join: null           // {ssid, sec, pw, show, busy}
    property var demoPairing: null
    // {stage: "search" | "confirm" | "busy", phone, code}
    readonly property var pairing: Bt.available ? (Bt.pairing.stage ? Bt.pairing : null) : demoPairing
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
    // "45 min", "3 h 20 min"
    function fmtMins(m) {
        m = Math.max(1, Math.round(m));
        return m < 60 ? m + " min" : Math.floor(m / 60) + " h" + (m % 60 ? " " + (m % 60) + " min" : "");
    }
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
    // What the Radio app says when there is no station to name
    function radioStatus() {
        if (!Radio.available || Radio.seeking) return "Searching";
        if (Radio.scanning) return "Finding stations";
        if (!Radio.tunerFound) return "Tuner not connected";
        return Radio.active ? "No signal" : "Not playing";
    }
    function sig() {
        if (Radio.available) {
            // Paused, it still shows the station it was on
            var heard = Radio.receiving || (!Radio.active && !!Radio.name);
            return { sig: Radio.signal,
                     stn: heard ? { n: Radio.name || radio.fm.toFixed(1) + " FM",
                                    g: Radio.genre || (!Radio.active ? "FM" : Radio.stereo ? "Stereo" : "Mono"),
                                    rt: Radio.text } : null };
        }
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
        if (!routeActive) return 0;
        return steps[nav.step].dir === "arrive" ? 0 : Ring.clamp(1 - nav.dist / 0.6, 0, 1);
    }
    function interlock() { return gear !== "P" && level.jacks !== "Up"; }
    // A prompt is being spoken or shown: the left knob is the guidance volume
    function guideOn() { return (Voice.speaking || clock < guideUntil) && !phone.call; }
    // A little longer once the voice stops, so a turn just after it still
    // adjusts the guidance volume
    readonly property Connections voiceEvents: Connections {
        target: Voice
        function onSpeakingChanged() {
            if (!Voice.speaking) { hmi.guideUntil = Math.max(hmi.guideUntil, Date.now() + 2000); hmi.guideTimer.restart(); }
            hmi.clock = Date.now();
        }
    }
    function recent(s) { return clock - lastTurn[s] < 2000; }
    readonly property bool inCall: !!phone.call
    readonly property bool incoming: inCall && phone.call.state === "incoming"
    // A call or message banner across the top of the screen
    readonly property bool bannerUp: incoming || !!msgNote || asking
    readonly property bool lowBattery: energy.soc < 20

    // ------------------------------------------------------------ app flow
    function openApp(id) {
        console.debug(lcUi, "app", id);
        if (gear === "R" && id !== "cams") { toast("Rear camera stays on in Reverse"); return; }
        if (!trafficAllowed && (id === "aircraft" || id === "ships")) return;
        if (!weatherAllowed && id === "weather") return;
        var call = !!phone.call;
        app = id; launcher = false; edit = false;
        confirmBox = null;
        if (id === "settings") resetSet();
        // Phones send no word of new contacts: read them again when Phone opens
        if (id === "phone" && phoneReal) Phonebook.refresh();
        // Another app stops the video, so its sound does not carry on unseen
        if (id !== "videos") upd("vids", { play: false, full: false });
        // Looking at the radio does not stop the phone's music; tuning does
        if (id === "radio" && audio !== "bt") takeRadio();
        if (id === "aircraft" || id === "ships") {
            upd(trackKey, { list: false });
            if (audio === "radio" && !muted) toast("Radio is off while " + (id === "ships" ? "Ships" : "Aircraft") + " is open");
        }
        // Back from the radio to the music it interrupted
        var back = id === "media" && audio === "radio";
        if (back) takeSource(lastMusic);
        if (id === "media") upd("media", { sel: msrc ? Math.max(0, msrc.queueIndex) : 0, panel: "queue" });
        if (call && (id === "radio" || back)) resume = true;
        if (call) return;
        if (back && msrc && msrc.queueIndex >= 0) msrc.play();
        if (id === "radio") muted = false;
    }
    function toggleLauncher() {
        if (gear === "R") { toast("Rear camera stays on in Reverse"); return; }
        var i = apps.findIndex(function (a) { return a.id === app; });
        lsel = Math.max(0, i);
        launcher = !launcher;
    }
    function pauseForCall() {
        // A message on screen waits for the call to end
        if (msgNote) { msgHeld = msgNote; msgNote = null; }
        upd("vids", { play: false });
        if (audioSrc === "bt") return;  // the phone pauses its own music for a call
        resume = resume || srcPlaying;
        if (msrc) msrc.pause();
    }
    function answerCall() {
        if (phoneReal) { Calls.answer(); return; }
        upd("phone", function (p) { return { call: Object.assign({}, p.call, { state: "active", t: 0 }) }; });
    }
    // c: {n, num, ini}
    function startCall(c) {
        if (!c) return;
        if (phoneReal) {
            if (!Calls.available) { toast("Connect a phone to make calls"); return; }
            Calls.dial(c.num);
            return;
        }
        pauseForCall();
        upd("phone", { call: Object.assign({ state: "active", t: 0 }, c), mic: true });
    }
    // Hang up or decline; the screen follows once the phone reports it
    function endCall(msg) {
        if (phoneReal) { Calls.hangup(); return; }
        finishCall(msg);
    }
    function finishCall(msg) {
        var r = resume, src = audio;
        resume = false;
        upd("phone", { call: null, mic: true, silent: false });
        if (r && msrc && src === audio) msrc.play();
        if (r && src === "radio") muted = false;
        toast(msg + (r ? " · " + D.SRC[audioSrc].n + " resumed" : ""));
        var held = msgHeld;
        msgHeld = null;
        if (held) showMessage(held);
    }
    function simIncoming() {
        if (phone.call) return;
        pauseForCall();
        upd("phone", { silent: false, call: Object.assign({ state: "incoming", t: 0 }, D.CONTACTS[3]) });
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
        if (g !== "P") upd("vids", { play: false, full: false });
        gear = g;
        toast({ P: "Park", R: "Reverse · rear camera on", D: "Drive" }[g]);
    }
    // Next station up, or down with dir -1
    function seek(dir) {
        if (Radio.available) {
            takeRadio();
            muted = false;
            Radio.seek(dir < 0 ? -1 : 1);
            return;
        }
        var R = radio, fm = R.band === "FM", cur = fm ? R.fm : R.am, L = fm ? D.FM : D.AM;
        var nx = dir < 0 ? (L.slice().reverse().find(function (x) { return x.f < cur - 0.001; }) || L[L.length - 1])
                         : (L.find(function (x) { return x.f > cur + 0.001; }) || L[0]);
        upd("radio", fm ? { fm: nx.f } : { am: nx.f });
        takeRadio();
        muted = false;
        toast("Seek · " + nx.n);
    }
    function tuneTo(b, f) {
        if (!f) return;
        upd("radio", b === "FM" ? { band: b, fm: f } : { band: b, am: f });
        takeRadio();
        if (inCall) resume = true; else muted = false;
    }
    // Plays row i of the source's queue (the source being heard unless id says)
    function playTrack(i, id) {
        var s = Music.source(id || audio);
        if (!s) return;
        takeSource(s.id);
        upd("media", { sel: i });
        if (inCall) { resume = true; return; }
        muted = false;
        s.playAt(i);
    }
    // Chooses a source in the Media app; it carries on where it left off
    function chooseSource(id) {
        if (id === audio) return;
        var was = srcPlaying && audio !== "radio";
        takeSource(id);
        var s = Music.source(id);
        upd("media", { sel: s ? Math.max(0, s.queueIndex) : 0, panel: "queue" });
        if (inCall) { resume = resume || was; return; }
        // The phone plays when chosen, as on any head unit; the phone itself
        // decides what (its last app and track)
        if (id === "bt") { if (btPhone && !btPlaying) Bt.play(); return; }
        if (s && was && s.queueIndex >= 0) s.play();
    }
    function toggleTheme() { upd("prefs", { appearance: Theme.dark ? "Light" : "Dark" }); applyAppearance(); }
    // Auto follows the time of day until a light sensor is fitted
    function applyAppearance() {
        var h = new Date().getHours();
        Theme.dark = prefs.appearance === "Dark" || (prefs.appearance === "Auto" && (h >= 19 || h < 7));
    }

    // ------------------------------------------------- knob arbitration
    // Who owns each knob right now: [left, right]
    // noMsg: who would own them without a message or a spoken request showing
    function owner(noMsg) {
        var call = phone.call, inc = call && call.state === "incoming";
        if (gear === "R") return [inc ? "silence" : call ? "call" : "audio", "cams"];
        if (noticeUp) return ["notice", "notice"];
        var L = call ? (inc ? "incoming" : "call") : guideOn() ? "guide" : "audio";
        if (interlock()) return [L, "interlock"];
        if (inc) return ["incoming", "incoming"];
        if (asking && !noMsg) return ["ask", "ask"];
        if (msgStage && !noMsg) return ["message", "message"];
        if (trailNaming) return [L, "trailname"];
        if (launcher) return [L, "launcher"];
        if (app === "climate") return call ? ["call", "climateR"] : ["climate", "climate"];
        if (call && (app === "media" || app === "radio" || app === "weather")) return ["call", "endcall"];
        if (tracking) return [call ? "call" : "track", app];
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
        if (k === "press") { Voice.stop(); guideUntil = 0; }
        if (k === "hold") { Voice.stop(); upd("nav", { voice: false }); guideUntil = 0; toast("Voice guidance off"); }
    }
    function o_notice(s, k, d) {
        if (k === "turn") docScroll(d);
        if (k === "press") acceptNotice();
        if (k === "hold") openApp("cams");
    }
    function o_interlock(s, k, d) { if (k !== "turn" && level.jacks === "Down") retract(); }
    function o_endcall(s, k, d) { if (k !== "turn") endCall("Call ended"); else toast("Audio paused during call"); }
    function o_call(s, k, d) {
        if (k === "turn") vol = Ring.clamp(vol + d * 2, 0, 100);
        if (k === "press") toggleMic();
        if (k === "hold") endCall("Call ended");
    }
    function o_audio(s, k, d) {
        var src = audioSrc;
        if (k === "turn") { vol = Ring.clamp(vol + d * 2, 0, 100); muted = false; }
        // Weather Radio is heard while its app is open, whatever the source is
        if (weatherOpen) { if (k === "press") muted = !muted; return; }
        if (k === "press") srcToggle();
        if (k === "hold") {
            if (src === "bt") { Bt.next(); toast("Next track"); }
            else if (msrc) { msrc.next(); toast("Next track"); }
            else seek();
        }
    }
    function o_launcher(s, k, d) {
        if (k === "turn") lsel = Ring.clamp(lsel + d, 0, apps.length - 1);
        if (k === "press") openApp(apps[lsel].id);
        if (k === "hold") launcher = false;
    }
    function o_climate(s, k, d) { climTurn(s ? "pas" : "drv", s, k, d, clim.mode[s]); }
    function o_climateR(s, k, d) {
        var z = czone ? "pas" : "drv";
        if (k === "press") { czone = czone ? 0 : 1; return; }
        if (k === "turn") climTurn(z, czone, k, d, 0);
    }
    function climTurn(z, s, k, d, m) {
        var mk = D.CMODES[m];
        if (k === "turn") {
            var C = clim, Z = Object.assign({}, C[z]), sync = C.sync, auto = C.auto;
            if (mk === "Temp") { Z.t = Ring.clamp(Z.t + d, 60, 84); if (z === "pas") sync = false; }
            if (mk === "Air") Z.air = D.AIR[Ring.clamp(airIndex(Z.air) + d, 0, D.AIR.length - 1)].k;
            if (mk === "Fan") { Z.fan = Ring.clamp(Z.fan + d, 0, D.FAN.length - 1); if (Z.fan !== fanAuto) auto = false; }
            if (mk === "Seat") Z.seat = Ring.clamp(Z.seat + d, -3, 3);
            var nc = Object.assign({}, C, { sync: sync, auto: auto });
            nc[z] = Z;
            if (mk === "Temp" && z === "drv" && sync) nc.pas = Object.assign({}, C.pas, { t: Z.t });
            clim = nc;
        }
        if (k === "press") upd("clim", function (c) { return { mode: c.mode.map(function (v, i) { return i === s ? (v + 1) % D.CMODES.length : v; }) }; });
        if (k === "hold") {
            if (s === 0) { var a = clim.auto; setClimAuto(!a); toast(a ? "Auto off" : "Auto on"); }
            else {
                var sy = clim.sync;
                upd("clim", function (c) { return { sync: !c.sync, pas: !c.sync ? Object.assign({}, c.pas, { t: c.drv.t }) : c.pas }; });
                toast(sy ? "Sync off" : "Passenger synced to driver");
            }
        }
    }
    // Touch: the same temperature step the zone's knob makes, sync included
    function nudgeTemp(z, d) { climTurn(z, z === "pas" ? 1 : 0, "turn", d, 0); }
    // Setting the fan by hand leaves Auto, as the knob does
    function setZone(z, patch) {
        upd("clim", function (c) {
            var o = {}; o[z] = Object.assign({}, c[z], patch);
            if ("fan" in patch && patch.fan !== fanAuto) o.auto = false;
            return o;
        });
    }
    // A zone's fan is an index into D.FAN: Off, Auto, 1-5, Max
    readonly property int fanAuto: 1
    // How hard the fan blows, 0 (off) to 6 (Max); Auto runs at a middle speed
    function fanSpeed(i) { return i === fanAuto ? 3 : Math.max(0, i - 1); }
    function airIndex(k) { return Math.max(0, D.AIR.findIndex(function (a) { return a.k === k; })); }
    // Climate Auto puts both fans on Auto; leaving it sets them to speed 3
    function setClimAuto(on) {
        upd("clim", function (c) {
            var fan = function (Z) { return Object.assign({}, Z, { fan: on ? fanAuto : Z.fan === fanAuto ? 4 : Z.fan }); };
            return { auto: on, drv: fan(c.drv), pas: fan(c.pas) };
        });
    }
    function toggleClim(k) {
        if (k === "auto") { setClimAuto(!clim.auto); return; }
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
        takeRadio();
        upd("radio", function (r) {
            return r.band === "FM" ? { fm: Math.round(Ring.clamp(r.fm + d * 0.2, 87.9, 107.9) * 10) / 10 }
                                   : { am: Ring.clamp(r.am + d * 10, 530, 1700) };
        });
    }
    function savePreset(i) {
        var R = radio, p = R.presets.slice();
        p[i] = { b: R.band, f: R.band === "FM" ? R.fm : R.am };
        upd("radio", { presets: p, ptr: (i + 1) % 6 });
        if (Radio.available) Radio.savePresets(p);
        toast("Saved to preset " + (i + 1));
    }
    // Plays row i of the Media app's Browse list
    function openMusicList(i) {
        var M = Music.source(audio === "radio" ? lastMusic : audio);
        if (!M || i < 0 || i >= M.lists.length) return;
        takeSource(M.id);
        upd("media", { bsel: i, sel: 0, panel: M.hasQueue ? "queue" : "browse" });
        if (inCall) { resume = true; return; }
        M.openList(M.lists[i].id);
    }
    // The right knob in Media: moves through Browse or the queue where the
    // source has them, else skips tracks. Hold changes source.
    function a_media(s, k, d) {
        var M = msrc, n = M && M.hasQueue ? M.queue.length : 0;
        if (k === "hold") { nextSource(); return; }
        if (M && media.panel === "browse" && M.lists.length) {
            if (k === "turn") upd("media", function (m) { return { bsel: Ring.clamp((m.bsel || 0) + d, 0, M.lists.length - 1) }; });
            if (k === "press") openMusicList(Math.min(media.bsel || 0, M.lists.length - 1));
            return;
        }
        if (!n) {
            if (k === "turn") { if (audio === "bt") { if (d > 0) Bt.next(); else Bt.previous(); } else if (M) { if (d > 0) M.next(); else M.previous(); } }
            if (k === "press") srcToggle();
            return;
        }
        if (k === "turn") upd("media", function (m) { return { sel: Ring.clamp(m.sel + d, 0, n - 1), panel: "queue" }; });
        if (k === "press") playTrack(Math.min(media.sel, n - 1));
    }
    // Plays music by name from a Media source, for the voice assistant:
    // playMusic("Hotel California") or playMusic("Eagles", "usb"). Without a
    // source, the one heard now when it has a queue, else Local. query "" plays
    // the whole library; shuffle turns shuffle on first (never off: that stays
    // the driver's setting)
    function playMusic(query, id, shuffle) {
        var sid = id || (msrc && msrc.hasQueue ? msrc.id : "local");
        var s = Music.source(sid);
        if (!s) return;
        takeSource(sid);
        upd("media", { panel: s.hasQueue ? "queue" : "browse", sel: 0, bsel: 0 });
        if (shuffle) s.setShuffle(true);
        if (inCall) { resume = true; return; }
        muted = false;
        if (query) s.playSearch(query);
        else { s.openList("all"); toast((shuffle ? "Shuffling " : "Playing ") + D.SRC[sid].n); }
    }
    // The Media app's sources in the order its buttons show them
    readonly property var musicSources: ["bt", "usb", "local"]
    function nextSource() {
        var L = musicSources, i = L.indexOf(audio);
        var id = L[(i + 1) % L.length];
        chooseSource(id);
        toast(D.SRC[id].n);
    }
    // The map view listens to these; it owns the camera
    signal mapEase(var camera, int duration)
    // About & Legal's text page listens; turns scroll it
    signal docScroll(int d)
    // A legal page's notice followed by its license texts
    function legalText(key) {
        var dc = L.doc(key);
        return [dc.head].concat((dc.lic || []).map(function (id) { return "\u2014 " + id + " \u2014\n\n" + Config.licenseText(id); })).join("\n\n\n");
    }
    // ---------------------------------------------------- destination search
    function openSearch() { navSearch = { q: "", sel: 0, preview: null }; Places.search("", 0, 0); }
    function closeSearch() { navSearch = null; Router.clear(); }
    function searchSet(patch) { navSearch = Object.assign({}, navSearch, patch); }
    // A new query goes to Photon on the head unit, near the map's center
    function searchQuery(q) { searchSet({ q: q, sel: 0, preview: null }); var p = pos(); Places.search(q, p[0], p[1]); }
    function searchKey(ch) { if (navSearch && gear === "P") searchQuery(navSearch.q + ch); }
    function searchBackspace() { if (navSearch) searchQuery(navSearch.q.slice(0, -1)); }
    function searchClear() { if (navSearch) searchQuery(""); }
    // Photon's matches for the query. No saved or recent places yet.
    function searchRows() { return navSearch && navSearch.q.trim() !== "" ? Places.results : []; }
    // Choosing a place plans the route to it, for the preview
    function searchPick(i) {
        var r = searchRows()[i];
        if (!r) return;
        searchSet({ sel: i, preview: r });
        var p = pos();
        if (Router.available) Router.plan(p[0], p[1], r.lat, r.lon);
    }
    function searchBack() { searchSet({ preview: null }); Router.clear(); }
    // The preview's route becomes the active one
    function startRoute(place) {
        // Router.route reads live, and closing search clears it: copy what is
        // needed first
        var R = Router.route;
        if (!R.steps || !R.steps.length) {
            toast(!Router.available ? "Routing is not installed" : Router.busy ? "Still finding a route" : "No route to " + place.n);
            return;
        }
        var st = R.steps.map(function (x) { return Object.assign({}, x); });
        var say = String(R.say || ""), shape = R.shape.slice();
        st[st.length - 1].st = place.n;
        steps = st;
        dest = place;
        routeShape = shape;
        navIdx = 0;
        offRoute = 0;
        routeActive = true;
        upd("nav", { step: 0, dist: st[0].d });
        closeSearch();
        driveView(st[0].d);
        prompt(st[0], "Route to " + place.n, say);
        toast("Route to " + place.n);
    }
    // Starting guidance: follow the vehicle, centred, zoomed for how far off the
    // first maneuver is (street level for a turn just ahead, wider for a long
    // first leg)
    function driveView(firstMiles) {
        var z = firstMiles < 0.5 ? 17 : firstMiles < 3 ? 16 : 15;
        follow = true;
        upd("nav", { zoom: z });
        var cam = { center: pos(), zoom: z };
        if (nav.up === "heading" && Gnss.fix && Gnss.speed > 3) cam.bearing = Gnss.course;
        mapEase(cam, 800);
    }
    // arrived: the arrival prompt is still being spoken, so leave it
    function endRoute(arrived) {
        routeActive = false; rerouting = false; routeShape = []; guideUntil = 0;
        if (!arrived) { Voice.stop(); toast("Route ended"); }
    }
    function routeMiles() { return nav.dist + steps.slice(nav.step + 1).reduce(function (a, x) { return a + x.d; }, 0); }
    // Valhalla's time for what is left: this step's share, then the rest
    function routeMins() {
        if (!routeActive) return 0;
        var cur = steps[nav.step], part = cur.d > 0 ? cur.t * nav.dist / cur.d : 0;
        return part + steps.slice(nav.step + 1).reduce(function (a, x) { return a + x.t; }, 0);
    }

    function a_nav(s, k, d) {
        if (navSearch) {
            var R = searchRows();
            if (navSearch.preview) {
                if (k === "press") startRoute(navSearch.preview);
                if (k === "hold") searchBack();
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
    readonly property int navZoomMin: 3    // all of the US on the screen
    readonly property int navZoomMax: 18
    function navZoom(d) { var z = Ring.clamp(nav.zoom + d, navZoomMin, navZoomMax); upd("nav", { zoom: z }); mapEase({ zoom: z }, 200); }
    // A pinch moved the map itself: the knob and buttons carry on from there
    function navPinched(z) { if (Math.round(z) !== nav.zoom) upd("nav", { zoom: Math.round(z) }); }
    function navRecenter() { follow = true; mapTarget = null; mapEase({ center: pos() }, 300); toast("Recentered"); }
    function navToggleUp() {
        var u = nav.up === "north" ? "heading" : "north";
        upd("nav", { up: u });
        if (u === "north") mapEase({ bearing: 0 }, 400);
        else if (Gnss.fix && Gnss.speed > 3) mapEase({ bearing: Gnss.course }, 400);
        toast(u === "north" ? "North up" : "Direction of travel up");
    }
    function navToggleView() {
        var v = nav.view === "2D" ? "3D" : "2D";
        upd("nav", { view: v }); mapEase({ pitch: v === "3D" ? 55 : 0 }, 300); toast(v + " view");
    }
    // What the right knob moves through: favorites then recents, or contacts
    function phoneList() {
        if (phone.tab === "all") return contacts.map(function (c) { return { c: c, fav: false }; });
        return favs.map(function (c) { return { c: c, fav: true }; })
            .concat(recents.map(function (r) { return { c: r, fav: false }; }));
    }
    function phoneTab(t) { upd("phone", { tab: t, sel: 0 }); }
    function a_phone(s, k, d) {
        if (phone.call) { if (k !== "turn") endCall("Call ended"); return; }
        var L = phoneList();
        if (k === "turn") upd("phone", function (p) { return { sel: Ring.clamp(p.sel + d, 0, L.length - 1) }; });
        if (k === "press" && L[phone.sel]) startCall(L[phone.sel].c);
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
    function a_pictures(s, k, d) {
        var n = Photos.items.length;
        if (gear !== "P") { if (k !== "turn") toast("Shift to Park to view photos"); return; }
        if (!n) return;
        if (k === "turn") upd("pics", { sel: Ring.clamp(picSel() + d, 0, n - 1) });
        if (k === "press") upd("pics", function (p) { return p.view ? { play: !p.play } : { view: true }; });
        if (k === "hold" && pics.view) upd("pics", { view: false, play: false });
        else if (k === "hold" && Photos.usbNew && !Photos.importing) Photos.importUsb();
    }
    function a_trails(s, k, d) {
        if (confirmBox) { if (k === "press") doConfirm(); else if (k === "hold") confirmBox = null; return; }
        var n = trailRows.length;
        if (!n) {
            if (k === "hold" && gear === "P" && Trails.usbNew && !Trails.importing) Trails.importUsb();
            return;
        }
        if (k === "turn") upd("trails", { file: trailRows[Ring.clamp(trailIdx() + d, 0, n - 1)].file });
        if (k === "press") trailToggle(trailRows[trailIdx()]);
        if (k === "hold") trailShow(trailRows[trailIdx()]);
    }
    function a_trailname(s, k, d) {
        if (k === "press") trailNameSave();
        if (k === "hold") trailNameCancel();
    }
    function a_videos(s, k, d) {
        var n = Videos.items.length, V = vids;
        if (gear !== "P") { if (k !== "turn") toast("Shift to Park to watch videos"); return; }
        if (k === "hold" && !V.view) { if (Videos.usbNew && !Videos.importing) Videos.importUsb(); return; }
        if (!n) return;
        if (!V.view) {
            if (k === "turn") upd("vids", { sel: Ring.clamp(vidSel() + d, 0, n - 1) });
            if (k === "press") setVid({ view: true, play: true });
            return;
        }
        if (k === "turn") vidSeek(d * 10);
        if (k === "press") setVid({ play: !V.play });
        if (k === "hold") upd("vids", V.full ? { full: false } : { view: false, play: false });
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
    // CAN 1 / CAN 2 port settings, saved and applied by CanBus
    readonly property var can: CanBus.ports

    function resetSet() { setStack = ["root"]; setArgs = [null]; confirmBox = null; join = null; endPairing(); }
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
        if (page === "deviceName") nameEdit = Bt.name;
        if (page === "btPair") startPairing();
        if (page === "wifi" && Net.available && wifi.on) Net.scan();
    }
    function backSet() {
        if (setStack.length < 2) return;
        if (setPage === "wifiJoin") join = null;
        if (setPage === "btPair") endPairing();
        setStack = setStack.slice(0, -1);
        setArgs = setArgs.slice(0, -1);
        edit = false;
    }
    function selSet(i) { var o = Object.assign({}, setSel); o[setPage] = i; setSel = o; edit = false; }
    function setTitle(page, arg) {
        if (page === "wifiNet") return arg;
        if (page === "btPhone") return bt.phones[arg] ? bt.phones[arg].name : "Phone";
        if (page === "canPort") return "CAN " + (arg + 1);
        if (page === "legalDoc") return arg ? L.doc(arg).title : "";
        return { root: "Settings", wifi: "Wi-Fi", wifiJoin: "Join Network", deviceName: "Device Name", bt: "Bluetooth & Phones", btPair: "Pair New Phone", cams: "Cameras",
                 can: "CAN Buses", display: "Display", sound: "Sound", navset: "Navigation", units: "Units, Date & Time",
                 system: "System", about: "About & Legal", licenses: "Open Source Licenses" }[page] || "Settings";
    }

    // Row builders
    function rowVal(n, key, min, max, step, fmt, owner) {
        var x = owner ? hmi[owner] : prefs[key];
        var put = function (y) {
            y = Ring.clamp(y, min, max);
            if ((owner ? hmi[owner] : prefs[key]) === y) return;
            if (owner) hmi[owner] = y; else { var o = {}; o[key] = y; upd("prefs", o); }
        };
        return { kind: "val", n: n, v: fmt(x), frac: (x - min) / (max - min), c: Ring.GREEN,
                 adj: function (d) { put((owner ? hmi[owner] : prefs[key]) + d * step); },
                 // A point along the bar, 0 to 1, by touch: on a step
                 setFrac: function (f) { put(min + Math.round(Ring.clamp(f, 0, 1) * (max - min) / step) * step); } };
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
            rows.push(rowSw("Bluetooth", bt.on, function (v) { btSetOn(v); }));
            if (!bt.on) return rows;
            bt.phones.forEach(function (ph, i) {
                rows.push({ kind: "phone", n: ph.name, v: (ph.connected ? "Connected" : "Paired") + (ph.primary ? " · Primary" : ""),
                            connected: ph.connected, primary: ph.primary, act: function () { openSet("btPhone", i); } });
            });
            rows.push({ kind: "page", n: "Pair New Phone", icon: "phone-portrait-outline", page: "btPair", v: "" });
            rows.push({ kind: "page", n: "Visible As", page: "deviceName", v: Bt.name });
            return rows;
        case "btPhone": {
            var ph = bt.phones[arg];
            if (!ph) return [{ kind: "info", n: "Phone", v: "Removed" }];
            var setPh = function (patch) { upd("demoBt", { phones: demoBt.phones.map(function (x, j) { return j === arg ? Object.assign({}, x, patch) : x; }) }); };
            rows.push({ kind: "action", n: ph.connected ? "Disconnect" : "Connect", v: "", act: function () {
                if (bt.live) {
                    if (ph.connected) Bt.disconnectPhone(ph.address); else Bt.connectPhone(ph.address);
                    toast((ph.connected ? "Disconnecting " : "Connecting to ") + ph.name);
                    return;
                }
                setPh({ connected: !ph.connected }); toast((ph.connected ? "Disconnected " : "Connected ") + ph.name);
            } });
            rows.push(rowSw("Primary Phone", ph.primary, function (v) {
                if (bt.live) { btPrimary = v ? ph.address : (bt.phones.find(function (x) { return x.address !== ph.address; }) || {}).address || ""; return; }
                upd("demoBt", { phones: demoBt.phones.map(function (x, j) { return Object.assign({}, x, { primary: j === arg ? v : (v ? false : x.primary) }); }) });
            }, "Calls and messages go to this phone when two are connected"));
            rows.push(rowSw("Sync Contacts", ph.contacts, function (v) {
                if (bt.live) { btNoContacts = v ? btNoContacts.filter(function (a) { return a !== ph.address; }) : btNoContacts.concat([ph.address]); return; }
                setPh({ contacts: v });
            }));
            rows.push(rowSw("Message Notifications", ph.messages !== false, function (v) {
                if (bt.live) { btNoMessages = v ? btNoMessages.filter(function (a) { return a !== ph.address; }) : btNoMessages.concat([ph.address]); return; }
                setPh({ messages: v });
            }, "Announces new texts; press Read to hear them"));
            if (bt.live) rows.push({ kind: "info", n: "Address", v: ph.address });
            else rows.push({ kind: "info", n: "Model", v: ph.model });
            rows.push({ kind: "action", n: "Forget Phone", danger: true, v: "", act: function () {
                askConfirm("Forget " + ph.name + "?", "You will need to pair it again to use it in the vehicle.", "Forget", function () {
                    if (bt.live) Bt.forget(ph.address);
                    else upd("demoBt", { phones: demoBt.phones.filter(function (x, j) { return j !== arg; }) });
                    backSet();
                    toast("Forgot " + ph.name);
                });
            } });
            return rows;
        }
        case "cams":
            D.CAMS.forEach(function (c) {
                var opts = ["auto", "off"].concat(CameraList.devices.map(function (d) { return d.id; }));
                var cur = camAssign[c.n] || "auto";
                if (opts.indexOf(cur) < 0) opts.push(cur); // assigned but unplugged
                rows.push(rowChoice(c.n, opts, cur, function (v) {
                    var o = Object.assign({}, camAssign);
                    if (v === "auto") delete o[c.n]; else o[c.n] = v;
                    camAssign = o;
                }, function (o) {
                    if (o === "auto") { var s = camSource(c.n); return "Automatic" + (s ? ": " + camLabel(s) : ""); }
                    return o === "off" ? "Off" : camLabel(o);
                }));
            });
            if (!CameraList.devices.length) rows.push({ kind: "info", n: "USB Cameras", v: "None connected" });
            return rows;
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
            var det = CanBus.detection, detecting = det.port === arg && det.state === "running";
            rows.push({ kind: "action", icon: "search", v: "",
                        n: detecting ? (det.rate ? "Listening at " + D.fmtRate(det.rate) + "…" : "Starting…") : "Detect Rates",
                        desc: detecting ? "Step " + det.step + " of " + det.steps + ". Listening only: nothing is sent on the bus."
                                        : "Listens at each bit rate, then each data rate, and offers the one that receives frames without errors. Nothing is sent on the bus. Needs traffic on the bus, so have the vehicle on.",
                        act: function () { detectCan(arg); } });
            rows.push({ kind: "info", n: "Status", v: canStatus(arg) });
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
        case "voice":
            return [
                rowSw("Hey Peregrine", prefs.wake, pref("wake"), "Listen for \"Hey Peregrine\", then ask for directions, a call, a text or a radio station"),
                { kind: "info", n: "Microphone", v: Wake.listening ? "Listening" : !Wake.available ? "Not installed" : prefs.wake ? "Paused" : "Off" },
                { kind: "info", n: "Language Model", v: Llm.available ? "Ready" : "Starting up" },
                { kind: "info", n: "Try Saying", v: "\"Get directions to Moab\"" }
            ];
        case "units":
            return [
                rowChoice("Temperature", ["F", "C"], prefs.tempUnit, pref("tempUnit"), function (u) { return "°" + u; }),
                rowChoice("Distance", ["mi", "km"], prefs.distUnit, pref("distUnit"), function (u) { return u === "mi" ? "Miles" : "Kilometres"; }),
                rowChoice("Liquids", ["gal", "L"], prefs.volUnit, pref("volUnit"), function (u) { return u === "gal" ? "Gallons" : "Litres"; }),
                rowChoice("Clock", [false, true], prefs.clock24, pref("clock24"), function (b) { return b ? "24-hour" : "12-hour"; }),
                rowSw("Set Time Automatically", prefs.autoTime, pref("autoTime"), "From the GPS receiver")
            ].concat(prefs.autoTime ? [] : [
                rowClock("Date", 86400, function (d) { return d.getDate() - 1; }, 30,
                         function (d) { return d.toLocaleDateString(Qt.locale("en_US"), "ddd MMM d, yyyy"); }),
                rowClock("Hour", 3600, function (d) { return d.getHours(); }, 23, function (d) { return fmtTime(d, true); }),
                rowClock("Minute", 60, function (d) { return d.getMinutes(); }, 59, function (d) { return fmtTime(d, true); })
            ]).concat([
                rowChoice("Time Zone", D.TIMEZONES, prefs.tz, setTimeZone)
            ]);
        case "about":
            return [
                { kind: "page", n: "Godwit", page: "legalDoc", arg: "godwit", v: "MIT" },
                { kind: "page", n: "Driver Safety Notice", page: "legalDoc", arg: "safety", v: "" },
                { kind: "page", n: "Open Source Licenses", page: "licenses", v: L.COMPONENTS.length + " components" },
                { kind: "page", n: "Map Data", page: "legalDoc", arg: "maps", v: "OpenStreetMap" },
                { kind: "page", n: "Operating System", page: "legalDoc", arg: "os", v: "AGL" },
                { kind: "page", n: "Source Code", page: "legalDoc", arg: "source", v: "" }
            ];
        case "licenses":
            return L.COMPONENTS.map(function (c, i) { return { kind: "page", n: c.n, page: "legalDoc", arg: "c" + i, v: c.lic[0] }; });
        case "system":
            return [
                { kind: "info", n: "Software", v: "Godwit " + L.VERSION },
                { kind: "info", n: "Platform", v: "AGL 22.0.0" },
                { kind: "action", n: "Check for Updates", icon: "download-outline", v: "", act: function () { toast("Godwit is up to date"); } },
                { kind: "page", n: "Device Name", page: "deviceName", v: Bt.name },
                rowChoice("Country", ["", "US", "Other"], Config.country, function (c) { Config.country = c; },
                          function (c) { return c === "US" ? "United States" : c === "Other" ? "Other" : "Not set"; }),
                { kind: "info", n: "Aircraft and Ships", v: trafficAllowed ? "Available" : "United States only" },
                { kind: "info", n: "Weather Radio", v: weatherAllowed ? "Available" : "United States only" },
                { kind: "page", n: "About & Legal", icon: "information-circle-outline", page: "about", v: "" },
                rowSw("Verbose Logging", Logs.verbose, function (v) { Logs.verbose = v; },
                      "For troubleshooting. Also records what is said, messages and phone details. Turning it off clears the logs."),
                { kind: "action", n: "Restart", icon: "power-outline", v: "", act: function () {
                    askConfirm("Restart Godwit?", "The screen goes dark until it has started again.", "Restart", function () { toast("Restarting"); Power.restart(); });
                } },
                { kind: "action", n: "Factory Reset", danger: true, v: "", act: function () {
                    askConfirm("Erase everything?",
                               "Removes paired phones, Wi-Fi networks, settings, the device name, messages, photos, videos, music, routes and logs. Maps stay installed.",
                               "Continue", function () {
                        askConfirm("This cannot be undone",
                                   "Everything personal on this head unit is erased for good and cannot be recovered. It restarts as it was out of the box.",
                                   "Erase Everything", function () { toast("Erasing"); Power.factoryReset(); });
                    });
                } }
            ];
        }
        return rows;
    }
    function setSummary(page) {
        switch (page) {
        case "wifi": return !wifi.on ? "Off" : wifi.ssid || "Not connected";
        case "cams": return CameraList.devices.length ? CameraList.devices.length + " connected" : "None connected";
        case "bt": {
            if (!bt.on) return "Off";
            var c = bt.phones.find(function (p) { return p.connected; });
            return c ? c.name : bt.phones.length + " paired";
        }
        case "can": return can.map(function (c, i) { return (i + 1) + ": " + (c.role === "Off" ? "Off" : c.role); }).join(" · ");
        case "display": return prefs.appearance;
        case "sound": return "Volume " + vol + "%";
        case "navset": return nav.voice ? "Voice on" : "Voice off";
        case "voice": return prefs.wake ? "Hey Peregrine on" : "Off";
        case "units": return "°" + prefs.tempUnit + " · " + (prefs.distUnit === "mi" ? "Miles" : "Km") + " · " + (prefs.clock24 ? "24h" : "12h");
        case "system": return "Godwit " + L.VERSION;
        }
        return "";
    }
    function canSummary(c) {
        if (c.role === "Off") return "Not used";
        return c.role + " · " + (c.fd ? "FD " + D.fmtRate(c.rate) + " / " + D.fmtRate(c.drate) : D.fmtRate(c.rate)) + (c.listen ? " · Listen only" : "");
    }
    // The interface behind a port, and whether the rate it is set to matches
    // the bus: frames decoding, errors (traffic at another rate) or silence
    function canStatus(i) {
        var s = CanBus.status[i] || {};
        if (!s.iface) return "Not found";
        if (CanBus.detection.port === i && CanBus.detection.state === "running") return s.iface + " · Detecting rates";
        if (can[i].role === "Off") return s.iface + " · Off";
        if (!s.up) return s.iface + " · Down";
        if (s.fps > 0) return s.iface + " · Receiving · " + s.fps.toLocaleString(Qt.locale(), "f", 0) + " frames/s" + (s.eps > 0 ? " · errors" : "");
        if (s.eps > 0) return s.iface + " · Errors · check the bit rate";
        return s.iface + " · No traffic";
    }
    function detectCan(i) {
        if (CanBus.detection.state === "running") { toast("Already detecting CAN " + (CanBus.detection.port + 1)); return; }
        if (!CanBus.detect(i)) { toast("CAN " + (i + 1) + ": rates can only be detected on the head unit"); return; }
        toast("Detecting CAN " + (i + 1) + " rates");
    }
    // When a detection finishes: offer what it found, or say why it found nothing
    function canDetected() {
        var d = CanBus.detection, i = d.port, name = "CAN " + (i + 1);
        if (d.state === "failed") { toast(name + ": rate detection failed"); return; }
        if (d.state !== "done") return;
        if (d.result === "nobus") { toast(name + ": no traffic heard. Is the vehicle on?"); return; }
        if (d.result === "errors") { toast(name + ": traffic heard, but no rate decodes it"); return; }
        var rate = D.fmtRate(d.rate);
        if (d.data === "found") {
            askConfirm(name + ": " + rate + " · " + D.fmtRate(d.drate) + " data",
                       "Frames decode without errors at " + rate + ", switching to " + D.fmtRate(d.drate) + " for their data. Use these rates, with CAN FD on?",
                       "Use", function () { setCan(i, { rate: d.rate, drate: d.drate, fd: true }); }, true);
        } else {
            askConfirm(name + ": " + rate,
                       "Frames decode at " + rate + ". " + (d.data === "nofast"
                           ? "None switched to a faster data rate, so the data rate could not be checked and stays at " + D.fmtRate(can[i].drate) + "."
                           : "Frames that switch to a faster data rate had errors at every data rate tried, so the data rate stays at " + D.fmtRate(can[i].drate) + ".")
                       + " Use " + rate + "?",
                       "Use", function () { setCan(i, { rate: d.rate }); }, true);
        }
    }
    readonly property Connections canDetectWatch: Connections {
        target: CanBus
        function onDetectionChanged() { hmi.canDetected(); }
    }
    function setCan(i, patch) {
        if (!CanBus.save(can.map(function (c, j) { return j === i ? Object.assign({}, c, patch) : c; }))) {
            toast("CAN " + (i + 1) + " not changed");
            return;
        }
        toast("CAN " + (i + 1) + " restarted · " + canSummary(can[i]));
    }
    // A bus can only be on one port: picking a role the other port has swaps them
    function setCanRole(i, role) {
        var other = 1 - i, swapped = role !== "Off" && can[other].role === role;
        var was = can[i].role;
        if (!CanBus.save(can.map(function (c, j) {
            if (j === i) return Object.assign({}, c, { role: role, listen: role === "Vehicle" ? true : c.listen });
            if (j === other && swapped) return Object.assign({}, c, { role: was, listen: was === "Vehicle" });
            return c;
        }))) {
            toast("CAN " + (i + 1) + " not changed");
            return;
        }
        toast(swapped ? "Swapped: CAN " + (i + 1) + " is " + role + ", CAN " + (other + 1) + " is " + can[other].role
                      : "CAN " + (i + 1) + " set to " + role);
    }

    // safe: the action is routine (accent colour), not destructive (alert colour)
    function askConfirm(title, body, ok, act, safe) { confirmBox = { title: title, body: body, ok: ok, act: act, safe: !!safe }; }
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
    // Settings > System > Device Name, on the same keyboard
    readonly property int nameMax: 24
    property string nameEdit: ""
    function nameKey(ch) { if (nameEdit.length < nameMax) nameEdit += ch; }
    function nameBackspace() { nameEdit = nameEdit.slice(0, -1); }
    function nameCanSave() { return gear === "P" && nameEdit.trim() !== ""; }
    function nameSave() {
        if (gear !== "P") { toast("Shift to Park to change the name"); return; }
        if (!nameCanSave()) { toast("Type a name first"); return; }
        Bt.name = nameEdit;
        toast("Phones will see " + nameEdit.trim());
        backSet();
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
    function startPairing() {
        if (Bt.available) { Bt.startPairing(); return; }
        demoPairing = { stage: "search" };
        pairTimer.restart();
    }
    function endPairing() {
        if (Bt.available) Bt.stopPairing();
        demoPairing = null;
        pairTimer.stop();
    }
    function btSetOn(v) {
        console.info("Bluetooth", v ? "on" : "off", Bt.available ? "" : "(demo, BlueZ not reachable)");
        if (Bt.available) Bt.setPowered(v); else upd("demoBt", { on: v });
        toast(v ? "Bluetooth on" : "Bluetooth off");
    }
    readonly property Connections btEvents: Connections {
        target: Bt
        function onPaired(name) {
            if (hmi.setPage === "btPair") hmi.backSet();
            hmi.toast("Paired with " + name);
        }
        function onPairingFailed(reason) { hmi.toast("Pairing did not finish. Try again from the phone."); }
        function onConnectFailed(name, reason) { hmi.toast("Could not connect to " + name); }
    }
    readonly property Timer pairTimer: Timer {
        interval: 3000
        onTriggered: {
            var code = String(100000 + Math.floor(Math.random() * 900000));
            hmi.demoPairing = { stage: "confirm", phone: D.PAIRING_PHONE, code: code.slice(0, 3) + " " + code.slice(3) };
        }
    }
    function pairAccept() {
        if (!pairing || pairing.stage !== "confirm") return;
        if (Bt.available) { Bt.acceptPairing(); return; }
        var ph = pairing.phone;
        demoPairing = Object.assign({}, pairing, { stage: "busy" });
        pairDone.phone = ph;
        pairDone.restart();
    }
    readonly property Timer pairDone: Timer {
        property var phone
        interval: 1200
        onTriggered: {
            var first = hmi.demoBt.phones.length === 0;
            hmi.upd("demoBt", { phones: hmi.demoBt.phones.concat([{ name: phone.name, model: phone.model, connected: true, primary: first, contacts: true }]) });
            hmi.demoPairing = null;
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
        if (setPage === "deviceName") { if (k === "press") nameSave(); else if (k === "hold") backSet(); return; }
        if (setPage === "btPair") { if (k === "press") pairAccept(); else if (k === "hold") backSet(); return; }
        if (setPage === "legalDoc") { if (k === "turn") docScroll(d); else if (k === "hold") backSet(); return; }
        var R = setRows(setPage, setArg), i = Math.min(setSel[setPage] || 0, R.length - 1), r = R[i];
        if (edit) { if (k === "turn") r.adj(d); else edit = false; return; }
        if (k === "turn") { selSet(Ring.clamp(i + d, 0, R.length - 1)); return; }
        if (k === "press") setActivate(r);
        if (k === "hold") backSet();
    }
    // Touch: the first tap selects a row, a second tap acts on it
    // One tap acts on the row, anywhere along it. Selecting first is for the
    // knob; touch should not need a second tap.
    function setTap(i) {
        var R = setRows(setPage, setArg), cur = Math.min(setSel[setPage] || 0, R.length - 1);
        if (i !== cur) selSet(i);
        var r = R[i];
        if (!r) return;
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
        if (msgNote && msgNote.until && Date.now() > msgNote.until) closeMessage();
        if (edit && Date.now() - lastInput > 6000) edit = false;
        if (prefs.appearance === "Auto") applyAppearance();
        clock = Date.now();
    }

    // ------------------------------------------------- encoder descriptors
    // What each knob's round display shows and what its ring does. mkEnc() fills
    // defaults; spec is the ring pattern. The display is 240 px round: the value
    // (or without one the icon) is centred, the label sits above and the sub
    // below, and an icon is preferred to a label wherever one says it. Every
    // volume carries the "ramp" scale, the wedge a car's volume control shows.
    function mkEnc(o) {
        var v = o.value === undefined || o.value === null ? "" : String(o.value);
        return {
            label: o.label || "", icon: o.icon || "", iconSize: o.iconSize || (v ? 30 : 56),
            value: v, unit: o.unit || "", sub: o.sub || "",
            color: o.color || "tx", small: !!o.small,
            vSize: o.vSize || (o.small ? (v.length > 9 ? 22 : v.length > 6 ? 26 : 30) : (v.length > 4 ? 46 : 64)),
            scale: o.scale || "", scaleSteps: o.steps || 0, scalePos: o.pos || 0,
            pager: o.pager || [], spec: o.spec || null, note: o.note || "",
            idc: o.idc || Ring.GREEN, holdC: o.hold ? (o.holdC || Ring.WHITE) : null,
            name: o.name || o.label || "",
            hint: [o.press ? "Press · " + o.press : "", o.hold ? "Hold · " + o.hold : ""].filter(Boolean).join(" / ")
        };
    }

    function navCue() {
        var N = nav, u = urg(), col = Ring.urgCol(u);
        if (!routeActive)
            return { D: { i: "", a: 0, l: "", seq: [] }, u: 0, col: col, spec: function () { return { c: col, a: 0.04 }; },
                     cur: { dir: "", st: "", d: 0 }, N: N };
        var cur = steps[N.step], Dr = D.DIRS[cur.dir];
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
        var hold = callMode ? "" : (s ? "Sync to Driver" : "Auto"), press = callMode ? "Switch Zone" : D.CMODES[(m + 1) % D.CMODES.length];
        var mk = D.CMODES[m];
        // The display's scale: temperature and seat on the cool | heat arc (60-84 °F
        // and vent 3 to heat 3, neutral at the top), fan speed as growing dots,
        // air flow as a four-way selector
        if (mk === "Temp") {
            var delta = Ring.clamp((Z.t - cab) / 12, -1, 1);
            return mkEnc({ label: nm, value: tempNum(Z.t), unit: "°", sub: heat ? "Heating" : cool ? "Cooling" : "Holding",
                       color: heat ? "heat" : cool ? "cool" : "tx", pager: pager,
                       scale: "split", steps: 25, pos: Z.t - 60,
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
        if (mk === "Air") {
            var ai = airIndex(Z.air), A = D.AIR[ai];
            return mkEnc({ icon: A.i, iconSize: 72, sub: A.l, pager: pager,
                       scale: "select", steps: D.AIR.length, pos: ai,
                       idc: Ring.WHITE, name: nm + " Air Flow", press: press, hold: hold,
                       note: "Bright LED = air flow position",
                       spec: Ring.mark5(ai / (D.AIR.length - 1), Ring.WHITE) });
        }
        if (mk === "Fan") {
            var sp = fanSpeed(Z.fan), fa = Z.fan === fanAuto;
            return mkEnc({ icon: "mode-fan", iconSize: 28, value: D.FAN[Z.fan], vSize: 64, color: fa ? "acc" : "tx", pager: pager,
                       scale: "dots", steps: 6, pos: Z.fan > fanAuto ? Z.fan - fanAuto : 0,
                       idc: Ring.GREEN, name: nm + " Fan", press: press, hold: hold,
                       note: "Fill = fan speed · sweep speed = airflow",
                       spec: function (k, T) {
                           var f = Ring.fill5(sp / 6, Ring.GREEN)(k);
                           return f.a > 0.1 ? { c: Ring.GREEN, a: f.a * (0.45 + 0.55 * Ring.comet5(k, T, 0.12 + sp * 0.1)) } : f;
                       } });
        }
        var ds = Z.seat / 3;
        return mkEnc({ icon: Z.seat > 0 ? "seat-heat" : "seat-vent", iconSize: 28,
                   value: Z.seat ? Math.abs(Z.seat) : "Off", sub: Z.seat > 0 ? "Heat" : Z.seat < 0 ? "Vent" : "--",
                   color: Z.seat > 0 ? "heat" : Z.seat < 0 ? "cool" : "tx", pager: pager,
                   scale: "split", steps: 7, pos: Z.seat + 3,
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
        case "notice":
            return mkEnc({ label: "Drive Safely", icon: "car", sub: "Press to accept", color: "warn", idc: Ring.AMBER, name: "Accept",
                       press: "Accept", hold: "Cameras", note: "Amber pulse = safety notice · turn to scroll · press to accept · hold for cameras",
                       spec: function (k, T) { return { c: Ring.AMBER, a: k === 2 ? 0.9 : 0.15 + 0.25 * Ring.pulse(T, 0.6) }; } });
        case "incoming":
            c = Ph.call;
            return mkEnc({ label: "Answer", icon: "call", sub: c.n, color: "acc", idc: Ring.LIME, name: "Answer Call", press: "Answer",
                       note: Ph.silent ? "Silenced · press to answer" : "Spinning green = incoming call · turn to silence",
                       spec: function (k, T) { return { c: Ring.LIME, a: 0.08 + 0.92 * Ring.comet5(k, T, Ph.silent ? 0.4 : 1.1) * (Ph.silent ? 0.5 : 1) }; } });
        case "call": {
            c = Ph.call;
            var mic = Ph.mic, v = vol;
            return mkEnc({ icon: mic ? "call" : "mic-off", value: v, sub: c.n, scale: "ramp", steps: 100, pos: v,
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
        case "track": {
            var Ls = trackRows, ln = Ls.length, lr = ln ? Ls[trackSel] : null;
            return mkEnc({ label: trackListing ? "List" : app === "ships" ? "Ships" : "Aircraft",
                           value: ln ? (trackSel + 1) + " of " + ln : "None", small: true,
                           sub: lr ? lr.name : trackHint(), idc: Ring.WHITE, name: trackListing ? "List" : "Choose",
                           press: trackListing ? "Select" : gear === "P" ? "List" : "",
                           note: "Bright LED = position in the list",
                           spec: Ring.mark5(ln > 1 ? trackSel / (ln - 1) : 0, Ring.WHITE) });
        }
        case "ask":
            if (ask.stage === "confirm")
                return mkEnc({ label: "End Route", icon: "flag", color: "heat", idc: Ring.RED, name: "End Route", press: "End Route",
                           note: "Press to end the route · the other knob keeps it",
                           spec: function (k, T) { return { c: Ring.RED, a: 0.15 + 0.6 * Ring.pulse(T, 0.8) }; } });
            if (ask.stage === "listening")
                return mkEnc({ label: "Done", icon: "checkmark", sub: "Listening", color: "acc", idc: Ring.CYAN, name: "Done Talking",
                           press: "Done Talking", note: "Cyan fill = how loud you are · stops by itself when you pause",
                           spec: function (k, T) { return Ring.fill5(Dictation.level, Ring.CYAN)(k); } });
            return mkEnc({ label: "Wait", icon: "sync", sub: (askStages[ask.stage] || {}).sub, idc: Ring.CYAN, name: "Hey Peregrine",
                       note: "Cyan sweep = working",
                       spec: function (k, T) { return { c: Ring.CYAN, a: 0.08 + 0.92 * Ring.comet5(k, T, 0.9) }; } });
        case "message": {
            var mn = msgNote, ms = msgStage, ml = ms.l, st = mn.stage;
            if (st === "listening")
                return mkEnc({ label: ml[0], icon: ml[1], sub: "Listening", color: "acc", idc: Ring.CYAN, name: mn.compose ? "Dictate Message" : "Dictate Reply",
                           press: ml[0], note: "Cyan fill = how loud you are · stops by itself when you pause",
                           spec: function (k, T) { return Ring.fill5(Dictation.level, Ring.CYAN)(k); } });
            if (!ml)
                return mkEnc({ label: "Wait", icon: "sync", sub: ms.sub, idc: Ring.CYAN, name: "Message",
                           note: "Cyan sweep = working",
                           spec: function (k, T) { return { c: Ring.CYAN, a: 0.08 + 0.92 * Ring.comet5(k, T, 0.9) }; } });
            return mkEnc({ label: ml[0], icon: ml[1], sub: mn.n, color: "acc", idc: Ring.GREEN, name: ml[0] + " Message",
                       press: ml[0], hold: st === "confirm" ? "Say Again" : "",
                       note: st === "new" ? "Green sweep = new message · press to hear it" : ms.sub,
                       spec: function (k, T) { return { c: Ring.GREEN, a: 0.08 + 0.92 * Ring.comet5(k, T, 0.6) }; } });
        }
        case "silence":
            c = Ph.call;
            return mkEnc({ icon: "notifications-off", sub: Ph.silent ? "Silenced" : c.n, name: "Silence Ringer",
                       press: "Silence", note: "Answering waits until you leave Reverse" });
        case "guide": {
            var gv = gvol;
            return mkEnc({ icon: "navigate", value: gv, sub: D.SRC[audioSrc].n + " ducked", idc: Ring.CYAN,
                       scale: "ramp", steps: 100, pos: gv,
                       name: "Guidance Volume", press: "Skip Prompt", hold: "Voice Off",
                       note: "Cyan = spoken guidance · fill = guidance volume",
                       spec: function (k, T) {
                           var f = Ring.fill5(gv / 100, Ring.CYAN)(k);
                           return f.a > 0.1 ? { c: Ring.CYAN, a: f.a * (0.6 + 0.4 * Ring.lvl(T)) } : f;
                       } });
        }
        default: {
            var src = weatherOpen ? "wx" : audioSrc, S = D.SRC[src], m = muted, vv = vol;
            // This app has the tuner, so the radio cannot be heard
            var playing = src === "wx" ? wxHeard : srcPlaying && !(src === "radio" && tracking);
            var volSpec = function (k, T) {
                if (m) return { c: Ring.RED, a: k === 2 ? 0.55 + 0.45 * Ring.pulse(T, 1) : 0.06 };
                var f = Ring.fill5(vv / 100, S.c)(k), lead = Math.min(4, Math.floor(vv / 20));
                return (playing && k === lead) ? { c: S.c, a: f.a * (0.55 + 0.45 * Ring.lvl(T)) }
                                               : (playing ? f : { c: S.c, a: Math.min(f.a, 0.35) });
            };
            var st = sig();
            var nowTxt = src === "wx" ? (wxCur.n || wxCur.f) : src === "bt" ? (Bt.player.title || Bt.player.name || "Phone")
                       : msrc ? (msrc.track.title || S.n) : (st.stn || { n: "--" }).n;
            var base = { icon: m ? "volume-mute" : (!playing && src !== "wx" ? "pause" : "volume-high"), value: m ? "" : vv,
                         scale: "ramp", steps: 100, pos: m ? 0 : vv,
                         sub: m ? "Muted" : (!playing && src !== "wx" ? "Paused" : nowTxt), color: m ? "heat" : "tx", idc: m ? Ring.RED : S.c,
                         name: S.vn + " Volume", press: m ? "Unmute" : src === "radio" || src === "wx" ? "Mute" : (playing ? "Pause" : "Play"),
                         hold: src === "radio" ? "Next Station" : src === "wx" ? "" : "Next Track",
                         note: "Fill = volume in source color · lead LED flickers with audio" };
            var cueActive = (app === "nav" && routeActive) || route.u > 0.5;
            if (cueActive && !recent(0)) {
                var dp = distParts(route.N.dist), arrive = route.cur.dir === "arrive";
                return mkEnc(Object.assign(base, {
                    label: "", icon: route.D.i, scale: "",
                    value: arrive ? "" : dp.v, unit: arrive ? "" : dp.u, sub: route.cur.st,
                    color: route.u > 0.6 ? "warn" : "tx", idc: route.col,
                    note: "Ring = next turn · cyan far → amber near · knob = " + S.vn.toLowerCase() + " volume",
                    spec: route.spec }));
            }
            return mkEnc(Object.assign(base, { spec: volSpec }));
        }
        }
    }

    function rightEnc(own, route) {
        var Ph = phone, c;
        switch (own) {
        case "notice":
            return mkEnc({ label: "Drive Safely", icon: "car", sub: "Press to accept", color: "warn", idc: Ring.AMBER, name: "Accept",
                       press: "Accept", hold: "Cameras", note: "Amber pulse = safety notice · turn to scroll · press to accept · hold for cameras",
                       spec: function (k, T) { return { c: Ring.AMBER, a: k === 2 ? 0.9 : 0.15 + 0.25 * Ring.pulse(T, 0.6) }; } });
        case "incoming":
            c = Ph.call;
            return mkEnc({ label: "Decline", icon: "close", sub: c.n, color: "heat", idc: Ring.RED, name: "Decline", press: "Decline",
                       note: "Spinning red = decline",
                       spec: function (k, T) { return { c: Ring.RED, a: 0.06 + 0.6 * Ring.comet5(k, T, -0.7) }; } });
        case "ask":
            if (ask.stage === "confirm")
                return mkEnc({ label: "Keep Route", icon: "navigate", color: "acc", name: "Keep Route", press: "Keep Route",
                               note: "Press to keep the route" });
            return mkEnc({ label: "Cancel", icon: "close", sub: "Hey Peregrine", name: "Cancel", press: "Cancel", note: (askStages[ask.stage] || {}).sub });
        case "message": {
            var mr = msgStage.r;
            if (!mr) return mkEnc({ label: "Wait", icon: "sync", sub: msgStage.sub, name: "Message" });
            return mkEnc({ label: mr[0], icon: mr[1], sub: msgNote.n, name: mr[0], press: mr[0],
                           note: msgNote.stage === "new" ? "Press to dismiss · the message stays unread on the phone" : msgStage.sub });
        }
        case "interlock": {
            var j = level.jacks;
            return mkEnc({ label: "Jacks", icon: j === "Retracting" ? "sync" : "warning", sub: j === "Retracting" ? "Stay stopped" : "Retract to drive",
                       color: "heat", idc: Ring.RED, name: "Retract Jacks", press: j === "Down" ? "Retract Jacks" : "",
                       note: "Red pulse = vehicle interlock · amber sweep = retracting" });
        }
        case "launcher": {
            var n = apps.length, a = apps[lsel];
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
                    return mkEnc({ label: "Start Route", value: sp.n, small: true, sub: Router.route.steps ? fd(Router.route.mi) + " · " + fmtMins(Router.route.mins)
                                                                                     : Router.busy ? "Finding route" : "No route", idc: Ring.CYAN,
                                   name: "Start Route", press: "Start", hold: "Back", note: "Press to start · hold for the list",
                                   spec: Ring.allC(Ring.CYAN, 0.8) });
                return mkEnc({ label: navSearch.q ? "Results" : "Places", value: SR.length ? SR[ssel].n : !navSearch.q ? "Type a place" : Places.busy ? "Searching" : "No matches", small: true,
                               sub: SR.length ? SR[ssel].a : navSearch.q && !Places.busy && !Places.available ? "Search is starting up" : navSearch.q ? "Try another search" : "", idc: Ring.WHITE, name: "Choose Destination",
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
                           return Ring.fill5((N.zoom - navZoomMin) / (navZoomMax - navZoomMin), Ring.CYAN)(k);
                       } });
        }
        case "endcall":
            c = Ph.call;
            return mkEnc({ label: "End", icon: "call", value: Ph.call.state === "dialing" ? "…" : D.fmtT(Ph.call.t), small: true, sub: c.n, color: "heat", idc: Ring.RED,
                       name: "End Call", press: "End Call", note: "Audio is paused for the call · red = end call",
                       spec: function (k, T) { return { c: Ring.RED, a: k === 2 ? 0.9 : 0.25 + 0.15 * Ring.pulse(T, 0.5) }; } });
        case "weather": {
            var Wn = wxRows.length, Wc = wxCur;
            return mkEnc({ label: (wxSel + 1) + " of " + Wn, value: Wc.f || "", small: true,
                       sub: Wc.n ? Wc.n + " · " + wxDist(Wc.mi) : wxHeard ? "Playing" : "No station heard",
                       idc: Ring.CYAN, name: "Channel", press: "Strongest", note: "Bright LED = position in the list",
                       spec: Ring.mark5(Wn > 1 ? wxSel / (Wn - 1) : 0, Ring.CYAN) });
        }
        case "aircraft": case "ships": {
            var As = trackRows, an = As.length, ar = an ? As[trackSel] : null, aTitle = own === "ships" ? "Ships" : "Aircraft";
            return mkEnc({ label: an ? (trackSel + 1) + " of " + an : aTitle, value: ar ? ar.name : "None", small: true,
                       sub: !ar ? trackHint() : ar.dist !== undefined ? ar.dist.toFixed(1) + " nm · " + Math.round(ar.brg) % 360 + "°" : ar.sub,
                       idc: Ring.CYAN, name: aTitle, press: "Range " + track.range + " nm",
                       note: "Bright LED = position in the list",
                       spec: Ring.mark5(an > 1 ? trackSel / (an - 1) : 0, Ring.CYAN) });
        }
        case "radio": {
            var Rr = radio, fm = Rr.band === "FM", f = fm ? Rr.fm : Rr.am, S = sig(), pos = fm ? (f - 87.9) / 20 : (f - 530) / 1170;
            var sg = S.sig, col = Ring.sigCol(sg);
            return mkEnc({ label: Rr.band, value: fm ? f.toFixed(1) : f, sub: S.stn ? S.stn.n + " · " + S.stn.g : radioStatus(),
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
            if (audio === "bt")
                return mkEnc({ icon: "bluetooth", value: btPlayer ? (btPlaying ? "Playing" : "Paused") : "", small: true,
                               sub: btPlayer ? (Bt.player.title || Bt.player.name || "") : btPhone ? "Play music on the phone" : "No phone connected",
                               idc: Ring.CYAN, name: "Phone Music", press: btPlayer ? (btPlaying ? "Pause" : "Play") : "", hold: "Source",
                               note: "Turn for the next or previous track", spec: Ring.allC(Ring.CYAN, btPlaying ? 0.6 : 0.15) });
            var MS = msrc, Q = MS && MS.hasQueue ? MS.queue : [], nt = Q.length;
            if (MS && media.panel === "browse" && MS.lists.length) {
                var BL = MS.lists, bi = Math.min(media.bsel || 0, BL.length - 1);
                return mkEnc({ label: "Browse", value: bi + 1, unit: "/" + BL.length, sub: BL[bi].title, idc: Ring.WHITE,
                               name: "Browse", press: "Play", hold: "Source", note: "Bright LED = position in the list",
                               spec: Ring.mark5(BL.length > 1 ? bi / (BL.length - 1) : 0, Ring.WHITE) });
            }
            if (!nt)
                return mkEnc({ label: D.SRC[audio].n, icon: "musical-notes", value: MS && MS.track.title ? (MS.playing ? "Playing" : "Paused") : "", small: true,
                               sub: MS ? (MS.track.title || MS.note) : "", idc: Ring.WHITE, name: D.SRC[audio].n,
                               press: MS && MS.track.title ? (MS.playing ? "Pause" : "Play") : "", hold: "Source",
                               note: "Turn for the next or previous track", spec: Ring.allC(Ring.WHITE, MS && MS.playing ? 0.6 : 0.15) });
            var M = media, ms = Math.min(M.sel, nt - 1);
            return mkEnc({ label: "Up Next", value: ms + 1, unit: "/" + nt, sub: Q[ms].title, idc: Ring.WHITE,
                       name: "Browse Queue", press: "Play", hold: "Source", note: "Bright LED = position in the queue",
                       spec: Ring.mark5(nt > 1 ? ms / (nt - 1) : 0, Ring.WHITE) });
        }
        case "phone": {
            if (Ph.call)
                return mkEnc({ label: "End", icon: "call", sub: Ph.call.state === "dialing" ? "Calling" : D.fmtT(Ph.call.t), color: "heat", idc: Ring.RED, name: "End Call",
                           press: "End Call", note: "Red = end call",
                           spec: function (k, T) { return { c: Ring.RED, a: 0.25 + 0.15 * Ring.pulse(T, 0.5) }; } });
            var Ls = phoneList(), it = Ls[Math.min(Ph.sel, Ls.length - 1)];
            if (!it)
                return mkEnc({ label: "Phone", icon: "call", sub: phoneReal && !Calls.available ? "No phone connected" : "No contacts",
                               idc: Ring.LIME, name: "Contacts", note: "", spec: Ring.mark5(0, Ring.LIME) });
            c = it.c;
            return mkEnc({ label: Ph.tab === "all" ? "Contacts" : it.fav ? "Favorites" : "Recents", value: c.ini, small: true, sub: c.n, idc: Ring.LIME, name: "Contacts",
                       press: "Call " + c.n.split(" ")[0], note: "Bright LED = position in the contact list",
                       spec: Ring.mark5(Ls.length > 1 ? Ph.sel / (Ls.length - 1) : 0, Ring.LIME) });
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
        case "pictures": {
            var Pl = Photos.items, Pn = Pl.length, Pi = picSel(), Pv = pics;
            if (gear !== "P")
                return mkEnc({ label: "Pictures", icon: "lock-closed", sub: "Park to view", idc: Ring.NEU, name: "Pictures",
                               note: "Pictures only show in Park", spec: Ring.allC(Ring.NEU, 0.06) });
            if (Photos.importing)
                return mkEnc({ label: "Importing", value: Photos.importDone, unit: "/" + Photos.importTotal, sub: "From USB", idc: Ring.WHITE,
                               name: "Importing Photos", note: "Fill = import progress",
                               spec: Ring.fill5(Photos.importTotal ? Photos.importDone / Photos.importTotal : 0, Ring.WHITE) });
            if (!Pn)
                return mkEnc({ label: "Pictures", icon: "images", sub: Photos.usbNew ? Photos.usbNew + " on USB" : "No photos", idc: Ring.WHITE, name: "Pictures",
                               hold: Photos.usbNew ? "Import from USB" : "", note: "", spec: Ring.allC(Ring.WHITE, 0.06) });
            return mkEnc({ label: !Pv.view ? "Pictures" : Pv.play ? "Slideshow" : "Photo", value: Pi + 1, unit: "/" + Pn, sub: Pl[Pi].n,
                           idc: Ring.WHITE, name: Pv.view ? "Browse Photos" : "Choose Photo",
                           press: !Pv.view ? "View" : Pv.play ? "Pause" : "Slideshow",
                           hold: Pv.view ? "All Photos" : Photos.usbNew && !Photos.importing ? "Import from USB" : "",
                           note: "Bright LED = position in the library",
                           spec: Ring.mark5(Pn > 1 ? Pi / (Pn - 1) : 0, Ring.WHITE) });
        }
        case "trailname":
            return mkEnc({ label: "Name", value: trailName ? trailName.edit : "", small: true,
                           sub: trailNameCanSave() ? "Press to save" : "Type a name on screen", idc: Ring.CYAN,
                           name: trailName ? trailName.title : "Name", press: "Save", hold: trailName && trailName.keep ? "Keep Date" : "Cancel",
                           note: "Names are typed on the touchscreen", spec: Ring.allC(Ring.CYAN, trailNameCanSave() ? 0.8 : 0.2) });
        case "trails": {
            if (confirmBox)
                return mkEnc({ label: "Confirm", value: confirmBox.ok, small: true, sub: "Hold to cancel", color: confirmBox.safe ? "acc" : "heat", idc: confirmBox.safe ? Ring.GREEN : Ring.AMBER,
                               name: confirmBox.ok, press: confirmBox.ok, hold: "Cancel", note: "Press to confirm · hold to cancel",
                               spec: Ring.allC(Ring.AMBER, 0.5) });
            var Tl = trailRows, Tn = Tl.length, Ti = trailIdx();
            if (!Tn)
                return mkEnc({ label: "Trails", icon: "trail-sign", sub: Trails.usbNew ? Trails.usbNew + " on USB" : "No trails", idc: Ring.WHITE, name: "Trails",
                               hold: gear === "P" && Trails.usbNew ? "Import from USB" : "", note: "", spec: Ring.allC(Ring.WHITE, 0.06) });
            return mkEnc({ label: "Trail", value: Ti + 1, unit: "/" + Tn, sub: Tl[Ti].n, idc: Ring.WHITE, name: "Choose Trail",
                           press: Tl[Ti].on ? "Hide" : "Show", hold: "See on Map", note: "Bright LED = position in the list",
                           spec: Ring.mark5(Tn > 1 ? Ti / (Tn - 1) : 0, Ring.WHITE) });
        }
        case "videos": {
            var Vl = Videos.items, Vn = Vl.length, Vi = vidSel(), Vv = vids;
            if (gear !== "P")
                return mkEnc({ label: "Videos", icon: "lock-closed", sub: "Park to watch", idc: Ring.NEU, name: "Videos",
                               note: "Videos only play in Park", spec: Ring.allC(Ring.NEU, 0.06) });
            if (Videos.importing)
                return mkEnc({ label: "Importing", value: Videos.importDone, unit: "/" + Videos.importTotal, sub: Math.round(Videos.importProgress * 100) + "% from USB",
                               idc: Ring.WHITE, name: "Importing Videos", note: "Fill = import progress",
                               spec: Ring.fill5(Videos.importProgress, Ring.WHITE) });
            if (!Vn)
                return mkEnc({ label: "Videos", icon: "film", sub: Videos.usbNew ? Videos.usbNew + " on USB" : "No videos", idc: Ring.WHITE, name: "Videos",
                               hold: Videos.usbNew ? "Import from USB" : "", note: "", spec: Ring.allC(Ring.WHITE, 0.06) });
            if (Vv.view)
                return mkEnc({ label: Vv.play ? "Playing" : "Paused", value: D.fmtT(vidPos), small: true, sub: Vl[Vi].n, idc: Ring.WHITE,
                               name: "Skip 10 s", press: Vv.play ? "Pause" : "Play", hold: Vv.full ? "Exit Full Screen" : "All Videos",
                               note: "Fill = how far into the video · turn to skip 10 s",
                               spec: Ring.fill5(vidDur ? vidPos / vidDur : 0, Ring.WHITE) });
            return mkEnc({ label: "Videos", value: Vi + 1, unit: "/" + Vn, sub: Vl[Vi].n, idc: Ring.WHITE, name: "Choose Video",
                           press: "Play", hold: Videos.usbNew ? "Import from USB" : "", note: "Bright LED = position in the library",
                           spec: Ring.mark5(Vn > 1 ? Vi / (Vn - 1) : 0, Ring.WHITE) });
        }
        case "settings": {
            var pg = setPage, title = setTitle(pg, setArg);
            if (confirmBox)
                return mkEnc({ label: "Confirm", value: confirmBox.ok, small: true, sub: "Hold to cancel", color: confirmBox.safe ? "acc" : "heat", idc: confirmBox.safe ? Ring.GREEN : Ring.AMBER,
                               name: confirmBox.ok, press: confirmBox.ok, hold: "Cancel", note: "Press to confirm · hold to cancel",
                               spec: function (k, T) { return { c: Ring.AMBER, a: k === 2 ? 0.9 : 0.15 + 0.2 * Ring.pulse(T, 0.8) }; } });
            if (pg === "deviceName") {
                var named = nameCanSave();
                return mkEnc({ label: "Name", value: nameEdit, small: true,
                               sub: gear !== "P" ? "Park to type" : named ? "Press to save" : "Type a name on screen",
                               idc: Ring.CYAN, name: "Device Name", press: "Save", hold: "Cancel",
                               note: "Names are typed on the touchscreen", spec: Ring.allC(Ring.CYAN, named ? 0.8 : 0.2) });
            }
            if (pg === "wifiJoin") {
                var ok = joinCanConnect();
                return mkEnc({ label: "Wi-Fi", value: join ? join.ssid : "", small: true,
                               sub: gear !== "P" ? "Park to type" : ok ? "Ready to connect" : "Type password on screen",
                               idc: Ring.CYAN, name: "Join Network", press: "Connect", hold: "Cancel",
                               note: "Passwords are typed on the touchscreen", spec: Ring.allC(Ring.CYAN, ok ? 0.8 : 0.2) });
            }
            if (pg === "legalDoc")
                return mkEnc({ label: title, icon: "information-circle-outline", small: true, sub: "Turn to scroll", idc: Ring.WHITE,
                               name: title, hold: "Back", note: "Turn to scroll · hold to go back", spec: Ring.allC(Ring.WHITE, 0.3) });
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
                                   scale: "", scaleSteps: 0, scalePos: 0,
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
                 scale: e.scale, scaleSteps: e.scaleSteps, scalePos: e.scalePos,
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

    function applyMix() {
        Mixer.setVolume("multimedia", vol);
        Mixer.setVolume("navigation", gvol);
        Mixer.setVolume("communication", phoneVolume);
        Mixer.setMuted("multimedia", muted || micOpen);
    }
    // The role sinks came back at full volume: start over at the power-on volume
    readonly property Connections mixerEvents: Connections {
        target: Mixer
        function onSinksReset() { hmi.vol = hmi.startVol; hmi.applyMix(); }
    }

    Component.onCompleted: {
        applyMix();
        playingIdx = msrc ? msrc.queueIndex : -1;
        Knobs.turned.connect(function (s, d) { hmi.act(s, "turn", d); });
        Knobs.pressed.connect(function (s) { hmi.act(s, "press", 0); });
        Knobs.held.connect(function (s) { hmi.act(s, "hold", 0); });
        Knobs.holdStarted.connect(function (s) { hmi.holdStart(s); });
        Knobs.holdCancelled.connect(function (s) { hmi.holdCancel(s); });
        Photos.imported.connect(function (count, failed) {
            hmi.toast(failed ? "Imported " + count + " · " + failed + " could not be copied"
                             : "Imported " + count + (count === 1 ? " photo" : " photos"));
        });
        Trails.imported.connect(function (count, failed) {
            hmi.toast(failed ? "Imported " + count + " · " + failed + " could not be copied"
                             : "Imported " + count + (count === 1 ? " trail" : " trails"));
        });
        if (Trails.resumed) toast("Still recording your route");
        Videos.imported.connect(function (count, failed) {
            hmi.toast(failed ? "Imported " + count + " · " + failed + " could not be copied"
                             : "Imported " + count + (count === 1 ? " video" : " videos"));
        });
    }
}
