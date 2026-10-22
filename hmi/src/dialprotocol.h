#pragma once

// The Godwit dial protocol, as the head unit speaks it. The dial firmware's
// copy, with the full description of every report, is
// dial-common/components/dial_usb/include/dial_protocol.h; the two must
// agree, and DialProto::Version is bumped on any incompatible change.
// dial-common/docs/protocol.md describes the exchange in prose.

#include <cstdint>

namespace DialProto {

constexpr int Version = 1;
constexpr int ReportLen = 63; // payload bytes after the report ID

// Dial -> head unit
constexpr uint8_t InEvent = 0x01;   // [kind, arg]
constexpr uint8_t InIconReq = 0x02; // [size px, name...]
constexpr uint8_t FeatureInfo = 0x03; // [version, side, firmware version...]

enum Event : uint8_t { EvDown = 1, EvUp = 2, EvTurn = 3 };

// Head unit -> dial
constexpr uint8_t OutRing = 0x10;      // 5 x [r, g, b]
constexpr uint8_t OutText = 0x11;      // [field, utf8...]
constexpr uint8_t OutFace = 0x12;      // Face; commits the text sent before it
constexpr uint8_t OutIconBegin = 0x13; // [size px, name...]
constexpr uint8_t OutIconData = 0x14;  // [offset lo, offset hi, count, bytes...]
constexpr uint8_t OutControl = 0x15;   // [cmd]

constexpr int TextMax = ReportLen - 1;
constexpr int IconChunk = ReportLen - 3;
constexpr int IconMaxPx = 96;

enum Field : uint8_t { Label, Value, Unit, Sub, Icon, FieldCount };
enum Color : uint8_t { Tx, Acc, Heat, Cool, Warn };

constexpr uint8_t FaceDark = 0x01;
constexpr uint8_t FaceSmall = 0x02;
constexpr uint8_t PagerNone = 0xff;

// DialProto::OutFace payload, in order
struct Face {
    uint8_t flags;
    uint8_t color;
    uint8_t valuePx;
    uint8_t iconPx;
    uint8_t pagerCount;
    uint8_t pagerOn;
    uint8_t backlight;
};
static_assert(sizeof(Face) == 7);

} // namespace DialProto
