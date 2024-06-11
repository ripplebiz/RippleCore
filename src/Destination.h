#pragma once

#include <RippleCore.h>
#include <Identity.h>

namespace ripple {

/**
 * \brief  Represents an end-point in the mesh, identified by a truncated SHA256 hash. (of DEST_HASH_SIZE)
 *      The hash is either from just a 'name' (C-string), and these can be thought of as 'broadcast' addresses,
 *      or can be the hash of name + Identity.public_key
*/
class Destination {
public:
  uint8_t hash[DEST_HASH_SIZE];

  Destination(const Identity& identity, const char* name);
  Destination(const char* name);
  Destination(const uint8_t desthash[]) { memcpy(hash, desthash, DEST_HASH_SIZE); }
  Destination();

  bool matches(const uint8_t* other_hash);
};

}
