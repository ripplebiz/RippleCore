#include "MeshTransportNone.h"
#include "Dispatcher.h"

namespace ripple {

#define  PATH_REQUEST_DELAY_MAX  5000   // in milliseconds
#define  PATH_REQUEST_DELAY_MIN  2000

void MeshTransportNone::onPacketSent(ripple::Packet* packet)  {
  if (packet == curr_self_announce && packet->hops == 0) {  // is our self-announce?
    // listen for retransmits of this announce with hops > 0 as confirmations.
    //    If nothing heard, then re-try SAME announce (ie. with same rand_blob) after 60 seconds.
    // NOTE: we are now HOLDING this Packet instance, ie. not RELEASING to pool yet!!
    confirmation_timeout = futureMillis(confirm_secs * 1000);
    confirm_secs = (confirm_secs * 4) / 3;   // exponential back-off
  } else {
    Mesh::onPacketSent(packet);  // otherwise, whatever std on sent behaviour (eg. release the packet)
  }
}

bool MeshTransportNone::isAnnounceNew(Packet* packet, const Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) {
  // check if this incoming Announce is a 'confirmation'
  if (curr_self_announce && memcmp(curr_self_announce->destination_hash, packet->destination_hash, DEST_HASH_SIZE) == 0) {  // yes it's a retransmit of ours
    if (packet->hops > 0) {
      // YES, a confirmation!
      cancelAnnounceConfirm();
    }
  }

  // Optimisation:  for "path.request"
  // if incoming announce matches one WE have queued for transmit AND their hops <= hops in our copy, then cancel the transmit
  for (int i = 0; i < _mgr->getOutboundCount(); i++) {
    Packet* outbound = _mgr->getOutboundByIdx(i);
    if (outbound->getPacketType() == PH_TYPE_ANNOUNCE) {
      const uint8_t* pub_key = outbound->getAnnouncePubKey();
      if (memcmp(packet->destination_hash, outbound->destination_hash, DEST_HASH_SIZE) == 0 && packet->hops <= outbound->hops) {
        _mgr->removeOutboundByIdx(i);
        releasePacket(outbound);   // put back into pool
        break;
      }
    }
  }

  return true;
}

void MeshTransportNone::onBeforeAnnounceRetransmit(Packet* packet) {
  // no-op
}

DispatcherAction MeshTransportNone::onAnnounceRecv(Packet* packet, const Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) {
  _tables->updateNextHop(packet->destination_hash, packet);

  return ACTION_RELEASE;
}

bool MeshTransportNone::isDatagramNew(Packet* packet, const uint8_t* packet_hash) {
  return _tables->getSeenPacketHash(packet_hash) == 0;
}

DispatcherAction MeshTransportNone::onDatagramRecv(Packet* packet, const uint8_t* packet_hash) {
  Destination test("path.request");
  if (test.matches(packet->destination_hash)) {  // is a path request (local broadcast)
    // NOTE: don't record these in 'seen' table:   _tables->setSeenPacketHash(packet_hash, 1);

    // required dest_hash is in payload

    if (curr_self_announce && memcmp(curr_self_announce->destination_hash, packet->payload, DEST_HASH_SIZE) == 0) {
      // this node is waiting for 'confirmation' of its Announce, but has just received a Path Request from peer.
      //  so cancel retries on Announce broadcasts
      cancelAnnounceConfirm();
    }

    if (_tables->getOrigAnnounce(packet->payload, packet)) {  // NOTE: re-use the received Packet instance for replaying the Announce
      // Multiple nodes around sender could all try to reply at once, so apply a random delay
      uint32_t rand_delay = _rng->nextInt(PATH_REQUEST_DELAY_MIN, PATH_REQUEST_DELAY_MAX);

      if (packet->hops > 0) {  // is NOT a locally generated Announce
        // if this is MeshTransportFull,  need to rew-rite the transport_id to be SELF!! ie. WE are the next-hop!
        onBeforeAnnounceRetransmit(packet);
      }

      //   See optimisation in isAnnounceNew():
      //        while waiting random interval, if incoming packet matches this announce.pub_key AND their hops <= hops in 
      //        our copy, then cancel this send

      // retransmit the original announce (Note: nodes that have already seen will just igore)
      return ACTION_RETRANSMIT_DELAYED(2, rand_delay);
    }
  }
  return ACTION_RELEASE;
}

DispatcherAction MeshTransportNone::onReplyRecv(Packet* packet) {
  return ACTION_RELEASE;
}

bool MeshTransportNone::isReplySignedNew(Packet* packet) {
  // lookup original dest_hash by packet_hash
  uint8_t orig_dest_hash[DEST_HASH_SIZE];
  if (_tables->getPacketHashDest(packet->destination_hash, orig_dest_hash)) {
    Packet orig_announce;
    if (_tables->getOrigAnnounce(orig_dest_hash, &orig_announce)) {
      Identity id;
      memcpy(id.pub_key, orig_announce.getAnnouncePubKey(), PUB_KEY_SIZE);
      // verify signature
      if (verifyReplySigned(packet, id)) {  // this reply SHOULD be signed by the announcer of original destination
        _tables->clearPacketHashDest(packet->destination_hash);  // prevent duplicates, if we receive this reply again
        return true;
      } else {
        RIPPLE_DEBUG_PRINTLN("MeshTransportNone::isReplySignedNew(): forged signature detected for reply packet!");
      }
    } else {
      // path not found... may have expired
      RIPPLE_DEBUG_PRINTLN("MeshTransportNone::isReplySignedNew(): path to destination no longer known");
    }
  } else {
    // unknown packet_hash, or packet_hash has already been received (ie. cleared from _tables)
    RIPPLE_DEBUG_PRINTLN("MeshTransportNone::isReplySignedNew(): unknown packet hash");
  }
  return false;
}

DispatcherAction MeshTransportNone::onReplySignedRecv(Packet* packet, const uint8_t* reply, size_t reply_len) {
  return ACTION_RELEASE;
}

void MeshTransportNone::sendAnnounce(Packet* packet, uint8_t priority, uint32_t confirm_timeout_secs) {
  cancelAnnounceConfirm();   // in case we are interupting a current confirmation await

  if (confirm_timeout_secs > 0) {
    curr_self_announce = packet; // NOTE: we now OWN this reference to Packet .. must release manually!
    confirm_secs = confirm_timeout_secs;
    retry_priority = priority;
  }
  sendPacket(packet, priority);  // now wait for onPacketSent()
}

void MeshTransportNone::cancelAnnounceConfirm() {
  if (curr_self_announce) {
    releasePacket(curr_self_announce);   // can now stop HOLDING this Packet instance
    curr_self_announce = NULL;
  }
  confirmation_timeout = 0;  // cancel our timout timer
}

bool MeshTransportNone::isWaitingAnnounceConfirm() const {
    return curr_self_announce != NULL;
}

bool MeshTransportNone::hasPathTo(const uint8_t* dest_hash, Packet* orig_announce, uint32_t* recv_timestamp) const {
  if (orig_announce != NULL) {
    uint32_t timestamp = _tables->getOrigAnnounce(dest_hash, orig_announce);
    if (recv_timestamp) *recv_timestamp = timestamp;
    return timestamp != 0;
  }
  return _tables->hasNextHop(dest_hash);
}

bool MeshTransportNone::requestPathTo(const uint8_t* dest_hash) {
  Destination dest("path.request");  // NOTE: not tied to any ID, is a general broadcast to immediate nodes
#if false
  uint8_t payload[DEST_HASH_SIZE + 4];
  memcpy(payload, dest_hash, DEST_HASH_SIZE);  // send dest_hash requested in payload
  _rng->random(&payload[DEST_HASH_SIZE], 4);   // append a random blob of 4 bytes, so packet_hash will be unique
  Packet* pkt = createDatagram(&dest, payload, sizeof(payload));
#else
  // send dest_hash requested in payload
  Packet* pkt = createDatagram(&dest, dest_hash, DEST_HASH_SIZE);
#endif
  
  if (pkt) {
    pkt->header &= ~PH_HAS_TRANS_ADDRESS;  // strip out any transport_id (shouldn't happen, but you never know)
    sendPacket(pkt, 0);
    return true;
  }
  return false;
}

void MeshTransportNone::prepareLocalAnnounce(Packet* packet, const uint8_t* rand_blob) {
  _tables->setHasForwarded(rand_blob);
  _tables->updateNextHop(packet->destination_hash, packet);  // store in our destinations table, in case we get "path.request"
}

void MeshTransportNone::prepareLocalDatagram(Packet* packet) {
  uint8_t packet_hash[DEST_HASH_SIZE];
  packet->calculatePacketHash(packet_hash);

  uint8_t next_hop[DEST_HASH_SIZE];
  if (_tables->getNextHop(packet->destination_hash, next_hop)) {
    if (memcmp(packet->destination_hash, next_hop, DEST_HASH_SIZE) != 0) {
      memcpy(packet->transport_id, next_hop, DEST_HASH_SIZE);  // address this datagram to this transport_id
      packet->header |= PH_HAS_TRANS_ADDRESS;
    }
    if (packet->header & PH_TYPE_KEEP_PATH) {   // sender is expecting reply
      // remember destination_hash for this packet_hash (to lookup original Announce, in case we need to verify signed replies)
      _tables->setPacketHashDest(packet_hash, packet->destination_hash);
      // keep special code (2), in case we get plain replies
      _tables->setSeenPacketHash(packet_hash, 2);
    } else {
      _tables->setSeenPacketHash(packet_hash, 1);
    }
  } else {
    // we don't have a path/next-hop to destination...  path expired?
    _tables->setSeenPacketHash(packet_hash, 1);
  }
}

void MeshTransportNone::prepareLocalReply(Packet* packet) {
  // TODO: set packet->transport_id to something identifying this node
}

void MeshTransportNone::begin() {
  Mesh::begin();
}

void MeshTransportNone::loop() {
  Mesh::loop();

  if (confirmation_timeout && millisHasNowPassed(confirmation_timeout)) {
    confirmation_timeout = 0;  // one-shot timer!

    // have not heard any 'confirmations' of our announce
    if (curr_self_announce) {
      sendPacket(curr_self_announce, retry_priority);  // re-try .. now wait for onPacketSent()
    } else {
      // was cancelled
    }
  }
}

}