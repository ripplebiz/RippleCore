
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

static volatile uint8_t state = STATE_IDLE;

// this function is called when a complete packet
// is transmitted by the module
static 
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  // we sent a packet, set the flag
  state |= STATE_INT_READY;
}

void RadioLibWrapper::begin() {
  _radio->setPacketReceivedAction(setFlag);  // this is also SentComplete interrupt
  state = STATE_IDLE;

  setFlag(); // in case LoRa packet is already received, eg. for deep sleep wakeup
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  if (state & STATE_INT_READY) {
    int len = _radio->getPacketLength();
    if (len > 0) {
      if (len > sz) { len = sz; }
      int err = _radio->readData(bytes, len);
      if (err != RADIOLIB_ERR_NONE) {
        RIPPLE_DEBUG_PRINTLN("RadioLibWrapper: error: readData()");
      } else {
      //  Serial.print("  readData() -> "); Serial.println(len);
      }
      n_recv++;
    }
    state = STATE_IDLE;   // need another startReceive()
    return len;
  }

  if (state != STATE_RX) {
    int err = _radio->startReceive();
    if (err != RADIOLIB_ERR_NONE) {
      RIPPLE_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive()");
    }
    state = STATE_RX;
  }
  return 0;
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len_bytes) {
  return _radio->getTimeOnAir(len_bytes) / 1000;
}

void RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  state = STATE_TX_WAIT;
  _board->onBeforeTransmit();
  int err = _radio->startTransmit((uint8_t *) bytes, len);
  if (err != RADIOLIB_ERR_NONE) {
    RIPPLE_DEBUG_PRINTLN("RadioLibWrapper: error: startTransmit()");
  }
}

bool RadioLibWrapper::isSendComplete() {
  if (state & STATE_INT_READY) {
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  _radio->finishTransmit();
  _board->onAfterTransmit();
  state = STATE_IDLE;
}
