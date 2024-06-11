#pragma once

#include <MeshTransportNone.h>

namespace ripple {

/**
 * \brief  Applications that also take on the 'Transport node' role should sub-class this. eg. Repeaters.
*/
class MeshTransportFull : public MeshTransportNone {
protected:
  DispatcherAction onAnnounceRecv(Packet* packet, const Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) override;
  DispatcherAction onDatagramRecv(Packet* packet, const uint8_t* packet_hash) override;
  DispatcherAction onReplyRecv(Packet* packet) override;
  DispatcherAction onReplySignedRecv(Packet* packet, const uint8_t* reply, size_t reply_len) override;

  void onBeforeAnnounceRetransmit(Packet* packet) override;

  void prepareLocalReply(Packet* packet) override;

public:
  /**
   * \brief Application should set its Identity typicaly during setup().
  */
  LocalIdentity  self_id;
  uint8_t max_hops_supported;

  MeshTransportFull(Radio& radio, MillisecondClock& ms, RNG& rng, RTCClock& rtc, PacketManager& mgr, MeshTables& tables)
    : MeshTransportNone(radio, ms, rng, rtc, mgr, tables)
  {
    max_hops_supported = 64;  // some standard default?
  }
  void begin();
  void loop();
};

}