#pragma once

#include <Mesh.h>

namespace ripple {

#define LATE_ANNOUNCE_SECS   (1*60)  // one minute

struct DestPathEntry {
  uint8_t  hops;
  uint32_t create_timestamp;
  uint32_t last_timestamp;
  ripple::Packet orig_announce;
};

/**
 * An abstraction of the data tables needed to be maintained, for the routing engine.
*/
class MeshTables {
protected:
  RTCClock* _rtc;

  MeshTables(RTCClock& rtc): _rtc(&rtc) { }

  /**
   * \brief  lookup destination hash
   * \param hash  IN - the destination hash to search for
   * \param handle  OUT - the corresponding table 'handle' for the entry
   * \param dest  OUT - if not NULL, the current entry found in table
   * \returns  true if found
  */
  virtual bool lookupDest(const uint8_t* hash, uint32_t& handle, DestPathEntry* dest=NULL) const = 0;

  /**
   * \brief  find a free table entry, evicting the oldest (by last_timestamp) if necessary.
   * \returns  the table 'handle' for the free entry
  */
  virtual uint32_t findFreeDest() = 0;

  /**
   * \brief  save the given entry in table.
   * \param handle  IN - the handle (as returned by lookup/find methods)
   * \param hash  IN - the destination hash (key)
   * \param dest  IN - the updated/new entry to save
   * \returns  true if successful
  */
  virtual bool saveDest(uint32_t handle, const uint8_t* hash, const DestPathEntry& dest) = 0;

public:
  virtual bool hasSeen(const uint8_t* rand_blob) const = 0;
  virtual void setHasSeen(const uint8_t* rand_blob) = 0;

  virtual int getSeenPacketHash(const uint8_t* hash) const = 0;
  virtual void setSeenPacketHash(const uint8_t* hash, int code) = 0;

  virtual bool getPacketHashDest(const uint8_t* packet_hash, uint8_t* destination_hash) = 0;
  virtual void setPacketHashDest(const uint8_t* packet_hash, const uint8_t* destination_hash) = 0;
  virtual void clearPacketHashDest(const uint8_t* packet_hash) = 0;

  /**
   * \returns true if the dest_hash is known to this node.
  */
  bool hasNextHop(const uint8_t* dest_hash);

  /**
   * \brief updates the next-hop table, for the given dest_hash, IF the incoming Announce is NEWER, or from a BETTER path.
   * \returns true if the table was updated.
  */
  bool updateNextHop(const uint8_t* dest_hash, const Packet* announce_pkt);  // returns true if tables now changed

  /**
   * Lookup the next-hop for the given dest_hash. Also updates timestamp to indicate this path has bee Recently Used. (for eviction algorithm)
   * \param  dest_hash IN - the Destination hash to lookup.
   * \param  next_hop OUT - the tranport_id of next-hop.
   * \returns  true, if next hop was found
  */
  bool getNextHop(const uint8_t* dest_hash, uint8_t* next_hop);

  /**
   * \brief   Lookup the original Announce packet which informed the next-hop to given dest_hash. (ie. the best path)
   * \param dest_hash IN - the Destintion
   * \param announce_pkt OUT - A copy of the original Announce we received for given dest_hash.
   * \returns 0 if not found, otherwise timestamp when we received announce (by local RTC clock)
  */
  uint32_t getOrigAnnounce(const uint8_t* dest_hash, Packet* announce_pkt);

  /**
   * \brief  For diagnostics.
   * \param max_age_secs  The maximum time, in seconds, since a Destination was 'active', ie. had some traffic.
   * \returns  The number of 'active' Destintions in our next-hop table.
  */
  virtual uint32_t getActiveNextHopCount(uint32_t max_age_secs) const = 0;
};

}