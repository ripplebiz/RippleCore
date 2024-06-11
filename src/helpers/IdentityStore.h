#pragma once

#include <FS.h>
#include <Identity.h>

class IdentityStore {
  fs::FS* _fs;
  const char* _dir;
public:
  IdentityStore(fs::FS& fs, const char* dir): _fs(&fs), _dir(dir) { }

  void begin() { _fs->mkdir(_dir); }
  bool load(const char *name, ripple::LocalIdentity& id);
  bool save(const char *name, const ripple::LocalIdentity& id);
};
