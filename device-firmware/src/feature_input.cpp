#include "feature_input.h"

#include "mesh_state.h"

#include <ctype.h>
#include <string.h>

namespace input {

FeatureInput::FeatureInput() { clear(); }

void FeatureInput::clear() {
  memset(text_, 0, sizeof(text_));
  length_ = 0;
  mode_ = Mode::Abc;
  commitTap();
}

bool FeatureInput::setText(const char *text) {
  if (text == nullptr) {
    return false;
  }
  const size_t length = strnlen(text, mesh::MAX_TEXT_LENGTH + 1);
  if (length > mesh::MAX_TEXT_LENGTH) {
    return false;
  }
  memcpy(text_, text, length + 1);
  length_ = length;
  commitTap();
  return true;
}

void FeatureInput::commitTap() {
  tapPending_ = false;
  pendingKey_ = '\0';
  pendingIndex_ = 0;
}

bool FeatureInput::append(char value) {
  if (length_ >= mesh::MAX_TEXT_LENGTH) {
    return false;
  }
  text_[length_++] = value;
  text_[length_] = '\0';
  return true;
}

bool FeatureInput::shouldAutoCap(size_t index) const {
  if (index == 0) {
    return true;
  }
  size_t cursor = index;
  while (cursor > 0 && text_[cursor - 1] == ' ') {
    --cursor;
  }
  if (cursor == 0) {
    return true;
  }
  const char previous = text_[cursor - 1];
  return previous == '.' || previous == '!' || previous == '?';
}

char FeatureInput::applyMode(char value, size_t index) const {
  if (!isalpha(static_cast<unsigned char>(value))) {
    return value;
  }
  if (mode_ == Mode::Upper) {
    return static_cast<char>(toupper(static_cast<unsigned char>(value)));
  }
  if (mode_ == Mode::Lower) {
    return static_cast<char>(tolower(static_cast<unsigned char>(value)));
  }
  if (mode_ == Mode::Abc) {
    return shouldAutoCap(index)
               ? static_cast<char>(toupper(static_cast<unsigned char>(value)))
               : static_cast<char>(tolower(static_cast<unsigned char>(value)));
  }
  return value;
}

const char *FeatureInput::mapping(char key) const {
  switch (key) {
    case '1':
      return ".,!?1";
    case '2':
      return "ABC2";
    case '3':
      return "DEF3";
    case '4':
      return "GHI4";
    case '5':
      return "JKL5";
    case '6':
      return "MNO6";
    case '7':
      return "PQRS7";
    case '8':
      return "TUV8";
    case '9':
      return "WXYZ9";
    default:
      return nullptr;
  }
}

Event FeatureInput::press(char key, uint32_t now) {
  if (key == 'A') {
    commitTap();
    mode_ = static_cast<Mode>((static_cast<uint8_t>(mode_) + 1U) % 4U);
    return Event::Changed;
  }
  if (key == 'C') {
    commitTap();
    if (length_ > 0) {
      text_[--length_] = '\0';
      return Event::Changed;
    }
    return Event::None;
  }
  if (key == '#') {
    commitTap();
    return length_ > 0 ? Event::Send : Event::None;
  }
  if (key == 'D') {
    commitTap();
    return Event::Back;
  }
  if (key == '*') {
    commitTap();
    return Event::OpenSymbols;
  }

  if (mode_ == Mode::Numeric && key >= '0' && key <= '9') {
    commitTap();
    return append(key) ? Event::Changed : Event::Full;
  }
  if (key == '0') {
    commitTap();
    return append(' ') ? Event::Changed : Event::Full;
  }

  const char *characters = mapping(key);
  if (characters == nullptr) {
    return Event::None;
  }
  const size_t count = strlen(characters);
  if (tapPending_ && pendingKey_ == key &&
      !mesh::intervalElapsed(now, pendingAt_, MULTITAP_WINDOW_MS) &&
      length_ > 0) {
    pendingIndex_ = static_cast<uint8_t>((pendingIndex_ + 1U) % count);
    text_[length_ - 1] = applyMode(characters[pendingIndex_], length_ - 1);
    pendingAt_ = now;
    return Event::Changed;
  }

  commitTap();
  if (!append(applyMode(characters[0], length_))) {
    return Event::Full;
  }
  tapPending_ = true;
  pendingKey_ = key;
  pendingAt_ = now;
  return Event::Changed;
}

Event FeatureInput::tick(uint32_t now) {
  if (tapPending_ &&
      mesh::intervalElapsed(now, pendingAt_, MULTITAP_WINDOW_MS)) {
    commitTap();
    return Event::Changed;
  }
  return Event::None;
}

Event FeatureInput::insertSymbol(char symbol) {
  commitTap();
  if (symbol < 0x20 || symbol > 0x7e) {
    return Event::None;
  }
  return append(symbol) ? Event::Changed : Event::Full;
}

void FeatureInput::setMode(Mode mode) {
  commitTap();
  mode_ = mode;
}

const char *FeatureInput::modeLabel() const {
  switch (mode_) {
    case Mode::Abc:
      return "Abc";
    case Mode::Lower:
      return "abc";
    case Mode::Upper:
      return "ABC";
    case Mode::Numeric:
      return "123";
  }
  return "Abc";
}

}  // namespace input
