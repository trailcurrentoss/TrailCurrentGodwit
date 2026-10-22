/*
 * dial_usb — the dial's USB face.
 *
 * One composite USB device, two functions:
 *
 *   HID (vendor page)  the dial protocol (dial_protocol.h): knob events and
 *                      icon requests in, ring frames, faces and icons out.
 *                      On Linux it is a hidraw node; no keyboard or mouse
 *                      input device is created, so nothing the dial sends
 *                      can reach another program.
 *   CDC (serial)       the ESP log, and the way in for flashing.
 *
 * The USB-C port has only one PHY, and TinyUSB takes it over from the
 * USB-Serial/JTAG peripheral `idf.py flash` normally talks to. To flash, the
 * firmware reboots itself into ROM download mode when asked -- a
 * `bootloader` line on the serial port, opening the port at 1200 baud, or
 * DIAL_CMD_BOOTLOADER over HID -- and the ROM comes back up on
 * USB-Serial/JTAG. dial-common/scripts/flash.sh does this for you.
 *
 * This layer knows the report IDs and sizes but not what they mean: output
 * reports go to the callback as they are, input reports are queued as given.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * An output report from the head unit. Runs on the TinyUSB task: keep it
 * short, and take the display lock before touching the UI. The next report
 * is not accepted until this returns, which is the protocol's flow control.
 */
typedef void (*dial_usb_report_cb_t)(uint8_t id, const uint8_t *data,
                                     size_t len, void *ctx);

typedef struct {
    const char          *product;   /* USB product string, e.g. "Godwit Dial Left" */
    const uint8_t       *info;      /* DIAL_FEATURE_INFO payload */
    size_t               info_len;
    dial_usb_report_cb_t on_report;
    void                *ctx;
} dial_usb_config_t;

/**
 * Start the USB device. Logs are mirrored to the CDC port once this
 * returns, so anything logged before it is only visible over UART0.
 */
esp_err_t dial_usb_init(const dial_usb_config_t *cfg);

/**
 * Queue an input report. Non-blocking, safe from any task. Returns
 * ESP_ERR_INVALID_STATE if no host has configured the device, and
 * ESP_ERR_TIMEOUT if the queue is full.
 */
esp_err_t dial_usb_send(uint8_t id, const void *data, size_t len);

/** True once the host has configured the device. */
bool dial_usb_mounted(void);

/** Reboot into ROM download mode, for flashing. Returns immediately. */
void dial_usb_request_bootloader(void);

#ifdef __cplusplus
}
#endif
