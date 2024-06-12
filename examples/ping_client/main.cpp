#include <Arduino.h>   // needed for PlatformIO
#include <MeshTransportNone.h>

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/RadioLibWrappers.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>

/* ------------------------------ Config -------------------------------- */

#ifdef HELTEC_LORA_V3
  #include <helpers/HeltecV3Board.h>
  static HeltecV3Board board;
#else
  #error "need to provide a 'board' object"
#endif

/* ------------------------------ Code -------------------------------- */

class MyMesh : public ripple::MeshTransportNone {
  ripple::Destination ping_dest;
  bool got_announce;

protected:

  // acts as filter for which Announces this app is interested in
  bool isAnnounceNew(ripple::Packet* packet, const ripple::Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) override {
    if (MeshTransportNone::isAnnounceNew(packet, id, rand_blob, app_data, app_data_len)) {
      // see if the name is correct, ie. Announce is for destination to *A* ping server
      ripple::Destination test(id, "sample.ping");
      return test.matches(packet->destination_hash);

      // ALTERNATIVE: check that the 'id' is one we already know about
      // return memcmp(id.pub_key, known_key, PUB_KEY_SIZE) == 0;

      // ALTERNATIVE: check the app_data to see if this is the Announce we want
      // return app_data_len == 4 && memcmp(app_data, "PING", 4) == 0;
    }
    return false;
  }

  ripple::DispatcherAction onAnnounceRecv(ripple::Packet* packet, const ripple::Identity& id, const uint8_t* rand_blob, const uint8_t* app_data, size_t app_data_len) override {
    Serial.print("Got announce, dest: "); ripple::Utils::printHex(Serial, packet->destination_hash, DEST_HASH_SIZE); Serial.println();

    memcpy(ping_dest.hash, packet->destination_hash, DEST_HASH_SIZE);  // take a copy
    got_announce = true;

    return MeshTransportNone::onAnnounceRecv(packet, id, rand_blob, app_data, app_data_len);
  }

  ripple::DispatcherAction onReplySignedRecv(ripple::Packet* packet, const uint8_t* reply, size_t reply_len) override {
    if (memcmp(last_ping_hash, packet->destination_hash, DEST_HASH_SIZE) == 0) {
      Serial.println("Got PING reply.");
    }
    return MeshTransportNone::onReplySignedRecv(packet, reply, reply_len);
  }

public:
  uint8_t last_ping_hash[DEST_HASH_SIZE];

  MyMesh(ripple::Radio& radio, ripple::RNG& rng, ripple::RTCClock& rtc)
     : ripple::MeshTransportNone(radio, *new ArduinoMillis(), rng, rtc, *new StaticPoolPacketManager(16), *new SimpleMeshTables(rtc))
  {
    got_announce = false;
    memset(last_ping_hash, 0, DEST_HASH_SIZE);
  }
  ripple::Destination* getPingDest() { 
    return got_announce ? &ping_dest : NULL;
  }
};

SPIClass spi;
StdRNG fast_rng;
SX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
MyMesh mesh(*new RadioLibWrapper(radio, board), fast_rng, *new VolatileRTCClock());
unsigned long nextPing;

void halt() {
  while (1) ;
}

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
  mesh.begin();

  nextPing = 0;
}

void loop() {
  if (mesh.millisHasNowPassed(nextPing)) {
    if (mesh.getPingDest() != NULL) {
      uint8_t data[4];
      mesh.getRNG()->random(data, 4);  // important, need random blob in packet, so that packet_hash will be unique

      ripple::Packet* pkt = mesh.createDatagram(mesh.getPingDest(), data, 4, true);  // NOTE: this is PLAINTEXT
      if (pkt) { 
        mesh.sendPacket(pkt, 0);
        pkt->calculatePacketHash(mesh.last_ping_hash);  // keep packet_hash of last PING
      }
    }
    nextPing = mesh.futureMillis(10000);  // attempt ping every 10 seconds
  }
  mesh.loop();
}
