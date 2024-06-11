#include <Arduino.h>   // needed for PlatformIO
#include <MeshTransportNone.h>

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/RadioLibWrappers.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

//#define RUN_AS_ALICE    true

#if RUN_AS_ALICE
  const char* alice_private = "B8830658388B2DDF22C3A508F4386975970CDE1E2A2A495C8F3B5727957A97629255A1392F8BA4C26A023A0DAB78BFC64D261C8E51507496DD39AFE3707E7B42";
#else
  const char *bob_private = "30BAA23CCB825D8020A59C936D0AB7773B07356020360FC77192813640BAD375E43BBF9A9A7537E4B9614610F1F2EF874AAB390BA9B0C2F01006B01FDDFEFF0C";
#endif
  const char *alice_public = "106A5136EC0DD797650AD204C065CF9B66095F6ED772B0822187785D65E11B1F";
  const char *bob_public = "020BCEDAC07D709BD8507EC316EB5A7FF2F0939AF5057353DCE7E4436A1B9681";

#define  ACK_STRATEGY_PLAIN   1
#define  ACK_STRATEGY_SIGNED  2
#define  ACK_STRATEGY   ACK_STRATEGY_SIGNED   // try _PLAIN or _SIGNED

#ifdef HELTEC_LORA_V3
  #include <helpers/HeltecV3Board.h>
  static HeltecV3Board board;
#else
  #error "need to provide a 'board' object"
#endif

/* -------------------------------------------------------------------------------------- */

#define MAX_CONTACTS  1

#define FROM_HASH_LEN  8  // how many bytes to truncate the hash of sender pub_key
#define MAX_TEXT_LEN    (13*CIPHER_BLOCK_SIZE)  // must be LESS than (MAX_PACKET_PAYLOAD - FROM_HASH_LEN - 4 - CIPHER_MAC_SIZE - 1)

struct ContactInfo {
  ripple::Identity id;
  const char* name;
  uint8_t shared_secret[PUB_KEY_SIZE];
};

class MyMesh : public ripple::MeshTransportNone {
public:
  ripple::LocalIdentity self_id;
  ContactInfo contacts[MAX_CONTACTS];
  int num_contacts;
  ripple::Destination* rep_req_dest;

  void addContact(const char* name, const ripple::Identity& id) {
    if (num_contacts < MAX_CONTACTS) {
      contacts[num_contacts].id = id;
      contacts[num_contacts].name = name;
      // only need to calculate the shared_secret once, for better performance
      self_id.calcSharedSecret(contacts[num_contacts].shared_secret, id);
      num_contacts++;
    }
  }

protected:
  bool isChatDest(const uint8_t* destination_hash, const ripple::Identity& id) {
    ripple::Destination test(id, "chat.msg");
    return memcmp(destination_hash, test.hash, DEST_HASH_SIZE) == 0;
  }
  void calcSenderHash(uint8_t* hash, const ripple::Identity& id) {
    ripple::Utils::sha256(hash, FROM_HASH_LEN, id.pub_key, PUB_KEY_SIZE);
  }

  // acts as filter for which Announces this app is interested in
  bool isAnnounceNew(ripple::Packet* packet, const ripple::Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) override {
    if (MeshTransportNone::isAnnounceNew(packet, id, rand_blob, app_data, app_data_len)) {
      if (isChatDest(packet->destination_hash, id)) {
        // see if the id is in our contacts
        for (int i = 0; i < num_contacts; i++) {
          if (id.matches(contacts[i].id)) return true;
        }
      }
    }
    return false;
  }

  ripple::DispatcherAction onAnnounceRecv(ripple::Packet* packet, const ripple::Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) override {
    Serial.print("Valid ANNOUNCE -> ");
    ripple::Utils::printHex(Serial, id.pub_key, PUB_KEY_SIZE);
    Serial.println();

    return MeshTransportNone::onAnnounceRecv(packet, id, rand_blob, app_data, app_data_len);
  }

  ripple::DispatcherAction onDatagramRecv(ripple::Packet* packet, const uint8_t* packet_hash) override {
    if (isChatDest(packet->destination_hash, self_id) // packet addressed to us AND is chat.msg dest?
     && packet->payload_len > FROM_HASH_LEN && packet->payload_len <= FROM_HASH_LEN + MAX_TEXT_LEN  // sanity check on pkt len
    ) { 
      // check which contact this came from, by FROM_HASH_LEN bytes prefix
      for (int i = 0; i < num_contacts; i++) {
        uint8_t test[FROM_HASH_LEN];
        calcSenderHash(test, contacts[i].id);
        if (memcmp(packet->payload, test, FROM_HASH_LEN) == 0) {  // a match?
          int ofs = FROM_HASH_LEN;  // have already processed from_hash above

          char text[4+MAX_TEXT_LEN+1];
          int len = ripple::Utils::MACThenDecrypt(contacts[i].shared_secret, (uint8_t*) text, &packet->payload[ofs], packet->payload_len - ofs);
          if (len == 0) {
            Serial.println("MSG -> forged message received!");
          } else {
            uint32_t timestamp;
            memcpy(&timestamp, text, 4);  // timestamp (by sender's RTC clock - which could be wrong)

            // len can be > original length, but 'text' will be padded with zeroes
            text[len] = 0; // need to make a C string again, with null terminator

            Serial.print("MSG -> from ");
            Serial.print(contacts[i].name);
            Serial.print(": ");
            Serial.println(&text[4]);

        #if ACK_STRATEGY == ACK_STRATEGY_PLAIN
            uint8_t ack_hash[4];    // calc truncated hash of the message timestamp + text + sender pub_key, to prove to sender that we got it
            ripple::Utils::sha256(ack_hash, 4, (const uint8_t *)text, 4 + strlen(&text[4]), contacts[i].id.pub_key, PUB_KEY_SIZE);

            // just using plain replies (not signed), as the reply data is just a hash, and not sensitive data
            // NOTE: these can be forged, and can be denied transport by bad nodes
            ripple::Packet* ack = createReply(packet_hash, ack_hash, 4);
        #else
            // simply signing the packet_hash is enough to prove we received message
            // NOTE: these can't be forged, and can't be denied transport by bad nodes
            ripple::Packet* ack = createReplySigned(packet_hash, self_id, NULL, 0);
        #endif
            if (ack) sendPacket(ack, 0);
          }
          break;
        }
      }
    }
    return MeshTransportNone::onDatagramRecv(packet, packet_hash);
  }

#if ACK_STRATEGY == ACK_STRATEGY_PLAIN
  ripple::DispatcherAction onReplyRecv(ripple::Packet* packet) override {
    if (packet->payload_len == 4 && memcmp(packet->payload, expected_ack_hash, 4) == 0) {      // got an ACK from recipient
      Serial.println("Got ACK reply.");
      // NOTE: the same plain reply can be received multiple times!
      memset(expected_ack_hash, 0, 4);  // reset our expected hash, now that we have received ACK
    }
    return MeshTransportNone::onReplyRecv(packet);
  }
#else
  ripple::DispatcherAction onReplySignedRecv(ripple::Packet* packet, const uint8_t* reply, size_t reply_len) override {
    if (memcmp(packet->destination_hash, last_packet_hash, DEST_HASH_SIZE) == 0) {      // got an ACK from recipient
      Serial.println("Got ACK reply.");
    }
    return MeshTransportNone::onReplySignedRecv(packet, reply, reply_len);
  }
#endif

public:
#if ACK_STRATEGY == ACK_STRATEGY_PLAIN
  uint8_t expected_ack_hash[4];
#else
  uint8_t last_packet_hash[DEST_HASH_SIZE];
#endif

  MyMesh(ripple::Radio& radio, ripple::RNG& rng, ripple::RTCClock& rtc)
     : ripple::MeshTransportNone(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), *new SimpleMeshTables(rtc))
  {
    num_contacts = 0;
  }

  void setRepeaterRequest(ripple::Destination* dest) { rep_req_dest = dest; }
  ripple::Destination* getRepeaterRequest() const { return rep_req_dest; }

  ripple::Packet* composeMsgPacket(const ripple::Destination& dest, const ContactInfo& recipient, const char *text) {
    int text_len = strlen(text);
    if (text_len > MAX_TEXT_LEN) return NULL;

    uint8_t payload[MAX_PACKET_PAYLOAD];

    uint8_t temp[4+MAX_TEXT_LEN+1];
    uint32_t timestamp = _rtc->getCurrentTime();
    memcpy(temp, &timestamp, 4);   // mostly an extra blob to help make packet_hash unique
    memcpy(&temp[4], text, text_len);

    int len = 0;
    calcSenderHash(&payload[len], self_id); len += FROM_HASH_LEN;

    len += ripple::Utils::encryptThenMAC(recipient.shared_secret, &payload[len], temp, 4 + text_len);
    // encrypted_len will be (multiple of the CIPHER_BLOCK_SIZE) + CIPHER_MAC_SIZE

    ripple::Packet* pkt = createDatagram(&dest, payload, len, true);
  #if ACK_STRATEGY == ACK_STRATEGY_PLAIN
    // calc expected ACK hash reply
    ripple::Utils::sha256(expected_ack_hash, 4, (const uint8_t *) temp, 4 + text_len, self_id.pub_key, PUB_KEY_SIZE);
  #else
    if (pkt) { pkt->calculate_hash(last_packet_hash); }
  #endif
    return pkt;
  }

  void sendSelfAnnounce() {
    ripple::Destination dest(self_id, "chat.msg");
    ripple::Packet* announce = createAnnounce(&dest, self_id);
    if (announce) {
      sendPacket(announce, 0);
      Serial.println("   (announce sent).");
    } else {
      Serial.println("   ERROR: unable to create packet.");
    }
  }
};

SPIClass spi;
StdRNG fast_rng;
SX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
MyMesh mesh(*new RadioLibWrapper(radio, board), fast_rng, *new VolatileRTCClock());

void halt() {
  while (1) ;
}

static char command[MAX_TEXT_LEN+1];

void setup() {
  Serial.begin(115200);

  board.begin();
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  int status = radio.begin(915.0, 250, 9, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 22);
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    halt();
  }

  fast_rng.begin(radio.random(0x7FFFFFFF));

#if RUN_AS_ALICE
  Serial.println("   --- user: Alice ---");
  mesh.self_id = ripple::LocalIdentity(alice_private, alice_public);
  mesh.addContact("Bob", ripple::Identity(bob_public));
#else
  Serial.println("   --- user: Bob ---");
  mesh.self_id = ripple::LocalIdentity(bob_private, bob_public);
  mesh.addContact("Alice", ripple::Identity(alice_public));
#endif
  Serial.println("Help:");
  Serial.println("  enter 'ann' to announce presence to mesh");
  Serial.println("  enter 'send {message text}' to send a message");

  mesh.begin();

  command[0] = 0;

  // send out initial Announce to the mesh
  mesh.sendSelfAnnounce();
}

void loop() {
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') { 
      command[len++] = c;
      command[len] = 0;
    }
    Serial.print(c);
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    command[len - 1] = 0;  // replace newline with C string null terminator

    if (memcmp(command, "send ", 5) == 0) {
      // TODO: some way to select recipient??
      const ContactInfo& recipient = mesh.contacts[0];  // just send to first contact for now

      ripple::Destination dest(recipient.id, "chat.msg");
      if (mesh.hasPathTo(dest.hash)) {
        const char *text = &command[5];
        ripple::Packet* pkt = mesh.composeMsgPacket(dest, recipient, text);
        if (pkt) { 
          mesh.sendPacket(pkt, 0);
          Serial.println("   (message sent)");
        } else {
          Serial.println("   ERROR: unable to create packet.");
        }
      } else {
        Serial.println("   ERROR: no path to contact yet. Requesting path...");
        mesh.requestPathTo(dest.hash);
      }
    } else if (strcmp(command, "ann") == 0) {
      mesh.sendSelfAnnounce();
    } else if (strcmp(command, "key") == 0) {
      ripple::LocalIdentity new_id(mesh.getRNG());
      new_id.printTo(Serial);
    } else {
      Serial.print("   ERROR: unknown command: "); Serial.println(command);
    }

    command[0] = 0;  // reset command buffer
  }

  mesh.loop();
}
