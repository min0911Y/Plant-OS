#pragma once

#include <chrono>
#include <optional>
#include <string_view>
#include <syscall.h>
#include <thread>
#include <tty_rpc.h>

struct PlatformTerminal {
  static inline bool testing = false;
  static auto read_char() -> std::optional<unsigned char> {
    static std::string_view pending;
    if (pending.empty()) {
      if (!_kbhit())
        return std::nullopt;
      const int key = getch();
      switch (key) {
      case KEY_INPUT_UP:
        pending = "\033[A";
        break;
      case KEY_INPUT_DOWN:
        pending = "\033[B";
        break;
      case KEY_INPUT_RIGHT:
        pending = "\033[C";
        break;
      case KEY_INPUT_LEFT:
        pending = "\033[D";
        break;
      default:
        return key >= 0 && key <= 255 ? std::optional<unsigned char>(key) : std::nullopt;
      }
    }
    const auto byte = static_cast<unsigned char>(pending.front());
    pending.remove_prefix(1);
    return byte;
  }

  static void wait_input(int milliseconds) {
    if (!_kbhit())
      std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
  }

};
