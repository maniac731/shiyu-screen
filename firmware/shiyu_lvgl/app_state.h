#pragma once

#include <stdint.h>

enum class AppMode : uint8_t {
  Active,
  Ambient,
};

enum class ActiveScreen : uint8_t {
  Home,
  Settings,
  Upload,
};

