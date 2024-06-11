#include "IdentityStore.h"

bool IdentityStore::load(const char *name, ripple::LocalIdentity& id) {
  bool loaded = false;
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  if (_fs->exists(filename)) {
    File file = _fs->open(filename);
    if (file) {
      loaded = id.readFrom(file);
      file.close();
    }
  }
  return loaded;
}

bool IdentityStore::save(const char *name, const ripple::LocalIdentity& id) {
  char filename[40];
  sprintf(filename, "%s/%s.id", _dir, name);
  File file = _fs->open(filename, "w", true);
  if (file) {
    id.writeTo(file);
    file.close();
    return true;
  }
  return false;
}
