.pragma library
// Colour maths and LED ring patterns for the two encoder rings.
// A pattern ("spec") is a function (k, T) -> {c: "#rrggbb", a: 0..1} giving
// LED k (0..4) at time T seconds. LEDs sit at -144, -72, 0, 72, 144 degrees:
// lower-left, upper-left, top, upper-right, lower-right.

var POS = [-144, -72, 0, 72, 144];

var COOL = "#48E6FE", WARM = "#FF5453", NEU = "#EBEBEB", GREEN = "#7BC96A",
    LIME = "#74FE00", AMBER = "#FFC107", RED = "#FF5453", CYAN = "#48E6FE",
    WHITE = "#FFFFFF";

function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }

function h2r(h) { return [1, 3, 5].map(function (i) { return parseInt(h.slice(i, i + 2), 16); }); }

function mix(a, b, t) {
    var A = h2r(a), B = h2r(b), tt = clamp(t, 0, 1);
    return "#" + A.map(function (v, i) {
        var s = Math.round(v + (B[i] - v) * tt).toString(16);
        return s.length < 2 ? "0" + s : s;
    }).join("");
}

var WARMW = mix("#FFFFFF", "#FFC107", 0.35);

function ang(a, b) { return Math.abs(((a - b) % 360 + 540) % 360 - 180); }
function pulse(T, hz) { return 0.5 + 0.5 * Math.sin(T * Math.PI * 2 * hz); }
// Pseudo audio level, for the "lead LED flickers with audio" effect
function lvl(T) { return 0.3 + 0.55 * Math.abs(Math.sin(T * 3.7)) * Math.abs(Math.sin(T * 1.3 + 1)); }

function fill5(lv, c, dim) {
    return function (k) {
        var a = clamp(lv * 5 - k, 0, 1);
        return { c: c, a: Math.max(a, dim === undefined ? 0.05 : dim) };
    };
}
function mark5(pos, c, dim) {
    return function (k) {
        var a = Math.max(0, 1 - Math.abs(pos * 4 - k));
        return a > 0.02 ? { c: c, a: a } : { c: c, a: dim === undefined ? 0.05 : dim };
    };
}
function dir5(dir, c, dim) {
    return function (k) {
        var a = Math.max(0, 1 - ang(POS[k], dir) / 72);
        return a > 0.02 ? { c: c, a: a } : { c: c, a: dim === undefined ? 0.04 : dim };
    };
}
// Spinning comet: rev revolutions per second, negative spins anticlockwise
function comet5(k, T, rev) {
    var h = ((T * rev * 360) % 360 + 360) % 360, th = (POS[k] + 360) % 360;
    var d = rev >= 0 ? ((h - th + 360) % 360) : ((th - h + 360) % 360);
    return d < 150 ? 1 - d / 150 : 0;
}
function allC(c, a) { return function () { return { c: c, a: a }; }; }

// cool -> neutral -> warm across the ring
function gradT(t) { return t < 0.5 ? mix(COOL, NEU, t * 2) : mix(NEU, WARM, (t - 0.5) * 2); }
// radio signal strength: red -> amber -> lime
function sigCol(s) { return s < 0.5 ? mix(RED, AMBER, s * 2) : mix(AMBER, LIME, (s - 0.5) * 2); }
// turn urgency: cyan -> lime -> amber -> red-amber
function urgCol(u) {
    return u < 0.5 ? mix(CYAN, LIME, u * 2)
         : u < 0.85 ? mix(LIME, AMBER, (u - 0.5) / 0.35)
         : mix(AMBER, mix(AMBER, RED, 0.45), (u - 0.85) / 0.15);
}

// Composite one frame of a ring: the pattern plus the press, detent, end-stop
// bump and hold-progress effects. Returns 5 premultiplied {r,g,b} in 0..255.
function frame(spec, fx, T, gain) {
    var out = [];
    for (var k = 0; k < 5; k++) {
        var o = null;
        try { o = spec ? spec(k, T) : null; } catch (e) { o = null; }
        var c = o ? o.c : null, a = o ? o.a : 0;
        if (fx.press) {
            var q = 1 - (T - fx.press) / 0.3;
            if (q > 0) { c = c ? mix(c, WHITE, q * 0.7) : WHITE; a = Math.max(a, q * 0.9); }
        }
        if (fx.det && k === fx.det.k) {
            var q2 = 1 - (T - fx.det.t) / 0.22;
            if (q2 > 0) { c = c ? mix(c, WHITE, q2 * 0.8) : WHITE; a = Math.max(a, q2); }
        }
        if (fx.bump && k === (fx.bump.d > 0 ? 4 : 0)) {
            var q3 = 1 - (T - fx.bump.t) / 0.45;
            if (q3 > 0) { c = RED; a = Math.max(a, q3); }
        }
        if (fx.hold) {
            var p = (T - fx.hold.t) / 0.43;
            if (p > 0 && k < p * 5) { c = fx.hold.c; a = 1; }
        }
        var A = c && a > 0.02 ? clamp(a * gain, 0, 1) : 0;
        if (!A) { out.push({ r: 0, g: 0, b: 0 }); continue; }
        var rgb = h2r(c);
        out.push({ r: Math.round(rgb[0] * A), g: Math.round(rgb[1] * A), b: Math.round(rgb[2] * A) });
    }
    return out;
}
