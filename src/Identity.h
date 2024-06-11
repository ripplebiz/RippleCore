#pragma once

#include <Utils.h>
#include <Stream.h>

namespace ripple {

/**
 * \brief  An identity in the mesh, with given Ed25519 public key, ie. a party whose signatures can be VERIFIED.
*/
class Identity {
public:
  uint8_t pub_key[PUB_KEY_SIZE];

  Identity();
  Identity(const char* pub_hex);
  Identity(const uint8_t* _pub) { memcpy(pub_key, _pub, PUB_KEY_SIZE); }

  /**
   * \brief  Performs Ed25519 signature verification.
   * \param sig IN - must be SIGNATURE_SIZE buffer.
   * \param message IN - the original message which was signed.
   * \param msg_len IN - the length in bytes of message.
   * \returns true, if signature is valid.
  */
  bool verify(const uint8_t* sig, const uint8_t* message, int msg_len) const;

  bool matches(const Identity& other) const { return memcmp(pub_key, other.pub_key, PUB_KEY_SIZE) == 0; }
  bool matches(const uint8_t* other_pubkey) const { return memcmp(pub_key, other_pubkey, PUB_KEY_SIZE) == 0; }

  bool readFrom(Stream& s);
  bool writeTo(Stream& s) const;
  void printTo(Stream& s) const;
};

/**
 * \brief  An Identity generated on THIS device, ie. with public/private Ed25519 key pair being on this device.
*/
class LocalIdentity : public Identity {
  uint8_t prv_key[PRV_KEY_SIZE];
public:
  LocalIdentity();
  LocalIdentity(const char* prv_hex, const char* pub_hex);
  LocalIdentity(RNG* rng);   // create new random

  /**
   * \brief  Ed25519 digital signature.
   * \param sig OUT - must be SIGNATURE_SIZE buffer.
   * \param message IN - the raw message bytes to sign.
   * \param msg_len IN - the length in bytes of message.
  */
  void sign(uint8_t* sig, const uint8_t* message, int msg_len) const;

  /**
   * \brief  the ECDH key exhange, with Ed25519 public key transposed to Ex25519.
   * \param  secret OUT - the 'shared secret'
   * \param  other IN - the second party in the exchange.
  */
  void calcSharedSecret(uint8_t* secret, const Identity& other);

  bool readFrom(Stream& s);
  bool writeTo(Stream& s) const;
  void printTo(Stream& s) const;
};

}

