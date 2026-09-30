#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "logo_bitmap.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LoRa.h>
#include <SPI.h>
#include <TinyGPSPlus.h>
#include <esp_system.h>

#include "app_config.h"
#include "crew_data.h"
#include "feature_input.h"
#include "mesh_protocol.h"
#include "mesh_state.h"
#include "mobile_ble.h"
#include "radar_math.h"
#include "secure_frame.h"
#include "ui_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint8_t SPI_SCK_PIN = 18, SPI_MISO_PIN = 19, SPI_MOSI_PIN = 23;
constexpr uint8_t TFT_CS_PIN = 15, TFT_DC_PIN = 2, TFT_RST_PIN = 4;
constexpr uint16_t SCREEN_WIDTH = 320, SCREEN_HEIGHT = 240;
// RGB565 approximations of the firmware palette in design.md.
constexpr uint16_t COLOR_BACKGROUND = 0x18C3; // #171717
constexpr uint16_t COLOR_SURFACE = 0x2945;    // #292929
constexpr uint16_t COLOR_TEXT = 0xF79D;       // #F5F3EE
constexpr uint16_t COLOR_MID_GREY = 0x8C70;   // #8C8C86
constexpr uint16_t COLOR_DARK_GREY = 0x4A49;
constexpr uint16_t COLOR_ORANGE = 0xF345;     // #F26A2E
constexpr uint16_t COLOR_GREEN = 0x2DAB;
constexpr uint16_t COLOR_AMBER = 0xFD20;
constexpr uint16_t COLOR_RED = 0xF944;
constexpr uint8_t LORA_CS_PIN = 5, LORA_RST_PIN = 16, LORA_DIO0_PIN = 22;
constexpr long LORA_FREQUENCY = 433000000L;
constexpr uint8_t LORA_SYNC_WORD = 0x4A, LORA_SPREADING_FACTOR = 9;
constexpr long LORA_BANDWIDTH = 125000L;
constexpr uint8_t LORA_CODING_RATE_DENOMINATOR = 5;
constexpr uint8_t GPS_RX_PIN = 34, GPS_TX_PIN = 17, BUZZER_PIN = 21;
constexpr uint32_t GPS_BAUD = 9600, GPS_STALE_MS = 10000UL;
constexpr byte KEYPAD_ROWS = 4, KEYPAD_COLS = 4;
// This keypad's signals run in reverse order across the eight wired pads.
byte rowPins[KEYPAD_ROWS] = {26, 25, 33, 32};
byte columnPins[KEYPAD_COLS] = {13, 12, 14, 27};
char keyMap[KEYPAD_ROWS][KEYPAD_COLS] = {
    {'1', '2', '3', 'A'}, {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'}, {'*', '0', '#', 'D'}};

constexpr uint32_t ACK_WINDOW_MS = 6000UL;
constexpr uint8_t MAX_SEND_ATTEMPTS = 3, RELAY_CANCEL_COPIES = 2;
constexpr uint32_t PRESENCE_MIN_MS = 27000UL, PRESENCE_MAX_MS = 33000UL;
Adafruit_ST7789 tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
HardwareSerial gpsSerial(2);
TinyGPSPlus gps;

using ui::Screen;

class MobileHandler final : public mobile::RequestHandler {
 public:
  void handleRequest(uint16_t frameId, const char *json, size_t length,
                     mobile::BleApi &api) override;
};

struct Counters {
  uint32_t localMessages = 0, receivedMessages = 0, relayed = 0, retries = 0;
  uint32_t duplicates = 0, malformed = 0, authFailures = 0, wrongCrew = 0;
  uint32_t queueDrops = 0, acksSent = 0, presenceSent = 0, conflicts = 0;
};
struct BuzzerState {
  bool active = false, high = false;
  uint8_t pulsesRemaining = 0;
  uint16_t onMs = 0, gapMs = 0;
  uint32_t deadline = 0;
};

bool configReady = false, cryptoReady = false, loraReady = false;
bool localAddressConflict = false;
bool radioTxInProgress = false;
volatile bool radioTxDone = false;
uint32_t radioTxStartedAt = 0;
uint32_t lastRadioActivityAt = 0;
uint8_t hardwareId[mesh::HARDWARE_ID_SIZE] = {};
char hardwareIdHex[mesh::HARDWARE_ID_SIZE * 2 + 1] = {};
mesh::SessionId bootSession;
uint32_t frameSequence = 0, nextFloodId = 0, nextMessageId = 0;
uint32_t nextPresenceAt = 0, lastSecondAt = 0, lastGpsDiagnosticAt = 0;
uint32_t lastGpsUiAt = 0;
ui::Navigator navigation;
Screen currentScreen = Screen::Home;
bool screenDirty = true, contentDirty = true;
bool statusBarDirty = true;
uint8_t listSelection = 0, symbolSelection = 0;
int selectedMessageIndex = -1, selectedPeerIndex = -1;
bool recipientReturnsToCompose = false;
input::FeatureInput composer;
uint16_t draftRecipient = mesh::BROADCAST_ADDRESS;
char activeOutgoingText[mesh::MAX_TEXT_LENGTH + 1] = {};
Counters counters;
BuzzerState buzzer;
mesh::SecureFrameCodec secureCodec;
mesh::SeenCache seenCache;
mesh::AckQueue ackQueue;
mesh::RelayQueue relayQueue;
mesh::DeliveryTracker delivery;
crew::PeerDirectory directory;
crew::MessageHistory history;
MobileHandler mobileHandler;
mobile::BleApi mobileApi;
uint16_t lastPacketOrigin = 0, lastPacketRelay = 0;
uint8_t lastPacketHop = 0;
int16_t lastPacketRssi = 0;
mesh::PacketType lastPacketType = mesh::PacketType::Invalid;
uint32_t mobileEventSequence = 0, lastMobilePeerRevision = 0;
uint32_t lastMobileMessageRevision = 0, lastMobileGpsEventAt = 0;
uint32_t lastMobileNodeSignature = 0;
bool mobileGpsDirty = true, mobileResyncRequired = false;
bool phoneForgetConfirm = false;
uint32_t phoneForgetDeadline = 0;
bool phoneDisconnectConfirm = false;
uint32_t phoneDisconnectDeadline = 0;
bool incomingBannerActive = false;
uint32_t incomingBannerDeadline = 0;
char incomingBannerName[mesh::MAX_NAME_LENGTH + 1] = {};
char incomingBannerText[25] = {};
Screen optionsOwner = Screen::Home;

uint32_t randomRange(uint32_t low, uint32_t high) {
  return high <= low ? low : low + esp_random() % (high - low + 1U);
}
uint32_t nextNonzero(uint32_t &value) {
  if (++value == 0) ++value;
  return value;
}
void prepareDisplayOperation() { digitalWrite(LORA_CS_PIN, HIGH); }
void prepareLoRaOperation() { digitalWrite(TFT_CS_PIN, HIGH); }
void IRAM_ATTR onRadioTxDone() { radioTxDone = true; }

void requestBeep(uint8_t pulses = 1, uint16_t onMs = 45,
                 uint16_t gapMs = 55) {
  if (!pulses) return;
  buzzer.active = true;
  buzzer.high = true;
  buzzer.pulsesRemaining = pulses;
  buzzer.onMs = onMs;
  buzzer.gapMs = gapMs;
  buzzer.deadline = millis() + onMs;
  digitalWrite(BUZZER_PIN, HIGH);
}
void serviceBuzzer(uint32_t now) {
  if (!buzzer.active || !mesh::timeReached(now, buzzer.deadline)) return;
  if (buzzer.high) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzer.high = false;
    if (buzzer.pulsesRemaining) --buzzer.pulsesRemaining;
    if (!buzzer.pulsesRemaining) buzzer.active = false;
    else buzzer.deadline = now + buzzer.gapMs;
  } else {
    digitalWrite(BUZZER_PIN, HIGH);
    buzzer.high = true;
    buzzer.deadline = now + buzzer.onMs;
  }
}

bool gpsCurrent() {
  return gps.location.isValid() && gps.location.age() <= GPS_STALE_MS;
}
const char *gpsLabel() {
  return gpsCurrent() ? "FIX" : gps.location.isValid() ? "STALE" : "SEARCH";
}
const char *deliveryLabel(mesh::DeliveryPhase phase) {
  switch (phase) {
    case mesh::DeliveryPhase::Backoff:
    case mesh::DeliveryPhase::AwaitingAck: return "Sending";
    case mesh::DeliveryPhase::Delivered: return "Delivered";
    case mesh::DeliveryPhase::Failed: return "Failed";
    default: return "Idle";
  }
}
bool deliveryBusy() {
  return delivery.phase() == mesh::DeliveryPhase::Backoff ||
         delivery.phase() == mesh::DeliveryPhase::AwaitingAck;
}
const char *peerName(uint16_t address, char *fallback, size_t capacity) {
  if (address == mesh::BROADCAST_ADDRESS) return "ALL CREW";
  const crew::PeerRecord *peer = directory.record(directory.find(address));
  if (peer && peer->used && peer->name[0]) return peer->name;
  snprintf(fallback, capacity, "#%u", address);
  return fallback;
}
void showScreen(Screen screen, bool resetSelection = false) {
  navigation.setSelection(listSelection);
  if (screen == Screen::Home) navigation.home();
  else navigation.open(screen, resetSelection);
  currentScreen = navigation.current();
  listSelection = navigation.selection();
  screenDirty = contentDirty = statusBarDirty = true;
}
void goBack() {
  navigation.setSelection(listSelection);
  navigation.back(); currentScreen = navigation.current();
  listSelection = navigation.selection();
  screenDirty = contentDirty = statusBarDirty = true;
}

enum class UiIcon : uint8_t {
  Home, Message, Inbox, Sent, Compose, Crew, Radar, Bluetooth, Status,
  Draft, Gps, Radio, Diagnostic, Check, Warning
};

void drawIcon(int16_t x, int16_t y, UiIcon icon, uint16_t color) {
  prepareDisplayOperation();
  switch (icon) {
    case UiIcon::Home:
      tft.drawLine(x, y + 7, x + 8, y, color); tft.drawLine(x + 8, y, x + 16, y + 7, color);
      tft.drawRect(x + 3, y + 7, 11, 9, color); break;
    case UiIcon::Message:
    case UiIcon::Inbox:
    case UiIcon::Sent:
      tft.drawRect(x, y + 2, 17, 12, color);
      tft.drawLine(x, y + 2, x + 8, y + 9, color);
      tft.drawLine(x + 17, y + 2, x + 8, y + 9, color);
      if (icon == UiIcon::Sent) { tft.drawLine(x + 10, y, x + 17, y, color); tft.drawLine(x + 17, y, x + 14, y + 3, color); }
      break;
    case UiIcon::Compose:
      tft.drawLine(x + 2, y + 14, x + 13, y + 3, color);
      tft.drawLine(x + 4, y + 16, x + 15, y + 5, color);
      tft.drawTriangle(x, y + 17, x + 2, y + 12, x + 5, y + 15, color); break;
    case UiIcon::Crew:
      tft.drawCircle(x + 5, y + 5, 3, color); tft.drawCircle(x + 13, y + 5, 3, color);
      tft.drawCircle(x + 9, y + 3, 3, color); tft.drawRoundRect(x, y + 10, 18, 7, 3, color); break;
    case UiIcon::Radar:
      tft.drawCircle(x + 8, y + 8, 8, color); tft.drawCircle(x + 8, y + 8, 4, color);
      tft.drawLine(x + 8, y + 8, x + 14, y + 3, color); break;
    case UiIcon::Bluetooth:
      tft.drawLine(x + 8, y, x + 8, y + 17, color); tft.drawLine(x + 8, y, x + 14, y + 5, color);
      tft.drawLine(x + 14, y + 5, x + 3, y + 13, color); tft.drawLine(x + 3, y + 4, x + 14, y + 13, color);
      tft.drawLine(x + 14, y + 13, x + 8, y + 17, color); break;
    case UiIcon::Status:
      tft.drawCircle(x + 8, y + 8, 8, color); tft.drawFastVLine(x + 8, y + 7, 6, color);
      tft.fillCircle(x + 8, y + 4, 1, color); break;
    case UiIcon::Draft:
      tft.drawRect(x + 2, y, 12, 17, color); tft.drawLine(x + 5, y + 5, x + 12, y + 5, color);
      tft.drawLine(x + 5, y + 9, x + 12, y + 9, color); break;
    case UiIcon::Gps:
      tft.drawCircle(x + 8, y + 8, 6, color); tft.drawFastVLine(x + 8, y, 17, color);
      tft.drawFastHLine(x, y + 8, 17, color); break;
    case UiIcon::Radio:
      tft.fillCircle(x + 8, y + 14, 2, color); tft.drawCircle(x + 8, y + 14, 6, color);
      tft.drawCircle(x + 8, y + 14, 11, color); break;
    case UiIcon::Diagnostic:
      tft.drawFastHLine(x, y + 9, 4, color); tft.drawLine(x + 4, y + 9, x + 7, y + 3, color);
      tft.drawLine(x + 7, y + 3, x + 10, y + 15, color); tft.drawLine(x + 10, y + 15, x + 13, y + 8, color);
      tft.drawFastHLine(x + 13, y + 8, 5, color); break;
    case UiIcon::Check:
      tft.drawLine(x + 1, y + 9, x + 6, y + 14, color); tft.drawLine(x + 6, y + 14, x + 16, y + 2, color); break;
    case UiIcon::Warning:
      tft.drawTriangle(x + 8, y, x, y + 16, x + 16, y + 16, color);
      tft.drawFastVLine(x + 8, y + 5, 6, color); tft.fillCircle(x + 8, y + 13, 1, color); break;
  }
}

void drawStatusBar() {
  prepareDisplayOperation();
  tft.fillRect(0, 0, SCREEN_WIDTH, 22, COLOR_BACKGROUND);
  tft.drawFastHLine(0, 21, SCREEN_WIDTH, COLOR_DARK_GREY);
  char uptime[12]; ui::formatUptime(millis(), uptime, sizeof(uptime));
  tft.setTextWrap(false); tft.setTextSize(1); tft.setTextColor(COLOR_TEXT);
  tft.setCursor(4, 7); tft.print(uptime);
  tft.setCursor(88, 7); tft.setTextColor(COLOR_ORANGE);
  if (!loraReady) tft.print("LORA ERR");
  else if (radioTxInProgress) tft.print("LORA TX");
  else if (lastRadioActivityAt && !mesh::intervalElapsed(millis(), lastRadioActivityAt, 120000UL)) tft.print("LORA RX");
  else tft.print("LORA READY");
  tft.setCursor(261, 7); tft.setTextColor(gpsCurrent() ? COLOR_GREEN : COLOR_MID_GREY);
  tft.print(gpsCurrent() ? "G" : "-");
  tft.setCursor(279, 7); tft.setTextColor(mobileApi.authorized() ? COLOR_GREEN : COLOR_MID_GREY);
  tft.print(mobileApi.authorized() ? "BT" : "--");
  tft.setCursor(301, 7); tft.setTextColor(history.unreadCount() ? COLOR_AMBER : COLOR_MID_GREY);
  tft.print(history.unreadCount());
}

void drawHeader(const char *title, UiIcon icon) {
  prepareDisplayOperation();
  tft.fillScreen(COLOR_BACKGROUND);
  drawStatusBar();
  tft.fillRect(0, 22, SCREEN_WIDTH, 34, COLOR_SURFACE);
  tft.drawFastHLine(0, 55, SCREEN_WIDTH, COLOR_ORANGE);
  drawIcon(8, 30, icon, COLOR_ORANGE);
  tft.setTextWrap(false); tft.setTextColor(COLOR_ORANGE); tft.setTextSize(2);
  tft.setCursor(33, 31); tft.print(title);
}
void drawFooter(const char *top, const char *bottom = nullptr) {
  prepareDisplayOperation();
  tft.fillRect(0, 206, SCREEN_WIDTH, 34, COLOR_BACKGROUND);
  tft.drawFastHLine(0, 205, SCREEN_WIDTH, COLOR_DARK_GREY);
  tft.setTextColor(COLOR_TEXT); tft.setTextSize(1);
  tft.setCursor(6, 212); tft.print(top);
  if (bottom) { tft.setCursor(6, 226); tft.print(bottom); }
}
void clearBody() {
  prepareDisplayOperation();
  tft.setTextWrap(false); tft.setTextSize(1); tft.setTextColor(COLOR_TEXT);
}
void printAddress(uint16_t address) { tft.print('#'); tft.print(address); }

void drawSignalBars(int16_t x, int16_t y, uint8_t bars, uint16_t color) {
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t height = 4 + i * 3;
    if (i < bars) tft.fillRect(x + i * 5, y + 13 - height, 3, height, color);
    else tft.drawRect(x + i * 5, y + 13 - height, 3, height, COLOR_DARK_GREY);
  }
}

void drawMenuRow(int16_t y, const char *label, bool selected, bool enabled,
                 const char *value = nullptr, UiIcon icon = UiIcon::Status) {
  prepareDisplayOperation();
  const uint16_t background = COLOR_SURFACE;
  tft.fillRoundRect(5, y, 310, 26, 3, background);
  tft.drawRoundRect(5, y, 310, 26, 3, selected ? COLOR_ORANGE : COLOR_SURFACE);
  drawIcon(11, y + 5, icon, enabled ? (selected ? COLOR_ORANGE : COLOR_TEXT) : COLOR_DARK_GREY);
  tft.setTextSize(1); tft.setTextColor(enabled ? COLOR_TEXT : COLOR_DARK_GREY);
  tft.setCursor(36, y + 9); tft.print(label);
  if (value) {
    const int16_t width = static_cast<int16_t>(strlen(value) * 6);
    tft.setCursor(309 - width, y + 9); tft.setTextColor(enabled ? COLOR_ORANGE : COLOR_DARK_GREY);
    tft.print(value);
  }
}

size_t buildPeerOrder(int order[crew::MAX_PEERS], bool radarOnly = false) {
  size_t count = 0;
  for (size_t i = 0; i < crew::MAX_PEERS; ++i) {
    const crew::PeerRecord *p = directory.record(i);
    if (p && p->used && (!radarOnly || (p->online && p->hasLocation)))
      order[count++] = static_cast<int>(i);
  }
  for (size_t i = 0; i < count; ++i) for (size_t j = i + 1; j < count; ++j) {
    const crew::PeerRecord *a = directory.record(order[i]);
    const crew::PeerRecord *b = directory.record(order[j]);
    const bool swap = a->online != b->online ? !a->online && b->online
                                             : a->address > b->address;
    if (swap) { int temp = order[i]; order[i] = order[j]; order[j] = temp; }
  }
  return count;
}

void renderHome() {
  clearBody();
  tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  tft.setTextSize(2); tft.setTextColor(COLOR_ORANGE); tft.setCursor(9, 62);
  tft.print(device_config::DEVICE_NAME);
  tft.setTextSize(1); tft.setTextColor(COLOR_TEXT); tft.setCursor(233, 65);
  printAddress(device_config::DEVICE_ADDRESS);
  tft.setCursor(233, 78); tft.print(deliveryLabel(delivery.phase()));
  tft.setCursor(9, 91); tft.setTextColor(loraReady ? COLOR_GREEN : COLOR_RED);
  tft.print("LORA "); tft.print(loraReady ? "READY" : "ERROR");
  tft.setCursor(111, 91); tft.setTextColor(gpsCurrent() ? COLOR_GREEN : COLOR_MID_GREY);
  tft.print("GPS "); tft.print(gpsLabel());
  tft.setCursor(221, 91); tft.setTextColor(mobileApi.authorized() ? COLOR_GREEN : COLOR_MID_GREY);
  tft.print("BT "); tft.print(mobileApi.authorized() ? "ON" : "OFF");
  const char *items[] = {"Messages", "Radar", "Crew", "Bluetooth", "Device Status", "New Message"};
  const UiIcon icons[] = {UiIcon::Message, UiIcon::Radar, UiIcon::Crew,
                          UiIcon::Bluetooth, UiIcon::Status, UiIcon::Compose};
  for (uint8_t i = 0; i < 6; ++i) {
    const int16_t x = 5 + (i % 2) * 158, y = 111 + (i / 2) * 30;
    tft.fillRoundRect(x, y, 152, 27, 3, COLOR_BACKGROUND);
    tft.drawRoundRect(x, y, 152, 27, 3, COLOR_SURFACE);
    tft.setTextColor(COLOR_ORANGE); tft.setCursor(x + 6, y + 10); tft.print(i + 1);
    drawIcon(x + 20, y + 5, icons[i], COLOR_TEXT);
    tft.setTextColor(COLOR_TEXT); tft.setCursor(x + 42, y + 10); tft.print(items[i]);
    if (i == 0 && history.unreadCount()) {
      tft.setTextColor(COLOR_AMBER); tft.setCursor(x + 133, y + 10);
      tft.print(history.unreadCount());
    }
  }
  if (localAddressConflict) {
    tft.fillRect(180, 78, 135, 11, COLOR_RED); tft.setTextColor(COLOR_TEXT);
    tft.setCursor(184, 80); tft.print("ADDRESS CONFLICT");
  }
  if (incomingBannerActive && !mesh::timeReached(millis(), incomingBannerDeadline)) {
    tft.fillRoundRect(5, 58, 310, 43, 4, COLOR_SURFACE);
    tft.drawRoundRect(5, 58, 310, 43, 4, COLOR_ORANGE);
    tft.setTextColor(COLOR_ORANGE); tft.setCursor(12, 66);
    tft.print("NEW FROM "); tft.print(incomingBannerName);
    tft.setTextColor(COLOR_TEXT); tft.setCursor(12, 84); tft.print(incomingBannerText);
  }
}

void renderMessagesHub() {
  clearBody();
  const size_t inbox = ui::filteredMessageCount(history, crew::MessageDirection::Incoming);
  const size_t sent = ui::filteredMessageCount(history, crew::MessageDirection::Outgoing);
  if (listSelection >= 5) listSelection = 0;
  char inboxValue[8], sentValue[8];
  snprintf(inboxValue, sizeof(inboxValue), "%u", static_cast<unsigned>(inbox));
  snprintf(sentValue, sizeof(sentValue), "%u", static_cast<unsigned>(sent));
  const char *labels[] = {"Inbox", "Sent", "New Direct Message", "Broadcast", "Resume Draft"};
  const char *values[] = {inboxValue, sentValue, nullptr, nullptr, composer.length() ? "SAVED" : "EMPTY"};
  const UiIcon icons[] = {UiIcon::Inbox, UiIcon::Sent, UiIcon::Compose, UiIcon::Radio, UiIcon::Draft};
  for (uint8_t i = 0; i < 5; ++i)
    drawMenuRow(60 + i * 29, labels[i], i == listSelection,
                i != 4 || composer.length(), values[i], icons[i]);
}

void renderMessages(crew::MessageDirection direction) {
  clearBody();
  tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  const size_t count = ui::filteredMessageCount(history, direction);
  if (!count) { tft.setCursor(8, 68); tft.print("No messages yet."); return; }
  if (listSelection >= count) listSelection = count - 1;
  constexpr uint8_t rowsVisible = 3;
  const uint8_t start = listSelection >= rowsVisible ? listSelection - rowsVisible + 1 : 0;
  for (uint8_t row = 0; row < rowsVisible; ++row) {
    const uint8_t offset = start + row;
    if (offset >= count) break;
    const crew::MessageRecord *m =
        history.record(ui::filteredMessageIndex(history, direction, offset));
    if (!m) continue;
    const int16_t y = 60 + row * 47;
    tft.fillRoundRect(5, y, 310, 43, 3, offset == listSelection ? COLOR_SURFACE : COLOR_BACKGROUND);
    tft.drawRoundRect(5, y, 310, 43, 3, offset == listSelection ? COLOR_ORANGE : COLOR_SURFACE);
    if (m->unread) tft.fillCircle(13, y + 12, 4, COLOR_ORANGE);
    drawIcon(20, y + 5, direction == crew::MessageDirection::Incoming ? UiIcon::Inbox : UiIcon::Sent,
             m->unread ? COLOR_AMBER : COLOR_TEXT);
    tft.setCursor(43, y + 7); tft.setTextColor(m->unread ? COLOR_AMBER : COLOR_TEXT);
    tft.print(m->peerName); tft.print(m->crewMessage ? " [CREW]" : " [DIRECT]");
    char age[8]; ui::formatAge(millis(), m->storedAt, age, sizeof(age));
    tft.setCursor(275, y + 7); tft.setTextColor(COLOR_MID_GREY); tft.print(age);
    char preview[43] = {}; strncpy(preview, m->text, sizeof(preview) - 1);
    tft.setCursor(11, y + 25); tft.setTextColor(COLOR_MID_GREY); tft.print(preview);
    if (m->direction == crew::MessageDirection::Outgoing) {
      tft.setCursor(290, y + 25);
      tft.setTextColor(m->delivery == crew::MessageDelivery::Delivered ? COLOR_GREEN
                       : m->delivery == crew::MessageDelivery::Failed ? COLOR_RED : COLOR_AMBER);
      tft.print(m->delivery == crew::MessageDelivery::Delivered ? "OK"
                : m->delivery == crew::MessageDelivery::Failed ? "X" : "...");
    }
  }
}

void renderMessageView() {
  clearBody();
  const crew::MessageRecord *m = history.record(selectedMessageIndex);
  if (!m || !m->used) { tft.setCursor(8, 66); tft.print("Message unavailable."); return; }
  tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  tft.fillRoundRect(5, 60, 310, 43, 3, COLOR_BACKGROUND);
  drawIcon(12, 67, m->direction == crew::MessageDirection::Incoming ? UiIcon::Inbox : UiIcon::Sent, COLOR_ORANGE);
  tft.setTextSize(1); tft.setTextColor(COLOR_ORANGE); tft.setCursor(38, 67);
  tft.print(m->direction == crew::MessageDirection::Incoming ? "FROM: " : "TO: ");
  tft.print(m->peerName);
  tft.setCursor(38, 84); tft.setTextColor(COLOR_TEXT);
  tft.print(m->crewMessage ? "CREW" : "DIRECT"); tft.print("   ID "); tft.print(m->messageId, HEX);
  char age[8]; ui::formatAge(millis(), m->storedAt, age, sizeof(age));
  tft.setCursor(277, 67); tft.setTextColor(COLOR_MID_GREY); tft.print(age);
  if (m->direction == crew::MessageDirection::Outgoing) {
    tft.setCursor(242, 84); tft.setTextColor(m->delivery == crew::MessageDelivery::Delivered ? COLOR_GREEN
      : m->delivery == crew::MessageDelivery::Failed ? COLOR_RED : COLOR_AMBER);
    tft.print(m->delivery == crew::MessageDelivery::Delivered ? "Delivered"
      : m->delivery == crew::MessageDelivery::Failed ? "Failed" : "Sending");
  }
  tft.drawRoundRect(5, 109, 310, 91, 3, COLOR_SURFACE);
  tft.setTextWrap(true); tft.setTextSize(2); tft.setTextColor(COLOR_TEXT);
  tft.setCursor(11, 119); tft.print(m->text);
  tft.setTextWrap(false); tft.setTextSize(1);
}

void renderCrew(bool recipients) {
  clearBody(); tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  int order[crew::MAX_PEERS]; const size_t peers = buildPeerOrder(order);
  const size_t count = peers + (recipients ? 1 : 0);
  if (!count) { tft.setCursor(8, 67); tft.print("No crew discovered yet."); return; }
  if (listSelection >= count) listSelection = count - 1;
  constexpr uint8_t rowsVisible = 4;
  const uint8_t start = listSelection >= rowsVisible ? listSelection - rowsVisible + 1 : 0;
  for (uint8_t row = 0; row < rowsVisible; ++row) {
    const uint8_t logical = start + row; if (logical >= count) break;
    const int16_t y = 59 + row * 36;
    tft.fillRoundRect(5, y, 310, 33, 3, logical == listSelection ? COLOR_SURFACE : COLOR_BACKGROUND);
    tft.drawRoundRect(5, y, 310, 33, 3, logical == listSelection ? COLOR_ORANGE : COLOR_SURFACE);
    if (recipients && logical == 0) {
      drawIcon(12, y + 8, UiIcon::Radio, COLOR_ORANGE);
      tft.setCursor(38, y + 12); tft.setTextColor(COLOR_ORANGE); tft.print("ALL CREW");
      tft.setCursor(272, y + 12); tft.print("GROUP"); continue;
    }
    const crew::PeerRecord *peer = directory.record(order[logical - (recipients ? 1 : 0)]);
    if (!peer) continue;
    drawSignalBars(11, y + 9, peer->online ? ui::rssiBars(peer->rssi) : 0,
                   peer->online ? COLOR_GREEN : COLOR_DARK_GREY);
    tft.setTextColor(peer->online ? COLOR_TEXT : COLOR_MID_GREY);
    tft.setCursor(36, y + 7); tft.print(peer->name);
    tft.setCursor(242, y + 7); printAddress(peer->address);
    if (peer->addressConflict) { tft.setCursor(302, y + 7); tft.setTextColor(COLOR_RED); tft.print('!'); }
    tft.setCursor(36, y + 20); tft.setTextColor(peer->online ? COLOR_MID_GREY : COLOR_DARK_GREY);
    if (peer->online) {
      tft.print("H"); tft.print(peer->hop); tft.print("  "); tft.print(peer->rssi); tft.print("dBm");
      if (gpsCurrent() && peer->hasLocation) {
        const radar::RelativePosition pos = radar::relativePosition(
            gps.location.lat(), gps.location.lng(), peer->latitudeE7 / 1e7, peer->longitudeE7 / 1e7);
        tft.setCursor(262, y + 20); tft.print(pos.distanceMeters, 0); tft.print('m');
      } else { tft.setCursor(282, y + 20); tft.print("--"); }
    } else { tft.print("OFFLINE "); tft.print((millis() - peer->lastSeenAt) / 1000UL); tft.print('s'); }
  }
}

void renderMember() {
  clearBody(); tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  const crew::PeerRecord *peer = directory.record(selectedPeerIndex);
  if (!peer || !peer->used) { tft.setCursor(8, 67); tft.print("Member unavailable."); return; }
  tft.setTextSize(2); tft.setTextColor(peer->online ? COLOR_ORANGE : COLOR_MID_GREY);
  tft.setCursor(10, 63); tft.print(peer->name);
  tft.setTextSize(1); tft.setTextColor(COLOR_TEXT); tft.setCursor(230, 67); printAddress(peer->address);
  tft.setCursor(10, 91); tft.print(peer->online ? "ONLINE" : "OFFLINE");
  tft.print("  last "); tft.print((millis() - peer->lastSeenAt) / 1000UL); tft.print('s');
  tft.setCursor(180, 91); tft.print("H"); tft.print(peer->hop);
  tft.print(" via #"); tft.print(peer->lastRelay);
  tft.setCursor(10, 112); tft.print("RSSI "); tft.print(peer->rssi); tft.print("dBm");
  tft.setCursor(130, 112); tft.print("Location: ");
  tft.print(!peer->hasLocation ? "none" : peer->gpsCurrent ? "current" : "last known");
  if (peer->hasLocation) {
    tft.setCursor(10, 133); tft.print(peer->latitudeE7 / 1e7, 6);
    tft.print(", "); tft.print(peer->longitudeE7 / 1e7, 6);
    tft.setCursor(238, 133); tft.print(directory.locationAgeSeconds(*peer, millis())); tft.print('s');
    if (gpsCurrent()) {
      radar::RelativePosition pos = radar::relativePosition(gps.location.lat(), gps.location.lng(),
                                      peer->latitudeE7 / 1e7, peer->longitudeE7 / 1e7);
      tft.setCursor(10, 154); tft.print("Distance "); tft.print(pos.distanceMeters, 0);
      tft.print("m bearing "); tft.print(pos.bearingDegrees, 0); tft.print(" deg T");
    }
  }
  tft.setCursor(10, 179); tft.print("HW ");
  for (uint8_t v : peer->hardwareId) { if (v < 16) tft.print('0'); tft.print(v, HEX); }
  if (peer->addressConflict) { tft.setCursor(190, 179); tft.setTextColor(COLOR_RED); tft.print("DUPLICATE ADDRESS"); }
}

void renderCompose() {
  clearBody(); char fallback[10];
  tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  tft.fillRoundRect(5, 60, 310, 29, 3, COLOR_BACKGROUND);
  tft.setCursor(12, 70); tft.setTextColor(COLOR_ORANGE); tft.print("TO: ");
  tft.setTextColor(COLOR_TEXT); tft.print(peerName(draftRecipient, fallback, sizeof(fallback)));
  tft.setCursor(240, 70); tft.setTextColor(COLOR_AMBER); tft.print(composer.modeLabel());
  tft.setCursor(287, 70); tft.setTextColor(COLOR_MID_GREY);
  tft.print(composer.length()); tft.print("/80");
  tft.drawRoundRect(5, 94, 310, 92, 3, COLOR_SURFACE); tft.setTextWrap(true);
  tft.setTextSize(2); tft.setCursor(11, 103);
  if (!composer.length()) { tft.setTextColor(COLOR_MID_GREY); tft.print("Type a message..."); }
  else { tft.setTextColor(COLOR_TEXT); tft.print(composer.text()); }
  tft.setTextWrap(false); tft.setTextSize(1);
  if (deliveryBusy()) { tft.setCursor(8, 191); tft.setTextColor(COLOR_AMBER); tft.print("Another message is sending"); }
  else if (localAddressConflict || !loraReady) {
    tft.setCursor(8, 191); tft.setTextColor(COLOR_RED);
    tft.print(localAddressConflict ? "Resolve address conflict" : "Radio unavailable");
  }
}
void renderComposeOptions() {
  clearBody(); tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  drawMenuRow(65, "Change recipient", listSelection == 0, true, nullptr, UiIcon::Crew);
  drawMenuRow(100, "Clear draft", listSelection == 1, composer.length(), nullptr, UiIcon::Draft);
  tft.setTextSize(1); tft.setTextColor(COLOR_MID_GREY); tft.setCursor(14, 152);
  tft.print("Draft stays in RAM when you leave.");
}
void renderSymbols() {
  clearBody(); tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  constexpr uint8_t columns = 6; const size_t count = strlen(input::SYMBOLS);
  for (size_t i = 0; i < count; ++i) {
    const int16_t x = 24 + (i % columns) * 49, y = 66 + (i / columns) * 27;
    tft.fillRect(x - 5, y - 3, 33, 25, i == symbolSelection ? COLOR_SURFACE : COLOR_BACKGROUND);
    tft.setTextSize(2); tft.setTextColor(i == symbolSelection ? COLOR_AMBER : COLOR_TEXT);
    tft.setCursor(x, y); tft.print(input::SYMBOLS[i]);
  }
}
void renderRadar() {
  clearBody(); tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  constexpr int16_t cx = 86, cy = 130, radius = 66;
  tft.drawCircle(cx, cy, radius, COLOR_GREEN);
  tft.drawCircle(cx, cy, radius * 2 / 3, COLOR_DARK_GREY);
  tft.drawCircle(cx, cy, radius / 3, COLOR_DARK_GREY);
  tft.drawFastVLine(cx, cy - radius, radius * 2, COLOR_DARK_GREY);
  tft.drawFastHLine(cx - radius, cy, radius * 2, COLOR_DARK_GREY);
  tft.setTextColor(COLOR_TEXT); tft.setCursor(cx - 3, cy - radius - 8); tft.print('N');
  tft.fillCircle(cx, cy, 4, COLOR_ORANGE);
  if (!gpsCurrent()) {
    tft.setTextColor(COLOR_AMBER); tft.setCursor(178, 114);
    tft.print("GPS not fixed"); return;
  }
  int order[crew::MAX_PEERS]; const size_t count = buildPeerOrder(order, true);
  if (!count) { tft.setCursor(165, 107); tft.print("No online members");
                tft.setCursor(165, 123); tft.print("have a location."); return; }
  if (listSelection >= count) listSelection = count - 1;
  radar::RelativePosition positions[crew::MAX_PEERS]; double farthest = 0;
  for (size_t i = 0; i < count; ++i) {
    const crew::PeerRecord *peer = directory.record(order[i]);
    positions[i] = radar::relativePosition(gps.location.lat(), gps.location.lng(),
                                            peer->latitudeE7 / 1e7, peer->longitudeE7 / 1e7);
    if (positions[i].distanceMeters > farthest) farthest = positions[i].distanceMeters;
  }
  const uint32_t range = radar::selectRangeMeters(farthest);
  for (size_t i = 0; i < count; ++i) {
    if (i == listSelection) continue;
    radar::PlotPoint point = radar::project(positions[i], range, cx, cy, radius - 4);
    tft.fillCircle(point.x, point.y, 3, COLOR_GREEN);
  }
  const radar::PlotPoint selectedPoint =
      radar::project(positions[listSelection], range, cx, cy, radius - 4);
  tft.fillCircle(selectedPoint.x, selectedPoint.y, 5, COLOR_AMBER);
  const crew::PeerRecord *peer = directory.record(order[listSelection]);
  const radar::RelativePosition &pos = positions[listSelection];
  tft.setCursor(165, 77); tft.setTextColor(COLOR_AMBER); tft.print(peer->name);
  tft.setCursor(165, 96); tft.print(pos.distanceMeters, 0); tft.print("m @ ");
  tft.print(pos.bearingDegrees, 0); tft.print("deg T");
  tft.setCursor(165, 129); tft.setTextColor(COLOR_TEXT); tft.print("Range ");
  if (range >= 1000) { tft.print(range / 1000.0, range % 1000 ? 1 : 0); tft.print("km"); }
  else { tft.print(range); tft.print('m'); }
  tft.setCursor(165, 149); tft.print("Age "); tft.print(directory.locationAgeSeconds(*peer, millis())); tft.print('s');
  tft.setCursor(165, 169); tft.print("H"); tft.print(peer->hop);
  tft.print("  RSSI "); tft.print(peer->rssi);
}
void renderStatus() {
  clearBody(); tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  char uptime[12]; ui::formatUptime(millis(), uptime, sizeof(uptime));
  char online[8], gpsState[18], messages[20];
  snprintf(online, sizeof(online), "%u/12", static_cast<unsigned>(directory.onlineCount(millis()) + 1));
  snprintf(gpsState, sizeof(gpsState), "%s (%lu)", gpsLabel(),
           static_cast<unsigned long>(gps.satellites.isValid() ? gps.satellites.value() : 0));
  snprintf(messages, sizeof(messages), "TX%lu RX%lu",
           static_cast<unsigned long>(counters.localMessages),
           static_cast<unsigned long>(counters.receivedMessages));
  const char *labels[] = {"LoRa", "GPS", "Bluetooth", "Online Nodes", "Uptime", "Messages", "Last Delivery"};
  const char *values[] = {loraReady ? "READY" : "ERROR", gpsState,
      !mobileApi.ready() ? "ERROR" : mobileApi.authorized() ? "SECURE" : mobileApi.connected() ? "AUTH" : "OFF",
      online, uptime, messages, deliveryLabel(delivery.phase())};
  for (uint8_t i = 0; i < 7; ++i) {
    const int16_t x = 8 + (i % 2) * 155, y = 61 + (i / 2) * 35;
    tft.setCursor(x, y); tft.setTextColor(COLOR_MID_GREY); tft.print(labels[i]);
    const bool good = (i == 0 && loraReady) || (i == 1 && gpsCurrent()) ||
                      (i == 2 && mobileApi.authorized());
    const bool fault = (i == 0 && !loraReady) || (i == 2 && !mobileApi.ready());
    tft.setCursor(x, y + 13); tft.setTextColor(good ? COLOR_GREEN : fault ? COLOR_RED
                                               : i < 3 ? COLOR_AMBER : COLOR_ORANGE);
    tft.print(values[i]);
  }
  tft.setTextColor(COLOR_ORANGE); tft.setCursor(190, 179); tft.print("D DIAGNOSTICS");
}

void renderDiagnostics() {
  clearBody(); tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  const uint8_t page = listSelection % 2;
  tft.setTextColor(COLOR_ORANGE); tft.setCursor(9, 64); tft.print("PAGE "); tft.print(page + 1); tft.print("/2");
  tft.setTextColor(COLOR_TEXT); int16_t y = 84;
#define DIAG_LINE() y += 16; tft.setCursor(9, y)
  if (page == 0) {
    tft.setCursor(9, y); tft.print(device_config::DEVICE_NAME); tft.print(" #"); tft.print(device_config::DEVICE_ADDRESS);
    tft.print(" HW "); tft.println(hardwareIdHex);
    DIAG_LINE(); tft.print("Crew "); tft.print(crew_config::CREW_ID, HEX); tft.print(" LC3 CCM8 "); tft.println(cryptoReady ? "OK" : "FAIL");
    DIAG_LINE(); tft.println("433MHz SW4A SF9 BW125k");
    DIAG_LINE(); tft.println("CR4/5 CRC ON TTL3");
    DIAG_LINE(); tft.print("Relay "); tft.print(counters.relayed); tft.print(" Retry "); tft.print(counters.retries); tft.print(" ACK "); tft.println(counters.acksSent);
    DIAG_LINE(); tft.print("Duplicate "); tft.print(counters.duplicates); tft.print(" Presence "); tft.println(counters.presenceSent);
    DIAG_LINE(); tft.print("Frame sequence "); tft.println(frameSequence);
  } else {
    tft.setCursor(9, y); tft.print("Malformed "); tft.print(counters.malformed); tft.print(" Auth "); tft.println(counters.authFailures);
    DIAG_LINE(); tft.print("Wrong crew "); tft.print(counters.wrongCrew); tft.print(" Drops "); tft.println(counters.queueDrops);
    DIAG_LINE(); tft.print("Conflicts "); tft.print(counters.conflicts); tft.print(" BLE RX/TX "); tft.print(mobileApi.rxDrops()); tft.print('/'); tft.println(mobileApi.txDrops());
    DIAG_LINE(); tft.print("Last type "); tft.print(static_cast<uint8_t>(lastPacketType)); tft.print(" origin #"); tft.println(lastPacketOrigin);
    DIAG_LINE(); tft.print("Via #"); tft.print(lastPacketRelay); tft.print(" hop "); tft.print(lastPacketHop); tft.print(" RSSI "); tft.println(lastPacketRssi);
    DIAG_LINE(); tft.print("GPS "); tft.print(gpsLabel()); tft.print(" sats "); tft.println(gps.satellites.isValid() ? gps.satellites.value() : 0);
    if (gps.location.isValid()) { DIAG_LINE(); tft.print(gps.location.lat(), 5); tft.print(','); tft.println(gps.location.lng(), 5); }
  }
#undef DIAG_LINE
}

void renderPhone() {
  clearBody(); tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  tft.setTextColor(COLOR_ORANGE); tft.setCursor(10, 63); tft.print("CL3-");
  tft.print(device_config::DEVICE_ADDRESS); tft.print('-'); tft.print(device_config::DEVICE_NAME);
  tft.setTextColor(COLOR_TEXT); tft.setCursor(10, 80); tft.print("Bonded phones: "); tft.print(mobileApi.bondCount());
  tft.setCursor(175, 80); tft.print("Connection: ");
  tft.setTextColor(!mobileApi.ready() ? COLOR_RED : mobileApi.authorized() ? COLOR_GREEN : COLOR_MID_GREY);
  tft.print(!mobileApi.ready() ? "ERROR" : mobileApi.authorized() ? "SECURE" :
            mobileApi.connected() ? "AUTH" : "OFF");
  if (mobileApi.pairing()) {
    tft.setCursor(20, 112); tft.setTextColor(COLOR_AMBER);
    tft.print("Enter this code on phone:");
    char passkey[7]; snprintf(passkey, sizeof(passkey), "%06lu",
                             static_cast<unsigned long>(mobileApi.passkey()));
    tft.setTextSize(3); tft.setCursor(188, 105); tft.print(passkey);
    tft.setTextSize(1); tft.setCursor(20, 160); tft.print("Expires in ");
    tft.print(mobileApi.pairingSecondsRemaining(millis())); tft.print(" seconds");
  } else {
    drawMenuRow(101, "Pair new phone", listSelection == 0,
                mobileApi.ready() && !mobileApi.connected() && mobileApi.bondCount() == 0 && !mobileApi.bondFault(), nullptr, UiIcon::Bluetooth);
    drawMenuRow(134, "Disconnect phone", listSelection == 1,
                mobileApi.connected(), nullptr, UiIcon::Warning);
    drawMenuRow(167, "Forget trusted phone", listSelection == 2,
                mobileApi.bondCount() || mobileApi.bondFault(), nullptr, UiIcon::Warning);
  }
  if (phoneForgetConfirm || phoneDisconnectConfirm) {
    tft.fillRoundRect(5, 173, 310, 29, 3, COLOR_RED);
    tft.setTextColor(COLOR_TEXT); tft.setCursor(11, 183);
    tft.print("Press D again to "); tft.print(phoneForgetConfirm ? "FORGET" : "DISCONNECT");
  }
}

struct OptionItem { const char *label; bool enabled; UiIcon icon; };

size_t buildOptions(OptionItem items[4]) {
  switch (optionsOwner) {
    case Screen::Home:
      items[0] = {"New direct message", true, UiIcon::Compose};
      items[1] = {"Broadcast", true, UiIcon::Radio};
      items[2] = {"Refresh presence", loraReady, UiIcon::Radio}; return 3;
    case Screen::MessagesHub:
    case Screen::Inbox:
    case Screen::Sent:
      items[0] = {"New direct message", true, UiIcon::Compose};
      items[1] = {"Broadcast", true, UiIcon::Radio};
      items[2] = {"Mark all read", history.unreadCount() > 0, UiIcon::Check}; return 3;
    case Screen::MessageView:
      items[0] = {"Reply", selectedMessageIndex >= 0, UiIcon::Compose};
      items[1] = {"Toggle read state",
                  selectedMessageIndex >= 0 && history.record(selectedMessageIndex) &&
                      history.record(selectedMessageIndex)->direction == crew::MessageDirection::Incoming,
                  UiIcon::Check}; return 2;
    case Screen::Crew:
    case Screen::Member:
    case Screen::Radar: {
      const crew::PeerRecord *peer = directory.record(selectedPeerIndex);
      items[0] = {"Message member", peer && peer->used && peer->online && !peer->addressConflict, UiIcon::Compose};
      items[1] = {"Member details", peer && peer->used && optionsOwner != Screen::Member, UiIcon::Crew};
      items[2] = {"View radar", peer && peer->used && peer->online && peer->hasLocation, UiIcon::Radar};
      items[3] = {"Refresh presence", loraReady, UiIcon::Radio}; return 4;
    }
    case Screen::Phone:
      items[0] = {"Disconnect phone", mobileApi.connected(), UiIcon::Bluetooth};
      items[1] = {"Forget trusted phone", mobileApi.bondCount() || mobileApi.bondFault(), UiIcon::Warning}; return 2;
    default:
      items[0] = {"Diagnostics", true, UiIcon::Diagnostic};
      items[1] = {"Bluetooth", true, UiIcon::Bluetooth};
      items[2] = {"Refresh presence", loraReady, UiIcon::Radio}; return 3;
  }
}

void renderOptions() {
  clearBody(); tft.fillRect(0, 56, SCREEN_WIDTH, 149, COLOR_BACKGROUND);
  OptionItem items[4]; const size_t count = buildOptions(items);
  if (listSelection >= count) listSelection = 0;
  for (size_t i = 0; i < count; ++i)
    drawMenuRow(61 + i * 34, items[i].label, i == listSelection,
                items[i].enabled, nullptr, items[i].icon);
}

void drawScreenFrame() {
  switch (currentScreen) {
    case Screen::Home: drawHeader("SHONGKET", UiIcon::Home); drawFooter("1-6 Shortcuts   # Options", "* Home"); break;
    case Screen::MessagesHub: drawHeader("MESSAGES", UiIcon::Message); drawFooter("A Up  B Down  D Select", "C Back  * Home  # Options"); break;
    case Screen::Inbox: drawHeader("INBOX", UiIcon::Inbox); drawFooter("A Up  B Down  D Open", "C Back  * Home  # Options"); break;
    case Screen::Sent: drawHeader("SENT", UiIcon::Sent); drawFooter("A Up  B Down  D Open", "C Back  * Home  # Options"); break;
    case Screen::MessageView: drawHeader("MESSAGE", UiIcon::Message); drawFooter("# Options", "C Back  * Home"); break;
    case Screen::Crew: drawHeader("CREW", UiIcon::Crew); drawFooter("A Up  B Down  D Details", "C Back  * Home  # Options"); break;
    case Screen::Member: drawHeader("MEMBER INFO", UiIcon::Status); drawFooter("# Options", "C Back  * Home"); break;
    case Screen::Recipients: drawHeader("RECIPIENT", UiIcon::Crew); drawFooter("A Up  B Down  D Select", "C Back  * Home"); break;
    case Screen::Compose: drawHeader("NEW MESSAGE", UiIcon::Compose); drawFooter("A Mode B Options * Symbols", "C Delete # Send D Save/Back"); break;
    case Screen::ComposeOptions: drawHeader("COMPOSE OPTIONS", UiIcon::Draft); drawFooter("A Up  B Down  D Select", "C Back"); break;
    case Screen::Symbols: drawHeader("SYMBOLS", UiIcon::Compose); drawFooter("2/8/4/6 Move  5 Insert", "D Close"); break;
    case Screen::Radar: drawHeader("NORTH-UP RADAR", UiIcon::Radar); drawFooter("A/B Select  D Details", "C Back  * Home  # Options"); break;
    case Screen::Phone: drawHeader("BLUETOOTH", UiIcon::Bluetooth); drawFooter("A Up  B Down  D Select", "C Back  * Home  # Options"); break;
    case Screen::Status: drawHeader("DEVICE STATUS", UiIcon::Status); drawFooter("D Diagnostics  # Options", "C Back  * Home"); break;
    case Screen::Diagnostics: drawHeader("DIAGNOSTICS", UiIcon::Diagnostic); drawFooter("A/B Change page", "C Back  * Home"); break;
    case Screen::Options: drawHeader("QUICK OPTIONS", UiIcon::Status); drawFooter("A Up  B Down  D Select", "C Back  * Home"); break;
    case Screen::Count: break;
  }
}
void renderScreenBody() {
  switch (currentScreen) {
    case Screen::Home: renderHome(); break;
    case Screen::MessagesHub: renderMessagesHub(); break;
    case Screen::Inbox: renderMessages(crew::MessageDirection::Incoming); break;
    case Screen::Sent: renderMessages(crew::MessageDirection::Outgoing); break;
    case Screen::MessageView: renderMessageView(); break;
    case Screen::Crew: renderCrew(false); break;
    case Screen::Member: renderMember(); break;
    case Screen::Recipients: renderCrew(true); break;
    case Screen::Compose: renderCompose(); break;
    case Screen::ComposeOptions: renderComposeOptions(); break;
    case Screen::Symbols: renderSymbols(); break;
    case Screen::Radar: renderRadar(); break;
    case Screen::Status: renderStatus(); break;
    case Screen::Phone: renderPhone(); break;
    case Screen::Diagnostics: renderDiagnostics(); break;
    case Screen::Options: renderOptions(); break;
    case Screen::Count: break;
  }
}
void serviceDisplay() {
  // The LoRa library's TX-done ISR touches SPI. Defer every TFT transfer until
  // that short interrupt-enabled TX period is over.
  if (radioTxInProgress) return;
  if (screenDirty) { screenDirty = false; drawScreenFrame(); contentDirty = true; statusBarDirty = false; }
  else if (statusBarDirty) { statusBarDirty = false; drawStatusBar(); }
  if (contentDirty) { contentDirty = false; renderScreenBody(); }
}

void selectListDelta(int delta, size_t count) {
  navigation.setSelection(listSelection);
  if (navigation.move(delta, count)) {
    listSelection = navigation.selection(); contentDirty = true;
  }
}
void openComposeFor(uint16_t recipient, bool explicitChange = false) {
  if (recipient != mesh::BROADCAST_ADDRESS) {
    const crew::PeerRecord *p = directory.record(directory.find(recipient));
    if (!p || !p->online || p->addressConflict) { requestBeep(2, 35, 45); return; }
  }
  if (!composer.length() || explicitChange) draftRecipient = recipient;
  if (currentScreen == Screen::Recipients) {
    const bool returning = recipientReturnsToCompose;
    recipientReturnsToCompose = false;
    goBack();
    if (returning && currentScreen == Screen::ComposeOptions) goBack();
    if (currentScreen != Screen::Compose) showScreen(Screen::Compose);
    else contentDirty = true;
  } else {
    showScreen(Screen::Compose);
  }
}
void updateTrackedHistory(crew::MessageDelivery state) {
  crew::MessageRecord *record = history.record(delivery.historyIndex());
  if (record && record->used && record->messageId == delivery.messageId())
    history.updateDelivery(delivery.historyIndex(), state);
}
void confirmDelivery(uint32_t messageId, const char *evidence) {
  if (!delivery.confirm(messageId)) return;
  updateTrackedHistory(crew::MessageDelivery::Delivered);
  Serial.printf("[DELIVERY] %08lX via %s\n", static_cast<unsigned long>(messageId), evidence);
  requestBeep(1, 75, 50); contentDirty = true;
}
enum class OriginSendResult : uint8_t {
  Ok = 0, Busy, RadioUnavailable, AddressConflict, InvalidDestination,
  PeerOffline, InvalidText,
};

OriginSendResult queueOriginatedMessage(uint16_t destination, const char *text,
                                        crew::MessageSource source,
                                        uint32_t now, uint32_t &messageId) {
  messageId = 0;
  if (deliveryBusy()) return OriginSendResult::Busy;
  if (!loraReady || !cryptoReady) return OriginSendResult::RadioUnavailable;
  if (localAddressConflict) return OriginSendResult::AddressConflict;
  const size_t textLength = text == nullptr
                                ? 0 : strnlen(text, mesh::MAX_TEXT_LENGTH + 1);
  if (textLength == 0 || textLength > mesh::MAX_TEXT_LENGTH) {
    return OriginSendResult::InvalidText;
  }
  for (size_t i = 0; i < textLength; ++i) {
    const uint8_t character = static_cast<uint8_t>(text[i]);
    if (character < 0x20 || character > 0x7e) {
      return OriginSendResult::InvalidText;
    }
  }
  if (destination != mesh::BROADCAST_ADDRESS) {
    if (!mesh::validNodeAddress(destination) ||
        destination == device_config::DEVICE_ADDRESS) {
      return OriginSendResult::InvalidDestination;
    }
    const crew::PeerRecord *p = directory.record(directory.find(destination));
    if (!p || !p->online) return OriginSendResult::PeerOffline;
    if (p->addressConflict) return OriginSendResult::AddressConflict;
  }
  crew::MessageRecord record;
  record.used = true; record.direction = crew::MessageDirection::Outgoing;
  record.source = source;
  record.crewMessage = destination == mesh::BROADCAST_ADDRESS;
  record.peerAddress = destination; record.originAddress = device_config::DEVICE_ADDRESS;
  char fallback[10]; strncpy(record.peerName, peerName(destination, fallback, sizeof(fallback)),
                             sizeof(record.peerName) - 1);
  record.messageId = nextNonzero(nextMessageId); record.timestamp = now / 1000UL;
  record.storedAt = now;
  record.delivery = crew::MessageDelivery::Sending;
  strncpy(record.text, text, sizeof(record.text) - 1);
  const int historyIndex = history.add(record);
  strncpy(activeOutgoingText, text, sizeof(activeOutgoingText) - 1);
  delivery.start(record.messageId, destination, record.crewMessage, historyIndex,
                 now, randomRange(150, 650));
  ++counters.localMessages; messageId = record.messageId;
  Serial.printf("[MSG] queued %08lX dest=%u %s\n",
                static_cast<unsigned long>(record.messageId), destination,
                record.crewMessage ? "crew" : "direct");
  contentDirty = true;
  return OriginSendResult::Ok;
}

bool queueLocalMessage(uint32_t now) {
  uint32_t messageId = 0;
  const OriginSendResult result = queueOriginatedMessage(
      draftRecipient, composer.text(), crew::MessageSource::Keypad, now,
      messageId);
  if (result != OriginSendResult::Ok) {
    requestBeep(2, 35, 45); contentDirty = true; return false;
  }
  composer.clear();
  showScreen(Screen::Home); return true;
}

const char *messageSourceLabel(crew::MessageSource source) {
  switch (source) {
    case crew::MessageSource::Keypad: return "keypad";
    case crew::MessageSource::Mobile: return "mobile";
    default: return "radio";
  }
}

const char *messageDeliveryLabel(crew::MessageDelivery state) {
  switch (state) {
    case crew::MessageDelivery::Sending: return "sending";
    case crew::MessageDelivery::Delivered: return "delivered";
    case crew::MessageDelivery::Failed: return "failed";
    default: return "received";
  }
}

template <size_t Capacity>
bool queueMobileJson(mobile::BleApi &api, uint16_t frameId,
                     StaticJsonDocument<Capacity> &document,
                     bool event = false) {
  if (measureJson(document) > mobile::MAX_JSON_SIZE) return false;
  char output[mobile::MAX_JSON_SIZE + 1];
  const size_t length = serializeJson(document, output, sizeof(output));
  if (length == 0 || length > mobile::MAX_JSON_SIZE) return false;
  return event ? api.queueEvent(output) : api.queueResponse(frameId, output);
}

void sendMobileError(mobile::BleApi &api, uint16_t frameId,
                     const char *code, const char *message) {
  StaticJsonDocument<192> response;
  response["v"] = 1; response["ok"] = false;
  JsonObject error = response.createNestedObject("error");
  error["code"] = code; error["message"] = message;
  queueMobileJson(api, frameId, response);
}

void addMessageSummary(JsonObject output, const crew::MessageRecord &message,
                       uint32_t now) {
  output["origin"] = message.originAddress;
  output["id"] = message.messageId;
  output["dir"] = message.direction == crew::MessageDirection::Incoming ? "in" : "out";
  output["kind"] = message.crewMessage ? "crew" : "direct";
  output["peer"] = message.peerAddress;
  output["name"] = message.peerName;
  char preview[16] = {};
  strncpy(preview, message.text, sizeof(preview) - 1);
  output["preview"] = preview;
  output["unread"] = message.unread;
  output["delivery"] = messageDeliveryLabel(message.delivery);
  output["ageSeconds"] = ui::ageSeconds(now, message.storedAt);
}

const char *sendErrorCode(OriginSendResult result) {
  switch (result) {
    case OriginSendResult::Busy: return "BUSY";
    case OriginSendResult::RadioUnavailable: return "RADIO_UNAVAILABLE";
    case OriginSendResult::AddressConflict: return "ADDRESS_CONFLICT";
    case OriginSendResult::InvalidDestination: return "INVALID_DESTINATION";
    case OriginSendResult::PeerOffline: return "PEER_OFFLINE";
    default: return "INVALID_TEXT";
  }
}

void MobileHandler::handleRequest(uint16_t frameId, const char *json,
                                  size_t length, mobile::BleApi &api) {
  StaticJsonDocument<768> request;
  const DeserializationError parseError = deserializeJson(request, json, length);
  if (parseError) {
    sendMobileError(api, frameId, "INVALID_JSON", "Request is not valid JSON");
    return;
  }
  if ((request["v"] | 0) != 1) {
    sendMobileError(api, frameId, "UNSUPPORTED_VERSION", "API version must be 1");
    return;
  }
  const char *operation = request["op"] | "";
  JsonObjectConst arguments = request["args"].as<JsonObjectConst>();
  const uint32_t now = millis();

  if (!strcmp(operation, "hello")) {
    StaticJsonDocument<512> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["api"] = 1; data["protocol"] = "LC3";
    char session[mesh::SESSION_ID_SIZE * 2 + 1] = {};
    for (size_t i = 0; i < mesh::SESSION_ID_SIZE; ++i)
      snprintf(session + i * 2, 3, "%02X", bootSession.bytes[i]);
    data["session"] = session; data["eventSeq"] = mobileEventSequence;
    data["peerRevision"] = directory.revision();
    data["messageRevision"] = history.revision();
    JsonArray capabilities = data.createNestedArray("capabilities");
    capabilities.add("directory"); capabilities.add("messages");
    capabilities.add("gps"); capabilities.add("status");
    queueMobileJson(api, frameId, response); return;
  }

  if (!strcmp(operation, "get_node")) {
    StaticJsonDocument<512> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["address"] = device_config::DEVICE_ADDRESS;
    data["name"] = device_config::DEVICE_NAME; data["hardwareId"] = hardwareIdHex;
    char crewId[9]; snprintf(crewId, sizeof(crewId), "%08lX",
                            static_cast<unsigned long>(crew_config::CREW_ID));
    data["crewId"] = crewId; data["uptime"] = now / 1000UL;
    data["radio"] = loraReady; data["crypto"] = cryptoReady;
    data["config"] = configReady; data["addressConflict"] = localAddressConflict;
    data["online"] = directory.onlineCount(now) + 1;
    data["unread"] = history.unreadCount();
    data["phoneConnected"] = api.authorized();
    queueMobileJson(api, frameId, response); return;
  }

  if (!strcmp(operation, "list_peers")) {
    const size_t offset = arguments["offset"] | 0U;
    size_t limit = arguments["limit"] | 3U;
    if (limit == 0) limit = 1; if (limit > 3) limit = 3;
    int order[crew::MAX_PEERS]; const size_t count = buildPeerOrder(order);
    StaticJsonDocument<768> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["revision"] = directory.revision(); data["total"] = count;
    JsonArray items = data.createNestedArray("items");
    for (size_t i = offset; i < count && i < offset + limit; ++i) {
      const crew::PeerRecord *peer = directory.record(order[i]);
      if (!peer) continue;
      JsonObject item = items.createNestedObject();
      item["address"] = peer->address; item["name"] = peer->name;
      item["online"] = peer->online; item["conflict"] = peer->addressConflict;
      item["hop"] = peer->hop; item["rssi"] = peer->rssi;
      item["location"] = peer->hasLocation;
    }
    const size_t next = offset + items.size();
    if (next < count) data["nextOffset"] = next;
    if (!queueMobileJson(api, frameId, response))
      sendMobileError(api, frameId, "RESPONSE_TOO_LARGE", "Use a smaller page");
    return;
  }

  if (!strcmp(operation, "get_peer")) {
    const uint16_t address = arguments["address"] | 0U;
    const crew::PeerRecord *peer = directory.record(directory.find(address));
    if (!peer || !peer->used) {
      sendMobileError(api, frameId, "NOT_FOUND", "Peer not found"); return;
    }
    StaticJsonDocument<640> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["address"] = peer->address; data["name"] = peer->name;
    data["online"] = peer->online; data["conflict"] = peer->addressConflict;
    data["lastSeenAge"] = (now - peer->lastSeenAt) / 1000UL;
    data["hop"] = peer->hop; data["lastRelay"] = peer->lastRelay;
    data["rssi"] = peer->rssi;
    JsonObject location = data.createNestedObject("location");
    location["available"] = peer->hasLocation;
    if (peer->hasLocation) {
      location["current"] = peer->gpsCurrent;
      location["lat"] = peer->latitudeE7 / 1e7;
      location["lon"] = peer->longitudeE7 / 1e7;
      location["satellites"] = peer->satellites;
      location["age"] = directory.locationAgeSeconds(*peer, now);
      if (gpsCurrent()) {
        const radar::RelativePosition relative = radar::relativePosition(
            gps.location.lat(), gps.location.lng(), peer->latitudeE7 / 1e7,
            peer->longitudeE7 / 1e7);
        location["distanceM"] = static_cast<uint32_t>(lround(relative.distanceMeters));
        location["bearing"] = static_cast<uint16_t>(lround(relative.bearingDegrees));
      }
    }
    if (!queueMobileJson(api, frameId, response))
      sendMobileError(api, frameId, "RESPONSE_TOO_LARGE", "Peer response is too large");
    return;
  }

  if (!strcmp(operation, "list_messages")) {
    const size_t offset = arguments["offset"] | 0U;
    size_t limit = arguments["limit"] | 2U;
    if (limit == 0) limit = 1; if (limit > 2) limit = 2;
    StaticJsonDocument<1024> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["revision"] = history.revision(); data["total"] = history.count();
    JsonArray items = data.createNestedArray("items");
    for (size_t i = offset; i < history.count() && i < offset + limit; ++i) {
      const crew::MessageRecord *message =
          history.record(history.physicalIndexForNewest(i));
      if (message) addMessageSummary(items.createNestedObject(), *message, now);
    }
    const size_t next = offset + items.size();
    if (next < history.count()) data["nextOffset"] = next;
    if (!queueMobileJson(api, frameId, response))
      sendMobileError(api, frameId, "RESPONSE_TOO_LARGE", "Use a smaller page");
    return;
  }

  if (!strcmp(operation, "get_message")) {
    const uint16_t origin = arguments["origin"] | 0U;
    const uint32_t id = arguments["id"] | 0UL;
    const crew::MessageRecord *message = history.record(history.find(origin, id));
    if (!message || !message->used) {
      sendMobileError(api, frameId, "NOT_FOUND", "Message not found"); return;
    }
    StaticJsonDocument<640> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    addMessageSummary(data, *message, now); data["text"] = message->text;
    data["source"] = messageSourceLabel(message->source);
    data["time"] = message->timestamp;
    queueMobileJson(api, frameId, response); return;
  }

  if (!strcmp(operation, "get_gps")) {
    StaticJsonDocument<320> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["state"] = gpsLabel(); data["valid"] = gps.location.isValid();
    data["current"] = gpsCurrent();
    data["satellites"] = gps.satellites.isValid() ? gps.satellites.value() : 0;
    data["ageMs"] = gps.location.isValid() ? gps.location.age() : UINT32_MAX;
    if (gps.location.isValid()) {
      data["lat"] = gps.location.lat(); data["lon"] = gps.location.lng();
    }
    queueMobileJson(api, frameId, response); return;
  }

  if (!strcmp(operation, "get_status")) {
    StaticJsonDocument<640> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["radio"] = loraReady; data["frequency"] = LORA_FREQUENCY;
    data["syncWord"] = LORA_SYNC_WORD; data["sf"] = LORA_SPREADING_FACTOR;
    data["bandwidth"] = LORA_BANDWIDTH; data["codingRate"] = "4/5";
    data["tx"] = counters.localMessages; data["rx"] = counters.receivedMessages;
    data["relays"] = counters.relayed; data["retries"] = counters.retries;
    data["duplicates"] = counters.duplicates; data["malformed"] = counters.malformed;
    data["authFailures"] = counters.authFailures; data["wrongCrew"] = counters.wrongCrew;
    data["queueDrops"] = counters.queueDrops;
    data["bleRxDrops"] = api.rxDrops(); data["bleTxDrops"] = api.txDrops();
    JsonObject last = data.createNestedObject("lastPacket");
    last["type"] = static_cast<uint8_t>(lastPacketType);
    last["origin"] = lastPacketOrigin; last["relay"] = lastPacketRelay;
    last["hop"] = lastPacketHop; last["rssi"] = lastPacketRssi;
    if (!queueMobileJson(api, frameId, response))
      sendMobileError(api, frameId, "RESPONSE_TOO_LARGE", "Status response is too large");
    return;
  }

  if (!strcmp(operation, "send_message")) {
    const uint16_t destination = arguments["destination"] | 0U;
    const char *text = arguments["text"] | "";
    uint32_t messageId = 0;
    const OriginSendResult result = queueOriginatedMessage(
        destination, text, crew::MessageSource::Mobile, now, messageId);
    if (result != OriginSendResult::Ok) {
      sendMobileError(api, frameId, sendErrorCode(result),
                      "Message was not queued"); return;
    }
    StaticJsonDocument<192> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["messageId"] = messageId; data["state"] = "sending";
    queueMobileJson(api, frameId, response); return;
  }

  if (!strcmp(operation, "mark_read")) {
    const uint16_t origin = arguments["origin"] | 0U;
    const uint32_t id = arguments["id"] | 0UL;
    const int index = history.find(origin, id);
    if (index < 0) {
      sendMobileError(api, frameId, "NOT_FOUND", "Message not found"); return;
    }
    history.markRead(index); contentDirty = true;
    StaticJsonDocument<128> response;
    response["v"] = 1; response["ok"] = true;
    response.createNestedObject("data")["unread"] = history.unreadCount();
    queueMobileJson(api, frameId, response); return;
  }

  if (!strcmp(operation, "mark_all_read")) {
    const size_t changed = history.markAllRead(); contentDirty = true;
    StaticJsonDocument<128> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["changed"] = changed; data["unread"] = history.unreadCount();
    queueMobileJson(api, frameId, response); return;
  }

  if (!strcmp(operation, "ping")) {
    StaticJsonDocument<160> response;
    response["v"] = 1; response["ok"] = true;
    JsonObject data = response.createNestedObject("data");
    data["uptime"] = now / 1000UL;
    if (arguments.containsKey("echo")) data["echo"] = arguments["echo"];
    if (!queueMobileJson(api, frameId, response))
      sendMobileError(api, frameId, "RESPONSE_TOO_LARGE", "Echo is too large");
    return;
  }

  sendMobileError(api, frameId, "UNKNOWN_OPERATION", "Unknown operation");
}

void openSelectedCrew(bool composeInstead) {
  int order[crew::MAX_PEERS]; const size_t count = buildPeerOrder(order);
  if (!count || listSelection >= count) return;
  selectedPeerIndex = order[listSelection];
  const crew::PeerRecord *p = directory.record(selectedPeerIndex);
  if (!p) return;
  if (composeInstead) openComposeFor(p->address); else showScreen(Screen::Member);
}

void startDirectMessage() {
  recipientReturnsToCompose = false;
  showScreen(Screen::Recipients, true);
}

void selectPeerForContext(Screen owner) {
  if (owner == Screen::Crew) {
    int order[crew::MAX_PEERS]; const size_t count = buildPeerOrder(order);
    if (count && listSelection < count) selectedPeerIndex = order[listSelection];
  } else if (owner == Screen::Radar) {
    int order[crew::MAX_PEERS]; const size_t count = buildPeerOrder(order, true);
    if (count && listSelection < count) selectedPeerIndex = order[listSelection];
  }
}

void openRadarForSelectedPeer() {
  showScreen(Screen::Radar);
  int order[crew::MAX_PEERS]; const size_t count = buildPeerOrder(order, true);
  for (size_t i = 0; i < count; ++i) {
    if (order[i] == selectedPeerIndex) {
      listSelection = static_cast<uint8_t>(i);
      navigation.setSelection(listSelection);
      contentDirty = true; break;
    }
  }
}

void openContextOptions() {
  optionsOwner = currentScreen;
  selectPeerForContext(optionsOwner);
  showScreen(Screen::Options, true);
}

void executeOption(uint32_t now) {
  OptionItem items[4]; const size_t count = buildOptions(items);
  if (listSelection >= count || !items[listSelection].enabled) {
    requestBeep(2, 30, 40); return;
  }
  const uint8_t selected = listSelection;
  const Screen owner = optionsOwner;
  goBack();
  if (owner == Screen::Home) {
    if (selected == 0) startDirectMessage();
    else if (selected == 1) openComposeFor(mesh::BROADCAST_ADDRESS, true);
    else { nextPresenceAt = now; requestBeep(); }
  } else if (owner == Screen::MessagesHub || owner == Screen::Inbox || owner == Screen::Sent) {
    if (selected == 0) startDirectMessage();
    else if (selected == 1) openComposeFor(mesh::BROADCAST_ADDRESS, true);
    else { history.markAllRead(); contentDirty = true; }
  } else if (owner == Screen::MessageView) {
    crew::MessageRecord *message = history.record(selectedMessageIndex);
    if (!message) return;
    if (selected == 0) {
      const uint16_t recipient = message->crewMessage ? mesh::BROADCAST_ADDRESS
          : message->direction == crew::MessageDirection::Incoming
                ? message->originAddress : message->peerAddress;
      openComposeFor(recipient);
    } else if (message->direction == crew::MessageDirection::Incoming) {
      history.setUnread(selectedMessageIndex, !message->unread); contentDirty = true;
    }
  } else if (owner == Screen::Crew || owner == Screen::Member || owner == Screen::Radar) {
    const crew::PeerRecord *peer = directory.record(selectedPeerIndex);
    if (selected == 0 && peer) openComposeFor(peer->address);
    else if (selected == 1 && peer && owner != Screen::Member) showScreen(Screen::Member);
    else if (selected == 2) openRadarForSelectedPeer();
    else if (selected == 3) { nextPresenceAt = now; requestBeep(); }
  } else if (owner == Screen::Phone) {
    if (selected == 0) { phoneDisconnectConfirm = true; phoneDisconnectDeadline = now + 5000UL; listSelection = 1; }
    else { phoneForgetConfirm = true; phoneForgetDeadline = now + 5000UL; listSelection = 2; }
    contentDirty = true;
  } else {
    if (selected == 0) showScreen(Screen::Diagnostics);
    else if (selected == 1) showScreen(Screen::Phone);
    else { nextPresenceAt = now; requestBeep(); }
  }
}

char readKeypad(uint32_t now) {
  // Drive rows and read columns in the direction verified by the raw wiring
  // probe. The Keypad library drives columns, which decoded A/B/C as A on
  // this particular module.
  for (byte row = 0; row < KEYPAD_ROWS; ++row)
    pinMode(rowPins[row], INPUT_PULLUP);
  for (byte col = 0; col < KEYPAD_COLS; ++col)
    pinMode(columnPins[col], INPUT_PULLUP);

  char detected = 0;
  for (byte row = 0; row < KEYPAD_ROWS; ++row) {
    pinMode(rowPins[row], OUTPUT);
    digitalWrite(rowPins[row], LOW);
    delayMicroseconds(100);
    for (byte col = 0; col < KEYPAD_COLS; ++col) {
      if (digitalRead(columnPins[col]) == LOW && !detected)
        detected = keyMap[row][col];
    }
    pinMode(rowPins[row], INPUT_PULLUP);
  }

  static char candidate = 0;
  static uint32_t changedAt = 0;
  static bool armed = true;
  if (detected != candidate) {
    candidate = detected;
    changedAt = now;
  }
  if (now - changedAt < 30) return 0;
  if (!candidate) {
    armed = true;
    return 0;
  }
  if (!armed) return 0;
  armed = false;
  return candidate;
}

void handleKeypad(uint32_t now) {
  const char key = readKeypad(now); if (!key) return;

  // Composition deliberately keeps the proven multi-tap control mapping.
  if (currentScreen == Screen::Compose) {
    if (key == 'B') { showScreen(Screen::ComposeOptions); return; }
    const input::Event event = composer.press(key, now);
    if (event == input::Event::Send) queueLocalMessage(now);
    else if (event == input::Event::Back) goBack();
    else if (event == input::Event::OpenSymbols) showScreen(Screen::Symbols);
    else if (event == input::Event::Full) requestBeep(2, 25, 35);
    else if (event == input::Event::Changed) contentDirty = true;
    return;
  }
  if (currentScreen == Screen::Symbols) {
    constexpr uint8_t columns = 6; const uint8_t count = strlen(input::SYMBOLS);
    if (key == '4' && symbolSelection) --symbolSelection;
    else if (key == '6' && symbolSelection + 1 < count) ++symbolSelection;
    else if (key == '2' && symbolSelection >= columns) symbolSelection -= columns;
    else if (key == '8' && symbolSelection + columns < count) symbolSelection += columns;
    else if (key == '5') {
      if (composer.insertSymbol(input::SYMBOLS[symbolSelection]) == input::Event::Full)
        requestBeep(2, 25, 35);
      goBack(); return;
    } else if (key == 'D') { goBack(); return; }
    contentDirty = true; return;
  }

  // Universal feature-phone navigation outside the editor.
  if (key == '*') { showScreen(Screen::Home); return; }
  if (key == 'C') {
    if (currentScreen == Screen::Recipients) recipientReturnsToCompose = false;
    phoneForgetConfirm = phoneDisconnectConfirm = false;
    goBack(); return;
  }
  if (key == '#' && currentScreen != Screen::Recipients &&
      currentScreen != Screen::ComposeOptions && currentScreen != Screen::Diagnostics &&
      currentScreen != Screen::Options) {
    openContextOptions(); return;
  }

  if (currentScreen == Screen::Home) {
    if (key == '1') showScreen(Screen::MessagesHub);
    else if (key == '2') showScreen(Screen::Radar);
    else if (key == '3') showScreen(Screen::Crew);
    else if (key == '4') showScreen(Screen::Phone);
    else if (key == '5') showScreen(Screen::Status);
    else if (key == '6') startDirectMessage();
    return;
  }
  if (currentScreen == Screen::MessagesHub) {
    if (key == 'A') selectListDelta(-1, 5);
    else if (key == 'B') selectListDelta(1, 5);
    else if (key == 'D') {
      if (listSelection == 0) showScreen(Screen::Inbox);
      else if (listSelection == 1) showScreen(Screen::Sent);
      else if (listSelection == 2) startDirectMessage();
      else if (listSelection == 3) openComposeFor(mesh::BROADCAST_ADDRESS, true);
      else if (composer.length()) showScreen(Screen::Compose);
      else requestBeep(2, 30, 40);
    }
    return;
  }
  if (currentScreen == Screen::Inbox || currentScreen == Screen::Sent) {
    const crew::MessageDirection direction = currentScreen == Screen::Inbox
        ? crew::MessageDirection::Incoming : crew::MessageDirection::Outgoing;
    const size_t count = ui::filteredMessageCount(history, direction);
    if (key == 'A') selectListDelta(-1, count);
    else if (key == 'B') selectListDelta(1, count);
    else if (key == 'D' && count) {
      selectedMessageIndex = ui::filteredMessageIndex(history, direction, listSelection);
      if (direction == crew::MessageDirection::Incoming) history.markRead(selectedMessageIndex);
      showScreen(Screen::MessageView);
    }
    return;
  }
  if (currentScreen == Screen::Crew) {
    int order[crew::MAX_PEERS]; const size_t count = buildPeerOrder(order);
    if (key == 'A') selectListDelta(-1, count);
    else if (key == 'B') selectListDelta(1, count);
    else if (key == 'D') openSelectedCrew(false);
    return;
  }
  if (currentScreen == Screen::Recipients) {
    int order[crew::MAX_PEERS]; const size_t peers = buildPeerOrder(order);
    const size_t count = peers + 1;
    if (key == 'A') selectListDelta(-1, count);
    else if (key == 'B') selectListDelta(1, count);
    else if (key == 'D') {
      if (!listSelection) openComposeFor(mesh::BROADCAST_ADDRESS, true);
      else { const crew::PeerRecord *p = directory.record(order[listSelection - 1]);
             if (p) openComposeFor(p->address, true); }
    }
    return;
  }
  if (currentScreen == Screen::ComposeOptions) {
    if (key == 'A') selectListDelta(-1, 2);
    else if (key == 'B') selectListDelta(1, 2);
    else if (key == 'D' && listSelection == 0) {
      recipientReturnsToCompose = true; showScreen(Screen::Recipients);
    } else if (key == 'D' && listSelection == 1) {
      if (composer.length()) { composer.clear(); requestBeep(); contentDirty = true; }
      else requestBeep(2, 30, 40);
    }
    return;
  }
  if (currentScreen == Screen::Radar) {
    int order[crew::MAX_PEERS]; const size_t count = buildPeerOrder(order, true);
    if (key == 'A') selectListDelta(-1, count);
    else if (key == 'B') selectListDelta(1, count);
    else if (key == 'D') {
      if (!gpsCurrent() || !count) showScreen(Screen::Crew);
      else { selectedPeerIndex = order[listSelection]; showScreen(Screen::Member); }
    }
    return;
  }
  if (currentScreen == Screen::Status) {
    if (key == 'D') showScreen(Screen::Diagnostics);
    return;
  }
  if (currentScreen == Screen::Diagnostics) {
    if (key == 'A' || key == 'B') { listSelection = listSelection ? 0 : 1; navigation.setSelection(listSelection); contentDirty = true; }
    return;
  }
  if (currentScreen == Screen::Phone && !mobileApi.pairing()) {
    if (key == 'A') selectListDelta(-1, 3);
    else if (key == 'B') selectListDelta(1, 3);
    else if (key == 'D') {
      if (phoneForgetConfirm && !mesh::timeReached(now, phoneForgetDeadline)) {
        if (mobileApi.forgetAllBonds()) requestBeep(); else requestBeep(2, 35, 45);
        phoneForgetConfirm = false;
      } else if (phoneDisconnectConfirm && !mesh::timeReached(now, phoneDisconnectDeadline)) {
        if (mobileApi.disconnectPhone()) requestBeep(); else requestBeep(2, 35, 45);
        phoneDisconnectConfirm = false;
      } else if (listSelection == 0) {
        if (!mobileApi.startPairing(now)) requestBeep(2, 35, 45);
      } else if (listSelection == 1 && mobileApi.connected()) {
        phoneDisconnectConfirm = true; phoneDisconnectDeadline = now + 5000UL;
      } else if (listSelection == 2 && (mobileApi.bondCount() || mobileApi.bondFault())) {
        phoneForgetConfirm = true; phoneForgetDeadline = now + 5000UL;
      } else requestBeep(2, 30, 40);
      contentDirty = true;
    }
    return;
  }
  if (currentScreen == Screen::Options) {
    OptionItem items[4]; const size_t count = buildOptions(items);
    if (key == 'A') selectListDelta(-1, count);
    else if (key == 'B') selectListDelta(1, count);
    else if (key == 'D') executeOption(now);
    return;
  }
  if (currentScreen == Screen::MessageView || currentScreen == Screen::Member) {
    if (key == 'D') requestBeep(2, 25, 35);
    return;
  }
}

bool transmitPacket(const mesh::Packet &packet) {
  if (!loraReady || !cryptoReady || radioTxInProgress) return false;
  uint8_t frame[mesh::MAX_FRAME_SIZE]; size_t length = 0;
  if (!secureCodec.seal(packet, crew_config::CREW_ID, device_config::DEVICE_ADDRESS,
                        bootSession, nextNonzero(frameSequence), frame,
                        sizeof(frame), length)) {
    ++counters.malformed; return false;
  }
  prepareLoRaOperation(); const int began = LoRa.beginPacket(); bool success = false;
  if (began == 1) {
    LoRa.write(frame, length);
    radioTxDone = false;
    LoRa.onTxDone(onRadioTxDone);
    success = LoRa.endPacket(true) == 1;
    if (success) {
      radioTxInProgress = true;
      radioTxStartedAt = millis();
    }
  }
  if (!success) {
    LoRa.onTxDone(nullptr);
    LoRa.receive();
  }
  digitalWrite(LORA_CS_PIN, HIGH); return success;
}

void serviceRadioTxState(uint32_t now) {
  if (!radioTxInProgress) return;
  const bool complete = radioTxDone;
  const bool timedOut = mesh::intervalElapsed(now, radioTxStartedAt, 5000UL);
  if (!complete && !timedOut) return;
  prepareLoRaOperation();
  LoRa.onTxDone(nullptr);  // Do not let RX-DONE trigger an SPI ISR while TFT runs.
  LoRa.receive();  // Mandatory return to continuous receive after every TX.
  digitalWrite(LORA_CS_PIN, HIGH);
  radioTxInProgress = false;
  if (timedOut && !complete) {
    ++counters.queueDrops;
    Serial.println("[TX] completion timeout; forced receive mode");
  }
}
mesh::Envelope makeEnvelope(mesh::PacketType type, uint16_t destination) {
  mesh::Envelope e; e.type = type; e.origin = device_config::DEVICE_ADDRESS;
  e.originSession = bootSession; e.destination = destination;
  e.floodId = nextNonzero(nextFloodId); e.hop = 0; return e;
}
void scheduleRelay(const mesh::Packet &packet, int seenIndex, uint32_t now) {
  if (packet.envelope.hop >= mesh::MAX_HOPS) return;
  const mesh::SeenEntry *entry = seenCache.entry(seenIndex);
  if (!entry || entry->relayed || entry->alternateCount >= RELAY_CANCEL_COPIES) return;
  const uint32_t delayMs = packet.envelope.type == mesh::PacketType::DeliveryAck
      ? randomRange(120, 450)
      : packet.envelope.type == mesh::PacketType::Presence
            ? randomRange(700, 1800) : randomRange(350, 1100);
  if (!relayQueue.schedule(packet, seenIndex, now + delayMs)) {
    ++counters.queueDrops; Serial.println("[QUEUE] relay full");
  }
}
void scheduleTargetAck(uint16_t destination, uint32_t messageId, uint32_t now) {
  if (!ackQueue.schedule(destination, messageId, now + randomRange(80, 350))) {
    ++counters.queueDrops; Serial.println("[QUEUE] ACK full");
  }
}
void storeIncoming(const mesh::Packet &packet, uint32_t now) {
  if (history.containsIncoming(packet.envelope.origin, packet.message.messageId)) return;
  crew::MessageRecord record; record.used = true;
  record.direction = crew::MessageDirection::Incoming;
  record.source = crew::MessageSource::Radio;
  record.crewMessage = packet.envelope.type == mesh::PacketType::CrewMessage;
  record.unread = true; record.peerAddress = packet.envelope.origin;
  record.originAddress = packet.envelope.origin;
  char fallback[10]; strncpy(record.peerName,
      peerName(packet.envelope.origin, fallback, sizeof(fallback)), sizeof(record.peerName) - 1);
  record.messageId = packet.message.messageId; record.timestamp = packet.message.timestamp;
  record.storedAt = now;
  record.delivery = crew::MessageDelivery::Received;
  strncpy(record.text, packet.message.text, sizeof(record.text) - 1);
  history.add(record); ++counters.receivedMessages;
  strncpy(incomingBannerName, record.peerName, sizeof(incomingBannerName) - 1);
  strncpy(incomingBannerText, record.text, sizeof(incomingBannerText) - 1);
  incomingBannerActive = true; incomingBannerDeadline = now + 3000UL;
  requestBeep(1, 55, 50); contentDirty = true;
  if (currentScreen == Screen::Home) {
    contentDirty = true;
  }
}
void processNewPacket(const mesh::FrameHeader &header, const mesh::Packet &packet,
                      int16_t rssi, uint32_t now, int seenIndex) {
  const bool addressedHere = packet.envelope.destination == device_config::DEVICE_ADDRESS;
  switch (packet.envelope.type) {
    case mesh::PacketType::Presence: {
      const crew::PresenceUpdate update = directory.updatePresence(
          packet.envelope.origin, packet.envelope.originSession, packet.envelope.floodId,
          packet.presence, packet.envelope.hop, header.transmitter, rssi, now);
      if (update == crew::PresenceUpdate::AddressConflict) ++counters.conflicts;
      if (update == crew::PresenceUpdate::TableFull) ++counters.queueDrops;
      break;
    }
    case mesh::PacketType::DirectMessage:
      if (mesh::shouldStoreMessage(packet.envelope, device_config::DEVICE_ADDRESS)) {
        storeIncoming(packet, now); scheduleTargetAck(packet.envelope.origin,
                                      packet.message.messageId, now); }
      break;
    case mesh::PacketType::CrewMessage:
      if (mesh::shouldStoreMessage(packet.envelope, device_config::DEVICE_ADDRESS))
        storeIncoming(packet, now);
      break;
    case mesh::PacketType::DeliveryAck:
      if (addressedHere) confirmDelivery(packet.ack.messageId, "target ACK");
      break;
    default: return;
  }
  const bool terminal = addressedHere &&
      (packet.envelope.type == mesh::PacketType::DirectMessage ||
       packet.envelope.type == mesh::PacketType::DeliveryAck);
  if (!terminal) scheduleRelay(packet, seenIndex, now);
}
void handleAuthenticatedFrame(const mesh::FrameHeader &header,
                              const mesh::Packet &packet, int16_t rssi,
                              uint32_t now) {
  const bool ownTransmitter = header.transmitter == device_config::DEVICE_ADDRESS &&
                              mesh::sessionEqual(header.transmitterSession, bootSession);
  if (ownTransmitter) return;
  lastRadioActivityAt = now; statusBarDirty = true;
  lastPacketType = packet.envelope.type; lastPacketOrigin = packet.envelope.origin;
  lastPacketRelay = header.transmitter; lastPacketHop = packet.envelope.hop;
  lastPacketRssi = rssi;
  const bool ownOrigin = packet.envelope.origin == device_config::DEVICE_ADDRESS &&
                         mesh::sessionEqual(packet.envelope.originSession, bootSession);
  if (packet.envelope.type == mesh::PacketType::Presence &&
      packet.envelope.origin == device_config::DEVICE_ADDRESS && !ownOrigin &&
      memcmp(packet.presence.hardwareId, hardwareId, mesh::HARDWARE_ID_SIZE)) {
    if (!localAddressConflict) ++counters.conflicts;
    localAddressConflict = true; contentDirty = true;
    Serial.println("[SECURITY] duplicate local device address detected");
  }
  if (ownOrigin) {
    if (packet.envelope.type == mesh::PacketType::CrewMessage && packet.envelope.hop)
      confirmDelivery(packet.message.messageId, "mesh relay");
    return;
  }
  const size_t onlineBefore = directory.onlineCount(now);
  directory.touch(packet.envelope.origin, packet.envelope.hop, header.transmitter, rssi, now);
  bool homeValueChanged = directory.onlineCount(now) != onlineBefore;
  const mesh::FloodKey key = mesh::floodKeyFor(packet);
  const mesh::SeenResult seen = seenCache.observe(key, header.transmitter, now);
  if (!seen.isNew) {
    ++counters.duplicates; const mesh::SeenEntry *entry = seenCache.entry(seen.index);
    if (entry && entry->alternateCount >= RELAY_CANCEL_COPIES) relayQueue.cancel(key);
    if (currentScreen == Screen::Status ||
        (currentScreen == Screen::Home && homeValueChanged)) {
      contentDirty = true;
    }
    return;
  }
  processNewPacket(header, packet, rssi, now, seen.index);
  homeValueChanged = homeValueChanged ||
                     directory.onlineCount(now) != onlineBefore;
  // Message storage and delivery confirmation mark their own visible fields.
  // A routine presence refresh must not erase/redraw Home unnecessarily.
  if (currentScreen != Screen::Home || homeValueChanged) contentDirty = true;
}
void serviceRadioReceive(uint32_t now) {
  if (!loraReady || !cryptoReady || radioTxInProgress) return;
  prepareLoRaOperation(); const int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) { digitalWrite(LORA_CS_PIN, HIGH); return; }
  uint8_t frame[mesh::MAX_FRAME_SIZE]; size_t length = 0; bool overflow = false;
  while (LoRa.available()) {
    int value = LoRa.read(); if (value < 0) break;
    if (length < sizeof(frame)) frame[length++] = static_cast<uint8_t>(value);
    else overflow = true;
  }
  const int16_t rssi = LoRa.packetRssi(); digitalWrite(LORA_CS_PIN, HIGH);
  if (overflow || packetSize != static_cast<int>(length)) { ++counters.malformed; return; }
  mesh::FrameHeader header; mesh::Packet packet;
  const mesh::FrameOpenResult result =
      secureCodec.open(frame, length, crew_config::CREW_ID, header, packet);
  if (result != mesh::FrameOpenResult::Ok) {
    if (result == mesh::FrameOpenResult::WrongCrew) ++counters.wrongCrew;
    else if (result == mesh::FrameOpenResult::AuthenticationFailed) ++counters.authFailures;
    else ++counters.malformed;
    if (currentScreen == Screen::Status) contentDirty = true;
    return;
  }
  handleAuthenticatedFrame(header, packet, rssi, now);
}

bool serviceAckTransmit(uint32_t now) {
  const int index = ackQueue.dueIndex(now); if (index < 0) return false;
  const mesh::AckJob job = *ackQueue.job(index); ackQueue.remove(index);
  mesh::Packet ack; ack.envelope = makeEnvelope(mesh::PacketType::DeliveryAck, job.destination);
  ack.ack.messageId = job.messageId;
  const mesh::SeenResult seen = seenCache.observe(mesh::floodKeyFor(ack),
                                                  device_config::DEVICE_ADDRESS, now);
  if (transmitPacket(ack)) { ++counters.acksSent; seenCache.markRelayed(seen.index); }
  else ++counters.queueDrops;
  return true;
}
bool serviceRelayTransmit(uint32_t now, bool presenceClass) {
  const int index = relayQueue.dueIndex(now, presenceClass); if (index < 0) return false;
  const mesh::RelayJob job = *relayQueue.job(index); relayQueue.remove(index);
  mesh::SeenEntry *entry = seenCache.entry(job.seenIndex);
  if (!entry || !entry->active || entry->relayed ||
      !mesh::floodKeyEqual(entry->key, mesh::floodKeyFor(job.packet)) ||
      entry->alternateCount >= RELAY_CANCEL_COPIES ||
      job.packet.envelope.hop >= mesh::MAX_HOPS) return true;
  mesh::Packet relay = job.packet; ++relay.envelope.hop;
  if (transmitPacket(relay)) ++counters.relayed; else ++counters.queueDrops;
  seenCache.markRelayed(job.seenIndex);
  if (currentScreen == Screen::Status) contentDirty = true;
  return true;
}
bool serviceLocalMessageTransmit(uint32_t now) {
  if (!delivery.sendDue(now)) return false;
  mesh::Packet packet;
  packet.envelope = makeEnvelope(delivery.crewMessage() ? mesh::PacketType::CrewMessage
                                                        : mesh::PacketType::DirectMessage,
                                 delivery.destination());
  packet.message.messageId = delivery.messageId(); packet.message.timestamp = now / 1000UL;
  strncpy(packet.message.text, activeOutgoingText, sizeof(packet.message.text) - 1);
  const mesh::SeenResult seen = seenCache.observe(mesh::floodKeyFor(packet),
                                                  device_config::DEVICE_ADDRESS, now);
  const bool sent = transmitPacket(packet); seenCache.markRelayed(seen.index);
  delivery.noteSent(now, ACK_WINDOW_MS); if (delivery.attempts() > 1) ++counters.retries;
  Serial.printf("[TX] msg %08lX flood=%08lX try=%u %s\n",
      static_cast<unsigned long>(packet.message.messageId),
      static_cast<unsigned long>(packet.envelope.floodId), delivery.attempts(), sent ? "OK" : "FAIL");
  contentDirty = true; return true;
}
bool serviceLocalPresence(uint32_t now) {
  if (!mesh::timeReached(now, nextPresenceAt)) return false;
  mesh::Packet packet; packet.envelope = makeEnvelope(mesh::PacketType::Presence,
                                                       mesh::BROADCAST_ADDRESS);
  memcpy(packet.presence.hardwareId, hardwareId, sizeof(hardwareId));
  strncpy(packet.presence.name, device_config::DEVICE_NAME, sizeof(packet.presence.name) - 1);
  packet.presence.gpsValid = gpsCurrent();
  if (packet.presence.gpsValid) {
    packet.presence.latitudeE7 = static_cast<int32_t>(lround(gps.location.lat() * 1e7));
    packet.presence.longitudeE7 = static_cast<int32_t>(lround(gps.location.lng() * 1e7));
    packet.presence.satellites = gps.satellites.isValid()
        ? static_cast<uint8_t>(gps.satellites.value()) : 0;
    const uint32_t age = gps.location.age() / 1000UL;
    packet.presence.fixAgeSeconds = age > UINT16_MAX ? UINT16_MAX : age;
  }
  const mesh::SeenResult seen = seenCache.observe(mesh::floodKeyFor(packet),
                                                  device_config::DEVICE_ADDRESS, now);
  if (transmitPacket(packet)) ++counters.presenceSent;
  seenCache.markRelayed(seen.index);
  nextPresenceAt = now + randomRange(PRESENCE_MIN_MS, PRESENCE_MAX_MS); return true;
}
void serviceRadioTransmit(uint32_t now) {
  if (!loraReady || !cryptoReady || radioTxInProgress) return;
  if (serviceAckTransmit(now)) return;                  // ACK origin
  if (serviceRelayTransmit(now, false)) return;         // ACK/user relay
  if (serviceLocalMessageTransmit(now)) return;         // user origin
  if (serviceRelayTransmit(now, true)) return;          // presence relay
  serviceLocalPresence(now);                            // presence origin
}

void serviceDeliveryTimeout(uint32_t now) {
  if (!delivery.handleTimeout(now, randomRange(700, 1800), MAX_SEND_ATTEMPTS)) return;
  if (delivery.phase() == mesh::DeliveryPhase::Failed) {
    updateTrackedHistory(crew::MessageDelivery::Failed); requestBeep(2, 80, 80);
    Serial.printf("[DELIVERY] %08lX failed after %u floods\n",
                  static_cast<unsigned long>(delivery.messageId()), delivery.attempts());
  }
  contentDirty = true;
}
void serviceGps(uint32_t now) {
  bool updated = false;
  while (gpsSerial.available() > 0) updated = gps.encode(gpsSerial.read()) || updated;
  if (updated) mobileGpsDirty = true;
  if (updated && mesh::intervalElapsed(now, lastGpsUiAt, 2000UL) &&
      (currentScreen == Screen::Radar || currentScreen == Screen::Member)) {
    lastGpsUiAt = now; contentDirty = true;
  }
  if (mesh::intervalElapsed(now, lastGpsDiagnosticAt, 30000UL)) {
    lastGpsDiagnosticAt = now;
    if (gps.charsProcessed() < 10)
      Serial.println("[GPS] no UART data; verify TX->GPIO34 at 9600 baud");
    else if (gps.location.isValid())
      Serial.printf("[GPS] %.6f,%.6f sats=%lu age=%lums\n", gps.location.lat(),
                    gps.location.lng(), static_cast<unsigned long>(gps.satellites.value()),
                    static_cast<unsigned long>(gps.location.age()));
  }
}
void serviceOneSecond(uint32_t now) {
  if (!mesh::intervalElapsed(now, lastSecondAt, 1000UL)) return;
  lastSecondAt = now;
  statusBarDirty = true;
  static uint8_t previousGpsState = 0xFF;
  const uint8_t gpsState = gpsCurrent() ? 2 : gps.location.isValid() ? 1 : 0;
  const bool gpsChanged = gpsState != previousGpsState;
  if (gpsChanged) mobileGpsDirty = true;
  previousGpsState = gpsState;
  const bool onlineChanged = directory.refreshOnline(now);
  static uint32_t previousMinute = UINT32_MAX;
  const uint32_t minute = now / 60000UL;
  const bool minuteChanged = minute != previousMinute;
  previousMinute = minute;
  const bool ageIsVisible = minuteChanged &&
      (currentScreen == Screen::Crew || currentScreen == Screen::Member ||
       currentScreen == Screen::Inbox || currentScreen == Screen::Sent ||
       currentScreen == Screen::MessageView);
  if (phoneForgetConfirm && mesh::timeReached(now, phoneForgetDeadline)) {
    phoneForgetConfirm = false;
  }
  if (phoneDisconnectConfirm && mesh::timeReached(now, phoneDisconnectDeadline)) {
    phoneDisconnectConfirm = false;
  }
  bool bannerExpired = false;
  if (incomingBannerActive && mesh::timeReached(now, incomingBannerDeadline)) {
    incomingBannerActive = false; bannerExpired = currentScreen == Screen::Home;
  }
  const bool phoneTimer = currentScreen == Screen::Phone &&
      (mobileApi.pairing() || phoneForgetConfirm || phoneDisconnectConfirm);
  if (onlineChanged || gpsChanged || ageIsVisible || phoneTimer || bannerExpired)
    contentDirty = true;
}

uint32_t mobileNodeSignature(uint32_t now) {
  uint32_t value = 2166136261UL;
#define MIX_NODE(v) value = (value ^ static_cast<uint32_t>(v)) * 16777619UL
  MIX_NODE(loraReady); MIX_NODE(cryptoReady); MIX_NODE(localAddressConflict);
  MIX_NODE(directory.onlineCount(now)); MIX_NODE(history.unreadCount());
  MIX_NODE(static_cast<uint8_t>(delivery.phase())); MIX_NODE(mobileApi.connected());
  MIX_NODE(mobileApi.authorized()); MIX_NODE(mobileApi.bondCount());
#undef MIX_NODE
  return value;
}

template <size_t Capacity>
bool queueMobileEvent(StaticJsonDocument<Capacity> &event) {
  event["v"] = 1;
  event["seq"] = mobileEventSequence + 1;
  if (!queueMobileJson(mobileApi, 0, event, true)) return false;
  ++mobileEventSequence;
  return true;
}

void serviceMobileEvents(uint32_t now) {
  if (!mobileApi.authorized()) return;
  if (mobileResyncRequired) {
    StaticJsonDocument<128> event;
    event["event"] = "resync_required";
    event.createNestedObject("data")["reason"] = "event_queue_overflow";
    if (queueMobileEvent(event)) mobileResyncRequired = false;
    else return;
  }

  const uint32_t peerRevision = directory.revision();
  if (peerRevision != lastMobilePeerRevision) {
    StaticJsonDocument<160> event;
    event["event"] = "peers_changed";
    JsonObject data = event.createNestedObject("data");
    data["revision"] = peerRevision;
    data["online"] = directory.onlineCount(now) + 1;
    if (!queueMobileEvent(event)) mobileResyncRequired = true;
    lastMobilePeerRevision = peerRevision;
  }

  const uint32_t messageRevision = history.revision();
  if (messageRevision != lastMobileMessageRevision) {
    StaticJsonDocument<160> event;
    event["event"] = "messages_changed";
    JsonObject data = event.createNestedObject("data");
    data["revision"] = messageRevision; data["count"] = history.count();
    data["unread"] = history.unreadCount();
    if (!queueMobileEvent(event)) mobileResyncRequired = true;
    lastMobileMessageRevision = messageRevision;
  }

  if (mobileGpsDirty && mesh::intervalElapsed(now, lastMobileGpsEventAt, 1000UL)) {
    StaticJsonDocument<256> event;
    event["event"] = "gps_changed";
    JsonObject data = event.createNestedObject("data");
    data["state"] = gpsLabel(); data["current"] = gpsCurrent();
    data["satellites"] = gps.satellites.isValid() ? gps.satellites.value() : 0;
    if (gps.location.isValid()) {
      data["lat"] = gps.location.lat(); data["lon"] = gps.location.lng();
      data["ageMs"] = gps.location.age();
    }
    if (!queueMobileEvent(event)) mobileResyncRequired = true;
    mobileGpsDirty = false; lastMobileGpsEventAt = now;
  }

  const uint32_t nodeSignature = mobileNodeSignature(now);
  if (nodeSignature != lastMobileNodeSignature) {
    StaticJsonDocument<192> event;
    event["event"] = "node_changed";
    JsonObject data = event.createNestedObject("data");
    data["online"] = directory.onlineCount(now) + 1;
    data["unread"] = history.unreadCount();
    data["delivery"] = deliveryLabel(delivery.phase());
    data["radio"] = loraReady; data["conflict"] = localAddressConflict;
    if (!queueMobileEvent(event)) mobileResyncRequired = true;
    lastMobileNodeSignature = nodeSignature;
  }
}
void drawBootStatus(const char *line, uint16_t color, int16_t y) {
  prepareDisplayOperation(); tft.fillRect(0, y, SCREEN_WIDTH, 18, COLOR_BACKGROUND);
  tft.setTextColor(color); tft.setTextSize(1); tft.setCursor(7, y + 4); tft.print(line);
}
void drawBootSplash() {
  prepareDisplayOperation(); tft.fillScreen(COLOR_BACKGROUND);
  const int16_t x0 = (SCREEN_WIDTH - LOGO_WIDTH) / 2;
  const int16_t y0 = 40;
  for (uint16_t y = 0; y < LOGO_HEIGHT; ++y) {
    for (uint16_t x = 0; x < LOGO_WIDTH; ++x) {
      const uint32_t index = static_cast<uint32_t>(y) * LOGO_WIDTH + x;
      tft.drawPixel(x0 + x, y0 + y, pgm_read_word(&LOGO_PIXELS[index]));
    }
  }
  tft.setTextColor(COLOR_TEXT); tft.setTextSize(1);
  tft.setCursor(100, 118); tft.print("Beyond mobile coverage.");
}
bool initializeLoRa() {
  prepareLoRaOperation(); LoRa.setPins(LORA_CS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
  bool initialized = false;
  for (uint8_t attempt = 1; attempt <= 3 && !initialized; ++attempt) {
    prepareLoRaOperation(); initialized = LoRa.begin(LORA_FREQUENCY) == 1;
    digitalWrite(LORA_CS_PIN, HIGH);
    if (!initialized) { Serial.printf("[BOOT] LoRa init attempt %u failed\n", attempt); delay(100); }
  }
  if (!initialized) return false;
  prepareLoRaOperation(); LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR); LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE_DENOMINATOR); LoRa.enableCrc(); LoRa.receive();
  digitalWrite(LORA_CS_PIN, HIGH); return true;
}
void initializeIdentity() {
  const uint64_t efuse = ESP.getEfuseMac();
  for (size_t i = 0; i < mesh::HARDWARE_ID_SIZE; ++i)
    hardwareId[mesh::HARDWARE_ID_SIZE - 1 - i] = static_cast<uint8_t>(efuse >> (i * 8));
  for (size_t i = 0; i < mesh::HARDWARE_ID_SIZE; ++i)
    snprintf(hardwareIdHex + i * 2, 3, "%02X", hardwareId[i]);
  esp_fill_random(bootSession.bytes, sizeof(bootSession.bytes));
  if (mesh::sessionIsZero(bootSession)) bootSession.bytes[0] = 1;
  frameSequence = esp_random(); nextFloodId = esp_random(); nextMessageId = esp_random();
  if (frameSequence == UINT32_MAX) frameSequence = 0;
  if (nextFloodId == UINT32_MAX) nextFloodId = 0;
  if (nextMessageId == UINT32_MAX) nextMessageId = 0;
}

}  // namespace

void setup() {
  Serial.begin(115200); delay(200); Serial.println();
  Serial.println("=== SHONGKET LC3 ENCRYPTED CREW LINK ===");
  initializeIdentity(); configReady = app_config::runtimeConfigValid();
  const bool cryptoKat = mesh::SecureFrameCodec::comprehensiveSelfTest();
  cryptoReady = cryptoKat && secureCodec.begin(crew_config::CREW_KEY);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);

  // Both shared-SPI peripherals are deselected before the bus starts.
  pinMode(TFT_CS_PIN, OUTPUT); pinMode(LORA_CS_PIN, OUTPUT);
  digitalWrite(TFT_CS_PIN, HIGH); digitalWrite(LORA_CS_PIN, HIGH);
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);

  // TFT initialization deliberately precedes LoRa initialization.
  prepareDisplayOperation(); tft.init(SCREEN_HEIGHT, SCREEN_WIDTH); tft.setRotation(1);
  tft.setTextWrap(false); drawBootSplash();
  drawBootStatus("Display: OK", COLOR_GREEN, 140);
  drawBootStatus(configReady ? "Config: OK" : "Config: INVALID",
                 configReady ? COLOR_GREEN : COLOR_RED, 156);
  drawBootStatus(cryptoReady ? "AES-CCM self-test: OK" : "AES-CCM: FAILED",
                 cryptoReady ? COLOR_GREEN : COLOR_RED, 172);
  loraReady = configReady && cryptoReady && initializeLoRa();
  drawBootStatus(loraReady ? "LoRa: OK" : "LoRa: FAILED",
                 loraReady ? COLOR_GREEN : COLOR_RED, 188);
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  drawBootStatus("GPS UART2: OK", COLOR_GREEN, 204);
  char bleName[32];
  snprintf(bleName, sizeof(bleName), "CL3-%u-%s",
           device_config::DEVICE_ADDRESS, device_config::DEVICE_NAME);
  const bool bleReady = mobileApi.begin(bleName, &mobileHandler);
  drawBootStatus(bleReady ? "Phone BLE API: OK" : "Phone BLE API: FAILED",
                 bleReady ? COLOR_GREEN : COLOR_RED, 220);
  Serial.printf("[BOOT] name=%s address=%u hardware=%s crew=%08lX\n",
                device_config::DEVICE_NAME, device_config::DEVICE_ADDRESS,
                hardwareIdHex, static_cast<unsigned long>(crew_config::CREW_ID));
  Serial.printf("[BOOT] config=%s crypto-KAT=%s LoRa=%s\n",
                configReady ? "OK" : "FAIL", cryptoKat ? "OK" : "FAIL",
                loraReady ? "OK" : "FAIL");
  Serial.printf("[BOOT] BLE API=%s bonds=%u (passkeys appear only on TFT)\n",
                bleReady ? "OK" : "FAIL", mobileApi.bondCount());
  Serial.printf("[BOOT] 433MHz sync=0x%02X SF=%u BW=%ld CR=4/%u CRC=ON\n",
                LORA_SYNC_WORD, LORA_SPREADING_FACTOR, LORA_BANDWIDTH,
                LORA_CODING_RATE_DENOMINATOR);
  Serial.println("[BOOT] GPIO12 is a strap pin; do not hold a key at reset");
  nextPresenceAt = millis() + randomRange(500, 30000);
  requestBeep(1, 70, 50); showScreen(Screen::Home); serviceDisplay();
}

void loop() {
  const uint32_t now = millis(); serviceGps(now); mobileApi.service(now);
  if (mobileApi.consumeStateChanged()) {
    statusBarDirty = true;
    if (currentScreen == Screen::Phone || currentScreen == Screen::Status ||
        currentScreen == Screen::Home) contentDirty = true;
  }
  if (composer.tick(now) == input::Event::Changed && currentScreen == Screen::Compose)
    contentDirty = true;
  handleKeypad(now); serviceRadioTxState(now); serviceRadioReceive(now);
  serviceDeliveryTimeout(now);
  serviceRadioTransmit(now); serviceOneSecond(now); serviceMobileEvents(now);
  serviceBuzzer(now);
  serviceDisplay(); yield();
}
