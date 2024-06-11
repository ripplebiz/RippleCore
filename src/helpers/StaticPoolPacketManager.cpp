#include "StaticPoolPacketManager.h"

PacketQueue::PacketQueue(int max_entries) {
  _table = new ripple::Packet*[max_entries];
  _pri_table = new uint8_t[max_entries];
  _schedule_table = new uint32_t[max_entries];
  _size = max_entries;
  _num = 0;
}

ripple::Packet* PacketQueue::get(uint32_t now) {
  uint8_t min_pri = 0xFF;
  int best_idx = -1;
  for (int j = 0; j < _num; j++) {
    if (_schedule_table[j] > now) continue;   // scheduled for future... ignore for now
    if (_pri_table[j] < min_pri) {  // select most important priority amongst non-future entries
      min_pri = _pri_table[j];
      best_idx = j;
    }
  }
  if (best_idx < 0) return NULL;   // empty, or all items are still in the future

  ripple::Packet* top = _table[best_idx];
  int i = best_idx;
  _num--;
  while (i < _num) {
    _table[i] = _table[i+1];
    _pri_table[i] = _pri_table[i+1];
    _schedule_table[i] = _schedule_table[i+1];
    i++;
  }
  return top;
}

ripple::Packet* PacketQueue::removeByIdx(int i) {
  if (i >= _num) return NULL;  // invalid index

  ripple::Packet* item = _table[i];
  _num--;
  while (i < _num) {
    _table[i] = _table[i+1];
    _pri_table[i] = _pri_table[i+1];
    _schedule_table[i] = _schedule_table[i+1];
    i++;
  }
  return item;
}

void PacketQueue::add(ripple::Packet* packet, uint8_t priority, uint32_t scheduled_for) {
  if (_num == _size) {
    // TODO: log "FATAL: queue is full!"
    return;
  }
  _table[_num] = packet;
  _pri_table[_num] = priority;
  _schedule_table[_num] = scheduled_for;
  _num++;
}

StaticPoolPacketManager::StaticPoolPacketManager(int pool_size): unused(pool_size), send_queue(pool_size) {
  // load up our unusued Packet pool
  for (int i = 0; i < pool_size; i++) {
    unused.add(new ripple::Packet(), 0, 0);
  }
}

ripple::Packet* StaticPoolPacketManager::allocNew() {
  return unused.removeByIdx(0);  // just get first one (returns NULL if empty)
}

void StaticPoolPacketManager::free(ripple::Packet* packet) {
  unused.add(packet, 0, 0);
}

void StaticPoolPacketManager::queueOutbound(ripple::Packet* packet, uint8_t priority, uint32_t scheduled_for) {
  send_queue.add(packet, priority, scheduled_for);
}

ripple::Packet* StaticPoolPacketManager::getNextOutbound(uint32_t now) {
  //send_queue.sort();   // sort by scheduled_for/priority first
  return send_queue.get(now);
}

int  StaticPoolPacketManager::getOutboundCount() const {
  return send_queue.count();
}

int StaticPoolPacketManager::getFreeCount() const {
  return unused.count();
}

ripple::Packet* StaticPoolPacketManager::getOutboundByIdx(int i) {
  return send_queue.itemAt(i);
}
ripple::Packet* StaticPoolPacketManager::removeOutboundByIdx(int i) {
  return send_queue.removeByIdx(i);
}
