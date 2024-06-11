#pragma once

#include <Mesh.h>
#include <MeshTables.h>

namespace ripple {

/**
 * The next layer of Mesh, for edge nodes.  Applications should sub-class this when NOT wanting to be Transport nodes.
*/
class MeshTransportNone : public Mesh {
  uint16_t confirm_secs;
  uint8_t  retry_priority;
  Packet*  curr_self_announce;
  unsigned long confirmation_timeout;

protected:
  MeshTables* _tables;

  void onPacketSent(ripple::Packet* packet) override;

  bool isAnnounceNew(Packet* packet, const Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) override;
  DispatcherAction onAnnounceRecv(Packet* packet, const Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) override;
  bool isDatagramNew(Packet* packet, const uint8_t* packet_hash) override;
  DispatcherAction onDatagramRecv(Packet* packet, const uint8_t* packet_hash) override;
  DispatcherAction onReplyRecv(Packet* packet) override;
  bool isReplySignedNew(Packet* packet) override;
  DispatcherAction onReplySignedRecv(Packet* packet, const uint8_t* reply, size_t reply_len) override;

  virtual void onBeforeAnnounceRetransmit(Packet* packet);

  void prepareLocalAnnounce(Packet* packet, const uint8_t* rand_blob) override;
  void prepareLocalDatagram(Packet* packet) override;
  void prepareLocalReply(Packet* packet) override;

public:
  MeshTransportNone(Radio& radio, MillisecondClock& ms, RNG& rng, RTCClock& rtc, PacketManager& mgr, MeshTables& tables)
     : Mesh(radio, ms, rng, rtc, mgr), _tables(&tables)
    {
      confirm_secs = 0;
      confirmation_timeout = 0;
      curr_self_announce = NULL;
    }

  void begin();
  void loop();

  /**
   * \brief   A helper method for looking up if we have a path, ie next-hop, to given dest_hash. Optionally retrieving
   *         the original Announce packet and timestamp when we received it. (by local RTC clock)
  */
  bool hasPathTo(const uint8_t* dest_hash, Packet* orig_announce=NULL, uint32_t* recv_timestamp=NULL) const;

  /**
   * \brief  A special utility method for sending out a one-hop broadcast, ie. just to immediate nodes, asking if they have 
   *       a next-hop to 'dest_hash'. Neighbor nodes either don't respond (if they don't know dest), or re-transmit the original
   *       Announce packet of 'dest_hash'.
  */
  bool requestPathTo(const uint8_t* dest_hash);

  /**
   * \brief  Sends an announce packet, optionally re-sending if no 'confirmations' received
   * \param confirm_timeout_secs  
   *        if > 0, will re-send the SAME exact Packet if no confirmations recv (in seconds, by expo back-off).
   *        Otherwise (if 0) a normal sendPacket()
  */
  void sendAnnounce(Packet* packet, uint8_t priority, uint32_t confirm_timeout_secs=0);

  void cancelAnnounceConfirm();
  bool isWaitingAnnounceConfirm() const;
};

}