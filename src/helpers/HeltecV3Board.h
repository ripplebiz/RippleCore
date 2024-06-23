#pragma once

#include "ESP32Board.h"
#include <Arduino.h>

// LoRa radio module pins for Heltec V3
#define  P_LORA_DIO_1   14
#define  P_LORA_NSS      8
#define  P_LORA_RESET   RADIOLIB_NC
#define  P_LORA_BUSY    13
#define  P_LORA_SCLK     9
#define  P_LORA_MISO    11
#define  P_LORA_MOSI    10

// built-ins
#define  PIN_VBAT_READ    1
#define  PIN_ADC_CTRL    37
#define  PIN_ADC_CTRL_ACTIVE    LOW
#define  PIN_ADC_CTRL_INACTIVE  HIGH
#define  PIN_LED_BUILTIN 35

#include <driver/rtc_io.h>

class HeltecV3Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      /*long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        flags |= _BD_FLAG_RX_PACKET;
      } else if (wakeup_source & (1 << PIN_WAKE_BTN)) {
        flags |= _BD_FLAG_WAKE_BTN;
      } */

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    }

    // battery read support
    pinMode(PIN_VBAT_READ, INPUT);
    adcAttachPin(PIN_VBAT_READ);
    analogReadResolution(10);
    pinMode(PIN_ADC_CTRL, OUTPUT);
  }

  void enterDeepSleep(uint32_t secs) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep 
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    esp_sleep_enable_ext1_wakeup( (1 << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // Finally set ESP32 into sleep
    esp_deep_sleep_start();   // CPU halts here and never returns!
  }

  uint16_t getBattMilliVolts() override {
    digitalWrite(PIN_ADC_CTRL, PIN_ADC_CTRL_ACTIVE);

    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / 8;

    digitalWrite(PIN_ADC_CTRL, PIN_ADC_CTRL_INACTIVE);

    return (5.2 * (3.3 / 1024.0) * raw) * 1000;
  }

  const char* getManufacturerName() const override {
    return "Heltec V3";
  }
};
