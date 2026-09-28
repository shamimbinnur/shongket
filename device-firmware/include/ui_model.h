#pragma once

#include "crew_data.h"

#include <stddef.h>
#include <stdint.h>

namespace ui {

enum class Screen : uint8_t {
  Home = 0,
  MessagesHub,
  Inbox,
  Sent,
  MessageView,
  Crew,
  Member,
  Recipients,
  Compose,
  ComposeOptions,
  Symbols,
  Radar,
  Phone,
  Status,
  Diagnostics,
  Options,
  Count,
};

class Navigator {
 public:
  Navigator();
  void reset();
  void open(Screen screen, bool resetSelection = false);
  bool back();
  void home();
  Screen current() const { return current_; }
  uint8_t selection() const;
  void setSelection(uint8_t value);
  bool move(int delta, size_t itemCount);
  uint8_t depth() const { return depth_; }

 private:
  static constexpr uint8_t STACK_DEPTH = 8;
  Screen current_ = Screen::Home;
  Screen stack_[STACK_DEPTH] = {};
  uint8_t depth_ = 0;
  uint8_t selections_[static_cast<uint8_t>(Screen::Count)] = {};
};

size_t filteredMessageCount(const crew::MessageHistory &history,
                            crew::MessageDirection direction);
int filteredMessageIndex(const crew::MessageHistory &history,
                         crew::MessageDirection direction,
                         size_t newestOffset);
uint8_t rssiBars(int16_t rssi);
uint32_t ageSeconds(uint32_t now, uint32_t storedAt);
void formatAge(uint32_t now, uint32_t storedAt, char *output,
               size_t capacity);
void formatUptime(uint32_t now, char *output, size_t capacity);

}  // namespace ui
