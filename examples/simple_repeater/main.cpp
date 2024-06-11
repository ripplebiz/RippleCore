#include <Arduino.h>   // needed for PlatformIO
#include <MeshTransportFull.h>
#include <SPIFFS.h>

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/CustomSX1262Wrapper.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/IdentityStore.h>

/* ------------------------------ Config -------------------------------- */

#define  ANNOUNCE_DATA   "repeater:0.1"

#define ADMIN_SECRET_KEY   "8802D56E21E4127A46AC244BA2E99A9AF8F5A90D7825CB81C10FE6AFDEE2AB55"

#if defined(HELTEC_LORA_V3)
  #include <helpers/HeltecV3Board.h>
  static HeltecV3Board board;
#else
  #error "need to provide a 'board' object"
#endif

/* ------------------------------ Code -------------------------------- */

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

class MyMesh : public ripple::MeshTransportFull {
  ripple::Destination* request_in;
  RadioLibWrapper* my_radio;
  float airtime_factor;
  uint8_t admin_secret[PUB_KEY_SIZE];

  ripple::Packet* handleRequest(ripple::Packet* pkt, const uint8_t* packet_hash) { 
    switch (pkt->payload[0]) {
      case CMD_GET_STATS: {
        uint32_t max_age_secs;
        if (pkt->payload_len - 1 >= 4) {
          memcpy(&max_age_secs, &pkt->payload[1], 4);    // first param in request pkt
        } else {
          max_age_secs = 12*60*60;   // default, 12 hours
        }

        RepeaterStats stats;
        stats.batt_milli_volts = board.getBattMilliVolts();
        stats.curr_tx_queue_len = _mgr->getOutboundCount();
        stats.n_packets_recv = my_radio->getPacketsRecv();
        stats.n_packets_sent = my_radio->getPacketsSent();
        stats.n_active_dest = _tables->getActiveNextHopCount(max_age_secs);
        stats.total_air_time_secs = getTotalAirTime() / 1000;
        stats.total_up_time_secs = _ms->getMillis() / 1000;
        return createReplySigned(packet_hash, self_id, (const uint8_t *) &stats, sizeof(stats));  // send signed reply
      }
      case CMD_SET_CLOCK: {
        uint8_t temp[MAX_PACKET_PAYLOAD];
        int len = ripple::Utils::MACThenDecrypt(admin_secret, temp, &pkt->payload[1], pkt->payload_len - 1);
        if (len >= 4) {
          uint32_t curr_epoch_secs;
          memcpy(&curr_epoch_secs, temp, 4);    // first param is current UNIX time
          _rtc->setCurrentTime(curr_epoch_secs);

          return createReplySigned(packet_hash, self_id, (const uint8_t *) "OK", 2);  // send signed reply
        }
        return NULL;  // invalid request (not authorised)
      }
      case CMD_SEND_ANNOUNCE: {
        uint8_t temp[MAX_PACKET_PAYLOAD];
        int len = ripple::Utils::MACThenDecrypt(admin_secret, temp, &pkt->payload[1], pkt->payload_len - 1);
        if (len >= 3 && memcmp(temp, "ANN", 3) == 0) {
          // this is a little unusual, but admin is either ONE hop away, knows this destination, and has sent a "path.request"
          //   OR was one hop away from a node who did have this destination in their tables.
          // so, this destination IS reachable, but admin may want to trigger a new Announce manually if this destination is
          //   currently not reachable by some other part of the mesh.
          return createAnnounce(request_in, self_id, (const uint8_t *)ANNOUNCE_DATA, strlen(ANNOUNCE_DATA));
        }
        return NULL;  // invalid request (not authorised)
      }
      case CMD_SET_CONFIG: {
        uint8_t temp[MAX_PACKET_PAYLOAD];
        int len = ripple::Utils::MACThenDecrypt(admin_secret, temp, &pkt->payload[1], pkt->payload_len - 1);
        if (len >= 3 && len < 32 && memcmp(temp, "AF", 2) == 0) {
          temp[len] = 0;  // make it a C string
          airtime_factor = atof((char *) &temp[2]);
          return createReplySigned(packet_hash, self_id, (const uint8_t *) "OK", 2);  // send signed reply
        } else {
          // other config vars here
        }
        return NULL;  // invalid request (not authorised, or unknown config variable)
      }
      default: {
        // unknown command
        break;
      }
    }
    return NULL;
  }

protected:
  float getAirtimeBudgetFactor() const override {
    return airtime_factor;
  }

  ripple::DispatcherAction onDatagramRecv(ripple::Packet* packet, const uint8_t* packet_hash) override {
    if (request_in->matches(packet->destination_hash)) {
      ripple::Packet* reply = handleRequest(packet, packet_hash);
      if (reply) sendPacket(reply, 0);

      _tables->setSeenPacketHash(packet_hash, 1);  // reject this packet if we hear it retransmitted
      return ACTION_RELEASE;
    }
    return MeshTransportFull::onDatagramRecv(packet, packet_hash);
  }

public:
  MyMesh(RadioLibWrapper& radio, ripple::MillisecondClock& ms, ripple::RNG& rng, ripple::RTCClock& rtc)
     : ripple::MeshTransportFull(radio, ms, rng, rtc, *new StaticPoolPacketManager(32), *new SimpleMeshTables(rtc))
  {
    my_radio = &radio;
    airtime_factor = 5.0;   // 1/6th
    ripple::Utils::fromHex(admin_secret, sizeof(admin_secret), ADMIN_SECRET_KEY);
  }

  void begin(ripple::Destination* dest) { 
    request_in = dest;
    ripple::MeshTransportFull::begin();
  }

  void sendSelfAnnounce() {
    ripple::Packet* pkt = createAnnounce(request_in, self_id, (const uint8_t *)ANNOUNCE_DATA, strlen(ANNOUNCE_DATA));
    if (pkt) {
      sendPacket(pkt, 2);
    } else {
      Serial.println("ERROR: unable to create announce packet!");
    }
  }
};

#if defined(P_LORA_SCLK)
SPIClass spi;
CustomSX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
CustomSX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif
MyMesh mesh(*new CustomSX1262Wrapper(radio, board), *new ArduinoMillis(), *new RadioNoiseGenerator(radio), *new VolatileRTCClock());

void halt() {
  while (1) ;
}

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

  SPIFFS.begin(true);
  IdentityStore store(SPIFFS, "/identity");
  if (!store.load("_main", mesh.self_id)) {
    mesh.self_id = ripple::LocalIdentity(mesh.getRNG());  // create new random identity
    store.save("_main", mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  ripple::Utils::printHex(Serial, mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();
  {
    ripple::Destination dest(mesh.self_id, "trans.data");
    Serial.print("   transport_id: ");
    ripple::Utils::printHex(Serial, dest.hash, DEST_HASH_SIZE); Serial.println();
  }

  // destination for admin to use
  auto request_dest = new ripple::Destination(mesh.self_id, "repeater.request");
  mesh.begin(request_dest);

  // send out initial Announce to the mesh
  mesh.sendSelfAnnounce();
}

void loop() {
  mesh.loop();
}
