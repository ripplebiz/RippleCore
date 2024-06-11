#include <Arduino.h>   // needed for PlatformIO
#include <MeshTransportNone.h>

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/CustomSX1262Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

const char *repeater_public = "71C6F9C760341EE00D32CC3B29782426F90ED15987707E6FD82DCDD1FE8920C8";

#define ADMIN_SECRET_KEY   "8802D56E21E4127A46AC244BA2E99A9AF8F5A90D7825CB81C10FE6AFDEE2AB55"

// in case we want to sign something... as 'admin'
#define ADMIN_PRIVATE_KEY  "C801D49DF921C81B26C7491722888F2B63FA97C8B0330D61CAFE2B958589B6440A8AD5A74D789D71B36568707D7BCD9C2F4F0562A2EA7F274B353E982DED998A"

#ifdef HELTEC_LORA_V3
  #include <helpers/HeltecV3Board.h>
  static HeltecV3Board board;
#else
  #error "need to provide a 'board' object"
#endif

/* -------------------------------------------------------------------------------------- */

#define MAX_TEXT_LEN    (13*CIPHER_BLOCK_SIZE)  // must be LESS than (MAX_PACKET_PAYLOAD - FROM_HASH_LEN - CIPHER_MAC_SIZE - 1)

#define CMD_GET_STATS      0x01
#define CMD_SET_CLOCK      0x02
#define CMD_SEND_ANNOUNCE  0x03
#define CMD_SET_CONFIG     0x04

struct RepeaterStats {
  uint16_t batt_milli_volts;
  uint16_t curr_tx_queue_len;
  uint32_t n_packets_recv;
  uint32_t n_packets_sent;
  uint32_t n_active_dest;
  uint32_t total_air_time_secs;
  uint32_t total_up_time_secs;
};

class MyMesh : public ripple::MeshTransportNone {
  ripple::Destination* rep_req_dest;
  uint8_t stats_packet_hash[DEST_HASH_SIZE];
  uint8_t set_packet_hash[DEST_HASH_SIZE];

protected:
  // acts as filter for which Announces this app is interested in
  bool isAnnounceNew(ripple::Packet* packet, const ripple::Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) override {
    if (MeshTransportNone::isAnnounceNew(packet, id, rand_blob, app_data, app_data_len)) {
      if (rep_req_dest->matches(packet->destination_hash)) {  // is the Repeater announce
        return true;
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

  ripple::DispatcherAction onReplySignedRecv(ripple::Packet* packet, const uint8_t* reply, size_t reply_len) override {
    if (memcmp(packet->destination_hash, stats_packet_hash, DEST_HASH_SIZE) == 0) {      // got an GET_STATS reply from repeater
      RepeaterStats stats;
      memcpy(&stats, reply, sizeof(stats));
      Serial.println("Repeater Stats:");
      Serial.printf("  battery: %d mV\n", (uint32_t) stats.batt_milli_volts);
      Serial.printf("  tx queue: %d\n", (uint32_t) stats.curr_tx_queue_len);
      Serial.printf("  num recv: %d\n", stats.n_packets_recv);
      Serial.printf("  num sent: %d\n", stats.n_packets_sent);
      Serial.printf("  num active destinations: %d  (in past hour)\n", stats.n_active_dest);
      Serial.printf("  air time (secs): %d\n", stats.total_air_time_secs);
      Serial.printf("  up time (secs): %d\n", stats.total_up_time_secs);
    } else if (memcmp(packet->destination_hash, set_packet_hash, DEST_HASH_SIZE) == 0) {   // got an SET_* reply from repeater
      char tmp[MAX_PACKET_PAYLOAD];
      memcpy(tmp, reply, reply_len);
      tmp[reply_len] = 0;  // make a C string of reply

      Serial.print("Reply: "); Serial.println(tmp);
    }
    return MeshTransportNone::onReplySignedRecv(packet, reply, reply_len);
  }

public:
  uint8_t admin_secret[PUB_KEY_SIZE];

  MyMesh(ripple::Radio& radio, ripple::RNG& rng, ripple::RTCClock& rtc)
     : ripple::MeshTransportNone(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), *new SimpleMeshTables(rtc))
  {
    ripple::Utils::fromHex(admin_secret, sizeof(admin_secret), ADMIN_SECRET_KEY);
  }

  void begin(ripple::Destination* dest) {
    rep_req_dest = dest;
    ripple::MeshTransportNone::begin();
  }

  ripple::Packet* createStatsRequest(uint32_t max_age) {
    uint8_t payload[9];
    payload[0] = CMD_GET_STATS;
    memcpy(&payload[1], &max_age, 4);
    getRNG()->random(&payload[5], 4);  // need to append random blob, for unique packet_hash

    ripple::Packet* pkt = createDatagram(rep_req_dest, payload, sizeof(payload), true);  // not encrypted
    if (pkt) pkt->calculate_hash(stats_packet_hash);

    return pkt;
  }

  ripple::Packet* createSetClockRequest(uint32_t timestamp) {
    uint8_t payload[4];
    memcpy(payload, &timestamp, 4);

    uint8_t enc_payload[CIPHER_BLOCK_SIZE+CIPHER_MAC_SIZE+1];
    enc_payload[0] = CMD_SET_CLOCK;
    int enc_len = ripple::Utils::encryptThenMAC(admin_secret, &enc_payload[1], payload, 4);
    ripple::Packet* pkt = createDatagram(rep_req_dest, enc_payload, enc_len + 1, true);
    if (pkt) pkt->calculate_hash(set_packet_hash);

    return pkt;
  }

  ripple::Packet* createSetAirtimeFactorRequest(float airtime_factor) {
    char payload[32];
    sprintf(payload, "AF%f", airtime_factor);

    uint8_t enc_payload[CIPHER_BLOCK_SIZE*2+CIPHER_MAC_SIZE+1];
    enc_payload[0] = CMD_SET_CONFIG;
    int enc_len = ripple::Utils::encryptThenMAC(admin_secret, &enc_payload[1], (const uint8_t *)payload, strlen(payload));
    ripple::Packet* pkt = createDatagram(rep_req_dest, enc_payload, enc_len + 1, true);
    if (pkt) pkt->calculate_hash(set_packet_hash);

    return pkt;
  }

  ripple::Packet* createAnnounceRequest() {
    uint8_t payload[3];
    memcpy(payload, "ANN", 3);

    uint8_t enc_payload[CIPHER_BLOCK_SIZE+CIPHER_MAC_SIZE+1];
    enc_payload[0] = CMD_SEND_ANNOUNCE;
    int enc_len = ripple::Utils::encryptThenMAC(admin_secret, &enc_payload[1], payload, 3);
    return createDatagram(rep_req_dest, enc_payload, enc_len + 1, true);
  }

  ripple::Packet* parseCommand(char* command) {
    if (strcmp(command, "stats") == 0) {
      return createStatsRequest(60*60);    // max_age = one hour
    } else if (memcmp(command, "setclock ", 9) == 0) {
      uint32_t timestamp = atol(&command[9]);
      return createSetClockRequest(timestamp);
    } else if (memcmp(command, "set AF=", 7) == 0) {
      float factor = atof(&command[7]);
      return createSetAirtimeFactorRequest(factor);
    } else if (strcmp(command, "ann") == 0) {
      return createAnnounceRequest();
    }
    return NULL;  // unknown command
  }

  ripple::Destination* getRepeaterRequest() const { return rep_req_dest; }
};

StdRNG fast_rng;
#if defined(P_LORA_SCLK)
SPIClass spi;
CustomSX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
CustomSX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif
MyMesh mesh(*new CustomSX1262Wrapper(radio, board), fast_rng, *new VolatileRTCClock());

void halt() {
  while (1) ;
}

static char command[MAX_TEXT_LEN+1];

#include <SHA256.h>

void setup() {
  Serial.begin(115200);
  delay(5000);

  board.begin();
#if defined(P_LORA_SCLK)
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  int status = radio.begin(915.0, 250, 9, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 22);
#else
  int status = radio.begin(915.0, 250, 9, 5, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, 22);
#endif
  if (status != RADIOLIB_ERR_NONE) {
    Serial.print("ERROR: radio init failed: ");
    Serial.println(status);
    halt();
  }

  fast_rng.begin(radio.random(0x7FFFFFFF));

/* add this to tests
  uint8_t mac_encrypted[CIPHER_MAC_SIZE+CIPHER_BLOCK_SIZE];
  const char *orig_msg = "original";
  int enc_len = ripple::Utils::encryptThenMAC(mesh.admin_secret, mac_encrypted, (const uint8_t *) orig_msg, strlen(orig_msg));
  char decrypted[CIPHER_BLOCK_SIZE*2];
  int len = ripple::Utils::MACThenDecrypt(mesh.admin_secret, (uint8_t *)decrypted, mac_encrypted, enc_len);
  if (len > 0) {
    decrypted[len] = 0;
    Serial.print("decrypted text: "); Serial.println(decrypted);
  } else {
    Serial.println("MACs DONT match!");
  }
*/

  Serial.println("Help:");
  Serial.println("  enter 'key' to generate new keypair");
  Serial.println("  enter 'stats' to request repeater stats");
  Serial.println("  enter 'setclock {unix-epoch-seconds}' to set repeater's clock");
  Serial.println("  enter 'set AF={factor}' to set airtime budget factor");
  Serial.println("  enter 'ann' to make repeater re-announce to mesh");

  ripple::Identity repeater(repeater_public);
  auto rep_req_dest = new ripple::Destination(repeater, "repeater.request");

  mesh.begin(rep_req_dest);

  command[0] = 0;
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

    if (strcmp(command, "key") == 0) {
      ripple::LocalIdentity new_id(mesh.getRNG());
      new_id.printTo(Serial);
    } else {
      if (mesh.hasPathTo(mesh.getRepeaterRequest()->hash)) {
        ripple::Packet* pkt = mesh.parseCommand(command);
        if (pkt) { 
          mesh.sendPacket(pkt, 0);
          Serial.println("   (request sent)");
        } else {
          Serial.print("   ERROR: unknown command: "); Serial.println(command);
        }
      } else {
        Serial.println("   ERROR: no path to repeater yet. Requesting path...");
        mesh.requestPathTo(mesh.getRepeaterRequest()->hash);
      }
    }
    command[0] = 0;  // reset command buffer
  }

  mesh.loop();
}
