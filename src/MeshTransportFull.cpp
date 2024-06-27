#include "MeshTransportFull.h"
#include "Dispatcher.h"

namespace ripple {

#define  ANNOUNCE_DELAY_MAX  5000   // in milliseconds
#define  ANNOUNCE_DELAY_MIN  2000

DispatcherAction MeshTransportFull::onAnnounceRecv(Packet* packet, const Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) {
  if (_tables->updateNextHop(packet->destination_hash, packet) && !_tables->hasForwarded(rand_blob)) {
    _tables->setHasForwarded(rand_blob);
    if (packet->hops >= max_hops_supported) return ACTION_RELEASE;

    onBeforeAnnounceRetransmit(packet);  // need to re-write 'transport_id'

    // Multiple nodes around sender could all try to rebroadcast at once, so apply a random delay
    uint32_t rand_delay = _rng->nextInt(ANNOUNCE_DELAY_MIN, ANNOUNCE_DELAY_MAX);

    // schedule this Announce propagation depending on packet->hops, 
    //    closer destinations propagate quicker, further propagate slower  (if available 'airtime' is contrained)

    return ACTION_RETRANSMIT_DELAYED(2 + packet->hops, rand_delay);  // keep re-broadcasting announce outwards
  }
  return ACTION_RELEASE;   // Announce is from a worse path, or same as currently held in tables - so don't retransmit
}

void MeshTransportFull::onBeforeAnnounceRetransmit(Packet* packet) {
  Destination dest(self_id, "trans.data");
  memcpy(packet->transport_id, dest.hash, DEST_HASH_SIZE);  // overwrite transport_id with our ID/destination
  packet->header |= PH_HAS_TRANS_ADDRESS;
}

DispatcherAction MeshTransportFull::onDatagramRecv(Packet* packet, const uint8_t* packet_hash) {
  Destination dest(self_id, "trans.data");
  if (dest.matches(packet->destination_hash)) {  // this node IS the destination
    // TODO: 
    _tables->setSeenPacketHash(packet_hash, 1);
  } else if ((packet->header & PH_HAS_TRANS_ADDRESS) != 0 && dest.matches(packet->transport_id)) {
    // we are being addressed in transport_id, forward to next hop
    if (_tables->getNextHop(packet->destination_hash, packet->transport_id)) {
      if (packet->header & PH_TYPE_KEEP_PATH) {   // sender is expecting reply
        // remember destination_hash for this packet_hash (to lookup original Announce, in case we need to verify signed replies)
        _tables->setPacketHashDest(packet_hash, packet->destination_hash);
        // keep special code (2), in case we get plain replies
        _tables->setSeenPacketHash(packet_hash, 2);
      } else {
        _tables->setSeenPacketHash(packet_hash, 1);
      }
      // TODO: do per-destination rate limiting
      return ACTION_RETRANSMIT(0);
    } else {
      // we don't have a path/next-hop to destination...  path expired?
      _tables->setSeenPacketHash(packet_hash, 1);
    }
  } else {
    // otherwise, this packet is not addressed to this node, ignore
  }

  return MeshTransportNone::onDatagramRecv(packet, packet_hash);
}

DispatcherAction MeshTransportFull::onReplyRecv(Packet* packet) {
  int code = _tables->getSeenPacketHash(packet->destination_hash); // destination_hash is packet_hash of original datagram
  if (code == 2) {
    _tables->setSeenPacketHash(packet->destination_hash, 1);  // only allow this ONCE!
    return ACTION_RETRANSMIT(3);
  }
  return MeshTransportNone::onReplyRecv(packet);
}

DispatcherAction MeshTransportFull::onReplySignedRecv(Packet* packet, const uint8_t* reply, size_t reply_len) {
  return ACTION_RETRANSMIT(1);
}

void MeshTransportFull::prepareLocalReply(Packet* packet) {
}

void MeshTransportFull::begin() {
  MeshTransportNone::begin();

  // TODO: init tables
}

void MeshTransportFull::loop() {
  MeshTransportNone::loop();

  // TODO: scan for stale paths, delete the entries from table
}

}