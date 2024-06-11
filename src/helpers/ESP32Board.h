#pragma once

#include <RippleCore.h>
#include <Arduino.h>

#if defined(ESP_PLATFORM)

#include <rom/rtc.h>
#include <sys/time.h>

class ESP32Board : public ripple::MainBoard {  // abstract class
public:
  void begin() {
    // for future use, sub-classes SHOULD call this from their begin()
  }

  void reboot() override {
    esp_restart();
  }
};

class ESP32RTCClock : public ripple::RTCClock {
public:
  ESP32RTCClock() { }
  void begin() {
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_POWERON) {
      // start with some date/time in the recent past
      struct timeval tv;
      tv.tv_sec = 1715770351;  // 15 May 2024, 8:50pm
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
    }
  }
  uint32_t getCurrentTime() override {
    time_t _now;
    time(&_now);
    return _now;
  }
  void setCurrentTime(uint32_t time) override { 
    struct timeval tv;
    tv.tv_sec = time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  }
};

#endif
