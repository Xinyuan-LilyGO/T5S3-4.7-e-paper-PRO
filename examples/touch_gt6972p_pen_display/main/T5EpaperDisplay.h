// SPDX-License-Identifier: MIT
#pragma once

#include <M5GFX.h>

namespace t5epd {

bool begin();
lgfx::LovyanGFX &display();
bool present(int x, int y, int width, int height);
bool clean();
uint32_t vsyncCount();

} // namespace t5epd
