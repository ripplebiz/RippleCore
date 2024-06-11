#pragma once

#include <Dispatcher.h>

class PacketQueue {
  ripple::Packet** _table;
  uint8_t* _pri_table;
  uint32_t* _schedule_table;
  int _size, _num;

public:
  PacketQueue(int max_entries);
  ripple::Packet* get(uint32_t now);
  void add(ripple::Packet* packet, uint8_t priority, uint32_t scheduled_for);
  int count() const { return _num; }
  ripple::Packet* itemAt(int i) const { return _table[i]; }
  ripple::Packet* removeByIdx(int i);
};

class StaticPoolPacketManager : public ripple::PacketManager {
  PacketQueue unused, send_queue;

public:
  StaticPoolPacketManager(int pool_size);

  ripple::Packet* allocNew() override;
  void free(ripple::Packet* packet) override;
  void queueOutbound(ripple::Packet* packet, uint8_t priority, uint32_t scheduled_for) override;
  ripple::Packet* getNextOutbound(uint32_t now) override;
  int getOutboundCount() const override;
  int getFreeCount() const override;
  ripple::Packet* getOutboundByIdx(int i) override;
  ripple::Packet* removeOutboundByIdx(int i) override;
};