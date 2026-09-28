#pragma once

#include "mobile_framing.h"

#include <stddef.h>
#include <stdint.h>

namespace mobile {

constexpr const char *SERVICE_UUID =
    "6c433000-7d2e-4f65-9f3b-2a1c00000001";
constexpr const char *COMMAND_UUID =
    "6c433001-7d2e-4f65-9f3b-2a1c00000001";
constexpr const char *EVENT_UUID =
    "6c433002-7d2e-4f65-9f3b-2a1c00000001";
constexpr size_t TX_QUEUE_SIZE = 8;
constexpr size_t REQUEST_QUEUE_SIZE = 3;

class BleApi;

class RequestHandler {
 public:
  virtual ~RequestHandler() = default;
  virtual void handleRequest(uint16_t frameId, const char *json,
                             size_t length, BleApi &api) = 0;
};

class BleApi {
 public:
  BleApi();
  bool begin(const char *advertisedName, RequestHandler *handler);
  void service(uint32_t now);

  bool queueResponse(uint16_t requestFrameId, const char *json);
  bool queueEvent(const char *json);
  bool startPairing(uint32_t now);
  bool disconnectPhone();
  bool forgetAllBonds();

  bool ready() const { return ready_; }
  bool connected() const { return connected_; }
  bool authorized() const { return authorized_; }
  bool pairing() const { return pairing_; }
  bool bondFault() const { return bondFault_; }
  uint8_t bondCount() const { return bondCount_; }
  uint32_t passkey() const { return pairing_ ? passkey_ : 0; }
  uint32_t pairingSecondsRemaining(uint32_t now) const;
  uint32_t rxDrops() const { return rxDrops_; }
  uint32_t txDrops() const { return txDrops_; }
  bool consumeStateChanged();

  // These hooks are called only by the BLE adapter callbacks.
  void ingestWrite(const uint8_t *data, size_t length);
  void noteConnect(uint16_t connectionId, const uint8_t address[6]);
  void noteDisconnect();
  bool allowSecurityRequest() const;
  uint32_t requestedPasskey() const { return passkey_; }
  void notePasskey(uint32_t passkey);
  void noteAuthentication(bool success, const uint8_t address[6]);
  void noteMtu(uint16_t mtu);

 private:
  struct RequestFrame {
    bool active = false;
    uint16_t frameId = 0;
    uint16_t length = 0;
    char json[MAX_JSON_SIZE + 1] = {};
  };
  struct OutboundFrame {
    bool active = false;
    uint16_t frameId = 0;
    uint16_t length = 0;
    uint16_t offset = 0;
    char json[MAX_JSON_SIZE + 1] = {};
  };

  bool enqueue(uint16_t frameId, const char *json);
  bool addressMatchesBond(const uint8_t address[6]) const;
  void refreshBonds();
  void updateAdvertising();
  void markStateChanged();

  RequestHandler *handler_ = nullptr;
  Reassembler reassembler_;
  RequestFrame requests_[REQUEST_QUEUE_SIZE];
  OutboundFrame outbound_[TX_QUEUE_SIZE];
  uint8_t requestHead_ = 0, requestTail_ = 0, requestCount_ = 0;
  uint8_t txHead_ = 0, txTail_ = 0, txCount_ = 0;
  bool ready_ = false;
  volatile bool connected_ = false;
  volatile bool authorized_ = false;
  volatile bool stateChanged_ = false;
  bool pairing_ = false;
  bool bondFault_ = false;
  bool advertising_ = false;
  bool restartAdvertising_ = false;
  bool disconnectRequested_ = false;
  bool clearQueuesRequested_ = false;
  uint8_t bondCount_ = 0;
  uint8_t bondedAddress_[6] = {};
  uint16_t connectionId_ = 0;
  uint16_t mtu_ = 23;
  uint16_t nextEventFrameId_ = 0x8000;
  uint32_t passkey_ = 0;
  uint32_t pairingDeadline_ = 0;
  uint32_t lastNotifyAt_ = 0;
  uint32_t rxDrops_ = 0;
  uint32_t txDrops_ = 0;
  void *server_ = nullptr;
  void *eventCharacteristic_ = nullptr;
  void *security_ = nullptr;
};

}  // namespace mobile
