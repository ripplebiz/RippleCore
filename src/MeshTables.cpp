#include "MeshTables.h"

namespace ripple {

bool MeshTables::hasNextHop(const uint8_t* dest_hash) {
  uint32_t handle;
  return lookupDest(dest_hash, handle) /* && not expired */;
}

bool MeshTables::updateNextHop(const uint8_t* dest_hash, const ripple::Packet* announce_pkt) {
  uint32_t now = _rtc->getCurrentTime();   // this is by OUR clock
  uint32_t newTimestamp = announce_pkt->getAnnounceTimestamp(); // this is by THEIR clock

  uint32_t i;
  DestPathEntry entry;
  if (lookupDest(dest_hash, i, &entry)) {
    uint32_t oldTimestamp = entry.orig_announce.getAnnounceTimestamp();
    if (newTimestamp < oldTimestamp) return false;  // is an OLD anounce (can't go back in time)

    if (newTimestamp == oldTimestamp) {  // same announce, but arriving via different path
      if (now > entry.create_timestamp + LATE_ANNOUNCE_SECS) {
        return false;   // this announce is too late to be considered
      }
      if (announce_pkt->hops >= entry.hops) return false;  // not a better path
    } else {
      // is a new Announce, so trumps whatever is currently in destination table
    }
  } else {
    i = findFreeDest();
  }

  entry.hops = announce_pkt->hops;
  entry.create_timestamp = now;
  entry.last_timestamp = now;
  entry.orig_announce = *announce_pkt;  // keep a copy of announce packet

  saveDest(i, dest_hash, entry);
  return true;   // table now changed
}

bool MeshTables::getNextHop(const uint8_t* dest_hash, uint8_t* next_hop) {
  uint32_t i;
  DestPathEntry entry;

  if (lookupDest(dest_hash, i, &entry) /* && not expired */) {
    const ripple::Packet* announce_pkt = &entry.orig_announce;
    if (announce_pkt->header & PH_HAS_TRANS_ADDRESS) {
      memcpy(next_hop, announce_pkt->transport_id, DEST_HASH_SIZE);  // transport_id is next hop
    } else {
      memcpy(next_hop, announce_pkt->destination_hash, DEST_HASH_SIZE);  // is in immediate vicinity
    }

    entry.last_timestamp = _rtc->getCurrentTime();
    saveDest(i, dest_hash, entry);
    return true;
  }
  return false;  // destination not known
}

uint32_t MeshTables::getOrigAnnounce(const uint8_t* dest_hash, ripple::Packet* announce_pkt) {
  uint32_t i;
  DestPathEntry entry;

  if (lookupDest(dest_hash, i, &entry) /* && not expired */) {
    *announce_pkt = entry.orig_announce;
    return entry.create_timestamp;   // when entry was created (by OUR clock)
  }
  return 0;   // destination not known
}

}