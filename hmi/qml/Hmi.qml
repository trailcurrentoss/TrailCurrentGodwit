pragma Singleton
import QtQuick
import Godwit
import "Ring.js" as Ring
import "DemoData.js" as D
import "Legal.js" as L

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
    property string audio: "media"         // media | radio | bt
    // The connected phone's media player (AVRCP). The phone becomes the
    // source when it starts playing and stays it while paused; tuning the
    // radio takes over and pauses the phone.
    readonly property bool btPlayer: Bt.player.status !== undefined
    readonly property bool btPlaying: btPlayer && Bt.player.status === "playing"
    // The demo queue stands in for a music library in the simulator only.
    // The head unit has no library yet, so there Media is the connected phone,
    // playing or not.
    readonly property bool demoMedia: !Bt.real
    readonly property string audioSrc: audio === "radio" ? "radio"
                                     : !demoMedia || (audio === "bt" && btPlayer) ? "bt" : "media"
    // A phone connected over Bluetooth, music or not
    readonly property bool btPhone: Bt.phones.some(function (p) { return p.connected; })
    onBtPlayingChanged: if (btPlaying) audio = "bt"
    onBtPlayerChanged: if (!btPlayer && audio === "bt") audio = "media"
    function btPlayPause() { if (btPlaying) Bt.pause(); else Bt.play(); }
    // Switch to the radio, pausing the phone's music
    function takeRadio() {
        if (audio === "bt" && btPlaying) Bt.pause();
        audio = "radio";
    }

    // Volumes into the system mixer: one role each, so guidance and calls
    // keep their own levels
    onVolChanged: Mixer.setVolume("multimedia", vol)
    onGvolChanged: Mixer.setVolume("navigation", gvol)
    onMutedChanged: Mixer.setMuted("multimedia", muted)
    readonly property int phoneVolume: prefs.phoneVol
    onPhoneVolumeChanged: Mixer.setVolume("communication", phoneVolume)
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
    // A text message on screen: {id, n, num, ini, stage, until, reply}. It
    // moves through the stages below; Read and Reply sit above the left knob,
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
        "listening":    { sub: "Listening · say your reply", l: ["Done Talking", "checkmark"], r: ["Cancel", "close"] },
        "transcribing": { sub: "Getting your words…", l: null, r: ["Cancel", "close"] },
        "confirm":      { sub: "Send this reply? · hold Send to say it again", l: ["Send", "send"], r: ["Cancel", "close"], wait: 20000 },
        "sending":      { sub: "Sending…", l: null, r: null }
    })
    readonly property var msgStage: msgNote && msgStages[msgNote.stage] || null
    readonly property bool dictating: !!msgNote && (msgNote.stage === "listening" || msgNote.stage === "transcribing")
    function msgTo(stage, patch) {
        var w = msgStages[stage].wait;
        msgNote = Object.assign({}, msgNote, patch || {}, { stage: stage, until: w ? Date.now() + w : 0 });
    }
    // Music is muted while listening, so the microphone hears only the driver
    onDictatingChanged: Mixer.setMuted("multimedia", dictating || muted)

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
        function onSent(number) { hmi.msgNote = null; hmi.toast("Reply sent"); Voice.say("Reply sent"); }
        function onSendFailed(number) {
            if (!hmi.msgNote || hmi.msgNote.stage !== "sending") return;
            hmi.msgNote = null;
            hmi.toast("Could not send the reply");
            Voice.say("The reply could not be sent");
        }
    }
    // Reading done: offer to reply
    readonly property Connections msgVoice: Connections {
        target: Voice
        function onSpeakingChanged() {
            if (Voice.speaking || !hmi.msgNote) return;
            if (hmi.msgNote.stage === "reading") hmi.msgTo("offer");
            else if (hmi.msgNote.stage === "confirm") hmi.msgTo("confirm");  // its wait starts now
        }
    }
    readonly property Connections msgDictation: Connections {
        target: Dictation
        function onStateChanged() {
            if (Dictation.state === "transcribing" && hmi.msgNote && hmi.msgNote.stage === "listening") hmi.msgTo("transcribing");
        }
        function onHeard(text) { if (hmi.dictating) hmi.confirmReply(text); }
        function onFailed(reason) {
            if (!hmi.dictating) return;
            hmi.msgTo("offer");
            hmi.toast(reason === "silence" ? "Didn't hear a reply" : "The microphone is not working");
        }
    }
    function showMessage(note) {
        if (phone.call) { msgHeld = note; return; }
        // A reply under way is not interrupted
        if (msgNote && msgNote.stage !== "new") return;
        msgNote = Object.assign({}, note, { stage: "new", until: Date.now() + msgStages["new"].wait });
        Voice.say("Message from " + note.n);
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
        if (st === "listening" || st === "transcribing") Dictation.cancel();
        if (st === "reading" || st === "confirm") Voice.stop();
        msgNote = null;
    }
    // Read aloud on the head unit; shown in the simulator, which has no voice
    function sayMessage(text, number) {
        if (phone.call) { msgNote = null; return; }
        msgTo("reading", number ? { num: number } : {});
        if (Voice.available) Voice.say(msgNote.n + " says: " + text);
        else { toast(msgNote.n + ": " + text); msgTo("offer"); }
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
        msgTo("confirm", { reply: text });
        if (Voice.available) Voice.say("Your reply to " + msgNote.n + ": " + text + (/[.!?]$/.test(text) ? "" : ".") + " Send it?");
        else toast("Reply: " + text);
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
        else if (st === "reading") { Voice.stop(); msgTo("offer"); }
        else if (st === "offer") startReply();
        else if (st === "listening") {
            if (Dictation.available) Dictation.finish();
            else { simHeardTimer.stop(); confirmReply("Sounds good, see you there"); }
        }
        else if (st === "confirm") { if (k === "hold") startReply(); else sendReply(); }
    }
    function simMessage() {
        var c = D.CONTACTS[1];
        showMessage({ id: "", n: c.n, num: c.num, ini: c.ini, text: "Running ten minutes late, save me a spot by the fire." });
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
    readonly property bool bannerUp: incoming || !!msgNote
    readonly property bool lowBattery: energy.soc < 20

    // ------------------------------------------------------------ app flow
    function openApp(id) {
        if (gear === "R" && id !== "cams") { toast("Rear camera stays on in Reverse"); return; }
        var call = !!phone.call;
        app = id; launcher = false; edit = false;
        if (id === "settings") resetSet();
        // Another app stops the video, so its sound does not carry on unseen
        if (id !== "videos") upd("vids", { play: false, full: false });
        // Looking at the radio does not stop the phone's music; tuning does
        if (id === "radio" && audio !== "bt") audio = "radio";
        if (id === "media") audio = btPlayer ? "bt" : "media";
        if (call && (id === "radio" || id === "media")) resume = true;
        if (id === "radio") upd("media", { playing: false });
        if (call) return;
        if (id === "media" && demoMedia) upd("media", { playing: true });
        if (id === "radio") muted = false;
    }
    function toggleLauncher() {
        if (gear === "R") { toast("Rear camera stays on in Reverse"); return; }
        var i = D.APPS.findIndex(function (a) { return a.id === app; });
        lsel = Math.max(0, i);
        launcher = !launcher;
    }
    function pauseForCall() {
        // A message on screen waits for the call to end
        if (msgNote) { msgHeld = msgNote; msgNote = null; }
        upd("vids", { play: false });
        if (audioSrc === "bt") return;  // the phone pauses its own music for a call
        resume = resume || (audio === "media" ? media.playing : !muted);
        upd("media", { playing: false });
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
        if (r && src === "media") upd("media", { playing: true });
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
        var R = radio, fm = R.band === "FM", cur = fm ? R.fm : R.am, L = fm ? D.FM : D.AM;
        var nx = dir < 0 ? (L.slice().reverse().find(function (x) { return x.f < cur - 0.001; }) || L[L.length - 1])
                         : (L.find(function (x) { return x.f > cur + 0.001; }) || L[0]);
        upd("radio", fm ? { fm: nx.f } : { am: nx.f });
        takeRadio();
        muted = false;
        toast("Seek · " + nx.n);
    }
    function tuneTo(b, f) {
        upd("radio", b === "FM" ? { band: b, fm: f } : { band: b, am: f });
        upd("media", { playing: false });
        takeRadio();
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
    // noMsg: who would own them without a message showing
    function owner(noMsg) {
        var call = phone.call, inc = call && call.state === "incoming";
        if (gear === "R") return [inc ? "silence" : call ? "call" : "audio", "cams"];
        if (noticeUp) return ["notice", "notice"];
        var L = call ? (inc ? "incoming" : "call") : guideOn() ? "guide" : "audio";
        if (interlock()) return [L, "interlock"];
        if (inc) return ["incoming", "incoming"];
        if (msgStage && !noMsg) return ["message", "message"];
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
        if (k === "press") {
            if (src === "bt") btPlayPause();
            else if (src === "media") upd("media", function (m) { return { playing: !m.playing }; });
            else muted = !muted;
        }
        if (k === "hold") {
            if (src === "bt") { Bt.next(); toast("Next track"); }
            else if (src === "media") {
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
    // Setting the fan by hand leaves Auto, as the knob does
    function setZone(z, patch) {
        upd("clim", function (c) {
            var o = {}; o[z] = Object.assign({}, c[z], patch);
            if ("fan" in patch) o.auto = false;
            return o;
        });
    }
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
        toast("Saved to preset " + (i + 1));
    }
    function a_media(s, k, d) {
        if (!demoMedia) {
            if (k === "turn") { if (d > 0) Bt.next(); else Bt.previous(); }
            if (k === "press") btPlayPause();
            return;
        }
        if (k === "turn") upd("media", function (m) { return { sel: Ring.clamp(m.sel + d, 0, D.TRACKS.length - 1) }; });
        if (k === "press") upd("media", function (m) { return { idx: m.sel, pos: 0, playing: true }; });
        if (k === "hold") { var sh = media.shuffle; upd("media", { shuffle: !sh }); toast(sh ? "Shuffle off" : "Shuffle on"); }
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
    function navZoom(d) { var z = Ring.clamp(nav.zoom + d, 10, 18); upd("nav", { zoom: z }); mapEase({ zoom: z }, 200); }
    function navRecenter() { follow = true; mapEase({ center: pos() }, 300); toast("Recentered"); }
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
    // CAN 1 / CAN 2 port settings
    property var can: [
        { role: "Vehicle", fd: false, rate: 500000, drate: 2000000, listen: true },
        { role: "TrailCurrent", fd: false, rate: 500000, drate: 2000000, listen: false }
    ]

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
        return { root: "Settings", wifi: "Wi-Fi", wifiJoin: "Join Network", bt: "Bluetooth & Phones", btPair: "Pair New Phone", cams: "Cameras",
                 can: "CAN Buses", display: "Display", sound: "Sound", navset: "Navigation", units: "Units, Date & Time",
                 system: "System", about: "About & Legal", licenses: "Open Source Licenses",
                 legalDoc: arg ? L.doc(arg).title : "" }[page] || "Settings";
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
            rows.push(rowSw("Bluetooth", bt.on, function (v) { btSetOn(v); }));
            if (!bt.on) return rows;
            bt.phones.forEach(function (ph, i) {
                rows.push({ kind: "phone", n: ph.name, v: (ph.connected ? "Connected" : "Paired") + (ph.primary ? " · Primary" : ""),
                            connected: ph.connected, primary: ph.primary, act: function () { openSet("btPhone", i); } });
            });
            rows.push({ kind: "page", n: "Pair New Phone", icon: "phone-portrait-outline", page: "btPair", v: "" });
            rows.push({ kind: "info", n: "Visible As", v: Bt.available ? Bt.name : "Godwit" });
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
                { kind: "info", n: "Device Name", v: "Godwit" },
                { kind: "page", n: "About & Legal", icon: "information-circle-outline", page: "about", v: "" },
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
        case "units": return "°" + prefs.tempUnit + " · " + (prefs.distUnit === "mi" ? "Miles" : "Km") + " · " + (prefs.clock24 ? "24h" : "12h");
        case "system": return "Godwit " + L.VERSION;
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
        if (media.playing) {
            var dur = D.TRACKS[media.idx][2], pos = media.pos + 1, idx = media.idx;
            if (pos >= dur) { pos = 0; idx = (idx + 1) % D.TRACKS.length; }
            upd("media", { pos: pos, idx: idx });
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
        if (msgNote && msgNote.until && Date.now() > msgNote.until) closeMessage();
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
        case "message": {
            var mn = msgNote, ms = msgStage, ml = ms.l, st = mn.stage;
            if (st === "listening")
                return mkEnc({ label: ml[0], icon: ml[1], sub: "Listening", color: "acc", idc: Ring.CYAN, name: "Dictate Reply",
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
            return mkEnc({ label: "Ringing", icon: "notifications-off", sub: Ph.silent ? "Silenced" : c.n, name: "Silence Ringer",
                       press: "Silence", note: "Answering waits until you leave Reverse" });
        case "guide": {
            var gv = gvol;
            return mkEnc({ label: "Guidance", icon: "navigate", value: gv, sub: D.SRC[audioSrc].n + " ducked", idc: Ring.CYAN,
                       name: "Guidance Volume", press: "Skip Prompt", hold: "Voice Off",
                       note: "Cyan = spoken guidance · fill = guidance volume",
                       spec: function (k, T) {
                           var f = Ring.fill5(gv / 100, Ring.CYAN)(k);
                           return f.a > 0.1 ? { c: Ring.CYAN, a: f.a * (0.6 + 0.4 * Ring.lvl(T)) } : f;
                       } });
        }
        default: {
            var src = audioSrc, S = D.SRC[src], m = muted && src === "radio", vv = vol;
            var playing = src === "bt" ? btPlaying : src === "media" ? media.playing : !m;
            var volSpec = function (k, T) {
                if (m) return { c: Ring.RED, a: k === 2 ? 0.55 + 0.45 * Ring.pulse(T, 1) : 0.06 };
                var f = Ring.fill5(vv / 100, S.c)(k), lead = Math.min(4, Math.floor(vv / 20));
                return (playing && k === lead) ? { c: S.c, a: f.a * (0.55 + 0.45 * Ring.lvl(T)) }
                                               : (playing ? f : { c: S.c, a: Math.min(f.a, 0.35) });
            };
            var st = sig();
            var nowTxt = src === "bt" ? (Bt.player.title || Bt.player.name || "Phone")
                       : src === "media" ? D.TRACKS[media.idx][0] : (st.stn || { n: "--" }).n;
            var base = { label: S.n + " Volume", icon: m ? "volume-mute" : (!playing ? "pause" : ""), value: m ? "" : vv,
                         sub: m ? "Muted" : (!playing ? "Paused" : nowTxt), color: m ? "heat" : "tx", idc: m ? Ring.RED : S.c,
                         name: S.n + " Volume", press: src === "radio" ? (m ? "Unmute" : "Mute") : (playing ? "Pause" : "Play"),
                         hold: src === "radio" ? "Next Station" : "Next Track",
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
        case "notice":
            return mkEnc({ label: "Drive Safely", icon: "car", sub: "Press to accept", color: "warn", idc: Ring.AMBER, name: "Accept",
                       press: "Accept", hold: "Cameras", note: "Amber pulse = safety notice · turn to scroll · press to accept · hold for cameras",
                       spec: function (k, T) { return { c: Ring.AMBER, a: k === 2 ? 0.9 : 0.15 + 0.25 * Ring.pulse(T, 0.6) }; } });
        case "incoming":
            c = Ph.call;
            return mkEnc({ label: "Decline", icon: "close", sub: c.n, color: "heat", idc: Ring.RED, name: "Decline", press: "Decline",
                       note: "Spinning red = decline",
                       spec: function (k, T) { return { c: Ring.RED, a: 0.06 + 0.6 * Ring.comet5(k, T, -0.7) }; } });
        case "message": {
            var mr = msgStage.r;
            if (!mr) return mkEnc({ label: "Wait", icon: "sync", sub: msgStage.sub, name: "Message" });
            return mkEnc({ label: mr[0], icon: mr[1], sub: msgNote.n, name: mr[0], press: mr[0],
                           note: msgNote.stage === "new" ? "Press to dismiss · the message stays unread on the phone" : msgStage.sub });
        }
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
                    return mkEnc({ label: "Start Route", value: sp.n, small: true, sub: Router.route.steps ? fd(Router.route.mi) + " · " + fmtMins(Router.route.mins)
                                                                                     : Router.busy ? "Finding route" : "No route", idc: Ring.CYAN,
                                   name: "Start Route", press: "Start", hold: "Back", note: "Press to start · hold for the list",
                                   spec: Ring.allC(Ring.CYAN, 0.8) });
                return mkEnc({ label: navSearch.q ? "Results" : "Destinations", value: SR.length ? SR[ssel].n : !navSearch.q ? "Type a place" : Places.busy ? "Searching" : "No matches", small: true,
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
                           return Ring.fill5((N.zoom - 10) / 8, Ring.CYAN)(k);
                       } });
        }
        case "endcall":
            c = Ph.call;
            return mkEnc({ label: "End Call", icon: "close", value: Ph.call.state === "dialing" ? "…" : D.fmtT(Ph.call.t), small: true, sub: c.n, color: "heat", idc: Ring.RED,
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
            if (!demoMedia)
                return mkEnc({ label: "Phone Music", icon: btPlayer ? "" : "bluetooth", value: btPlayer ? (btPlaying ? "Playing" : "Paused") : "", small: true,
                               sub: btPlayer ? (Bt.player.title || Bt.player.name || "") : btPhone ? "Play music on the phone" : "No phone connected",
                               idc: Ring.CYAN, name: "Phone Music", press: btPlayer ? (btPlaying ? "Pause" : "Play") : "",
                               note: "Turn for the next or previous track", spec: Ring.allC(Ring.CYAN, btPlaying ? 0.6 : 0.15) });
            var M = media, nt = D.TRACKS.length;
            return mkEnc({ label: "Up Next", value: M.sel + 1, unit: "/" + nt, sub: D.TRACKS[M.sel][0], idc: Ring.WHITE,
                       name: "Browse Queue", press: "Play", hold: "Shuffle", note: "Bright LED = position in the queue",
                       spec: Ring.mark5(M.sel / (nt - 1), Ring.WHITE) });
        }
        case "phone": {
            if (Ph.call)
                return mkEnc({ label: "End Call", icon: "close", sub: Ph.call.state === "dialing" ? "Calling" : D.fmtT(Ph.call.t), color: "heat", idc: Ring.RED, name: "End Call",
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
        Mixer.setVolume("multimedia", vol);
        Mixer.setVolume("navigation", gvol);
        Mixer.setVolume("communication", phoneVolume);
        Mixer.setMuted("multimedia", muted);
        Knobs.turned.connect(function (s, d) { hmi.act(s, "turn", d); });
        Knobs.pressed.connect(function (s) { hmi.act(s, "press", 0); });
        Knobs.held.connect(function (s) { hmi.act(s, "hold", 0); });
        Knobs.holdStarted.connect(function (s) { hmi.holdStart(s); });
        Knobs.holdCancelled.connect(function (s) { hmi.holdCancel(s); });
        Photos.imported.connect(function (count, failed) {
            hmi.toast(failed ? "Imported " + count + " · " + failed + " could not be copied"
                             : "Imported " + count + (count === 1 ? " photo" : " photos"));
        });
        Videos.imported.connect(function (count, failed) {
            hmi.toast(failed ? "Imported " + count + " · " + failed + " could not be copied"
                             : "Imported " + count + (count === 1 ? " video" : " videos"));
        });
    }
}
