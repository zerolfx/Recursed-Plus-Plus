#pragma once
#include "snapshot.h"
#include <cstdint>

namespace peek {
// Read-only view of the verified x86 build. Never owns game pointers.
struct GlobalState {
    bool available = false;
    bool initialized = false;
    std::vector<Object> objects;
    std::string error;
};
GlobalState readGlobals(uintptr_t host, uintptr_t sourceRoom, const std::string& target);
bool wetAt(const Snapshot& room, float x, float y);
void applyGlobals(Snapshot& room, const GlobalState& state);
}
