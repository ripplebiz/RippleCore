#pragma once

#include <RadioLib.h>

#define SX126X_IRQ_HEADER_VALID                     0b0000010000  //  4     4     valid LoRa header received

class CustomSX1268 : public SX1268 {
  public:
    CustomSX1268(Module *mod) : SX1268(mod) { }

    bool isReceiving() {
      uint16_t irq = getIrqStatus();
      bool hasPreamble = (irq & SX126X_IRQ_HEADER_VALID);
      return hasPreamble;
    }
};