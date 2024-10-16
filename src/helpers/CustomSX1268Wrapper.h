#pragma once

#include "CustomSX1268.h"
#include "RadioLibWrappers.h"

class CustomSX1268Wrapper : public RadioLibWrapper {
public:
  CustomSX1268Wrapper(CustomSX1268& radio, ripple::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceiving() override { return ((CustomSX1268 *)_radio)->isReceiving(); }
  float getLastRSSI() const override { return ((CustomSX1268 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomSX1268 *)_radio)->getSNR(); }
};
