#pragma once

#include <RippleCore.h>
#include <Destination.h>

namespace ripple {

// Packet::header values
#define PH_TYPE_MASK         0x07
#define PH_TYPE_DATA         0x00
#define PH_TYPE_ANNOUNCE     0x01
#define PH_TYPE_REPLY        0x02
#define PH_TYPE_REPLY_SIGNED 0x03

#define PH_TYPE_KEEP_PATH    0x08   // combined with PH_TYPE_DATA (wants reply)
#define PH_HAS_TRANS_ADDRESS 0x80

/**
 * \brief  The fundamental transmission unit.
*/
class Packet {
public:
  Packet();

  uint8_t header;
  uint8_t hops;
  uint8_t destination_hash[DEST_HASH_SIZE];
  uint8_t transport_id[DEST_HASH_SIZE];
  uint8_t payload[MAX_PACKET_PAYLOAD];
  uint16_t payload_len;

  void setDestinationHash(Destination* dest);
  bool isDestination(const uint8_t* hash);
  void calculatePacketHash(uint8_t* dest_hash);

  // general helpers
  uint8_t getPacketType() const { return header & PH_TYPE_MASK; }

  // helper method for Announce packets
  uint32_t getAnnounceTimestamp() const;
  const uint8_t* getAnnouncePubKey() const;
};

}
