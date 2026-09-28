#pragma once

#include "mesh_protocol.h"

#include <stddef.h>
#include <stdint.h>

namespace input {

constexpr uint32_t MULTITAP_WINDOW_MS = 800;
constexpr char SYMBOLS[] = "@#$_&-+()/%*=<>[]{}:;\"'";

enum class Mode : uint8_t {
  Abc = 0,
  Lower,
  Upper,
  Numeric,
};

enum class Event : uint8_t {
  None = 0,
  Changed,
  Send,
  Back,
  OpenSymbols,
  Full,
};

class FeatureInput {
 public:
  FeatureInput();
  void clear();
  bool setText(const char *text);
  Event press(char key, uint32_t now);
  Event tick(uint32_t now);
  Event insertSymbol(char symbol);
  void setMode(Mode mode);

  const char *text() const { return text_; }
  size_t length() const { return length_; }
  Mode mode() const { return mode_; }
  const char *modeLabel() const;
  bool tapPending() const { return tapPending_; }

 private:
  void commitTap();
  bool append(char value);
  bool shouldAutoCap(size_t index) const;
  char applyMode(char value, size_t index) const;
  const char *mapping(char key) const;

  char text_[mesh::MAX_TEXT_LENGTH + 1] = {};
  size_t length_ = 0;
  Mode mode_ = Mode::Abc;
  bool tapPending_ = false;
  char pendingKey_ = '\0';
  uint8_t pendingIndex_ = 0;
  uint32_t pendingAt_ = 0;
};

}  // namespace input
