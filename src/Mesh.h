#pragma once

#include <Dispatcher.h>

namespace ripple {

/**
 * An abstraction of the device's Realtime Clock.
*/
class RTCClock {
public:
  /**
   * \returns  the current time. in UNIX epoch seconds.
  */
  virtual uint32_t getCurrentTime() = 0;

  /**
   * \param time  current time in UNIX epoch seconds.
  */
  virtual void setCurrentTime(uint32_t time) = 0;
};

/**
 * \brief  The next layer in the basic Dispatcher task, Mesh recognises the particular Packet TYPES (eg. Announce),
 *     and provides virtual methods for sub-classes on handling incoming, and also preparing outbound Packets.
*/
class Mesh : public Dispatcher {
protected:
  RTCClock* _rtc;
  RNG* _rng;

  DispatcherAction onRecvPacket(Packet* pkt) override;

  /**
   * \brief  This acts as a kind of 'filter' for the actual Application, and should only return true for incoming Announces which
   *        are new, ie. not one seen recently, AND which this application is interested in. Many announces could come, but not 
   *        all need to be dealt with.
   * \returns  true, if this incoming Announce should continue to be processed. (onAnnounceRecv() then follows)
  */
  virtual bool isAnnounceNew(Packet* packet, const Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) = 0;
  
  /**
   * \brief  A new incoming Announce has been received.
  */
  virtual DispatcherAction onAnnounceRecv(Packet* packet, const Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) = 0;

  /**
   * \brief  This also acts as a kind of filter for the Application, and should only return true for incoming Datagrams which
   *        are new, ie. not seen recently, AND which are addressed to this node/application (onDatagramRecv() then follows). Otherwise ignore.
   * \param  packet_hash  For convenience, and performance, pre-calculated hash of this Packet's contents.
  */
  virtual bool isDatagramNew(Packet* packet, const uint8_t* packet_hash) = 0;

  /**
   * \brief  A new incoming Datagram has been received.
   * \param  packet_hash  For convenience, and performance, pre-calculated hash of this Packet's contents.
  */
  virtual DispatcherAction onDatagramRecv(Packet* packet, const uint8_t* packet_hash) = 0;

  /**
   * \brief  A 'plain' Reply packet has been received.
  */
  virtual DispatcherAction onReplyRecv(Packet* packet) = 0;

  /**
   * \brief  Does initial vetting of a signed reply, making sure it is valid and belongs to a known outgoing Datagram.
  */
  virtual bool isReplySignedNew(Packet* packet) = 0;
  
  /**
   * \brief  A new and VALID incoming signed reply has been received.
   * \param  packet   The dest_hash will be the packet-hash of original Datagram.
  */
  virtual DispatcherAction onReplySignedRecv(Packet* packet, const uint8_t* reply, size_t reply_len) = 0;

  /**
   * \brief  Called to prepare a locally-generated, ie. outbound, Announce packet for transmission.
  */
  virtual void prepareLocalAnnounce(Packet* packet, const uint8_t* rand_blob) = 0;

  /**
   * \brief  Called to prepare a locally-generated, ie outbound, Datagram packet for transmission. Most vitally, it
   *       needs to know the next-hop for the given packet->Destination.
  */
  virtual void prepareLocalDatagram(Packet* packet) = 0;

  /**
   * \brief  Called to prepare a locally-generated, ie outbound, Reply packet for transmission.
  */
  virtual void prepareLocalReply(Packet* packet) = 0;

  Mesh(Radio& radio, MillisecondClock& ms, RNG& rng, RTCClock& rtc, PacketManager& mgr)
    : Dispatcher(radio, ms, mgr), _rng(&rng), _rtc(&rtc)
  {
  }

public:
  void begin();
  void loop();

  RNG* getRNG() const { return _rng; }
  RTCClock* getRTCClock() const { return _rtc; }

  Packet* createAnnounce(const char* dest_name, const LocalIdentity& id, const uint8_t* app_data=NULL, size_t app_data_len=0);
  Packet* createDatagram(const Destination* destination, const uint8_t* payload, int len, bool wantReply=false);
  Packet* createReply(const uint8_t* packet_hash, const uint8_t *reply, size_t reply_len);
  Packet* createReplySigned(const uint8_t* packet_hash, const LocalIdentity& id, const uint8_t *reply, size_t reply_len);
  bool verifyReplySigned(const Packet* packet, const Identity& id);
};

}
