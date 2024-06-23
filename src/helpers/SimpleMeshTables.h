#pragma once

#include <MeshTransportNone.h>

#define MAX_RAND_BLOBS     32
#define MAX_PACKET_HASHES  64
#define MAX_DEST_HASHES    64
#define MAX_MAPPING_HASHES 64

struct HashMappingEntry {
  uint8_t  packet_hash[DEST_HASH_SIZE];
  uint8_t  orig_dest[DEST_HASH_SIZE];
};

class SimpleMeshTables : public ripple::MeshTables {
  uint8_t _seen_blobs[MAX_RAND_BLOBS*8];
  int _next_seen_idx;

  uint8_t _seen_hashes[MAX_PACKET_HASHES*DEST_HASH_SIZE];
  uint8_t _hash_code[MAX_PACKET_HASHES];
  int _next_hash_idx;

  HashMappingEntry _hash_mappings[MAX_MAPPING_HASHES];
  int _next_mapping_idx;

  uint8_t _dest_hashes[MAX_DEST_HASHES*DEST_HASH_SIZE];
  ripple::DestPathEntry _dest_entries[MAX_DEST_HASHES];

  int lookupHashIndex(const uint8_t* hash) const {
    const uint8_t* sp = _seen_hashes;
    for (int i = 0; i < MAX_PACKET_HASHES; i++, sp += DEST_HASH_SIZE) {
      if (memcmp(hash, sp, DEST_HASH_SIZE) == 0) return i;
    }
    return -1;
  }

  int lookupMappingIndex(const uint8_t* packet_hash) const {
    for (int i = 0; i < MAX_MAPPING_HASHES; i++) {
      if (memcmp(packet_hash, _hash_mappings[i].packet_hash, DEST_HASH_SIZE) == 0) return i;
    }
    return -1;
  }

protected:
  bool lookupDest(const uint8_t* hash, uint32_t& handle, ripple::DestPathEntry* dest) const override {
    const uint8_t* sp = _dest_hashes;
    for (int i = 0; i < MAX_DEST_HASHES; i++, sp += DEST_HASH_SIZE) {
      if (_dest_entries[i].last_timestamp != 0 && memcmp(hash, sp, DEST_HASH_SIZE) == 0) {
        handle = i;
        if (dest) *dest = _dest_entries[i];
        return true;
      }
    }
    return false;
  }

  uint32_t findFreeDest() override {
    int min_i = 0;
    uint32_t min_time = 0xFFFFFFFF;
    for (int i = 0; i < MAX_DEST_HASHES; i++) {
      if (_dest_entries[i].last_timestamp < min_time) {
        min_i = i;
        min_time = _dest_entries[i].last_timestamp;
      }
    }
    return min_i;
  }

  bool saveDest(uint32_t handle, const uint8_t* hash, const ripple::DestPathEntry& dest) override {
    memcpy(&_dest_hashes[handle*DEST_HASH_SIZE], hash, DEST_HASH_SIZE);
    _dest_entries[handle] = dest;
    return true;
  }

public:
  SimpleMeshTables(ripple::RTCClock& rtc): ripple::MeshTables(rtc) { 
    memset(_seen_blobs, 0, sizeof(_seen_blobs));
    _next_seen_idx = 0;

    memset(_seen_hashes, 0, sizeof(_seen_hashes));
    memset(_hash_code, 0, sizeof(_hash_code));
    _next_hash_idx = 0;

    memset(_hash_mappings, 0, sizeof(_hash_mappings));
    _next_mapping_idx = 0;

    memset(_dest_entries, 0, sizeof(_dest_entries));  // set all last_timestamp fields to zero
  }

  void restoreFrom(File f) {
    f.read(_seen_blobs, sizeof(_seen_blobs));
    f.read((uint8_t *) &_next_seen_idx, sizeof(_next_seen_idx));

    f.read(_seen_hashes, sizeof(_seen_hashes));
    f.read(_hash_code, sizeof(_hash_code));
    f.read((uint8_t *) &_next_hash_idx, sizeof(_next_hash_idx));

    f.read((uint8_t *) _hash_mappings, sizeof(_hash_mappings));
    f.read((uint8_t *) &_next_mapping_idx, sizeof(_next_mapping_idx));

    f.read(_dest_hashes, sizeof(_dest_hashes));
    f.read((uint8_t *) _dest_entries, sizeof(_dest_entries));
  }
  void saveTo(File f) {
    f.write(_seen_blobs, sizeof(_seen_blobs));
    f.write((const uint8_t *) &_next_seen_idx, sizeof(_next_seen_idx));

    f.write(_seen_hashes, sizeof(_seen_hashes));
    f.write(_hash_code, sizeof(_hash_code));
    f.write((const uint8_t *) &_next_hash_idx, sizeof(_next_hash_idx));

    f.write((const uint8_t *) _hash_mappings, sizeof(_hash_mappings));
    f.write((const uint8_t *) &_next_mapping_idx, sizeof(_next_mapping_idx));

    f.write(_dest_hashes, sizeof(_dest_hashes));
    f.write((const uint8_t *) _dest_entries, sizeof(_dest_entries));
  }

  bool hasSeen(const uint8_t* rand_blob) const override {
    const uint8_t* sp = _seen_blobs;
    while (sp - _seen_blobs < sizeof(_seen_blobs)) {
      if (memcmp(rand_blob, sp, 8) == 0) return true;
      sp += 8;
    }
    return false;
  }

  void setHasSeen(const uint8_t* rand_blob) override {
    memcpy(&_seen_blobs[_next_seen_idx*8], rand_blob, 8);
    _next_seen_idx = (_next_seen_idx + 1) % MAX_RAND_BLOBS;  // cyclic table
  }

  int getSeenPacketHash(const uint8_t* hash) const override {
    int i = lookupHashIndex(hash);
    return i >= 0 ? _hash_code[i] : 0;
  }

  void setSeenPacketHash(const uint8_t* hash, int code) override {
    int i = lookupHashIndex(hash);
    if (i >= 0) {
      _hash_code[i] = (uint8_t) code;
    } else {
      memcpy(&_seen_hashes[_next_hash_idx*DEST_HASH_SIZE], hash, DEST_HASH_SIZE);
      _hash_code[_next_hash_idx] = (uint8_t) code;

      _next_hash_idx = (_next_hash_idx + 1) % MAX_PACKET_HASHES;  // cyclic table
    }
  }

  bool getPacketHashDest(const uint8_t* packet_hash, uint8_t* destination_hash) override {
    int i = lookupMappingIndex(packet_hash);
    if (i >= 0) {
      memcpy(destination_hash, _hash_mappings[i].orig_dest, DEST_HASH_SIZE);
      return true;
    }
    return false;
  }
  void setPacketHashDest(const uint8_t* packet_hash, const uint8_t* destination_hash) override {
    int i = lookupMappingIndex(packet_hash);
    if (i < 0) {   // not found, append to table (cyclic)
      i = _next_mapping_idx;
      _next_mapping_idx = (_next_mapping_idx + 1) % MAX_MAPPING_HASHES;

      memcpy(_hash_mappings[i].packet_hash, packet_hash, DEST_HASH_SIZE);  // set the key
    }
    memcpy(_hash_mappings[i].orig_dest, destination_hash, DEST_HASH_SIZE);
  }
  void clearPacketHashDest(const uint8_t* packet_hash) override {
    int i = lookupMappingIndex(packet_hash);
    if (i >= 0) {
      memset(_hash_mappings[i].packet_hash, 0, DEST_HASH_SIZE);  // clear the key
    }
  }

  uint32_t getActiveNextHopCount(uint32_t max_age_secs) const override {
    uint32_t count = 0;
    uint32_t min_time = _rtc->getCurrentTime() - max_age_secs;
    for (int i = 0; i < MAX_DEST_HASHES; i++) {
      if (_dest_entries[i].last_timestamp > min_time) count++;
    }
    return count;
  }
};
