.pragma library
// Demo content from the design mockup. Each block is replaced by a backend
// (BlueZ/PipeWire, tuner, Valhalla, CAN) as those land; the shapes stay the same.

// n: the source's name; vn: what its volume is called
var SRC = {
    local:   { n: "Local", vn: "Media", c: "#7BC96A", icon: "library" },
    usb:     { n: "USB", vn: "Media", c: "#7BC96A", icon: "hardware-chip-outline" },
    radio:   { n: "Radio", vn: "Radio", c: "#FFC107", icon: "radio" },
    bt:      { n: "Bluetooth", vn: "Bluetooth", c: "#48A8FE", icon: "bluetooth" },
    call:    { n: "Call", vn: "Call", c: "#74FE00", icon: "call" },
    wx:      { n: "Weather Radio", vn: "Weather Radio", c: "#FFC107", icon: "partly-sunny" },
    guide:   { n: "Guidance", vn: "Guidance", c: "#48E6FE", icon: "navigate" }
};

var APPS = [
    { id: "nav", name: "Navigation", icon: "navigate" },
    { id: "trails", name: "Trails", icon: "trail-sign" },
    { id: "media", name: "Media", icon: "musical-notes" },
    { id: "radio", name: "Radio", icon: "radio" },
    { id: "aircraft", name: "Aircraft", icon: "airplane" },
    { id: "ships", name: "Ships", icon: "boat" },
    { id: "weather", name: "Weather Radio", icon: "partly-sunny" },
    { id: "climate", name: "Climate", icon: "thermometer" },
    { id: "phone", name: "Phone", icon: "call" },
    { id: "energy", name: "Energy", icon: "battery-charging" },
    { id: "water", name: "Water Tanks", icon: "water" },
    { id: "level", name: "Leveling", icon: "speedometer" },
    { id: "track", name: "Track", icon: "flag" },
    { id: "lights", name: "Lighting", icon: "bulb" },
    { id: "cams", name: "Cameras", icon: "videocam" },
    { id: "pictures", name: "Pictures", icon: "images" },
    { id: "videos", name: "Videos", icon: "film" },
    { id: "settings", name: "Settings", icon: "settings" }
];
var DOCK = ["nav", "media", "radio", "climate", "phone"];

var AIR = [
    { k: "face", l: "Face", i: "air-face" },
    { k: "bi", l: "Face + Feet", i: "air-bi" },
    { k: "feet", l: "Feet", i: "air-feet" },
    { k: "defrost", l: "Feet + Defrost", i: "air-feet-defrost" }
];
// What a press of the climate knob steps through, in the screen's top-down order
var CMODES = ["Temp", "Air", "Fan", "Seat"];
// Fan settings in knob order; a zone's fan is an index into this
var FAN = ["Off", "Auto", "1", "2", "3", "4", "5", "Max"];

var FM = [
    { f: 88.5, n: "KQED", g: "Public Radio", rt: "Morning Edition", s: 0.9 },
    { f: 91.3, n: "KXRT", g: "Classical", rt: "Symphony No. 9 · Dvořák", s: 0.7 },
    { f: 94.7, n: "KTRL", g: "Country", rt: "Long Haul Home · The Drifters Co.", s: 1 },
    { f: 97.1, n: "KPNW", g: "News", rt: "Traffic and weather on the 7s", s: 0.5 },
    { f: 101.9, n: "KMTN", g: "Rock", rt: "Canyon Lines · Red Mesa", s: 1 },
    { f: 104.3, n: "KOUT", g: "Talk", rt: "Trail Talk", s: 0.8 },
    { f: 106.7, n: "KRVR", g: "Hits", rt: "Lowlight · Juno Park", s: 0.6 }
];
var AM = [
    { f: 620, n: "KTAR", g: "Sports", rt: "Game day coverage", s: 0.8 },
    { f: 880, n: "KRCO", g: "News", rt: "Regional news hour", s: 1 },
    { f: 1190, n: "KEXR", g: "Talk", rt: "Open lines", s: 0.55 }
];

// The Weather Radio app's demo channels, as Hmi.wxRows gives the real ones: the
// nearest transmitters to Denver, with made-up strengths
var WX = [
    { mhz: 162.400, f: "162.400", n: "Dillon", c: "WNG737", st: "CO", mi: 60, s: 0 },
    { mhz: 162.425, f: "162.425", n: "Anton", c: "KJY84", st: "CO", mi: 92, s: 0 },
    { mhz: 162.450, f: "162.450", n: "Franktown", c: "WNG550", st: "CO", mi: 30, s: 0.6 },
    { mhz: 162.475, f: "162.475", n: "Mead/Longmont", c: "WXM51", st: "CO", mi: 35, s: 0.4 },
    { mhz: 162.500, f: "162.500", n: "Deer Trail", c: "WXJ45", st: "CO", mi: 54, s: 0.2 },
    { mhz: 162.525, f: "162.525", n: "Fort Morgan", c: "KWN40", st: "CO", mi: 68, s: 0 },
    { mhz: 162.550, f: "162.550", n: "Denver", c: "KEC76", st: "CO", mi: 4, s: 1 }
];

// The Aircraft and Ships apps' demo traffic, as Hmi.trackRows gives the real
// thing: the nearest first there, though not here. dist in nautical miles, brg in
// degrees.
var AIRCRAFT = [
    { id: "A1B2C3", name: "SWA1423", sub: "B737 · PHX → DEN", alt: 11000, speed: 280, dist: 11.2, brg: 32 },
    { id: "A23456", name: "CFLT21", sub: "C25B · SLC → ASE", alt: 8600, speed: 210, dist: 9.1, brg: 73 },
    { id: "A34567", name: "N738PX", sub: "C172", alt: 5500, speed: 104, dist: 7.3, brg: 140 },
    { id: "A45678", name: "SKW5340", sub: "CRJ7 · DEN → GJT", alt: 16400, speed: 340, dist: 14.1, brg: 306 },
    { id: "A56789", name: "UAL587", sub: "A320 · LAX → DEN", alt: 24000, speed: 410, dist: 23.2, brg: 260 },
    { id: "A6789A", name: "DAL2204", sub: "B739 · ATL → SLC", alt: 33000, speed: 455, dist: 31.4, brg: 12 }
];
var SHIPS = [
    { id: "563001000", name: "Maersk Kolkata", sub: "Cargo · to Long Beach", speed: 14.2, course: 310, dist: 10.0, brg: 260 },
    { id: "367200100", name: "Island Hopper", sub: "Passenger · to Avalon", speed: 21.5, course: 135, dist: 5.0, brg: 317 },
    { id: "338200200", name: "Sea Otter", sub: "Sailing", speed: 5.8, course: 220, dist: 3.3, brg: 309 },
    { id: "338200300", name: "Windfall", sub: "Pleasure craft", speed: 0.1, course: 90, dist: 1.6, brg: 273 },
    { id: "636200400", name: "Pacific Star", sub: "Tanker · to San Pedro", speed: 0.2, course: 45, dist: 2.7, brg: 221 },
    { id: "372200500", name: "Harbor Queen", sub: "Tug", speed: 7.4, course: 180, dist: 14.2, brg: 95 }
];
// What an aircraft says it is, when the tables do not know it (ADS-B's emitter
// categories)
var AIR_KIND = { A1: "Light", A2: "Small", A3: "Large", A4: "Large", A5: "Heavy", A6: "High performance", A7: "Helicopter",
                 B1: "Glider", B2: "Balloon", B3: "Parachutist", B4: "Ultralight", B6: "Drone", B7: "Spacecraft" };
var AIR_RANGES = [10, 25, 50, 100];
var SHIP_RANGES = [5, 10, 25, 50];
// What a ship says it is: AIS's ship type codes
function shipKind(code) {
    var exact = { 30: "Fishing", 31: "Towing", 32: "Towing", 33: "Dredger", 34: "Dive vessel", 35: "Military", 36: "Sailing",
                  37: "Pleasure craft", 50: "Pilot", 51: "Search and rescue", 52: "Tug", 53: "Port tender", 55: "Law enforcement",
                  58: "Medical" };
    if (exact[code]) return exact[code];
    return { 2: "Wing in ground", 4: "High speed", 6: "Passenger", 7: "Cargo", 8: "Tanker" }[Math.floor(code / 10)] || "";
}

// Cover colours for music without a cover
var ARTS = ["#3D7B31", "#4a4a4a", "#83A79C", "#2a2a2a", "#9AB090"];

// seq = LEDs that chase toward the turn when it is close
// Keys are what Router gives each step (see router.cpp)
var DIRS = {
    depart: { i: "navigate", a: 0, l: "Head out", seq: [] },
    straight: { i: "arrow-up", a: 0, l: "Continue", seq: [] },
    slight: { i: "trending-up", a: 36, l: "Bear right", seq: [2, 3] },
    slightL: { i: "trending-up-back", a: -36, l: "Bear left", seq: [2, 1] },
    right: { i: "return-up-forward", a: 72, l: "Turn right", seq: [2, 3, 4] },
    left: { i: "return-up-back", a: -72, l: "Turn left", seq: [2, 1, 0] },
    sharpR: { i: "return-down-forward", a: 108, l: "Sharp right", seq: [2, 3, 4] },
    sharpL: { i: "return-down-back", a: -108, l: "Sharp left", seq: [2, 1, 0] },
    uturn: { i: "arrow-undo", a: -144, l: "Make a U-turn", seq: [2, 1, 0] },
    exitR: { i: "trending-up", a: 36, l: "Take the exit", seq: [2, 3] },
    exitL: { i: "trending-up-back", a: -36, l: "Take the exit", seq: [2, 1] },
    merge: { i: "arrow-up", a: 0, l: "Merge", seq: [] },
    round: { i: "sync", a: 0, l: "Roundabout", seq: [] },
    ferry: { i: "boat", a: 0, l: "Take the ferry", seq: [] },
    arrive: { i: "flag", a: 0, l: "Arrive", seq: [] }
};

var CONTACTS = [
    { n: "Maya Chen", num: "(435) 555-0142", ini: "MC", adr: "217 E Center St, Moab, UT 84532" },
    { n: "Jordan Reyes", num: "(801) 555-0199", ini: "JR" },
    { n: "Campground", num: "(435) 555-0110", ini: "CG" },
    { n: "Sam Ortiz", num: "(970) 555-0123", ini: "SO" },
    { n: "Priya Nair", num: "(208) 555-0176", ini: "PN" },
    { n: "Roadside", num: "(800) 555-0100", ini: "RA" }
];
var RECENTS = [
    { c: 0, type: "in", t: "9:58 AM" }, { c: 2, type: "missed", t: "Yesterday" },
    { c: 1, type: "out", t: "Yesterday" }, { c: 3, type: "in", t: "Mon" },
    { c: 5, type: "out", t: "Sun" }, { c: 4, type: "missed", t: "Sat" }
];
var FAVS = [0, 1, 3, 4, 2, 5];

var CAMS = [{ n: "Rear", dir: 180 }, { n: "Left", dir: -90 }, { n: "Right", dir: 90 }, { n: "Front", dir: 0 }];

// Map centre: Moab, UT [lat, lon]
var MAPC = [38.5733, -109.5498];

// Simulated rear park-distance sensors, metres
function camDist(T) { return 1.65 + 1.35 * Math.sin(T * 0.45); }
function camDistR(T) { return 1.8 + 1.25 * Math.sin(T * 0.45 + 1.1); }

function fmtT(s) { var r = s % 60; return Math.floor(s / 60) + ":" + (r < 10 ? "0" : "") + r; }

// ---- Settings ----

// Top level of Settings. page = the page id each row opens.
var SET_SECTIONS = [
    { page: "wifi", n: "Wi-Fi", icon: "wifi" },
    { page: "bt", n: "Bluetooth & Phones", icon: "bluetooth-outline" },
    { page: "can", n: "CAN Buses", icon: "git-network-outline" },
    { page: "trailcurrent", n: "TrailCurrent", icon: "home-outline" },
    { page: "solar", n: "Solar / House Battery", icon: "battery-charging" },
    { page: "cams", n: "Cameras", icon: "videocam" },
    { page: "display", n: "Display", icon: "sunny-outline" },
    { page: "sound", n: "Sound", icon: "volume-high-outline" },
    { page: "navset", n: "Navigation", icon: "navigate-outline" },
    { page: "voice", n: "Voice Assistant", icon: "mic" },
    { page: "units", n: "Units, Date & Time", icon: "time-outline" },
    { page: "system", n: "System", icon: "hardware-chip-outline" }
];

// Nearby Wi-Fi networks until ConnMan is connected. sig 0..1
var WIFI_NETS = [
    { ssid: "Campground Guest", sig: 0.82, sec: "WPA2", saved: true },
    { ssid: "Moab Coffee Co", sig: 0.64, sec: "WPA2", saved: false },
    { ssid: "Site 14 Starlink", sig: 0.91, sec: "WPA3", saved: true },
    { ssid: "RV Park Free", sig: 0.47, sec: "Open", saved: false },
    { ssid: "KOA-Office", sig: 0.33, sec: "WPA2", saved: false }
];

// Paired phones until BlueZ is connected
var PHONES = [
    { name: "Maya's iPhone", model: "iPhone 15", connected: true, primary: true, contacts: true },
    { name: "Jordan's Pixel 8", model: "Pixel 8", connected: false, primary: false, contacts: true }
];
// The phone that asks to pair during "Pair New Phone"
var PAIRING_PHONE = { name: "Sam's Galaxy S24", model: "Galaxy S24" };

var TIMEZONES = ["Pacific", "Mountain", "Arizona", "Central", "Eastern", "Alaska", "Hawaii", "UTC"];
// The system zone each one sets
var ZONES = { Pacific: "America/Los_Angeles", Mountain: "America/Denver", Arizona: "America/Phoenix",
              Central: "America/Chicago", Eastern: "America/New_York", Alaska: "America/Anchorage",
              Hawaii: "Pacific/Honolulu", UTC: "UTC" };

// CAN ports. Each can be the vehicle's bus or the TrailCurrent bus, whichever
// way it is wired. Rates in bit/s; the data rate applies with CAN FD only.
var CAN_ROLES = ["Vehicle", "TrailCurrent", "Off"];
var CAN_RATES = [125000, 250000, 500000, 1000000];
var CAN_DATA_RATES = [1000000, 2000000, 4000000, 5000000, 8000000];
function fmtRate(r) { return r >= 1000000 ? (r / 1000000) + " Mbit/s" : (r / 1000) + " kbit/s"; }

