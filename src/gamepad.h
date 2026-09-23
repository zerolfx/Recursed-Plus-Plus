#pragma once
#include <cstdint>
namespace peek {
using GamepadLog=void(*)(const char*,...);
// Takes over the game's input poll: a controller connected after the game started is taken into
// the game's controls as if it had been there from the start, and the gamepad's undo buttons are
// read where the game reads its own, so only while the game window has the focus.
bool installGamepad(uintptr_t base,GamepadLog log);
}
