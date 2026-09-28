#include "crew_data.h"
#include "feature_input.h"
#include "mesh_protocol.h"
#include "mesh_state.h"
#include "mobile_framing.h"
#include "radar_math.h"
#include "ui_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

mesh::SessionId session(uint8_t seed) {
  mesh::SessionId value;
  for (size_t i = 0; i < mesh::SESSION_ID_SIZE; ++i) value.bytes[i] = seed + i;
  return value;
}

mesh::Packet message(mesh::PacketType type = mesh::PacketType::DirectMessage,
                     uint16_t origin = 1, uint16_t destination = 2,
                     uint32_t flood = 3, uint32_t id = 4) {
  mesh::Packet packet;
  packet.envelope.type = type;
  packet.envelope.origin = origin;
  packet.envelope.originSession = session(origin);
  packet.envelope.destination =
      type == mesh::PacketType::CrewMessage ? mesh::BROADCAST_ADDRESS : destination;
  packet.envelope.floodId = flood;
  packet.message.messageId = id;
  packet.message.timestamp = 99;
  strcpy(packet.message.text, "Meet at gate 2");
  return packet;
}

mesh::Packet presence(uint16_t origin, uint32_t flood, const char *name,
                      uint8_t hardwareSeed, bool gps = true) {
  mesh::Packet packet;
  packet.envelope.type = mesh::PacketType::Presence;
  packet.envelope.origin = origin;
  packet.envelope.originSession = session(origin);
  packet.envelope.destination = mesh::BROADCAST_ADDRESS;
  packet.envelope.floodId = flood;
  for (size_t i = 0; i < mesh::HARDWARE_ID_SIZE; ++i)
    packet.presence.hardwareId[i] = hardwareSeed + i;
  strcpy(packet.presence.name, name);
  packet.presence.gpsValid = gps;
  packet.presence.latitudeE7 = 237800000;
  packet.presence.longitudeE7 = 904100000;
  packet.presence.satellites = 8;
  packet.presence.fixAgeSeconds = 2;
  return packet;
}

void testHeaderNonceAndPlaintext() {
  mesh::FrameHeader header;
  header.crewId = 0x12345678;
  header.transmitter = 42;
  header.transmitterSession = session(9);
  header.frameSequence = 0xABCDEF01;
  header.plaintextLength = mesh::ENVELOPE_SIZE + 4;
  uint8_t encoded[mesh::FRAME_HEADER_SIZE];
  assert(mesh::encodeHeader(header, encoded));
  assert(encoded[0] == 'L' && encoded[1] == '3');
  mesh::FrameHeader decoded;
  assert(mesh::decodeHeader(encoded, sizeof(encoded), decoded));
  assert(decoded.crewId == header.crewId && decoded.transmitter == 42);

  uint8_t nonce1[mesh::CCM_NONCE_SIZE], nonce2[mesh::CCM_NONCE_SIZE];
  mesh::buildNonce(header, nonce1);
  ++header.frameSequence;
  mesh::buildNonce(header, nonce2);
  assert(memcmp(nonce1, nonce2, sizeof(nonce1)) != 0);
  header.frameSequence--;
  header.transmitter = 43;
  mesh::buildNonce(header, nonce2);
  assert(memcmp(nonce1, nonce2, sizeof(nonce1)) != 0);

  const mesh::Packet cases[] = {
      message(), message(mesh::PacketType::CrewMessage),
      presence(3, 8, "BRAVO", 20),
  };
  for (const mesh::Packet &source : cases) {
    uint8_t plain[mesh::MAX_PLAINTEXT_SIZE]; size_t length = 0;
    assert(mesh::encodePlaintext(source, plain, sizeof(plain), length));
    mesh::Packet parsed;
    assert(mesh::decodePlaintext(plain, length, parsed));
    assert(parsed.envelope.type == source.envelope.type);
    assert(parsed.envelope.origin == source.envelope.origin);
    assert(parsed.envelope.floodId == source.envelope.floodId);
    if (source.envelope.type == mesh::PacketType::Presence)
      assert(strcmp(parsed.presence.name, "BRAVO") == 0);
    else
      assert(strcmp(parsed.message.text, source.message.text) == 0);
  }
  mesh::Packet ack;
  ack.envelope = message().envelope;
  ack.envelope.type = mesh::PacketType::DeliveryAck;
  ack.ack.messageId = 0xDEADBEEF;
  uint8_t plain[mesh::MAX_PLAINTEXT_SIZE]; size_t length = 0;
  assert(mesh::encodePlaintext(ack, plain, sizeof(plain), length));
  mesh::Packet parsed;
  assert(mesh::decodePlaintext(plain, length, parsed));
  assert(parsed.ack.messageId == 0xDEADBEEF);
}

void testMalformedAndDirectFiltering() {
  mesh::Packet packet = message();
  uint8_t plain[mesh::MAX_PLAINTEXT_SIZE]; size_t length = 0;
  assert(mesh::encodePlaintext(packet, plain, sizeof(plain), length));
  mesh::Packet parsed;
  assert(!mesh::decodePlaintext(plain, length - 1, parsed));
  plain[16] = mesh::MAX_HOPS + 1;
  assert(!mesh::decodePlaintext(plain, length, parsed));
  packet.envelope.hop = mesh::MAX_HOPS + 1;
  assert(!mesh::encodePlaintext(packet, plain, sizeof(plain), length));
  packet = message();
  memset(packet.message.text, 'X', sizeof(packet.message.text));
  assert(!mesh::encodePlaintext(packet, plain, sizeof(plain), length));

  mesh::Envelope direct = message().envelope;
  assert(mesh::shouldStoreMessage(direct, 2));
  assert(!mesh::shouldStoreMessage(direct, 3));
  assert(!mesh::shouldStoreMessage(direct, 1));
  mesh::Envelope crew = message(mesh::PacketType::CrewMessage).envelope;
  assert(mesh::shouldStoreMessage(crew, 2));
  assert(!mesh::shouldStoreMessage(crew, 1));
}

void testDedupRelayQueuesAndWrap() {
  mesh::SeenCache cache;
  const mesh::FloodKey key = mesh::floodKeyFor(message());
  mesh::SeenResult first = cache.observe(key, 2, 1000);
  assert(first.isNew);
  assert(!cache.observe(key, 2, 1100).isNew);
  cache.observe(key, 3, 1200);
  cache.observe(key, 4, 1300);
  assert(cache.entry(first.index)->alternateCount == 2);
  assert(cache.entry(first.index)->duplicateCopies == 3);
  cache.markRelayed(first.index);
  assert(cache.entry(first.index)->relayed);
  assert(cache.find(key, 1300 + mesh::SEEN_EXPIRY_MS) == -1);

  cache.clear();
  const uint32_t nearWrap = 0xFFFFFF00UL;
  first = cache.observe(key, 2, nearWrap);
  assert(cache.find(key, nearWrap + 500) >= 0);
  assert(cache.find(key, nearWrap + mesh::SEEN_EXPIRY_MS) == -1);
  assert(mesh::timeReached(0x10, 0xFFFFFFF0));
  assert(mesh::intervalElapsed(0x10, 0xFFFFFFF0, 0x20));

  mesh::RelayQueue relays;
  mesh::Packet packet = message(); packet.envelope.hop = 2;
  assert(relays.schedule(packet, 5, 100));
  packet.envelope.hop = 1;
  assert(relays.schedule(packet, 6, 200));
  int index = relays.dueIndex(100, false);
  assert(index >= 0 && relays.job(index)->packet.envelope.hop == 1);
  assert(relays.cancel(mesh::floodKeyFor(packet)) == 1);
  for (size_t i = 0; i < mesh::RELAY_QUEUE_SIZE; ++i) {
    packet.envelope.floodId = 100 + i;
    assert(relays.schedule(packet, i, 1));
  }
  packet.envelope.floodId = 999;
  assert(!relays.schedule(packet, 0, 1));

  mesh::AckQueue acks;
  assert(acks.schedule(1, 1, 10));
  assert(acks.schedule(1, 1, 20));  // coalesced
  for (size_t i = 1; i < mesh::ACK_QUEUE_SIZE; ++i)
    assert(acks.schedule(1, i + 1, 10));
  assert(!acks.schedule(2, 99, 10));
}

void testRetries() {
  mesh::DeliveryTracker tracker;
  const uint32_t nearWrap = 0xFFFFFFF0UL;
  tracker.start(0xAA55, 2, false, 7, nearWrap, 32);
  assert(!tracker.sendDue(nearWrap + 31)); assert(tracker.sendDue(nearWrap + 32));
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    const uint32_t sentAt = nearWrap + 32 + attempt * 150;
    tracker.noteSent(sentAt, 100);
    assert(tracker.handleTimeout(sentAt + 100, 50, 3));
    if (attempt < 2) assert(tracker.sendDue(sentAt + 150));
  }
  assert(tracker.phase() == mesh::DeliveryPhase::Failed);
  tracker.start(0xBB66, mesh::BROADCAST_ADDRESS, true, 2, 10, 0);
  assert(!tracker.confirm(0xAA55)); assert(tracker.confirm(0xBB66));
  assert(tracker.phase() == mesh::DeliveryPhase::Delivered);
}

void testDirectoryExpiryConflictEvictionAndStaleGps() {
  crew::PeerDirectory peers;
  mesh::Packet p = presence(2, 1, "BRAVO", 10);
  assert(peers.updatePresence(2, p.envelope.originSession, 1, p.presence,
                              1, 9, -80, 1000) == crew::PresenceUpdate::Added);
  assert(peers.onlineCount(1000) == 1);
  assert(peers.updatePresence(2, p.envelope.originSession, 1, p.presence,
                              1, 9, -80, 1100) == crew::PresenceUpdate::ReplayIgnored);
  p.presence.gpsValid = false;
  assert(peers.updatePresence(2, p.envelope.originSession, 2, p.presence,
                              2, 8, -90, 2000) == crew::PresenceUpdate::Updated);
  const crew::PeerRecord *record = peers.record(peers.find(2));
  assert(record->hasLocation && !record->gpsCurrent);
  assert(peers.locationAgeSeconds(*record, 5000) == 6);
  assert(peers.refreshOnline(2000 + crew::OFFLINE_TIMEOUT_MS));
  assert(!peers.record(peers.find(2))->online);

  p = presence(2, 3, "IMPOSTER", 80);
  assert(peers.updatePresence(2, p.envelope.originSession, 3, p.presence,
                              1, 2, -60, 123000) == crew::PresenceUpdate::AddressConflict);

  peers.clear();
  for (uint16_t address = 2; address < 2 + crew::MAX_PEERS; ++address) {
    p = presence(address, address, "NODE", address);
    assert(peers.updatePresence(address, p.envelope.originSession, address,
        p.presence, 1, address, -70, address * 100) == crew::PresenceUpdate::Added);
  }
  peers.refreshOnline(crew::OFFLINE_TIMEOUT_MS + 5000);
  p = presence(99, 1, "NEW", 99);
  assert(peers.updatePresence(99, p.envelope.originSession, 1, p.presence,
      1, 99, -70, crew::OFFLINE_TIMEOUT_MS + 5000) == crew::PresenceUpdate::Added);
  assert(peers.find(99) >= 0);
}

void testMessageRingAndUnread() {
  crew::MessageHistory history;
  for (uint32_t i = 0; i < 25; ++i) {
    crew::MessageRecord record; record.used = true; record.unread = true;
    record.direction = crew::MessageDirection::Incoming;
    record.source = crew::MessageSource::Radio;
    record.originAddress = 2; record.messageId = i; strcpy(record.text, "x");
    history.add(record);
  }
  assert(history.count() == crew::MESSAGE_HISTORY_SIZE);
  assert(history.unreadCount() == crew::MESSAGE_HISTORY_SIZE);
  assert(!history.containsIncoming(2, 0));
  assert(history.containsIncoming(2, 24));
  const int newest = history.physicalIndexForNewest(0);
  assert(history.record(newest)->messageId == 24);
  assert(history.markRead(newest)); assert(history.unreadCount() == 19);
  const uint32_t revision = history.revision();
  assert(history.find(2, 24) == newest);
  assert(history.markAllRead() == 19); assert(history.unreadCount() == 0);
  assert(history.revision() == revision + 1);
  assert(history.setUnread(newest, true)); assert(history.unreadCount() == 1);
  assert(history.setUnread(newest, false)); assert(history.unreadCount() == 0);
}

void testMobileFraming() {
  const char *json =
      "{\"v\":1,\"op\":\"send_message\",\"args\":{\"destination\":2,"
      "\"text\":\"Meet at gate 2\"}}";
  const size_t jsonLength = strlen(json);
  const size_t capacities[] = {20, 182, 244};
  for (const size_t capacity : capacities) {
    mobile::Reassembler reassembler;
    size_t offset = 0;
    while (offset < jsonLength) {
      uint8_t fragment[244]; size_t written = 0, next = 0;
      assert(mobile::buildFragment(77, json, jsonLength, offset, capacity,
                                   fragment, written, next));
      uint16_t completedId = 0; const char *completed = nullptr;
      size_t completedLength = 0;
      const mobile::FragmentResult result = reassembler.ingest(
          fragment, written, completedId, completed, completedLength);
      if (next == jsonLength) {
        assert(result == mobile::FragmentResult::Complete);
        assert(completedId == 77 && completedLength == jsonLength);
        assert(strcmp(completed, json) == 0);
      } else {
        assert(result == mobile::FragmentResult::Accepted);
      }
      offset = next;
    }
  }

  uint8_t fragment[32]; size_t written = 0, next = 0;
  assert(mobile::buildFragment(10, json, jsonLength, 12, sizeof(fragment),
                               fragment, written, next));
  mobile::Reassembler reassembler; uint16_t completedId = 0;
  const char *completed = nullptr; size_t completedLength = 0;
  assert(reassembler.ingest(fragment, written, completedId, completed,
                            completedLength) ==
         mobile::FragmentResult::OutOfOrder);
  fragment[0] = 9;
  assert(reassembler.ingest(fragment, written, completedId, completed,
                            completedLength) == mobile::FragmentResult::Invalid);

  uint8_t oversized[9] = {mobile::TRANSPORT_VERSION,
                          mobile::FLAG_START | mobile::FLAG_END,
                          0, 1, 2, 1, 0, 0, 'x'};  // Total length 513.
  assert(reassembler.ingest(oversized, sizeof(oversized), completedId,
                            completed, completedLength) ==
         mobile::FragmentResult::TooLarge);
  char tooLarge[mobile::MAX_JSON_SIZE + 2]; memset(tooLarge, 'x', sizeof(tooLarge));
  assert(!mobile::buildFragment(1, tooLarge, sizeof(tooLarge), 0, 20,
                                fragment, written, next));
}

void testUiNavigationFilteringAndFormatting() {
  ui::Navigator navigation;
  assert(navigation.current() == ui::Screen::Home && navigation.depth() == 0);
  navigation.open(ui::Screen::MessagesHub, true);
  assert(navigation.move(1, 5)); assert(navigation.selection() == 1);
  navigation.open(ui::Screen::Sent, true);
  assert(navigation.move(-1, 3)); assert(navigation.selection() == 2);
  assert(navigation.back());
  assert(navigation.current() == ui::Screen::MessagesHub);
  assert(navigation.selection() == 1);  // Per-screen selection is remembered.
  navigation.home();
  assert(navigation.current() == ui::Screen::Home && navigation.depth() == 0);

  crew::MessageHistory messages;
  for (uint32_t i = 0; i < 5; ++i) {
    crew::MessageRecord record; record.used = true; record.originAddress = 2;
    record.messageId = i; record.storedAt = 1000 + i * 1000; strcpy(record.text, "x");
    record.direction = i % 2 ? crew::MessageDirection::Outgoing
                             : crew::MessageDirection::Incoming;
    messages.add(record);
  }
  assert(ui::filteredMessageCount(messages, crew::MessageDirection::Incoming) == 3);
  assert(ui::filteredMessageCount(messages, crew::MessageDirection::Outgoing) == 2);
  const int newestIncoming = ui::filteredMessageIndex(
      messages, crew::MessageDirection::Incoming, 0);
  assert(messages.record(newestIncoming)->messageId == 4);
  assert(ui::filteredMessageIndex(messages, crew::MessageDirection::Outgoing, 1) >= 0);

  assert(ui::rssiBars(-60) == 4); assert(ui::rssiBars(-80) == 3);
  assert(ui::rssiBars(-100) == 2); assert(ui::rssiBars(-115) == 1);
  assert(ui::rssiBars(-121) == 0);
  assert(ui::ageSeconds(0x000003E8UL, 0xFFFFFF00UL) == 1);
  char text[16]; ui::formatAge(61000, 0, text, sizeof(text));
  assert(strcmp(text, "1m") == 0);
  ui::formatUptime(3661000, text, sizeof(text));
  assert(strcmp(text, "01:01:01") == 0);
}

void testFeaturePhoneInput() {
  input::FeatureInput in;
  assert(in.press('2', 0) == input::Event::Changed);
  assert(in.press('2', 100) == input::Event::Changed);
  assert(strcmp(in.text(), "B") == 0);  // Abc starts uppercase.
  in.press('0', 1000); in.press('2', 1100);
  assert(strcmp(in.text(), "B a") == 0);
  in.press('1', 2000);                 // '.'
  in.press('0', 3000); in.press('2', 3100);
  assert(strcmp(in.text(), "B a. A") == 0);  // sentence capitalization
  in.press('A', 4000); assert(in.mode() == input::Mode::Lower);
  in.press('A', 4100); assert(in.mode() == input::Mode::Upper);
  in.press('A', 4200); assert(in.mode() == input::Mode::Numeric);
  const size_t before = in.length(); in.press('7', 4300); in.press('7', 4350);
  assert(in.length() == before + 2 && in.text()[before] == '7');
  assert(in.press('*', 5000) == input::Event::OpenSymbols);
  assert(in.insertSymbol('@') == input::Event::Changed);
  assert(in.press('C', 5100) == input::Event::Changed);
  char eighty[mesh::MAX_TEXT_LENGTH + 1]; memset(eighty, 'x', mesh::MAX_TEXT_LENGTH);
  eighty[mesh::MAX_TEXT_LENGTH] = 0; assert(in.setText(eighty));
  assert(in.press('1', 6000) == input::Event::Full);
  assert(in.press('D', 7000) == input::Event::Back);
}

void testRadar() {
  const radar::RelativePosition north = radar::relativePosition(0, 0, 0.001, 0);
  const radar::RelativePosition east = radar::relativePosition(0, 0, 0, 0.001);
  const radar::RelativePosition south = radar::relativePosition(0, 0, -0.001, 0);
  const radar::RelativePosition west = radar::relativePosition(0, 0, 0, -0.001);
  assert(north.bearingDegrees < 1 || north.bearingDegrees > 359);
  assert(fabs(east.bearingDegrees - 90) < 1);
  assert(fabs(south.bearingDegrees - 180) < 1);
  assert(fabs(west.bearingDegrees - 270) < 1);
  assert(radar::selectRangeMeters(99) == 100);
  assert(radar::selectRangeMeters(101) == 250);
  assert(radar::selectRangeMeters(20000) == 10000);
  radar::PlotPoint point = radar::project(east, 100, 100, 100, 50);
  assert(point.clamped && point.x == 150);
}

}  // namespace

int main() {
  testHeaderNonceAndPlaintext();
  testMalformedAndDirectFiltering();
  testDedupRelayQueuesAndWrap();
  testRetries();
  testDirectoryExpiryConflictEvictionAndStaleGps();
  testMessageRingAndUnread();
  testMobileFraming();
  testUiNavigationFilteringAndFormatting();
  testFeaturePhoneInput();
  testRadar();
  puts("mesh_logic_tests: all tests passed");
  return 0;
}
