#pragma once

#include "CustomSX1262.h"
#include "RadioLibWrappers.h"

class CustomSX1262Wrapper : public RadioLibWrapper {
public:
  CustomSX1262Wrapper(CustomSX1262& radio, ripple::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceiving() override { return ((CustomSX1262 *)_radio)->isReceiving(); }
};
