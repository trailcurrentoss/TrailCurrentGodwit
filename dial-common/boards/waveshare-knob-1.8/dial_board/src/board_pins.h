/*
 * Waveshare ESP32-S3-Knob-Touch-LCD-1.8 — pin map, for the ESP32-S3.
 *
 * Source: the board's schematic (ESP32-S3-Knob-Touch-LCD-1.8-schematic.zip
 *         from the wiki, sheet 2 "ESP32S3-R8", the net list down the right)
 *         and Waveshare's demo (ESP32-S3-Knob-Touch-LCD-1.8-Demo.zip,
 *         ESP-IDF/08_LVGL_Test/main/user_config.h, 04_Encoder_Test and
 *         03_DRV2605_Test). The two agree on every value.
 *
 * The board carries two MCUs. Everything here is on the ESP32-S3R8 (8 MB
 * octal PSRAM, 16 MB flash); the ESP32-U4WDH beside it has its own encoder
 * contacts, its own USB-UART and a UART link to the S3 (GPIO38/48), and the
 * dial firmware leaves it alone. The USB-C port reaches only one of them at
 * a time -- the switch on the base must be on the ESP32-S3 side.
 *
 * If you change a number here, cite where it came from. A wrong GPIO in this
 * file presents as a dead panel, a dead knob or a dead touch, and the
 * debugging goes looking at the driver.
 */
#pragma once

/* QSPI panel: ST77916, 360x360. LCD_TE is not wired to the S3. */
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_QSPI_SCK      13
#define BOARD_LCD_QSPI_CS       14
#define BOARD_LCD_QSPI_D0       15
#define BOARD_LCD_QSPI_D1       16
#define BOARD_LCD_QSPI_D2       17
#define BOARD_LCD_QSPI_D3       18
#define BOARD_LCD_RST           21
#define BOARD_LCD_QSPI_HZ       (40 * 1000 * 1000)   /* as the demo runs it */

/* Backlight: LCD_BLK drives an AO3400A N-FET in the LED cathode, active
 * high, with a 10 K pull-down -- dark until the firmware says otherwise. */
#define BOARD_BL_GPIO           47
#define BOARD_BL_PWM_HZ         20000
#define BOARD_BL_PWM_BITS       8

/* I2C: the touch controller and the DRV2605 share the bus, which has 5K1
 * pull-ups to 3V3 on the board (R5, R6). The demo runs both at 300 kHz. */
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_I2C_SDA           11
#define BOARD_I2C_SCL           12
#define BOARD_I2C_HZ            300000

/* CST816 touch controller. INT and RST go to the panel's FPC. */
#define BOARD_TOUCH_ADDR        0x15
#define BOARD_TOUCH_INT         9
#define BOARD_TOUCH_RST         10

/* DRV2605L vibration motor driver. Its EN is tied to 3V3 and IN/TRIG to
 * GND, so it is always on and plays by I2C. */
#define BOARD_HAPTIC_ADDR       0x5A

/*
 * The knob. EC1_A/EC1_B are the two contacts of an Alps SSCM110100 two-way
 * detector switch (SW2), each with a 10 K pull-up (R59, R60) and closing to
 * GND: NOT a quadrature encoder. One contact closes for a step clockwise and
 * the other for a step anticlockwise. (SW1, EC2_A/B, is the ESP32's.)
 */
#define BOARD_KNOB_CW           8    /* EC1_A, active low */
#define BOARD_KNOB_CCW          7    /* EC1_B, active low */

/*
 * Not driven by this firmware:
 *   GPIO0         I2S_SWITCH_IN (picks which MCU feeds the DAC) and the
 *                 BOOT strap, pulled up
 *   GPIO1         BATT_ADC
 *   GPIO2-6, 42   TF card (SDMMC)
 *   GPIO19/20     USB D-/D+ (TinyUSB's)
 *   GPIO38/48     UART to the ESP32
 *   GPIO39-41     I2S to the PCM5100A DAC
 *   GPIO45/46     PDM microphone
 * GPIO43/44 (UART0) carry the ESP console -- see sdkconfig.defaults.
 */
