module;
#include "prelude.hpp"
export module keyboard;

export struct KeyboardMode {
  auto read_char() const -> std::optional<unsigned char> {
    return PlatformTerminal::read_char();
  }
};
