.pragma library

// The icons a light button can have: the same Font Awesome 6 Free Solid
// glyphs, keys, names and groups as the other TrailCurrent screens
// (Headwaters frontend js/components/fireside-icons.js, which follows
// Fireside's CURATED_ICONS). A button stores the key, so it looks the same on
// every screen. Keep this list in step with that one.
var ICONS = [
    // Lights & Weather
    { cp: 0xf0eb, key: "lightbulb", label: "Light Bulb", group: "Lights & Weather" },
    { cp: 0xf185, key: "sun", label: "Sun", group: "Lights & Weather" },
    { cp: 0xf186, key: "moon", label: "Moon", group: "Lights & Weather" },
    { cp: 0xf0c2, key: "cloud", label: "Cloud", group: "Lights & Weather" },
    { cp: 0xf0e9, key: "umbrella", label: "Umbrella", group: "Lights & Weather" },
    { cp: 0xf2dc, key: "snowflake", label: "Snowflake", group: "Lights & Weather" },
    { cp: 0xf06d, key: "fire", label: "Fire", group: "Lights & Weather" },
    { cp: 0xf7e4, key: "fire-flame-curved", label: "Flame", group: "Lights & Weather" },

    // Power & Utility
    { cp: 0xf0e7, key: "bolt", label: "Bolt", group: "Power & Utility" },
    { cp: 0xf1e6, key: "plug", label: "Plug", group: "Power & Utility" },
    { cp: 0xf240, key: "battery-full", label: "Battery", group: "Power & Utility" },
    { cp: 0xf5e7, key: "charging-station", label: "Charging Station", group: "Power & Utility" },
    { cp: 0xf863, key: "fan", label: "Fan", group: "Power & Utility" },
    { cp: 0xf5aa, key: "paint-roller", label: "Paint Roller", group: "Power & Utility" },

    // Water
    { cp: 0xf043, key: "droplet", label: "Droplet", group: "Water" },
    { cp: 0xe005, key: "faucet", label: "Faucet", group: "Water" },
    { cp: 0xf773, key: "water", label: "Water", group: "Water" },
    { cp: 0xf2cd, key: "bath", label: "Bath / Shower", group: "Water" },

    // Kitchen
    { cp: 0xf2e7, key: "utensils", label: "Utensils", group: "Kitchen" },
    { cp: 0xf7b6, key: "mug-hot", label: "Hot Mug", group: "Kitchen" },
    { cp: 0xf0f4, key: "mug-saucer", label: "Coffee", group: "Kitchen" },

    // Home & Places
    { cp: 0xf015, key: "house", label: "House", group: "Home & Places" },
    { cp: 0xf1ad, key: "building", label: "Building", group: "Home & Places" },
    { cp: 0xf52a, key: "door-closed", label: "Door", group: "Home & Places" },
    { cp: 0xf494, key: "warehouse", label: "Warehouse", group: "Home & Places" },
    { cp: 0xf236, key: "bed", label: "Bed", group: "Home & Places" },
    { cp: 0xf54f, key: "shop", label: "Shop", group: "Home & Places" },
    { cp: 0xf553, key: "shirt", label: "Shirt", group: "Home & Places" },

    // Outdoors
    { cp: 0xf1bb, key: "tree", label: "Tree", group: "Outdoors" },
    { cp: 0xf06c, key: "leaf", label: "Leaf", group: "Outdoors" },
    { cp: 0xf6fc, key: "mountain", label: "Mountain", group: "Outdoors" },
    { cp: 0xf0ac, key: "globe", label: "Globe", group: "Outdoors" },
    { cp: 0xf8ff, key: "caravan", label: "Caravan", group: "Outdoors" },

    // Vehicle & Travel
    { cp: 0xf1b9, key: "car", label: "Car", group: "Vehicle & Travel" },
    { cp: 0xf0d1, key: "truck", label: "Truck", group: "Vehicle & Travel" },
    { cp: 0xf206, key: "bicycle", label: "Bicycle", group: "Vehicle & Travel" },
    { cp: 0xf072, key: "plane", label: "Plane", group: "Vehicle & Travel" },
    { cp: 0xf135, key: "rocket", label: "Rocket", group: "Vehicle & Travel" },

    // Navigation
    { cp: 0xf3c5, key: "location-dot", label: "Location", group: "Navigation" },
    { cp: 0xf041, key: "location-pin", label: "Pin", group: "Navigation" },
    { cp: 0xf5fd, key: "layer-group", label: "Layers", group: "Navigation" },

    // Media
    { cp: 0xf083, key: "camera-retro", label: "Camera", group: "Media" },
    { cp: 0xf008, key: "film", label: "Film", group: "Media" },
    { cp: 0xf001, key: "music", label: "Music", group: "Media" },
    { cp: 0xf025, key: "headphones", label: "Headphones", group: "Media" },
    { cp: 0xf04b, key: "play", label: "Play", group: "Media" },
    { cp: 0xf04a, key: "backward", label: "Backward", group: "Media" },
    { cp: 0xf04d, key: "stop", label: "Stop", group: "Media" },
    { cp: 0xf11b, key: "gamepad", label: "Gamepad", group: "Media" },

    // Communication
    { cp: 0xf095, key: "phone", label: "Phone", group: "Communication" },
    { cp: 0xf075, key: "comment", label: "Comment", group: "Communication" },
    { cp: 0xf1eb, key: "wifi", label: "Wi-Fi", group: "Communication" },
    { cp: 0xf7c0, key: "satellite-dish", label: "Satellite Dish", group: "Communication" },

    // People
    { cp: 0xf0c0, key: "users", label: "Group", group: "People" },
    { cp: 0xf007, key: "user", label: "Person", group: "People" },
    { cp: 0xf183, key: "person", label: "Adult", group: "People" },
    { cp: 0xf21b, key: "user-secret", label: "Incognito", group: "People" },

    // Alerts & Safety
    { cp: 0xf0f3, key: "bell", label: "Bell", group: "Alerts & Safety" },
    { cp: 0xf0f1, key: "stethoscope", label: "Stethoscope", group: "Alerts & Safety" },
    { cp: 0xf06e, key: "eye", label: "Eye", group: "Alerts & Safety" },
    { cp: 0xf132, key: "shield", label: "Shield", group: "Alerts & Safety" },
    { cp: 0xf794, key: "dumpster-fire", label: "Emergency", group: "Alerts & Safety" },

    // Security
    { cp: 0xf023, key: "lock", label: "Lock", group: "Security" },
    { cp: 0xf084, key: "key", label: "Key", group: "Security" },
    { cp: 0xf2f6, key: "right-to-bracket", label: "Sign In", group: "Security" },

    // Time
    { cp: 0xf017, key: "clock", label: "Clock", group: "Time" },
    { cp: 0xf133, key: "calendar", label: "Calendar", group: "Time" },
    { cp: 0xf073, key: "calendar-days", label: "Calendar Days", group: "Time" },

    // Favorites
    { cp: 0xf004, key: "heart", label: "Heart", group: "Favorites" },
    { cp: 0xf005, key: "star", label: "Star", group: "Favorites" },
    { cp: 0xf164, key: "thumbs-up", label: "Thumbs Up", group: "Favorites" },
    { cp: 0xf06b, key: "gift", label: "Gift", group: "Favorites" },

    // Storage
    { cp: 0xf09d, key: "credit-card", label: "Credit Card", group: "Storage" },
    { cp: 0xf0b1, key: "briefcase", label: "Briefcase", group: "Storage" },
    { cp: 0xf0c6, key: "paperclip", label: "Paperclip", group: "Storage" },
    { cp: 0xf02d, key: "book", label: "Book", group: "Storage" },
    { cp: 0xf290, key: "bag-shopping", label: "Shopping Bag", group: "Storage" },

    // System
    { cp: 0xf013, key: "gear", label: "Settings", group: "System" },
    { cp: 0xf1de, key: "sliders", label: "Sliders", group: "System" },
    { cp: 0xf002, key: "magnifying-glass", label: "Search", group: "System" },
    { cp: 0xf05a, key: "circle-info", label: "Info", group: "System" },
    { cp: 0xf0ee, key: "cloud-arrow-up", label: "Cloud Upload", group: "System" },
    { cp: 0xf065, key: "expand", label: "Expand", group: "System" },
    { cp: 0xf1f8, key: "trash", label: "Trash", group: "System" }
];

var BY_KEY = {};
ICONS.forEach(function (i) { BY_KEY[i.key] = i; });

var GROUPS = [];
ICONS.forEach(function (i) { if (GROUPS.indexOf(i.group) < 0) GROUPS.push(i.group); });

// The glyph for a key; a light bulb for one not known
function glyph(key) {
    var i = BY_KEY[key] || BY_KEY.lightbulb;
    return String.fromCharCode(i.cp);
}
function label(key) { return (BY_KEY[key] || BY_KEY.lightbulb).label; }
