#include "Destination.h"
#include "Utils.h"
#include <string.h>

namespace ripple {

Destination::Destination(const Identity& identity, const char* name) {
  Utils::sha256(hash, DEST_HASH_SIZE, (const uint8_t *)name, strlen(name), identity.pub_key, PUB_KEY_SIZE);
}

Destination::Destination(const char* name) {
  Utils::sha256(hash, DEST_HASH_SIZE, (const uint8_t *)name, strlen(name));
}

Destination::Destination() {
  memset(hash, 0, DEST_HASH_SIZE);
}

bool Destination::matches(const uint8_t* other_hash) {
  return memcmp(hash, other_hash, DEST_HASH_SIZE) == 0;
}

}