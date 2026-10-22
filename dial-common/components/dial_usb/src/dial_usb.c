/*
 * dial_usb — TinyUSB composite device: vendor HID + one CDC-ACM port.
 * See dial_usb.h for the interface layout, dial_protocol.h for the reports.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_cntl_struct.h"

#include "class/hid/hid_device.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

#include "dial_protocol.h"
#include "dial_usb.h"

static const char *TAG = "usb";

/* ------------------------------------------------------------------------ *
 * Descriptors
 * ------------------------------------------------------------------------ */

enum {
    ITF_CDC = 0,      /* CDC uses two interfaces: control + data */
    ITF_CDC_DATA,
    ITF_HID,
    ITF_COUNT,
};

enum {
    STR_LANG = 0,
    STR_MANUFACTURER,
    STR_PRODUCT,
    STR_SERIAL,
    STR_CDC,
    STR_HID,
    STR_COUNT,
};

/* Four IN endpoints including EP0's, of the ESP32-S3's five IN FIFOs. */
#define EP_CDC_NOTIF  0x81
#define EP_CDC_OUT    0x02
#define EP_CDC_IN     0x82
#define EP_HID_OUT    0x03
#define EP_HID_IN     0x83

#define HID_EP_SIZE   64
#define HID_INTERVAL  1      /* ms: ~120 ms for an 84 px icon */

/* One report of n bytes, of the given direction. (TinyUSB's HID_REPORT_ID
 * carries its own trailing comma; the other item macros do not.) */
#define DIAL_REPORT(id, usage, n, kind) \
    HID_REPORT_ID(id) HID_USAGE(usage), HID_REPORT_COUNT(n), \
    kind(HID_DATA | HID_VARIABLE | HID_ABSOLUTE)

static const uint8_t s_hid_report_desc[] = {
    HID_USAGE_PAGE_N(HID_USAGE_PAGE_VENDOR, 2),
    HID_USAGE(0x01),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_LOGICAL_MIN(0x00),
        HID_LOGICAL_MAX_N(0xFF, 2),
        HID_REPORT_SIZE(8),
        DIAL_REPORT(DIAL_IN_EVENT,       0x01, DIAL_IN_EVENT_LEN, HID_INPUT),
        DIAL_REPORT(DIAL_IN_ICON_REQ,    0x02, DIAL_REPORT_LEN,   HID_INPUT),
        DIAL_REPORT(DIAL_FEATURE_INFO,   0x03, DIAL_REPORT_LEN,   HID_FEATURE),
        DIAL_REPORT(DIAL_OUT_RING,       0x10, DIAL_REPORT_LEN,   HID_OUTPUT),
        DIAL_REPORT(DIAL_OUT_TEXT,       0x11, DIAL_REPORT_LEN,   HID_OUTPUT),
        DIAL_REPORT(DIAL_OUT_FACE,       0x12, DIAL_REPORT_LEN,   HID_OUTPUT),
        DIAL_REPORT(DIAL_OUT_ICON_BEGIN, 0x13, DIAL_REPORT_LEN,   HID_OUTPUT),
        DIAL_REPORT(DIAL_OUT_ICON_DATA,  0x14, DIAL_REPORT_LEN,   HID_OUTPUT),
        DIAL_REPORT(DIAL_OUT_CONTROL,    0x15, DIAL_REPORT_LEN,   HID_OUTPUT),
    HID_COLLECTION_END,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const uint8_t s_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, STR_CDC, EP_CDC_NOTIF, 8,
                       EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_HID_INOUT_DESCRIPTOR(ITF_HID, STR_HID, HID_ITF_PROTOCOL_NONE,
                             sizeof(s_hid_report_desc), EP_HID_OUT, EP_HID_IN,
                             HID_EP_SIZE, HID_INTERVAL),
};

/*
 * Espressif's VID with the PID TinyUSB's own default scheme gives a
 * CDC + HID device (0x4000 | CDC<<0 | HID<<2). Both dials share it; the head
 * unit finds them by name and tells them apart by DIAL_FEATURE_INFO.
 */
static const tusb_desc_device_t s_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    /* Composite with an IAD (the CDC function). */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A,
    .idProduct          = 0x4005,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STR_MANUFACTURER,
    .iProduct           = STR_PRODUCT,
    .iSerialNumber      = STR_SERIAL,
    .bNumConfigurations = 1,
};

static char s_serial[13];   /* filled from the chip's MAC at init */

static const char *s_strings[STR_COUNT] = {
    [STR_LANG]         = (const char[]){ 0x09, 0x04 },
    [STR_MANUFACTURER] = "TrailCurrent",
    [STR_PRODUCT]      = NULL,   /* from dial_usb_config_t */
    [STR_SERIAL]       = s_serial,
    [STR_CDC]          = "Godwit dial log",
    [STR_HID]          = "Godwit dial",
};

/* ------------------------------------------------------------------------ *
 * HID: reports from the host
 * ------------------------------------------------------------------------ */

static dial_usb_report_cb_t s_report_cb;
static void                *s_report_ctx;
static uint8_t              s_info[DIAL_REPORT_LEN];

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    if (report_type != HID_REPORT_TYPE_FEATURE || report_id != DIAL_FEATURE_INFO) {
        return 0;
    }
    const uint16_t n = reqlen < sizeof(s_info) ? reqlen : sizeof(s_info);
    memcpy(buffer, s_info, n);
    return n;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_type;
    /* On the interrupt OUT endpoint TinyUSB passes report ID 0 and leaves the
     * ID in the first byte; on a SET_REPORT control transfer it has already
     * stripped it. Accept both. */
    if (report_id == 0) {
        if (bufsize == 0) {
            return;
        }
        report_id = buffer[0];
        buffer++;
        bufsize--;
    }
    if (s_report_cb) {
        s_report_cb(report_id, buffer, bufsize, s_report_ctx);
    }
}

/* ------------------------------------------------------------------------ *
 * HID: reports to the host, queued so callers never wait on USB
 * ------------------------------------------------------------------------ */

#define SEND_QUEUE_LEN 32

typedef struct {
    uint8_t id;
    uint8_t len;
    uint8_t data[DIAL_REPORT_LEN];
} out_report_t;

static QueueHandle_t s_send_q;

static void hid_send_task(void *arg)
{
    (void)arg;
    out_report_t r;
    for (;;) {
        if (xQueueReceive(s_send_q, &r, portMAX_DELAY) != pdTRUE || !tud_mounted()) {
            continue;
        }
        if (tud_suspended()) {
            tud_remote_wakeup();
        }
        /* The IN endpoint is polled every millisecond; a host that stops
         * polling for 100 ms has gone, and the report goes with it. */
        for (int i = 0; i < 50 && !tud_hid_ready(); i++) {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (!tud_hid_ready() || !tud_hid_report(r.id, r.data, r.len)) {
            ESP_LOGW(TAG, "report 0x%02x dropped", r.id);
        }
    }
}

esp_err_t dial_usb_send(uint8_t id, const void *data, size_t len)
{
    if (!tud_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    out_report_t r = { .id = id };
    /* Input reports are fixed size: DIAL_IN_EVENT short, the rest full. */
    r.len = (id == DIAL_IN_EVENT) ? DIAL_IN_EVENT_LEN : DIAL_REPORT_LEN;
    memcpy(r.data, data, len < r.len ? len : r.len);
    return xQueueSend(s_send_q, &r, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool dial_usb_mounted(void) { return tud_mounted(); }

/* ------------------------------------------------------------------------ *
 * Serial: the log, and the `bootloader` command
 * ------------------------------------------------------------------------ */

static SemaphoreHandle_t s_tx_lock;

static void cdc_write(const char *buf, size_t len)
{
    if (!tud_cdc_n_connected(0) || !s_tx_lock) {
        return;
    }
    /* Never block the caller on USB: a full buffer drops the rest of the
     * line. Logging must not be able to stall the LVGL or USB task. */
    if (xSemaphoreTake(s_tx_lock, 0) != pdTRUE) {
        return;
    }
    tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (const uint8_t *)buf, len);
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
    xSemaphoreGive(s_tx_lock);
}

static vprintf_like_t s_prev_vprintf;

/* Log to the previous sink (UART0) AND to the CDC port. */
static int log_vprintf(const char *fmt, va_list ap)
{
    va_list copy;
    va_copy(copy, ap);
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, copy);
    va_end(copy);
    if (n > 0) {
        cdc_write(buf, n < (int)sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
    }
    return s_prev_vprintf ? s_prev_vprintf(fmt, ap) : n;
}

static void bootloader_task(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "rebooting into ROM download mode");
    vTaskDelay(pdMS_TO_TICKS(50));   /* let the log line out */
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));  /* host sees the detach */

    /*
     * TinyUSB routed the internal PHY to USB-OTG by setting
     * RTCCNTL.usb_conf.sw_hw_usb_phy_sel. That register is in the RTC
     * domain and survives esp_restart(), so without clearing it the ROM
     * would come up on OTG instead of the USB-Serial/JTAG port esptool
     * expects. Zero hands the PHY back to the hardware default.
     */
    RTCCNTL.usb_conf.sw_hw_usb_phy_sel = 0;
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}

void dial_usb_request_bootloader(void)
{
    static bool requested;
    if (!requested) {
        requested = true;
        xTaskCreate(bootloader_task, "usb_boot", 3072, NULL, 10, NULL);
    }
}

static char   s_rx_line[32];
static size_t s_rx_len;

static void cdc_rx_cb(int itf, cdcacm_event_t *event)
{
    (void)event;
    uint8_t buf[64];
    size_t got = 0;
    if (tinyusb_cdcacm_read(itf, buf, sizeof(buf), &got) != ESP_OK) {
        return;
    }
    for (size_t i = 0; i < got; i++) {
        const char c = (char)buf[i];
        if (c == '\n' || c == '\r') {
            s_rx_line[s_rx_len] = '\0';
            if (strcmp(s_rx_line, "bootloader") == 0) {
                dial_usb_request_bootloader();
            } else if (s_rx_len) {
                ESP_LOGW(TAG, "unknown command '%s'", s_rx_line);
            }
            s_rx_len = 0;
        } else if (s_rx_len < sizeof(s_rx_line) - 1) {
            s_rx_line[s_rx_len++] = c;
        }
    }
}

static uint32_t s_line_baud;

static void cdc_line_coding_cb(int itf, cdcacm_event_t *event)
{
    (void)itf;
    s_line_baud = event->line_coding_changed_data.p_line_coding->bit_rate;
}

static void cdc_line_state_cb(int itf, cdcacm_event_t *event)
{
    (void)itf;
    if (event->line_state_changed_data.dtr) {
        s_rx_len = 0;
    } else if (s_line_baud == 1200) {
        /* The "1200 baud touch": open at 1200, close. Same convention the
         * Arduino core uses, so generic tooling can trigger it. */
        dial_usb_request_bootloader();
    }
}

/* ------------------------------------------------------------------------ */

esp_err_t dial_usb_init(const dial_usb_config_t *cfg)
{
    s_report_cb  = cfg->on_report;
    s_report_ctx = cfg->ctx;
    memcpy(s_info, cfg->info,
           cfg->info_len < sizeof(s_info) ? cfg->info_len : sizeof(s_info));
    s_strings[STR_PRODUCT] = cfg->product;

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    s_tx_lock = xSemaphoreCreateMutex();
    s_send_q  = xQueueCreate(SEND_QUEUE_LEN, sizeof(out_report_t));
    ESP_RETURN_ON_FALSE(s_tx_lock && s_send_q, ESP_ERR_NO_MEM, TAG, "alloc");

    tinyusb_config_t tcfg = TINYUSB_DEFAULT_CONFIG();
    tcfg.descriptor.device            = &s_device_desc;
    tcfg.descriptor.string            = s_strings;
    tcfg.descriptor.string_count      = STR_COUNT;
    tcfg.descriptor.full_speed_config = s_config_desc;
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tcfg), TAG, "tinyusb install");

    const tinyusb_config_cdcacm_t acm = {
        .cdc_port                     = TINYUSB_CDC_ACM_0,
        .callback_rx                  = cdc_rx_cb,
        .callback_line_state_changed  = cdc_line_state_cb,
        .callback_line_coding_changed = cdc_line_coding_cb,
    };
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&acm), TAG, "cdc init");

    ESP_RETURN_ON_FALSE(xTaskCreate(hid_send_task, "usb_hid", 3072, NULL, 5, NULL) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "hid task");

    s_prev_vprintf = esp_log_set_vprintf(log_vprintf);

    ESP_LOGI(TAG, "USB up: \"%s\", HID + CDC, serial %s", cfg->product, s_serial);
    return ESP_OK;
}
