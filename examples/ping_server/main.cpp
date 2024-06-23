#include <Arduino.h>   // needed for PlatformIO
#include <MeshTransportFull.h>
#include <SPIFFS.h>

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

class MyMesh : public ripple::MeshTransportFull {
  ripple::Destination* ping_in;

public:
  ripple::DispatcherAction onDatagramRecv(ripple::Packet* packet, const uint8_t* packet_hash) override {
    if (ping_in && memcmp(packet->destination_hash, ping_in->hash, DEST_HASH_SIZE) == 0) {
      ripple::Packet* reply = createReplySigned(packet_hash, self_id, NULL, 0);  // send signed reply to origin
      if (reply) sendPacket(reply, 1);

      _tables->setSeenPacketHash(packet_hash, 1);  // reject this packet if we hear it retransmitted
      return ACTION_RELEASE;
    }
    return MeshTransportFull::onDatagramRecv(packet, packet_hash);
  }

  MyMesh(ripple::Radio& radio, ripple::MillisecondClock& ms, ripple::RNG& rng, ripple::RTCClock& rtc)
     : ripple::MeshTransportFull(radio, ms, rng, rtc, *new StaticPoolPacketManager(16), *new SimpleMeshTables(rtc))
  {
    ping_in = NULL;
  }

  void setPingDest(ripple::Destination* ping) { ping_in = ping; }
};

SPIClass spi;
StdRNG fast_rng;
SX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
MyMesh mesh(*new RadioLibWrapper(radio, board), *new ArduinoMillis(), fast_rng, *new VolatileRTCClock());

unsigned long nextAnnounce;

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

  RadioNoiseGenerator true_rng(radio);
  mesh.self_id = ripple::LocalIdentity(&true_rng);  // create new random identity
  mesh.setPingDest(new ripple::Destination(mesh.self_id, "sample.ping"));
  nextAnnounce = 0;
}

void loop() {
  if (mesh.millisHasNowPassed(nextAnnounce)) {
    ripple::Packet* pkt = mesh.createAnnounce("sample.ping", mesh.self_id /*, (const uint8_t *)"PING", 4 */);
    if (pkt) mesh.sendPacket(pkt, 2);

    nextAnnounce = mesh.futureMillis(30000);  // announce every 30 seconds (test only, don't do in production!)
  }
  mesh.loop();
}
