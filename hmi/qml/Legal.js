.pragma library
// About & Legal: the driver safety notice shown at every start, and the
// notices the head unit's open source components and map data require.
// Keep COMPONENTS in step with THIRD_PARTY_LICENSES.md at the repository
// root. License texts are assets/licenses/<id>.txt, read through Config.

var VERSION = "0.1.0";
var COPYRIGHT = "Copyright (c) 2026 TrailCurrent Open Source";
var SOURCE_URL = "https://github.com/trailcurrentoss/TrailCurrentGodwit";

// Above the title, the first thing on the notice
var DEMO_NOTICE = "This software is for demonstration and test purposes only";
var SAFETY_TITLE = "Drive safely";
// Accepted once per boot and covers every function, so no feature asks again
var SAFETY = [
    "Using Godwit while driving can take your attention away from the road. Your first responsibility is to drive safely and obey the law.",
    "Keep your eyes on the road and your hands on the wheel. Set destinations, music and settings before you drive, or ask a passenger. Use calls, messages and dictated replies hands-free and only where the law allows, and pull over somewhere safe for anything that needs more than a glance.",
    "Navigation directions, speed limits and travel times can be wrong, out of date, or unsuitable for your vehicle's height, weight or load, the road conditions or the weather. Always obey traffic laws and road signs and use your own judgement.",
    "Cameras, parking distances, and tank, battery, leveling and other vehicle readings are aids only and can be wrong or delayed. Keep checking your mirrors and surroundings, and do not rely on Godwit for emergency calls or safety-critical information.",
    "Godwit is open-source software provided as is, without warranty of any kind. Licenses and notices are under Settings \u203a System \u203a About & Legal, where this notice can also be read again.",
    "By accepting, you agree to these terms for every Godwit function until the head unit is next switched off."
];

// lic: license text ids, shown in order under the component's notice
var COMPONENTS = [
    { n: "Qt", ver: "6.12", lic: ["LGPL-3.0-only", "GPL-3.0-only"], by: "Copyright (c) The Qt Company Ltd. and other contributors",
      role: "The user interface toolkit. Godwit links Qt dynamically, so it can be replaced with a modified version of Qt. Qt's source code is available from download.qt.io and from the source offer in About & Legal." },
    { n: "FFmpeg", ver: "8.0", lic: ["LGPL-2.1-or-later"], by: "Copyright (c) FFmpeg contributors",
      role: "Decodes videos. Godwit links FFmpeg dynamically through Qt Multimedia, so it can be replaced with a modified version. Source code is available from ffmpeg.org and from the source offer in About & Legal." },
    { n: "MapLibre Native Qt", ver: "4.0", lic: ["BSD-2-Clause"], by: "Copyright (c) MapLibre contributors. Portions copyright (c) Mapbox, Inc.",
      role: "Draws the map." },
    { n: "Valhalla", ver: "3.9.1", lic: ["MIT"], by: "Copyright (c) Valhalla contributors. Copyright (c) 2015-2017 Mapillary AB, Mapzen",
      role: "Plans routes and turn-by-turn directions." },
    { n: "Photon", ver: "1.3.0", lic: ["Apache-2.0"], by: "Copyright (c) komoot GmbH and contributors. Includes OpenSearch, copyright (c) OpenSearch contributors",
      role: "Destination search." },
    { n: "Eclipse Temurin JRE", ver: "21", lic: ["GPL-2.0-only", "Classpath-exception-2.0"], by: "Copyright (c) Oracle and/or its affiliates, and Eclipse Adoptium contributors",
      role: "Runs Photon. Source code is available from adoptium.net and from the source offer in About & Legal." },
    { n: "Piper", ver: "2023.11.14-2", lic: ["MIT"], by: "Copyright (c) 2022 Michael Hansen",
      role: "Speaks navigation directions and messages." },
    { n: "Piper voice: en_US LJSpeech", ver: "medium", lic: ["MIT"], by: "Copyright (c) Michael Hansen. Trained on the LJ Speech dataset by Keith Ito, which is in the public domain",
      role: "The voice Godwit speaks with." },
    { n: "eSpeak NG", ver: "", lic: ["GPL-3.0-only"], by: "Copyright (c) Reece H. Dunn, Jonathan Duddington and eSpeak NG contributors",
      role: "Turns text into sounds for Piper. Runs as part of Piper's separate program. Licensed under GPL-3.0-or-later; source code is available from github.com/espeak-ng/espeak-ng and from the source offer in About & Legal." },
    { n: "ONNX Runtime", ver: "", lic: ["MIT"], by: "Copyright (c) Microsoft Corporation",
      role: "Runs Piper's voice model." },
    { n: "whisper.cpp and ggml", ver: "1.7.6", lic: ["MIT"], by: "Copyright (c) 2023-2024 The ggml authors",
      role: "Turns dictated replies into text." },
    { n: "Whisper base.en model", ver: "", lic: ["MIT"], by: "Copyright (c) 2022 OpenAI",
      role: "The speech recognition model whisper.cpp uses." },
    { n: "Boost", ver: "", lic: ["BSL-1.0"], by: "Copyright (c) Boost contributors",
      role: "C++ libraries used by Valhalla and Godwit." },
    { n: "Protocol Buffers", ver: "", lic: ["BSD-3-Clause"], by: "Copyright (c) 2008 Google Inc.",
      role: "Valhalla's data format." },
    { n: "LZ4", ver: "", lic: ["BSD-2-Clause"], by: "Copyright (c) 2011-2020 Yann Collet",
      role: "Compression, used by Valhalla." },
    { n: "zlib", ver: "", lic: ["Zlib"], by: "Copyright (c) 1995-2024 Jean-loup Gailly and Mark Adler",
      role: "Compression." },
    { n: "ICU", ver: "", lic: ["ICU"], by: "Copyright (c) Unicode, Inc. and others",
      role: "Text handling for map labels." },
    { n: "Inter typeface", ver: "", lic: ["OFL-1.1"], by: "Copyright (c) 2016 The Inter Project Authors",
      role: "The interface font." },
    { n: "Ionicons", ver: "", lic: ["MIT"], by: "Copyright (c) 2015-present Ionic",
      role: "Interface icons." },
    { n: "OpenMapTiles style", ver: "3", lic: ["BSD-3-Clause"], by: "Copyright (c) OpenMapTiles contributors, with TrailCurrent changes",
      role: "The map's appearance." },
    { n: "Noto Sans", ver: "", lic: ["OFL-1.1"], by: "Copyright (c) Google Inc.",
      role: "Map label font." },
    { n: "Roboto", ver: "", lic: ["Apache-2.0"], by: "Copyright (c) Google Inc.",
      role: "Map label font." },
    { n: "Metropolis", ver: "", lic: ["Unlicense"], by: "Chris Simpson, released into the public domain",
      role: "Map label font." },
    { n: "Maki icons", ver: "8.0.0", lic: ["CC0-1.0"], by: "Mapbox and contributors, dedicated to the public domain",
      role: "Map place icons." },
    { n: "ESP-IDF", ver: "5.5", lic: ["Apache-2.0"], by: "Copyright (c) Espressif Systems (Shanghai) Co., Ltd.",
      role: "Encoder dial firmware platform. Also esp_lvgl_port, esp_lcd_gc9a01 and esp_tinyusb, under the same license." },
    { n: "LVGL", ver: "9.2.2", lic: ["MIT"], by: "Copyright (c) 2021 LVGL Kft",
      role: "Draws the encoder dials' round displays." },
    { n: "TinyUSB", ver: "0.21", lic: ["MIT"], by: "Copyright (c) 2012-2026 Ha Thach (tinyusb.org)",
      role: "The encoder dials' USB connection." }
];

var DOCS = {
    safety: { title: "Driver Safety Notice", head: [DEMO_NOTICE + "."].concat(SAFETY).join("\n\n") },
    godwit: { title: "Godwit", lic: ["MIT"],
              head: "Godwit " + VERSION + "\n" + COPYRIGHT + "\n\nPart of the TrailCurrent open-source vehicle platform. Source code: " + SOURCE_URL },
    maps: { title: "Map Data", lic: ["ODbL-1.0", "CC-BY-4.0"],
            head: "Map, search and routing data © OpenStreetMap contributors, available under the Open Database License (ODbL) 1.0. openstreetmap.org/copyright\n\n"
                + "Vector tile schema © OpenMapTiles, available under CC BY 4.0. openmaptiles.org\n\n"
                + "Anyone who passes on a map bundle, or data taken from one, must keep these notices and share that data under the ODbL." },
    source: { title: "Source Code",
              head: "Godwit's own source code is at " + SOURCE_URL + ".\n\n"
                  + "Some software on this head unit is licensed under the GNU GPL or LGPL, including the Linux kernel, Qt, FFmpeg, eSpeak NG and the Java runtime. "
                  + "You can get the complete corresponding source code for those components, and the scripts used to build and install them, from the address above, "
                  + "or by asking the TrailCurrent project through it. This offer is valid for three years from the date this software was distributed, and for as long as spare parts or support are offered for it.\n\n"
                  + "The Qt libraries are linked dynamically. You may replace them with a modified version: the build instructions in the source repository describe how to rebuild and install the image." },
    os: { title: "Operating System",
          head: "Godwit runs on Automotive Grade Linux, built with the Yocto Project. It includes the Linux kernel (GPL-2.0-only), "
              + "BlueZ, ConnMan and oFono (GPL-2.0-or-later), PipeWire and WirePlumber (MIT), systemd (LGPL-2.1-or-later), Mesa (MIT) and many other packages.\n\n"
              + "Each package's license text is installed on the head unit under /usr/share/common-licenses, and the full list of packages and their licenses is in /usr/share/common-licenses/license.manifest." }
};

function doc(key) {
    if (key.charAt(0) === "c") {
        var c = COMPONENTS[parseInt(key.slice(1))];
        return { title: c.n, lic: c.lic,
                 head: c.n + (c.ver ? " " + c.ver : "") + "\n" + c.by + "\n\n" + c.role };
    }
    return DOCS[key];
}
