#pragma once

#include <Arduino.h>

#ifndef RETERMINAL_MODEL
#define RETERMINAL_MODEL 1005
#endif

#include "panel_traits.h"

namespace config {

constexpr int MODEL = panel_traits::MODEL;
constexpr int PANEL_WIDTH = panel_traits::WIDTH;
constexpr int PANEL_HEIGHT = panel_traits::HEIGHT;

}  // namespace config
