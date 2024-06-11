#pragma once

#include <stdint.h>

#define DEST_HASH_SIZE       8
#define PUB_KEY_SIZE        32
#define PRV_KEY_SIZE        64
#define SEED_SIZE           32
#define SIGNATURE_SIZE      64
#define CIPHER_KEY_SIZE     16
#define CIPHER_BLOCK_SIZE   16
#define CIPHER_MAC_SIZE      4

#define MAX_PACKET_PAYLOAD  235
#define MAX_APP_DATA_SIZE    32
#define MAX_TRANS_UNIT      255

#if RIPPLE_DEBUG
  #include <Arduino.h>
  #define RIPPLE_DEBUG_PRINT(...) Serial.printf(__VA_ARGS__)
  #define RIPPLE_DEBUG_PRINTLN(F, ...) Serial.printf(F "\n", ##__VA_ARGS__)
#else
  #define RIPPLE_DEBUG_PRINT(...) {}
  #define RIPPLE_DEBUG_PRINTLN(...) {}
#endif

namespace ripple {

class MainBoard {
public:
  virtual uint16_t getBattMilliVolts() = 0;
  virtual const char* getManufacturerName() const = 0;
  virtual void onBeforeTransmit() { }
  virtual void onAfterTransmit() { }
  virtual void reboot() = 0;
};

}