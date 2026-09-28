#include "ui_model.h"

#include <stdio.h>
#include <string.h>

namespace ui {

Navigator::Navigator() { reset(); }

void Navigator::reset() {
  current_ = Screen::Home;
  depth_ = 0;
  memset(stack_, 0, sizeof(stack_));
  memset(selections_, 0, sizeof(selections_));
}

void Navigator::open(Screen screen, bool resetSelection) {
  if (screen == current_) {
    if (resetSelection) setSelection(0);
    return;
  }
  if (depth_ < STACK_DEPTH) {
    stack_[depth_++] = current_;
  } else {
    memmove(stack_, stack_ + 1, sizeof(stack_) - sizeof(stack_[0]));
    stack_[STACK_DEPTH - 1] = current_;
  }
  current_ = screen;
  if (resetSelection) setSelection(0);
}

bool Navigator::back() {
  if (depth_ == 0) {
    home();
    return false;
  }
  current_ = stack_[--depth_];
  return true;
}

void Navigator::home() {
  current_ = Screen::Home;
  depth_ = 0;
}

uint8_t Navigator::selection() const {
  return selections_[static_cast<uint8_t>(current_)];
}

void Navigator::setSelection(uint8_t value) {
  selections_[static_cast<uint8_t>(current_)] = value;
}

bool Navigator::move(int delta, size_t itemCount) {
  if (itemCount == 0 || delta == 0) return false;
  int value = static_cast<int>(selection()) + delta;
  while (value < 0) value += static_cast<int>(itemCount);
  while (value >= static_cast<int>(itemCount))
    value -= static_cast<int>(itemCount);
  setSelection(static_cast<uint8_t>(value));
  return true;
}

size_t filteredMessageCount(const crew::MessageHistory &history,
                            crew::MessageDirection direction) {
  size_t count = 0;
  for (size_t offset = 0; offset < history.count(); ++offset) {
    const crew::MessageRecord *record =
        history.record(history.physicalIndexForNewest(offset));
    if (record && record->used && record->direction == direction) ++count;
  }
  return count;
}

int filteredMessageIndex(const crew::MessageHistory &history,
                         crew::MessageDirection direction,
                         size_t newestOffset) {
  size_t matched = 0;
  for (size_t offset = 0; offset < history.count(); ++offset) {
    const int physical = history.physicalIndexForNewest(offset);
    const crew::MessageRecord *record = history.record(physical);
    if (!record || !record->used || record->direction != direction) continue;
    if (matched++ == newestOffset) return physical;
  }
  return -1;
}

uint8_t rssiBars(int16_t rssi) {
  if (rssi >= -75) return 4;
  if (rssi >= -90) return 3;
  if (rssi >= -105) return 2;
  if (rssi >= -120) return 1;
  return 0;
}

uint32_t ageSeconds(uint32_t now, uint32_t storedAt) {
  return static_cast<uint32_t>(now - storedAt) / 1000UL;
}

void formatAge(uint32_t now, uint32_t storedAt, char *output,
               size_t capacity) {
  if (!output || capacity == 0) return;
  const uint32_t seconds = ageSeconds(now, storedAt);
  if (seconds < 60) snprintf(output, capacity, "%lus", static_cast<unsigned long>(seconds));
  else if (seconds < 3600)
    snprintf(output, capacity, "%lum", static_cast<unsigned long>(seconds / 60));
  else if (seconds < 86400)
    snprintf(output, capacity, "%luh", static_cast<unsigned long>(seconds / 3600));
  else snprintf(output, capacity, "%lud", static_cast<unsigned long>(seconds / 86400));
}

void formatUptime(uint32_t now, char *output, size_t capacity) {
  if (!output || capacity == 0) return;
  const uint32_t total = now / 1000UL;
  const uint32_t hours = total / 3600UL;
  const uint32_t minutes = (total / 60UL) % 60UL;
  const uint32_t seconds = total % 60UL;
  snprintf(output, capacity, "%02lu:%02lu:%02lu",
           static_cast<unsigned long>(hours),
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
}

}  // namespace ui
