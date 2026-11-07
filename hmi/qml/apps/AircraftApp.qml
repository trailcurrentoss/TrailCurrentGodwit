import QtQuick
import Godwit

// Aircraft nearby, from their ADS-B broadcasts
TrafficPage {
    id: page
    appId: "aircraft"
    icon: "airplane"
    title: "Aircraft"
    frequency: "1090.000 MHz · ADS-B"
    noun: "AIRCRAFT"
    emptyTitle: "No aircraft yet"
    function fmt(v, unit) {
        return v === undefined ? "--" : String(v).replace(/\B(?=(\d{3})+(?!\d))/g, ",") + " " + unit;
    }
    tiles: [
        { l: "ALTITUDE", v: page.cur ? fmt(page.cur.alt, "ft") : "--" },
        { l: "SPEED", v: page.cur ? fmt(page.cur.speed, "kt") : "--" }
    ]
}
