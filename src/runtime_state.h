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
struct RoomReference {uintptr_t room=0;std::string name,error;int depth=-1;};
RoomReference readRoomReference(uintptr_t host,uintptr_t sourceRoom,int ancestors);
Snapshot readRoomSnapshot(uintptr_t host,uintptr_t sourceRoom,int ancestors,const Snapshot& appearance);
bool wetAt(const Snapshot& room, float x, float y);
void applyGlobals(Snapshot& room, const GlobalState& state);
}
