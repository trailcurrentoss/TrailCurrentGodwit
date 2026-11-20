/*
 * The DRV2605 vibration motor driver: a click when the glass is touched,
 * so the select button has some feel, and a tick of the head unit's chosen
 * strength for each turn it acts on.
 *
 * Set up the way Waveshare's DRV2605 demo sets it up (SensorLib's
 * SensorDRV2605::initImpl, then selectLibrary(5) and internal trigger):
 * out of standby, no real-time playback, one effect in the sequencer, and
 * the motor driven as an ERM in open loop. CONFIG_WSKNOB_HAPTIC_MOTOR_LRA
 * drives it as an LRA in closed loop instead. Playing is one register
 * write (GO), so it is done straight from the input task.
 *
 * TURN TICKS are not library effects: those come in three fixed strengths at
 * best. A tick uses real-time playback (RTP) instead, where the motor is
 * driven at whatever amplitude is in RTP_INPUT for as long as the mode is
 * RTP, so strength is continuous. dial_board_haptic_tick() only leaves the
 * strength for the input task, which owns the I2C bus: board_haptic_poll()
 * starts the tick, and stops it CONFIG_WSKNOB_HAPTIC_TURN_MS later. The
 * motor therefore never runs longer than that after the last tick asked.
 */

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_internal.h"
#include "dial_board.h"
#include "board_pins.h"
#include "sdkconfig.h"

static const char *TAG = "board.haptic";

#ifdef CONFIG_WSKNOB_HAPTIC

#define REG_STATUS      0x00    /* bits 7:5 device id */
#define REG_MODE        0x01    /* 0: internal trigger; bit 6 standby */
#define REG_RTPIN       0x02
#define REG_LIBRARY     0x03    /* 1-5 ERM libraries, 6 LRA */
#define REG_WAVESEQ1    0x04
#define REG_WAVESEQ2    0x05
#define REG_GO          0x0C
#define REG_OVERDRIVE   0x0D
#define REG_SUSTAINPOS  0x0E
#define REG_SUSTAINNEG  0x0F
#define REG_BREAK       0x10
#define REG_AUDIOMAX    0x13
#define REG_FEEDBACK    0x1A    /* bit 7: 0 ERM, 1 LRA */
#define REG_CONTROL3    0x1D    /* bit 5 ERM open loop; bit 3 RTP unsigned; bit 0 LRA open loop */

#define MODE_INTERNAL   0x00    /* library effects, played by GO */
#define MODE_RTP        0x05    /* drive at RTP_INPUT until the mode changes */
#define CTRL3_RTP_UNSIGNED 0x08 /* clear: RTP_INPUT is signed, 0x7F full scale */

static i2c_master_dev_handle_t s_dev;
static bool                    s_ready;

#ifdef CONFIG_WSKNOB_HAPTIC_TURN
static uint8_t s_tick_req;      /* any task -> input task, atomically */
static bool    s_rtp;           /* input task only: a tick is playing */
static int64_t s_rtp_end_us;
#endif

static esp_err_t wr(uint8_t reg, uint8_t val)
{
    return board_i2c_write_reg(s_dev, reg, val);
}

static esp_err_t rmw(uint8_t reg, uint8_t clear, uint8_t set)
{
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(board_i2c_read_regs(s_dev, reg, &v, 1), TAG, "read 0x%02x", reg);
    return wr(reg, (uint8_t)((v & ~clear) | set));
}

esp_err_t board_haptic_init(void)
{
    ESP_RETURN_ON_ERROR(board_i2c_add(BOARD_HAPTIC_ADDR, &s_dev), TAG, "add device");

    uint8_t status = 0;
    esp_err_t err = board_i2c_read_regs(s_dev, REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no answer from the DRV2605 at 0x%02x -- no haptics", BOARD_HAPTIC_ADDR);
        return err;
    }
    const unsigned id = status >> 5;      /* 3 DRV2605, 7 DRV2605L, 4/6 DRV2604(L) */

    ESP_RETURN_ON_ERROR(wr(REG_MODE, 0x00), TAG, "mode");            /* out of standby, internal trigger */
    ESP_RETURN_ON_ERROR(wr(REG_RTPIN, 0x00), TAG, "rtp");
    ESP_RETURN_ON_ERROR(wr(REG_WAVESEQ1, CONFIG_WSKNOB_HAPTIC_EFFECT), TAG, "seq1");
    ESP_RETURN_ON_ERROR(wr(REG_WAVESEQ2, 0x00), TAG, "seq2");       /* end of sequence */
    ESP_RETURN_ON_ERROR(wr(REG_OVERDRIVE, 0x00), TAG, "overdrive");
    ESP_RETURN_ON_ERROR(wr(REG_SUSTAINPOS, 0x00), TAG, "sustain+");
    ESP_RETURN_ON_ERROR(wr(REG_SUSTAINNEG, 0x00), TAG, "sustain-");
    ESP_RETURN_ON_ERROR(wr(REG_BREAK, 0x00), TAG, "brake");
    ESP_RETURN_ON_ERROR(wr(REG_AUDIOMAX, 0x64), TAG, "audiomax");
#ifdef CONFIG_WSKNOB_HAPTIC_MOTOR_LRA
    ESP_RETURN_ON_ERROR(rmw(REG_FEEDBACK, 0x00, 0x80), TAG, "lra");
    ESP_RETURN_ON_ERROR(rmw(REG_CONTROL3, 0x01 | CTRL3_RTP_UNSIGNED, 0x00), TAG, "closed loop");
    ESP_RETURN_ON_ERROR(wr(REG_LIBRARY, 6), TAG, "library");
    const char *motor = "LRA, closed loop, library 6";
#else
    ESP_RETURN_ON_ERROR(rmw(REG_FEEDBACK, 0x80, 0x00), TAG, "erm");
    ESP_RETURN_ON_ERROR(rmw(REG_CONTROL3, CTRL3_RTP_UNSIGNED, 0x20), TAG, "open loop");
    ESP_RETURN_ON_ERROR(wr(REG_LIBRARY, 5), TAG, "library");
    const char *motor = "ERM, open loop, library 5";
#endif
    s_ready = true;
    ESP_LOGI(TAG, "DRV2605 id %u: %s, effect %d on touch", id, motor, CONFIG_WSKNOB_HAPTIC_EFFECT);
#ifdef CONFIG_WSKNOB_HAPTIC_TURN
    ESP_LOGI(TAG, "turn ticks: amplitude %d-%d of 127 for %d ms", CONFIG_WSKNOB_HAPTIC_TURN_MIN,
             CONFIG_WSKNOB_HAPTIC_TURN_MAX, CONFIG_WSKNOB_HAPTIC_TURN_MS);
#endif
    return ESP_OK;
}

#ifdef CONFIG_WSKNOB_HAPTIC_TURN

static void rtp_stop(void)
{
    s_rtp = false;
    if (wr(REG_RTPIN, 0x00) != ESP_OK || wr(REG_MODE, MODE_INTERNAL) != ESP_OK) {
        ESP_LOGD(TAG, "tick stop failed");
    }
}

void dial_board_haptic_tick(uint8_t strength)
{
    __atomic_store_n(&s_tick_req, strength, __ATOMIC_RELEASE);
}

void board_haptic_poll(void)
{
    if (!s_ready) {
        return;
    }
    const uint8_t req = __atomic_exchange_n(&s_tick_req, 0, __ATOMIC_ACQ_REL);
    const int64_t now = esp_timer_get_time();
    if (req) {
        /* 1..255 onto MIN..MAX, rounded */
        const int span = CONFIG_WSKNOB_HAPTIC_TURN_MAX - CONFIG_WSKNOB_HAPTIC_TURN_MIN;
        const uint8_t amp = (uint8_t)(CONFIG_WSKNOB_HAPTIC_TURN_MIN + (span * (req - 1) + 127) / 254);
        /* The amplitude first, so RTP never starts at a stale one */
        if (wr(REG_RTPIN, amp) == ESP_OK && (s_rtp || wr(REG_MODE, MODE_RTP) == ESP_OK)) {
            s_rtp = true;
            s_rtp_end_us = now + (int64_t)CONFIG_WSKNOB_HAPTIC_TURN_MS * 1000;
        }
        return;
    }
    if (s_rtp && now >= s_rtp_end_us) {
        rtp_stop();
    }
}

#else

void dial_board_haptic_tick(uint8_t strength)
{
    (void)strength;
}

void board_haptic_poll(void)
{
}

#endif /* CONFIG_WSKNOB_HAPTIC_TURN */

void board_haptic_click(void)
{
    if (!s_ready) {
        return;
    }
#ifdef CONFIG_WSKNOB_HAPTIC_TURN
    /* GO plays a library effect only in internal-trigger mode */
    if (s_rtp) {
        rtp_stop();
    }
#endif
    if (wr(REG_GO, 0x01) != ESP_OK) {
        ESP_LOGD(TAG, "click failed");
    }
}

#else /* !CONFIG_WSKNOB_HAPTIC */

esp_err_t board_haptic_init(void)
{
    ESP_LOGI(TAG, "haptics off (CONFIG_WSKNOB_HAPTIC)");
    return ESP_OK;
}

void board_haptic_click(void)
{
}

void board_haptic_poll(void)
{
}

void dial_board_haptic_tick(uint8_t strength)
{
    (void)strength;
}

#endif
