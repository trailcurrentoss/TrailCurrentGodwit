.pragma library
// Spoken commands after "Hey Peregrine": the requests the HMI understands by
// itself, before it asks the language model. Patterns are tried first
// because they answer at once and never guess; the model (Llm) takes the
// loosely worded rest, and fromModel() checks what it wrote.
//
// A command is one of
//   { action: "directions", place }
//   { action: "call", contact }              contact: a name, or a number's digits
//   { action: "text", contact, message }     message may be ""
//   { action: "radio", band, freq } | { action: "radio", preset } | { action: "radio", station } | { action: "radio" }
//   { action: "music", words, source, shuffle }  source: "spotify", "youtube", "local" or "" for any;
//                                            words "" plays the whole library (local) or resumes
//   { action: "endroute" }                   asks before it ends the route

var HELP = "I can help with directions, calls, texts, music and the radio.";

// What Whisper wrote, without the wake word, politeness and end punctuation
function clean(text) {
    var s = String(text || "").trim();
    s = s.replace(/^(?:(?:hey|hi|ok|okay)[\s,]+)?peregrine\b[\s,.!?]*/i, "");
    var lead = /^(?:please|can you|could you|would you|will you|i want to|i'd like to|i would like to|let's|i need to)\s+/i;
    while (lead.test(s)) s = s.replace(lead, "");
    s = s.replace(/[\s,]*\bplease\b/i, "");
    return s.replace(/[\s.!?,]+$/, "").trim();
}

// ---------------------------------------------------------------- numbers
var UNITS = { zero: 0, oh: 0, o: 0, one: 1, two: 2, three: 3, four: 4, five: 5, six: 6, seven: 7, eight: 8, nine: 9 };
var TEENS = { ten: 10, eleven: 11, twelve: 12, thirteen: 13, fourteen: 14, fifteen: 15, sixteen: 16,
              seventeen: 17, eighteen: 18, nineteen: 19 };
var TENS = { twenty: 20, thirty: 30, forty: 40, fifty: 50, sixty: 60, seventy: 70, eighty: 80, ninety: 90 };
var ORD = { first: 1, second: 2, third: 3, fourth: 4, fifth: 5, sixth: 6 };

// "ninety four point seven" -> "94.7", "one oh one point nine" -> "101.9",
// "eleven ninety" -> "1190", "preset three" -> "preset 3". Radio only: in a
// message the words stay as they were said.
function digits(text) {
    var words = String(text).toLowerCase().replace(/-/g, " ").split(/\s+/), out = [], run = "";
    function flush() { if (run !== "") { out.push(run); run = ""; } }
    for (var i = 0; i < words.length; i++) {
        var w = words[i];
        if (w in TENS) {
            var n = TENS[w];
            if (i + 1 < words.length && words[i + 1] in UNITS && UNITS[words[i + 1]] > 0) n += UNITS[words[++i]];
            run += String(n);
        } else if (w in TEENS) {
            run += String(TEENS[w]);
        } else if (w in UNITS && (w !== "o" || run !== "")) {
            run += String(UNITS[w]);
        } else if (w === "hundred" && run !== "") {
            var h = parseInt(run, 10) * 100, rest = 0;
            if (i + 1 < words.length && words[i + 1] in TENS) {
                rest = TENS[words[++i]];
                if (i + 1 < words.length && words[i + 1] in UNITS) rest += UNITS[words[++i]];
            } else if (i + 1 < words.length && words[i + 1] in TEENS) {
                rest = TEENS[words[++i]];
            } else if (i + 1 < words.length && words[i + 1] in UNITS) {
                rest = UNITS[words[++i]];
            }
            run = String(h + rest);
        } else if ((w === "point" || w === "dot") && run !== "" && run.indexOf(".") < 0) {
            run += ".";
        } else if (w in ORD) {
            flush();
            out.push(String(ORD[w]));
        } else {
            flush();
            out.push(w);
        }
    }
    flush();
    return out.join(" ");
}

// ------------------------------------------------------------- contacts
// Words Whisper writes for digits said one at a time
var SAID_DIGIT = { zero: "0", oh: "0", o: "0", one: "1", won: "1", two: "2", to: "2", too: "2",
                   three: "3", four: "4", for: "4", fore: "4", five: "5", six: "6", seven: "7",
                   eight: "8", ate: "8", nine: "9" };

// The digits of a spoken phone number, or "" when the words are not one:
// "435 555 0142", "(435) 555-0142", "four three five, five five five...",
// "8 for 3", "double five"
function phoneNumber(text) {
    var words = String(text || "").toLowerCase().replace(/[()\-.,]/g, " ").split(/\s+/).filter(Boolean);
    var out = "", twice = false;
    for (var i = 0; i < words.length; i++) {
        var w = words[i], d;
        if (w === "double") { twice = true; continue; }
        if (w === "plus" && out === "") { out = "+"; continue; }
        if (/^\+?\d+$/.test(w)) d = w;
        else if (w in SAID_DIGIT) d = SAID_DIGIT[w];
        else if (w in TEENS) d = String(TEENS[w]);
        else if (w in TENS) {
            d = String(TENS[w]);
            if (i + 1 < words.length && words[i + 1] in UNITS && UNITS[words[i + 1]] > 0) d = String(TENS[w] + UNITS[words[++i]]);
        } else if (w === "hundred") { d = "00"; }
        else return "";
        out += twice ? d + d : d;
        twice = false;
    }
    return out.replace(/\D/g, "").length >= 3 ? out : "";
}

// "8015551212" -> "(801) 555-1212"; other lengths as they are
function showNumber(num) {
    var m = /^(?:\+?1)?(\d{3})(\d{3})(\d{4})$/.exec(num);
    return m ? "(" + m[1] + ") " + m[2] + "-" + m[3] : num;
}

// A name to say aloud: a phone number digit by digit, in its groups, so
// the voice says "8 0 1, 5 5 5" rather than "eight hundred million". A bare
// run of digits is grouped as 1, 801, 555, 1212.
function sayName(name) {
    var s = String(name || "");
    if (!/^[\d()+.\-\s]+$/.test(s)) return s;
    var groups = s.replace(/[()+.]/g, " ").split(/[\s-]+/).filter(Boolean);
    if (groups.length === 1) {
        var m = /^(1?)(\d{3})(\d{3})(\d{4})$/.exec(groups[0]);
        if (m) groups = m.slice(m[1] ? 1 : 2);
    }
    return groups.map(function (g) { return g.split("").join(" "); }).join(", ");
}

function similarity(a, b) {
    if (a === b) return 1;
    if (!a.length || !b.length) return 0;
    var prev = [], cur = [];
    for (var j = 0; j <= b.length; j++) prev[j] = j;
    for (var i = 1; i <= a.length; i++) {
        cur = [i];
        for (j = 1; j <= b.length; j++)
            cur[j] = Math.min(prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
        prev = cur;
    }
    return 1 - prev[b.length] / Math.max(a.length, b.length);
}

function plain(s) {
    return String(s || "").toLowerCase().replace(/'s\b/g, "").replace(/[^a-z0-9 ]/g, " ").replace(/\s+/g, " ").trim();
}

// The contact a spoken name means, or null. contacts: [{n, num, ini}].
// A spoken number becomes a contact of its own.
// minScore: how close a name must be (default 0.72, for Whisper's spellings)
function findContact(spoken, contacts, minScore) {
    var num = phoneNumber(spoken);
    if (num) return { n: showNumber(num), num: num, ini: "#" };
    var s = plain(spoken).replace(/^(?:my|the)\s+/, "");
    if (!s) return null;
    var best = null, bestScore = 0;
    (contacts || []).forEach(function (c) {
        var name = plain(c.n), parts = name.split(" "), score;
        if (name === s) score = 1;
        else if (parts.indexOf(s) >= 0) score = parts[0] === s ? 0.95 : 0.9;
        else if (name.indexOf(s) === 0) score = 0.85;
        else score = Math.max(similarity(s, name), similarity(s, parts[0]));
        if (score > bestScore) { best = c; bestScore = score; }
    });
    return bestScore >= (minScore || 0.72) ? best : null;
}

// A destination that is a contact with an address on the phone: "Maya",
// "Maya's house", "Maya Chen's place". Only a close match counts, so places
// are never mistaken for people. Returns the contact or null.
function contactPlace(place, contacts) {
    var name = String(place || "").replace(/'s?\s*(?:house|home|place|apartment|work|office)?$/i, "")
                                  .replace(/\s+(?:house|home|place)$/i, "").trim();
    if (!name || /^(?:home|work)$/i.test(name)) return null;
    var c = findContact(name, (contacts || []).filter(function (x) { return x.adr; }), 0.9);
    return c && c.adr ? c : null;
}

// "435 555 0142 I'm running late" -> ["435 555 0142", "I'm running late"]:
// the longest run of words at the start that is a phone number, or null
function splitNumber(rest) {
    var words = rest.split(/\s+/);
    for (var k = words.length; k > 0; k--) {
        var head = words.slice(0, k).join(" ");
        if (phoneNumber(head)) return [head, words.slice(k).join(" ")];
    }
    return null;
}

// "Maya I'm running late" -> ["Maya", "I'm running late"], using the
// contacts' names to tell where the name ends
function splitName(rest, names) {
    var low = rest.toLowerCase(), best = "";
    (names || []).forEach(function (n) {
        var full = String(n).toLowerCase(), first = full.split(" ")[0];
        [full, first].forEach(function (cand) {
            if (cand.length > best.length && (low === cand || low.indexOf(cand + " ") === 0)) best = cand;
        });
    });
    if (!best) return [rest, ""];
    return [rest.slice(0, best.length), rest.slice(best.length).replace(/^[\s,:]+/, "")];
}

// ---------------------------------------------------------------- radio
function inBand(band, f) {
    return band === "FM" ? f >= 87.5 && f <= 108 : f >= 530 && f <= 1710;
}

// stations: [{b, f, n}] for call letters
function radio(text, stations) {
    var t = digits(text);
    var context = /\b(?:radio|station|fm|am|preset|tune|frequency)\b/i.test(t);
    var lead = /^(?:play|put on|listen to|switch to|change to|go to|tune to|tune in to|set it to)\s+\d/i.test(t);
    var m = /\bpreset\s*(?:number\s*)?(\d)\b/i.exec(t) || /\b(\d)(?:st|nd|rd|th)?\s+(?:radio\s+)?preset\b/i.exec(t);
    if (m && context) return { action: "radio", preset: parseInt(m[1], 10) };
    var bm = /\b(fm|am)\b/i.exec(t), band = bm ? bm[1].toUpperCase() : "";
    var fm = /\b(\d{2,4}(?:\.\d{1,2})?)\b/.exec(t);
    if (fm && (context || lead)) {
        var f = parseFloat(fm[1]);
        // "101.9" heard without the point: AM stations are on multiples of 10
        if (band !== "AM" && f > 108 && f % 10 !== 0 && inBand("FM", f / 10)) { band = "FM"; f = f / 10; }
        if (!band) band = f <= 108 ? "FM" : "AM";
        if (inBand(band, f)) return { action: "radio", band: band, freq: band === "FM" ? Math.round(f * 10) / 10 : Math.round(f) };
        return null;
    }
    var hit = (stations || []).find(function (x) { return new RegExp("\\b" + x.n + "\\b", "i").test(t); });
    if (hit && (context || /^(?:play|listen to|put on|switch to|tune to|tune in to)\b/i.test(t)))
        return { action: "radio", station: hit.n };
    if (context) {
        if (/^(?:turn on|switch to|play|put on|listen to|open)\s+(?:the\s+)?radio$/i.test(t) || /^radio$/i.test(t))
            return { action: "radio" };
        if (band && /^(?:switch to|play|put on|listen to|go to)\s+(?:fm|am)(?:\s+radio)?$/i.test(t))
            return { action: "radio", band: band };
    }
    return null;
}

// The answer to a yes or no question: "yes", "no", "again" (say the
// message again) or "" when it is none of them
function yesNo(text) {
    var s = clean(text).toLowerCase().replace(/[^a-z' ]/g, " ").replace(/\s+/g, " ").trim();
    if (/\b(?:change|again|redo|re-?do|try again|start over|rewrite|fix)\b/.test(s)) return "again";
    if (/^(?:no|nope|nah|don't|do not|cancel|stop|never ?mind|forget it|don't send)\b/.test(s)) return "no";
    if (/^(?:yes|yeah|yep|yup|sure|ok|okay|send|send it|do it|go ahead|correct|that's right|sounds good|please do|affirmative)\b/.test(s)) return "yes";
    return "";
}

// ---------------------------------------------------------------- match
// names: the contacts' names, to split "Text Maya I'm late"
function match(text, names, stations) {
    var s = clean(text), m;
    if (!s) return null;

    // Radio first, so "go to 101.9" tunes rather than searches; a call or a
    // text that mentions a station is still a call or a text
    var r = /^(?:text|message|tell|send|write|call|phone|dial|ring|give)\b/i.test(s) ? null : radio(s, stations);
    if (r) return r;

    if (/^(?:cancel|stop|end|quit|exit|clear|turn off)\s+(?:the\s+|my\s+)?(?:route|navigation|navigating|directions|guidance|trip)$/i.test(s)
        || /^stop navigating$/i.test(s))
        return { action: "endroute" };

    var place = function (p) { return p.replace(/^(?:the\s+)?(?:nearest|closest)\s+/i, "").replace(/^the\s+/i, "").trim(); };
    // "keep" is Whisper's usual mishearing of "get" here
    if ((m = /^(?:get\s+|keep\s+|give me\s+|find\s+|show me\s+|start\s+)?(?:directions|a route|route|navigation)\s+(?:back\s+)?(?:to|for)\s+(.+)$/i.exec(s))
        || (m = /^(?:navigate|drive|take me|take us|head|get me|get us|go)(?:\s+over)?\s+(?:back\s+)?(?:to|towards?)\s+(.+)$/i.exec(s))
        || (m = /^navigate\s+(.+)$/i.exec(s))
        || (m = /^how (?:do|can|would) (?:i|we) get (?:back\s+)?to\s+(.+)$/i.exec(s))
        || (m = /^where(?:'s| is) the (?:nearest|closest)\s+(.+)$/i.exec(s))
        || (m = /^(?:find|search for|look for)(?:\s+me)?\s+(?:an?\s+|the\s+)?(.+)$/i.exec(s)))
        return place(m[1]) ? { action: "directions", place: place(m[1]) } : null;

    if ((m = /^give\s+(.+?)\s+a\s+(?:call|ring)$/i.exec(s))
        || (m = /^(?:make|place) a (?:phone\s+)?call to\s+(.+)$/i.exec(s))
        || (m = /^(?:call|phone|dial|ring)(?:\s+up)?\s+(.+?)(?:\s+back)?(?:\s+(?:on\s+(?:his|her|their)\s+)?(?:mobile|cell|cellphone|home|work)(?:\s+phone)?)?$/i.exec(s)))
        return { action: "call", contact: m[1].trim() };

    // Music, after the radio: "Play Taylor Swift on Spotify", "Put on some Johnny Cash",
    // "Shuffle my library", "Play my music on shuffle"
    var mixed = /\s+(?:on shuffle|shuffled|shuffle|in a random order|at random|randomly)$/i;
    var shuffle = /^shuffle\s+/i.test(s) || mixed.test(s);
    var ms = s.replace(/^shuffle\s+/i, "play ").replace(mixed, "");
    if ((m = /^(?:play|put on|listen to|i want to hear|start)\s+(?:some\s+|the\s+song\s+|the\s+album\s+)?(.+?)(?:\s+(?:on|from|in)\s+(spotify|youtube music|youtube|my music|my library|usb|local music))?$/i.exec(ms))) {
        var src = (m[2] || "").toLowerCase();
        src = src === "spotify" ? "spotify" : /youtube/.test(src) ? "youtube" : src ? "local" : "";
        // The whole library, not a search for the words "my library"
        if (/^(?:all\s+(?:of\s+)?)?(?:my\s+|the\s+)(?:library|music library|music|songs|local music)$|^all\s+(?:songs|music|my songs)$|^everything$/i.test(m[1]))
            return { action: "music", words: "", source: src || "local", shuffle: shuffle };
        var words = m[1].replace(/\s+(?:music|songs)$/i, "").trim();
        if (words && !/^(?:music|something|anything)$/i.test(words)) return { action: "music", words: words, source: src, shuffle: shuffle };
        // "Play some music", "Shuffle something on Spotify": whatever is chosen
        return { action: "music", words: "", source: src, shuffle: shuffle };
    }
    if ((m = /^let\s+(.+?)\s+know\s+(?:that\s+)?(.+)$/i.exec(s)))
        return { action: "text", contact: m[1].trim(), message: m[2].trim() };
    if ((m = /^send\s+(.+?)\s+an?\s+(?:text|message|text message)(?:\s+(?:saying|that says|that)\s+(.+))?$/i.exec(s)))
        return { action: "text", contact: m[1].trim(), message: (m[2] || "").trim() };
    if ((m = /^(?:send|write)\s+(?:a\s+)?(?:new\s+)?(?:text|text message|message|sms)\s+to\s+(.+)$/i.exec(s))
        || (m = /^(?:text|message)\s+(.+)$/i.exec(s))
        || (m = /^tell\s+(?!me\b)(.+)$/i.exec(s))) {
        var rest = m[1].trim(), said = /^(.+?)\s*(?:,|:|\s+saying|\s+that says|\s+and say|\s+to say|\s+that)\s+(.+)$/i.exec(rest);
        var parts = said ? [said[1], said[2]] : splitNumber(rest) || splitName(rest, names);
        return { action: "text", contact: parts[0].trim(), message: parts[1].trim() };
    }
    return null;
}

// The language model's answer as a command, or null when it is not one
function fromModel(cmd, stations) {
    if (!cmd || typeof cmd.action !== "string") return null;
    var str = function (v) { return typeof v === "string" ? v.trim() : typeof v === "number" ? String(v) : ""; };
    switch (cmd.action) {
    case "directions":
        return str(cmd.place) ? { action: "directions", place: str(cmd.place) } : null;
    case "call":
        return str(cmd.contact) ? { action: "call", contact: str(cmd.contact) } : null;
    case "text":
        return str(cmd.contact) ? { action: "text", contact: str(cmd.contact), message: str(cmd.message) } : null;
    case "endroute":
        return { action: "endroute" };
    case "music":
        return str(cmd.words) ? { action: "music", words: str(cmd.words), source: /^(?:spotify|youtube|local)$/.test(str(cmd.source)) ? str(cmd.source) : "", shuffle: cmd.shuffle === true } : null;
    case "radio": {
        var p = parseInt(cmd.preset, 10);
        if (p >= 1) return { action: "radio", preset: p };
        var f = parseFloat(cmd.frequency), band = str(cmd.band).toUpperCase();
        if (!isNaN(f)) {
            if (band !== "FM" && band !== "AM") band = f <= 108 ? "FM" : "AM";
            return inBand(band, f) ? { action: "radio", band: band, freq: band === "FM" ? Math.round(f * 10) / 10 : Math.round(f) } : null;
        }
        if (str(cmd.station)) return radio("radio " + str(cmd.station), stations) || null;
        return { action: "radio" };
    }
    }
    return null;
}
