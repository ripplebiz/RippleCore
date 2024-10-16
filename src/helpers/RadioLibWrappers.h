#pragma once

#include <Mesh.h>
#include <RadioLib.h>

class RadioLibWrapper : public ripple::Radio {
protected:
  PhysicalLayer* _radio;
  ripple::MainBoard* _board;
  uint32_t n_recv, n_sent;

public:
  RadioLibWrapper(PhysicalLayer& radio, ripple::MainBoard& board) : _radio(&radio), _board(&board) { n_recv = n_sent = 0; }

  void begin() override;
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  void startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsSent() const { return n_sent; }
  virtual float getLastRSSI() const;
  virtual float getLastSNR() const;
};

/**
 * \brief  an RNG impl using the noise from the LoRa radio as entropy.
 *         NOTE: this is QUITE SLOW!  Use only for things like creating new LocalIdentity
*/
class RadioNoiseGenerator : public ripple::RNG {
  PhysicalLayer* _radio;
public:
  RadioNoiseGenerator(PhysicalLayer& radio): _radio(&radio) { }

  void random(uint8_t* dest, size_t sz) override {
    for (int i = 0; i < sz; i++) {
      dest[i] = _radio->randomByte() ^ (::random(0, 256) & 0xFF);
    }
  }
};
