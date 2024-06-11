#include "Mesh.h"
#include <Arduino.h>

namespace ripple {

void Mesh::begin() {
  Dispatcher::begin();
}

void Mesh::loop() {
  Dispatcher::loop();
}

DispatcherAction Mesh::onRecvPacket(Packet* pkt) {
  DispatcherAction action = ACTION_RELEASE;

  pkt->hops++;  // one receive = one hop :-)

  switch (pkt->header & PH_TYPE_MASK) {
    case PH_TYPE_ANNOUNCE: {
      int i = 0;
      Identity id;
      memcpy(id.pub_key, &pkt->payload[i], PUB_KEY_SIZE); i += PUB_KEY_SIZE;

      uint8_t* rand_blob = &pkt->payload[i]; i += 8;
      uint8_t* signature = &pkt->payload[i]; i += SIGNATURE_SIZE;

      if (i > pkt->payload_len) {
        RIPPLE_DEBUG_PRINTLN("Mesh::onRecvPacket(): incomplete announce packet");
      } else {
        uint8_t* app_data = &pkt->payload[i];
        int app_data_len = pkt->payload_len - i;
        if (app_data_len > MAX_APP_DATA_SIZE) { app_data_len = MAX_APP_DATA_SIZE; }

        // check that signature is valid
        bool is_ok;
        {
          uint8_t message[DEST_HASH_SIZE + PUB_KEY_SIZE + 8 + MAX_APP_DATA_SIZE];
          int msg_len = 0;
          memcpy(&message[msg_len], pkt->destination_hash, DEST_HASH_SIZE); msg_len += DEST_HASH_SIZE;
          memcpy(&message[msg_len], id.pub_key, PUB_KEY_SIZE); msg_len += PUB_KEY_SIZE;
          memcpy(&message[msg_len], rand_blob, 8); msg_len += 8;
          memcpy(&message[msg_len], app_data, app_data_len); msg_len += app_data_len;

          is_ok = id.verify(signature, message, msg_len);
        }
        if (is_ok) {
          RIPPLE_DEBUG_PRINTLN("Mesh::onRecvPacket(): valid announce received!");
          if (isAnnounceNew(pkt, id, rand_blob, app_data, app_data_len)) { // this also acts as a filter for apps
            action = onAnnounceRecv(pkt, id, rand_blob, app_data, app_data_len);
          } else {
            RIPPLE_DEBUG_PRINTLN("Mesh::onRecvPacket(): Announce being re-played, or app is not interested in this Announce");
          }
        } else {
          RIPPLE_DEBUG_PRINTLN("Mesh::onRecvPacket(): announce signature forgery received!");
        }
      }
      break;
    }
    case PH_TYPE_DATA: {
      uint8_t packet_hash[DEST_HASH_SIZE];
      pkt->calculate_hash(packet_hash);
      if (isDatagramNew(pkt, packet_hash)) {
        action = onDatagramRecv(pkt, packet_hash);
      }
      break;
    }
    case PH_TYPE_REPLY: {
      action = onReplyRecv(pkt);
      break;
    }
    case PH_TYPE_REPLY_SIGNED: {
      if (isReplySignedNew(pkt)) {  // checks if reply is new AND that signature is valid
        action = onReplySignedRecv(pkt, &pkt->payload[SIGNATURE_SIZE], pkt->payload_len - SIGNATURE_SIZE);  // reply data is after signature
      }
      break;
    }
    default:
      RIPPLE_DEBUG_PRINTLN("Mesh::onRecvPacket(): invalid header, %d", (int) pkt->header);
      break;
  }
  return action;
}

Packet* Mesh::createAnnounce(Destination* destination, const LocalIdentity& id, const uint8_t* app_data, size_t app_data_len) {
  if (app_data_len > MAX_APP_DATA_SIZE) return NULL;

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    RIPPLE_DEBUG_PRINTLN("Mesh::createAnnounce(): error, packet pool empty");
    return NULL;
  }

  packet->header = PH_TYPE_ANNOUNCE;
  packet->hops = 0;
  memcpy(packet->destination_hash, destination->hash, DEST_HASH_SIZE);

  int len = 0;
  memcpy(&packet->payload[len], id.pub_key, PUB_KEY_SIZE); len += PUB_KEY_SIZE;

  uint8_t* rand_blob = &packet->payload[len];
  {
    uint32_t emitted_timestamp = _rtc->getCurrentTime();
    memcpy(rand_blob, &emitted_timestamp, 4); len += 4;
  }
  _rng->random(&rand_blob[4], 4); len += 4;

  uint8_t* signature = &packet->payload[len]; len += SIGNATURE_SIZE;  // will fill this in later

  memcpy(&packet->payload[len], app_data, app_data_len); len += app_data_len;

  packet->payload_len = len;

  {
    uint8_t message[DEST_HASH_SIZE + PUB_KEY_SIZE + 8 + MAX_APP_DATA_SIZE];
    int msg_len = 0;
    memcpy(&message[msg_len], packet->destination_hash, DEST_HASH_SIZE); msg_len += DEST_HASH_SIZE;
    memcpy(&message[msg_len], id.pub_key, PUB_KEY_SIZE); msg_len += PUB_KEY_SIZE;
    memcpy(&message[msg_len], rand_blob, 8); msg_len += 8;
    memcpy(&message[msg_len], app_data, app_data_len); msg_len += app_data_len;

    id.sign(signature, message, msg_len);
  }
  prepareLocalAnnounce(packet, rand_blob);

  return packet;
}

Packet* Mesh::createDatagram(const Destination* destination, const uint8_t* payload, int len, bool wantReply) {
  if (len > MAX_PACKET_PAYLOAD) return NULL;

  Packet* packet = obtainNewPacket();
  if (packet == NULL) {
    RIPPLE_DEBUG_PRINTLN("Mesh::createDatagram(): error, packet pool empty");
    return NULL;
  }
  packet->header = wantReply ? (PH_TYPE_DATA | PH_TYPE_KEEP_PATH) : PH_TYPE_DATA;
  packet->hops = 0;
  memcpy(packet->destination_hash, destination->hash, DEST_HASH_SIZE);
  memcpy(packet->payload, payload, len);
  packet->payload_len = len;

  prepareLocalDatagram(packet);

  return packet;
}

Packet* Mesh::createReply(const uint8_t* packet_hash, const uint8_t *reply, size_t reply_len) {
  if (reply_len > MAX_PACKET_PAYLOAD) return NULL;

  Packet* rp = obtainNewPacket();
  if (rp == NULL) {
    RIPPLE_DEBUG_PRINTLN("Mesh::createReply(): error, packet pool empty");
    return NULL;
  }

  rp->header = PH_TYPE_REPLY;
  rp->hops = 0;
  memcpy(rp->destination_hash, packet_hash, DEST_HASH_SIZE);
  memcpy(rp->payload, reply, reply_len);
  rp->payload_len = reply_len;

  prepareLocalReply(rp);

  return rp;
}

Packet* Mesh::createReplySigned(const uint8_t* packet_hash, const LocalIdentity& id, const uint8_t *reply, size_t reply_len) {
  if (reply_len > (MAX_PACKET_PAYLOAD - SIGNATURE_SIZE)) return NULL;

  Packet* rp = obtainNewPacket();
  if (rp == NULL) {
    RIPPLE_DEBUG_PRINTLN("Mesh::createReplySigned(): error, packet pool empty");
    return NULL;
  }

  rp->header = PH_TYPE_REPLY_SIGNED;
  rp->hops = 0;
  memcpy(rp->destination_hash, packet_hash, DEST_HASH_SIZE);
  {
    uint8_t message[MAX_PACKET_PAYLOAD - SIGNATURE_SIZE + DEST_HASH_SIZE + PUB_KEY_SIZE];
    int msg_len = 0;
    memcpy(&message[msg_len], packet_hash, DEST_HASH_SIZE); msg_len += DEST_HASH_SIZE;
    memcpy(&message[msg_len], id.pub_key, PUB_KEY_SIZE); msg_len += PUB_KEY_SIZE;
    memcpy(&message[msg_len], reply, reply_len); msg_len += reply_len;

    id.sign(rp->payload, message, msg_len);  // put signature at start of payload
  }
  memcpy(&rp->payload[SIGNATURE_SIZE], reply, reply_len);  // append reply data after signature
  rp->payload_len = SIGNATURE_SIZE + reply_len;

  //prepareLocalReplySigned(rp);

  return rp;
}

bool Mesh::verifyReplySigned(const Packet* packet, const Identity& id) {
  uint8_t message[MAX_PACKET_PAYLOAD - SIGNATURE_SIZE + DEST_HASH_SIZE + PUB_KEY_SIZE];
  int msg_len = 0;
  memcpy(&message[msg_len], packet->destination_hash, DEST_HASH_SIZE); msg_len += DEST_HASH_SIZE;
  memcpy(&message[msg_len], id.pub_key, PUB_KEY_SIZE); msg_len += PUB_KEY_SIZE;
  memcpy(&message[msg_len], &packet->payload[SIGNATURE_SIZE], packet->payload_len - SIGNATURE_SIZE); msg_len += packet->payload_len - SIGNATURE_SIZE;

  return id.verify(packet->payload, message, msg_len);  // signature is at start of payload
}

}