#include "mobile_ble.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <esp_gap_ble_api.h>
#include <esp_system.h>

#include <string.h>

namespace mobile {
namespace {

BleApi *activeApi = nullptr;
portMUX_TYPE requestMux = portMUX_INITIALIZER_UNLOCKED;

void onGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  if (param == nullptr) return;
  switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
      Serial.printf("[BLE] advertisement data status=%d\n", param->adv_data_raw_cmpl.status);
      break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
      Serial.printf("[BLE] scan response status=%d\n", param->scan_rsp_data_raw_cmpl.status);
      break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      Serial.printf("[BLE] advertising start status=%d\n", param->adv_start_cmpl.status);
      break;
    default:
      break;
  }
}

bool reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

class ServerCallbacks final : public BLEServerCallbacks {
 public:
  void onConnect(BLEServer *, esp_ble_gatts_cb_param_t *parameter) override {
    if (activeApi != nullptr && parameter != nullptr) {
      activeApi->noteConnect(parameter->connect.conn_id,
                             parameter->connect.remote_bda);
    }
  }
  void onDisconnect(BLEServer *, esp_ble_gatts_cb_param_t *) override {
    if (activeApi != nullptr) activeApi->noteDisconnect();
  }
  void onMtuChanged(BLEServer *, esp_ble_gatts_cb_param_t *parameter) override {
    if (activeApi != nullptr && parameter != nullptr) {
      activeApi->noteMtu(parameter->mtu.mtu);
    }
  }
};

class CommandCallbacks final : public BLECharacteristicCallbacks {
 public:
  void onWrite(BLECharacteristic *characteristic,
               esp_ble_gatts_cb_param_t *) override {
    if (activeApi == nullptr || characteristic == nullptr) return;
    const std::string value = characteristic->getValue();
    activeApi->ingestWrite(reinterpret_cast<const uint8_t *>(value.data()),
                           value.size());
  }
};

class SecurityCallbacks final : public BLESecurityCallbacks {
 public:
  uint32_t onPassKeyRequest() override {
    return activeApi == nullptr ? 0 : activeApi->requestedPasskey();
  }
  void onPassKeyNotify(uint32_t passkey) override {
    if (activeApi != nullptr) activeApi->notePasskey(passkey);
  }
  bool onSecurityRequest() override {
    return activeApi != nullptr && activeApi->allowSecurityRequest();
  }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t result) override {
    if (!result.success) {
      Serial.printf("[BLE] authentication failed reason=0x%02X\n",
                    result.fail_reason);
    } else {
      Serial.println("[BLE] secure authentication completed");
    }
    if (activeApi != nullptr) {
      activeApi->noteAuthentication(result.success, result.bd_addr);
    }
  }
  bool onConfirmPIN(uint32_t) override {
    return activeApi != nullptr && activeApi->allowSecurityRequest();
  }
};

ServerCallbacks serverCallbacks;
CommandCallbacks commandCallbacks;
SecurityCallbacks securityCallbacks;

}  // namespace

BleApi::BleApi() = default;

bool BleApi::begin(const char *advertisedName, RequestHandler *handler) {
  if (ready_ || advertisedName == nullptr || advertisedName[0] == '\0' ||
      handler == nullptr) {
    return false;
  }
  handler_ = handler;
  activeApi = this;
  BLEDevice::init(advertisedName);
  BLEDevice::setCustomGapHandler(onGapEvent);
  BLEDevice::setMTU(247);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
  BLEDevice::setSecurityCallbacks(&securityCallbacks);

  BLESecurity *security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  security->setCapability(ESP_IO_CAP_OUT);
  security->setKeySize(16);
  security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security_ = security;

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(&serverCallbacks);
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *command = service->createCharacteristic(
      COMMAND_UUID, BLECharacteristic::PROPERTY_WRITE);
  command->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
  command->setCallbacks(&commandCallbacks);

  BLECharacteristic *events = service->createCharacteristic(
      EVENT_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  events->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  BLE2902 *subscription = new BLE2902();
  subscription->setAccessPermissions(static_cast<esp_gatt_perm_t>(
      ESP_GATT_PERM_READ_ENC_MITM | ESP_GATT_PERM_WRITE_ENC_MITM));
  events->addDescriptor(subscription);
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  // A 128-bit service UUID and the CL3 name do not fit in one BLE packet.
  // Keep the name in the primary advertisement so discovery does not depend
  // on a scan response; place the UUID in the separate scan response.
  BLEAdvertisementData advertisementData;
  advertisementData.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
  advertisementData.setName(advertisedName);
  advertising->setAdvertisementData(advertisementData);
  BLEAdvertisementData scanResponseData;
  scanResponseData.setCompleteServices(BLEUUID(SERVICE_UUID));
  advertising->setScanResponseData(scanResponseData);
  advertising->setMinPreferred(0x06);
  advertising->setMinPreferred(0x12);
  server_ = server;
  eventCharacteristic_ = events;
  refreshBonds();
  ready_ = true;
  updateAdvertising();
  markStateChanged();
  return true;
}

void BleApi::refreshBonds() {
  const int count = esp_ble_get_bond_device_num();
  bondCount_ = count < 0 ? 0 : count > 255 ? 255 : static_cast<uint8_t>(count);
  memset(bondedAddress_, 0, sizeof(bondedAddress_));
  bondFault_ = bondCount_ > 1;
  if (bondCount_ == 1) {
    int requested = 1;
    esp_ble_bond_dev_t bond;
    if (esp_ble_get_bond_device_list(&requested, &bond) == ESP_OK &&
        requested == 1) {
      memcpy(bondedAddress_, bond.bd_addr, sizeof(bondedAddress_));
    } else {
      bondFault_ = true;
    }
  }
}

bool BleApi::addressMatchesBond(const uint8_t address[6]) const {
  return address != nullptr && bondCount_ == 1 &&
         memcmp(address, bondedAddress_, sizeof(bondedAddress_)) == 0;
}

void BleApi::updateAdvertising() {
  if (!ready_) return;
  const bool shouldAdvertise = !connected_ && !bondFault_ &&
                               (bondCount_ == 1 || pairing_);
  if (shouldAdvertise && !advertising_) {
    BLEDevice::startAdvertising();
    advertising_ = true;
  } else if (!shouldAdvertise && advertising_) {
    BLEDevice::stopAdvertising();
    advertising_ = false;
  }
}

bool BleApi::startPairing(uint32_t now) {
  if (!ready_ || connected_ || bondCount_ != 0 || bondFault_) return false;
  passkey_ = 100000UL + esp_random() % 900000UL;
  BLESecurity *security = static_cast<BLESecurity *>(security_);
  security->setStaticPIN(passkey_);
  // ESP32 BLE Arduino's setStaticPIN() changes the authentication mode to
  // SC_ONLY internally. Restore MITM + persistent bonding afterwards.
  security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  pairing_ = true;
  pairingDeadline_ = now + 60000UL;
  updateAdvertising();
  markStateChanged();
  return true;
}

bool BleApi::disconnectPhone() {
  if (!ready_ || !connected_) return false;
  disconnectRequested_ = true;
  markStateChanged();
  return true;
}

bool BleApi::forgetAllBonds() {
  if (!ready_) return false;
  int count = esp_ble_get_bond_device_num();
  if (count < 0 || count > 8) return false;
  esp_ble_bond_dev_t bonds[8];
  int requested = count;
  if (count > 0 &&
      esp_ble_get_bond_device_list(&requested, bonds) != ESP_OK) {
    return false;
  }
  if (connected_) disconnectRequested_ = true;
  for (int i = 0; i < requested; ++i) {
    esp_ble_remove_bond_device(bonds[i].bd_addr);
  }
  bondCount_ = 0;
  bondFault_ = false;
  authorized_ = false;
  pairing_ = false;
  updateAdvertising();
  markStateChanged();
  return true;
}

uint32_t BleApi::pairingSecondsRemaining(uint32_t now) const {
  if (!pairing_ || reached(now, pairingDeadline_)) return 0;
  return (pairingDeadline_ - now + 999UL) / 1000UL;
}

void BleApi::markStateChanged() { stateChanged_ = true; }

bool BleApi::consumeStateChanged() {
  const bool changed = stateChanged_;
  stateChanged_ = false;
  return changed;
}

void BleApi::noteConnect(uint16_t connectionId, const uint8_t address[6]) {
  (void)address;
  if (connected_) {
    static_cast<BLEServer *>(server_)->disconnect(connectionId);
    return;
  }
  connected_ = true;
  authorized_ = false;
  connectionId_ = connectionId;
  advertising_ = false;
  // A bonded phone may reconnect with a resolvable private address. Defer the
  // identity comparison until the controller reports authentication complete.
  if ((bondCount_ == 0 && !pairing_) || bondFault_) {
    disconnectRequested_ = true;
  }
  markStateChanged();
}

void BleApi::noteDisconnect() {
  connected_ = false;
  authorized_ = false;
  clearQueuesRequested_ = true;
  restartAdvertising_ = true;
  markStateChanged();
}

bool BleApi::allowSecurityRequest() const {
  return pairing_ || bondCount_ == 1;
}

void BleApi::notePasskey(uint32_t passkey) {
  if (!pairing_ || passkey > 999999UL) return;
  // Display the controller-provided value. This remains correct even if the
  // ESP-IDF controller elects to generate a passkey instead of using the
  // configured static value.
  passkey_ = passkey;
  markStateChanged();
}

void BleApi::noteAuthentication(bool success, const uint8_t address[6]) {
  if (!success) {
    authorized_ = false;
    markStateChanged();
    return;
  }
  const bool hadBond = bondCount_ == 1;
  if (hadBond && !addressMatchesBond(address)) {
    esp_ble_remove_bond_device(const_cast<uint8_t *>(address));
    disconnectRequested_ = true;
    authorized_ = false;
    markStateChanged();
    return;
  }
  refreshBonds();
  authorized_ = bondCount_ == 1 && addressMatchesBond(address) && !bondFault_;
  if (authorized_) {
    pairing_ = false;
    passkey_ = 0;
  } else {
    disconnectRequested_ = true;
  }
  markStateChanged();
}

void BleApi::noteMtu(uint16_t mtu) {
  if (mtu >= 23) mtu_ = mtu;
}

void BleApi::ingestWrite(const uint8_t *data, size_t length) {
  if (!authorized_) {
    ++rxDrops_;
    return;
  }
  uint16_t frameId = 0;
  const char *json = nullptr;
  size_t jsonLength = 0;
  const FragmentResult result =
      reassembler_.ingest(data, length, frameId, json, jsonLength);
  if (result == FragmentResult::Complete) {
    portENTER_CRITICAL(&requestMux);
    if (requestCount_ < REQUEST_QUEUE_SIZE) {
      RequestFrame &request = requests_[requestTail_];
      request.active = true;
      request.frameId = frameId;
      request.length = static_cast<uint16_t>(jsonLength);
      memcpy(request.json, json, jsonLength + 1);
      requestTail_ = (requestTail_ + 1U) % REQUEST_QUEUE_SIZE;
      ++requestCount_;
    } else {
      ++rxDrops_;
    }
    portEXIT_CRITICAL(&requestMux);
  } else if (result != FragmentResult::Accepted) {
    ++rxDrops_;
  }
}

bool BleApi::enqueue(uint16_t frameId, const char *json) {
  if (!ready_ || frameId == 0 || json == nullptr) return false;
  const size_t length = strnlen(json, MAX_JSON_SIZE + 1);
  if (length == 0 || length > MAX_JSON_SIZE || txCount_ >= TX_QUEUE_SIZE) {
    ++txDrops_;
    return false;
  }
  OutboundFrame &frame = outbound_[txTail_];
  frame.active = true;
  frame.frameId = frameId;
  frame.length = static_cast<uint16_t>(length);
  frame.offset = 0;
  memcpy(frame.json, json, length + 1);
  txTail_ = (txTail_ + 1U) % TX_QUEUE_SIZE;
  ++txCount_;
  return true;
}

bool BleApi::queueResponse(uint16_t requestFrameId, const char *json) {
  if (requestFrameId == 0 || requestFrameId > 0x7FFF) return false;
  return enqueue(requestFrameId, json);
}

bool BleApi::queueEvent(const char *json) {
  uint16_t frameId = nextEventFrameId_++;
  if (nextEventFrameId_ < 0x8000) nextEventFrameId_ = 0x8000;
  return enqueue(frameId, json);
}

void BleApi::service(uint32_t now) {
  if (!ready_) return;
  if (pairing_ && reached(now, pairingDeadline_)) {
    pairing_ = false;
    passkey_ = 0;
    if (connected_ && !authorized_) disconnectRequested_ = true;
    markStateChanged();
  }
  if (disconnectRequested_) {
    disconnectRequested_ = false;
    if (connected_) static_cast<BLEServer *>(server_)->disconnect(connectionId_);
  }
  if (clearQueuesRequested_) {
    clearQueuesRequested_ = false;
    reassembler_.reset();
    portENTER_CRITICAL(&requestMux);
    requestHead_ = requestTail_ = requestCount_ = 0;
    portEXIT_CRITICAL(&requestMux);
    txHead_ = txTail_ = txCount_ = 0;
    memset(requests_, 0, sizeof(requests_));
    memset(outbound_, 0, sizeof(outbound_));
  }
  if (restartAdvertising_) {
    restartAdvertising_ = false;
    refreshBonds();
  }
  updateAdvertising();

  RequestFrame request;
  bool haveRequest = false;
  portENTER_CRITICAL(&requestMux);
  if (requestCount_ > 0) {
    request = requests_[requestHead_];
    requests_[requestHead_] = RequestFrame{};
    requestHead_ = (requestHead_ + 1U) % REQUEST_QUEUE_SIZE;
    --requestCount_;
    haveRequest = true;
  }
  portEXIT_CRITICAL(&requestMux);
  if (haveRequest && handler_ != nullptr) {
    if (request.frameId <= 0x7FFF) {
      handler_->handleRequest(request.frameId, request.json, request.length,
                              *this);
    } else {
      ++rxDrops_;
    }
  }

  if (!connected_ || !authorized_ || txCount_ == 0 ||
      static_cast<uint32_t>(now - lastNotifyAt_) < 12UL) {
    return;
  }
  OutboundFrame &frame = outbound_[txHead_];
  const size_t packetCapacity =
      mtu_ > 250 ? 247 : mtu_ > 11 ? mtu_ - 3 : 20;
  uint8_t packet[247];
  size_t written = 0, nextOffset = frame.offset;
  if (!buildFragment(frame.frameId, frame.json, frame.length, frame.offset,
                     packetCapacity, packet, written, nextOffset)) {
    frame = OutboundFrame{};
    txHead_ = (txHead_ + 1U) % TX_QUEUE_SIZE;
    --txCount_;
    ++txDrops_;
    return;
  }
  BLECharacteristic *events =
      static_cast<BLECharacteristic *>(eventCharacteristic_);
  events->setValue(packet, written);
  events->notify();
  lastNotifyAt_ = now;
  frame.offset = static_cast<uint16_t>(nextOffset);
  if (frame.offset == frame.length) {
    frame = OutboundFrame{};
    txHead_ = (txHead_ + 1U) % TX_QUEUE_SIZE;
    --txCount_;
  }
}

}  // namespace mobile
