import QtQuick
import Godwit

// Ships nearby, from their AIS broadcasts. A ship at anchor reports only every
// three minutes, so a position stays good for longer than an aircraft's.
TrafficPage {
    id: page
    appId: "ships"
    icon: "boat"
    title: "Ships"
    frequency: "161.975 and 162.025 MHz · AIS"
    noun: "VESSELS"
    emptyTitle: "No vessels yet"
    staleMs: 300000
    tiles: [
        { l: "SPEED", v: page.cur && page.cur.speed !== undefined ? page.cur.speed.toFixed(1) + " kn" : "--" },
        { l: "COURSE", v: page.cur && page.cur.course !== undefined ? Math.round(page.cur.course) % 360 + "°" : "--" }
    ]
}
