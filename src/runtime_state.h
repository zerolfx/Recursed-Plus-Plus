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
// A jar names a saved instance, not a Lua room. An unused name enters the glitch room.
RoomReference readJarReference(uintptr_t host,uintptr_t sourceRoom,const std::string& jar);
Snapshot readJarSnapshot(uintptr_t host,uintptr_t sourceRoom,const std::string& jar,const Snapshot& appearance);
// Where walking out through a flame leads. The room below takes the player back only if what
// its player went in through is still in it once the room has taken its globals back, or is
// carried out; otherwise the game leaves the whole stack behind and builds a paradox room.
// from counts the flame's room out from the room being played; a flame further out walks out of
// every room in between first. Bit n of greens marks the flame out of the room n out as green:
// a green flame takes nothing through, so what is held is put down before it.
struct ExitTarget {
    bool available=false,paradox=false;
    std::string room,error;
    int ancestors=-1; // Distance out of the room below, when there is no paradox.
    // What the room walked into takes back: the globals the room below had put aside when it was
    // left, or what a paradox room restores.
    GlobalState globals;
};
ExitTarget readExitTarget(uintptr_t host,uintptr_t sourceRoom,int from,uint32_t greens);
// The timeline being played: the room it started from, which names its colours.
std::string readTimeline(uintptr_t host);
Snapshot readRoomSnapshot(uintptr_t host,uintptr_t sourceRoom,int ancestors,const Snapshot& appearance);
bool wetAt(const Snapshot& room, float x, float y);
void applyGlobals(Snapshot& room, const GlobalState& state);
}
