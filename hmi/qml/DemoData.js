.pragma library
// Demo content from the design mockup. Each block is replaced by a backend
// (BlueZ/PipeWire, tuner, Valhalla, CAN) as those land; the shapes stay the same.

var SRC = {
    media: { n: "Media", c: "#7BC96A" },
    radio: { n: "Radio", c: "#FFC107" },
    call:  { n: "Call", c: "#74FE00" },
    guide: { n: "Guidance", c: "#48E6FE" }
};

var APPS = [
    { id: "nav", name: "Navigation", icon: "navigate" },
    { id: "media", name: "Media", icon: "musical-notes" },
    { id: "radio", name: "Radio", icon: "radio" },
    { id: "climate", name: "Climate", icon: "thermometer" },
    { id: "phone", name: "Phone", icon: "call" },
    { id: "energy", name: "Energy", icon: "battery-charging" },
    { id: "water", name: "Water Tanks", icon: "water" },
    { id: "level", name: "Leveling", icon: "speedometer" },
    { id: "lights", name: "Lighting", icon: "bulb" },
    { id: "cams", name: "Cameras", icon: "videocam" },
    { id: "settings", name: "Settings", icon: "settings" }
];
var DOCK = ["nav", "media", "radio", "climate", "phone"];

var AIR = [
    { k: "face", l: "Face", i: "arrow-forward" },
    { k: "bi", l: "Face + Feet", i: "swap-vertical" },
    { k: "feet", l: "Feet", i: "arrow-down" },
    { k: "defrost", l: "Feet + Defrost", i: "arrow-up" }
];
var CMODES = ["Temp", "Fan", "Seat"];

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

// [title, artist, seconds]
var TRACKS = [
    ["Switchback", "Red Mesa", 222], ["Canyon Lines", "Red Mesa", 245],
    ["Lowlight", "Juno Park", 198], ["Long Haul Home", "The Drifters Co.", 264],
    ["Basin Wind", "Hollis Gray", 231], ["Slickrock", "Red Mesa", 207],
    ["Night Shift", "Juno Park", 189], ["Desert Radio", "Ada Lune", 252],
    ["Old Highway", "The Drifters Co.", 240], ["Last Light", "Hollis Gray", 276]
];
var ARTS = ["#3D7B31", "#4a4a4a", "#83A79C", "#2a2a2a", "#9AB090"];

var STEPS = [
    { dir: "right", st: "Center St", d: 0.6 },
    { dir: "left", st: "Main St / US-191 N", d: 0.5 },
    { dir: "slight", st: "UT-128 E", d: 2.1 },
    { dir: "straight", st: "Continue on UT-128", d: 12.4 },
    { dir: "left", st: "Castle Valley Dr", d: 3.2 },
    { dir: "arrive", st: "Castle Valley", d: 0 }
];
// seq = LEDs that chase toward the turn when it is close
var DIRS = {
    right: { i: "return-up-forward", a: 72, l: "Turn right", seq: [2, 3, 4] },
    left: { i: "return-up-back", a: -72, l: "Turn left", seq: [2, 1, 0] },
    slight: { i: "trending-up", a: 36, l: "Bear right", seq: [2, 3] },
    straight: { i: "arrow-up", a: 0, l: "Continue", seq: [] },
    arrive: { i: "flag", a: 0, l: "Arrive", seq: [] }
};

var CONTACTS = [
    { n: "Maya Chen", num: "(435) 555-0142", ini: "MC" },
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
    { page: "display", n: "Display", icon: "sunny-outline" },
    { page: "sound", n: "Sound", icon: "volume-high-outline" },
    { page: "navset", n: "Navigation", icon: "navigate-outline" },
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

var TIMEZONES = ["Pacific", "Mountain", "Arizona", "Central", "Eastern", "Alaska", "Hawaii"];

// CAN ports. Each can be the vehicle's bus or the TrailCurrent bus, whichever
// way it is wired. Rates in bit/s; the data rate applies with CAN FD only.
var CAN_ROLES = ["Vehicle", "TrailCurrent", "Off"];
var CAN_RATES = [125000, 250000, 500000, 1000000];
var CAN_DATA_RATES = [1000000, 2000000, 4000000, 5000000, 8000000];
function fmtRate(r) { return r >= 1000000 ? (r / 1000000) + " Mbit/s" : (r / 1000) + " kbit/s"; }

// ---- Navigation search ----
// Places near the demo position (Moab, UT) until Photon search is connected.
// mi = straight-line distance from the vehicle.
var PLACES = [
    { n: "Arches National Park", a: "Visitor Center · Moab, UT", icon: "leaf-outline", mi: 4.6 },
    { n: "Canyonlands · Island in the Sky", a: "Visitor Center · Moab, UT", icon: "leaf-outline", mi: 31.8 },
    { n: "Dead Horse Point State Park", a: "UT-313 · Moab, UT", icon: "leaf-outline", mi: 29.4 },
    { n: "Castle Valley", a: "Castle Valley, UT", icon: "flag-outline", mi: 16.9 },
    { n: "Goose Island Campground", a: "UT-128 · Moab, UT", icon: "bonfire-outline", mi: 1.6 },
    { n: "Ken's Lake Campground", a: "Spanish Valley · Moab, UT", icon: "bonfire-outline", mi: 9.2 },
    { n: "Sand Flats Recreation Area", a: "Sand Flats Rd · Moab, UT", icon: "bonfire-outline", mi: 2.8 },
    { n: "Moab KOA", a: "US-191 S · Moab, UT", icon: "bonfire-outline", mi: 4.1 },
    { n: "City Market", a: "Main St · Moab, UT", icon: "storefront-outline", mi: 0.9 },
    { n: "Moab Brewery", a: "Main St · Moab, UT", icon: "restaurant-outline", mi: 1.1 },
    { n: "Moab Coffee Roasters", a: "Main St · Moab, UT", icon: "cafe-outline", mi: 0.4 },
    { n: "Slickrock Bike Trail", a: "Sand Flats Rd · Moab, UT", icon: "location-outline", mi: 3.4 }
];
var SAVED_PLACES = [
    { n: "Home", a: "Salt Lake City, UT", icon: "home-outline", mi: 232 },
    { n: "Favourite campsite", a: "Goose Island Campground", icon: "star-outline", mi: 1.6 }
];
var RECENT_DEST = ["Castle Valley", "Arches National Park", "City Market"];
