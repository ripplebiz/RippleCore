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

class HeltecV3Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();

    // battery read support
    pinMode(PIN_VBAT_READ, INPUT);
    adcAttachPin(PIN_VBAT_READ);
    analogReadResolution(10);
    pinMode(PIN_ADC_CTRL, OUTPUT);
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
