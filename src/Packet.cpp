#include "Packet.h"
#include <string.h>
#include <SHA256.h>

namespace ripple {

Packet::Packet() {
  header = 0;
  hops = 0;
  payload_len = 0;
}

void Packet::setDestinationHash(Destination* dest) {
  memcpy(destination_hash, dest->hash, DEST_HASH_SIZE);
}

bool Packet::isDestination(const uint8_t* hash) {
  return memcmp(destination_hash, hash, DEST_HASH_SIZE) == 0;
}

void Packet::calculatePacketHash(uint8_t* hash) {
  SHA256 sha;
  uint8_t hdr = header & PH_TYPE_MASK;
  sha.update(&hdr, 1);
  sha.update(destination_hash, DEST_HASH_SIZE);
  sha.update(payload, payload_len);
  sha.finalize(hash, DEST_HASH_SIZE);
}

uint32_t Packet::getAnnounceTimestamp() const {
  uint32_t timestamp;
  memcpy(&timestamp, &payload[PUB_KEY_SIZE + NAME_HASH_SIZE], 4);
  return timestamp;
}

const uint8_t* Packet::getAnnouncePubKey() const {
  return payload;   // is first field in payload
}

}