#include "Dispatcher.h"
//#include <Arduino.h>

namespace ripple {

void Dispatcher::begin() {
  _radio->begin();
}

float Dispatcher::getAirtimeBudgetFactor() const {
  return 5.0;   // default, 16.6%  (1/6th)
}

void Dispatcher::loop() {
  if (outbound) {  // waiting for outbound send to be completed
    if (_radio->isSendComplete()) {
      long t = _ms->getMillis() - outbound_start;
      total_air_time += t;  // keep track of how much air time we are using
      //Serial.print("  airtime="); Serial.println(t);

      // will need radio silence up to next_tx_time
      next_tx_time = futureMillis(t * getAirtimeBudgetFactor());

      _radio->onSendFinished();
      onPacketSent(outbound);
      outbound = NULL;
    } else if (millisHasNowPassed(outbound_expiry)) {
      RIPPLE_DEBUG_PRINTLN("Dispatcher::loop(): WARNING: outbound packed send timed out!");
      //Serial.println("  timed out");

      _radio->onSendFinished();
      releasePacket(outbound);  // return to pool
      outbound = NULL;
    } else {
      return;  // can't do any more radio activity until send is complete or timed out
    }
  }

  checkRecv();
  checkSend();
}

void Dispatcher::onPacketSent(Packet* packet) {
  releasePacket(packet);  // default behaviour, return packet to pool
}

void Dispatcher::checkRecv() {
  Packet* pkt;
  {
    uint8_t raw[MAX_TRANS_UNIT];
    int len = _radio->recvRaw(raw, MAX_TRANS_UNIT);
    if (len > 0) {
      pkt = _mgr->allocNew();
      if (pkt == NULL) {
        RIPPLE_DEBUG_PRINTLN("Dispatcher::checkRecv(): WARNING: received data, no unused packets available!");
      } else {
        int i = 0;
#ifdef NODE_ID
        uint8_t sender_id = raw[i++];
        if (sender_id == NODE_ID - 1 || sender_id == NODE_ID + 1) {  // simulate that NODE_ID can only hear NODE_ID-1 or NODE_ID+1, eg. 3 can't hear 1
        } else {
          _mgr->free(pkt);  // put back into pool
          return;
        }
#endif
        //Serial.print("LoRa recv: len="); Serial.println(len);

        pkt->header = raw[i++];
        pkt->hops = raw[i++];
        if (pkt->header & PH_HAS_TRANS_ADDRESS) {
          memcpy(pkt->transport_id, &raw[i], DEST_HASH_SIZE); i += DEST_HASH_SIZE;
        } else {
          memset(pkt->transport_id, 0, DEST_HASH_SIZE);  // useful for comparisons
        }

        if (pkt->getPacketType() == PH_TYPE_ANNOUNCE) {
          // destination_hash can now be calculated from Announce payload, so don't include in wire format
        } else {
          memcpy(pkt->destination_hash, &raw[i], DEST_HASH_SIZE); i += DEST_HASH_SIZE;
        }

        if (i > len) {
          RIPPLE_DEBUG_PRINTLN("Dispatcher::checkRecv(): partial packet received, len=%d", len);
          _mgr->free(pkt);  // put back into pool
          pkt = NULL;
        } else {
          pkt->payload_len = len - i;  // payload is remainder
          memcpy(pkt->payload, &raw[i], pkt->payload_len);
        }
      }
    } else {
      pkt = NULL;
    }
  }
  if (pkt) {
    DispatcherAction action = onRecvPacket(pkt);
    if (action == ACTION_RELEASE) {
      _mgr->free(pkt);
    } else if (action == ACTION_MANUAL_HOLD) {
      // sub-class is wanting to manually hold Packet instance, and call releasePacket() at appropriate time
    } else {   // ACTION_RETRANSMIT*
      uint8_t priority = (action >> 24) - 1;
      uint32_t _delay = action & 0xFFFFFF;

      _mgr->queueOutbound(pkt, priority, futureMillis(_delay));
    }
  }
}

void Dispatcher::checkSend() {
  if (_mgr->getOutboundCount() == 0) return;  // nothing waiting to send
  if (!millisHasNowPassed(next_tx_time)) return;   // still in 'radio silence' phase (from airtime budget setting)
  if (_radio->isReceiving()) return;  // check if radio is currently mid-receive

  outbound = _mgr->getNextOutbound(_ms->getMillis());
  if (outbound) {
    int len = 0;
    uint8_t raw[MAX_TRANS_UNIT];

    // optimisation
    if (memcmp(outbound->destination_hash, outbound->transport_id, DEST_HASH_SIZE) == 0) {
      outbound->header &= ~PH_HAS_TRANS_ADDRESS;  // next hop IS the destination, don't need 'transport_id' address
    }
#ifdef NODE_ID
    raw[len++] = NODE_ID;
#endif
    raw[len++] = outbound->header;
    raw[len++] = outbound->hops;
    if (outbound->header & PH_HAS_TRANS_ADDRESS) {
      memcpy(&raw[len], outbound->transport_id, DEST_HASH_SIZE); len += DEST_HASH_SIZE;
    }

    if (outbound->getPacketType() == PH_TYPE_ANNOUNCE) {
      // destination_hash can now be calculated from Announce payload, so don't include in wire format
    } else {
      memcpy(&raw[len], outbound->destination_hash, DEST_HASH_SIZE); len += DEST_HASH_SIZE;
    }

    if (len + outbound->payload_len > MAX_TRANS_UNIT) {
      RIPPLE_DEBUG_PRINTLN("Dispatcher::checkSend(): FATAL: Invalid packet queued... too long, len=%d", len + outbound->payload_len);
      _mgr->free(outbound);
      outbound = NULL;
    } else {
      memcpy(&raw[len], outbound->payload, outbound->payload_len); len += outbound->payload_len;

      uint32_t max_airtime = _radio->getEstAirtimeFor(len)*3/2;
      outbound_start = _ms->getMillis();
      _radio->startSendRaw(raw, len);
      outbound_expiry = futureMillis(max_airtime);

      //Serial.print("LoRa send: len="); Serial.print(len);
    }
  }
}

Packet* Dispatcher::obtainNewPacket() {
  return _mgr->allocNew();  // TODO: zero out all fields
}

void Dispatcher::releasePacket(Packet* packet) {
  _mgr->free(packet);
}

void Dispatcher::sendPacket(Packet* packet, uint8_t priority, uint32_t delay_millis) {
  _mgr->queueOutbound(packet, priority, futureMillis(delay_millis));
}

// Utility function -- handles the case where millis() wraps around back to zero
//   2's complement arithmetic will handle any unsigned subtraction up to HALF the word size (32-bits in this case)
bool Dispatcher::millisHasNowPassed(unsigned long timestamp) const {
  return (long)(_ms->getMillis() - timestamp) > 0;
}

unsigned long Dispatcher::futureMillis(int millis_from_now) const {
  return _ms->getMillis() + millis_from_now;
}

}