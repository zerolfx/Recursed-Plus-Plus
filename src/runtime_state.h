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
// stack names a timeline whose rooms a cauldron's switch put aside, to read those instead of the
// ones being played; ancestors then counts out from the room that switch would return to.
RoomReference readRoomReference(uintptr_t host,uintptr_t sourceRoom,int ancestors,const std::string& stack={});
// A jar names a saved instance, not a Lua room. An unused name enters the glitch room.
RoomReference readJarReference(uintptr_t host,uintptr_t sourceRoom,const std::string& jar);
Snapshot readJarSnapshot(uintptr_t host,uintptr_t sourceRoom,const std::string& jar,const Snapshot& appearance);
// Where walking out through a flame leads. The room below takes the player back only if what
// its player went in through is still in it once the room has taken its globals back, or is
// carried out; otherwise the game leaves the whole stack behind and builds a paradox room.
// from counts the flame's room out from the room being played; a flame further out walks out of
// every room in between first. Bit n of greens marks the flame out of the room n out as green:
// a green flame takes nothing through, so what is held is put down before it. With stack, the
// walk is out of the rooms a cauldron into that timeline returns to, after that switch.
struct ExitTarget {
    bool available=false,paradox=false;
    std::string room,error;
    int ancestors=-1; // Distance out of the room below, when there is no paradox.
    int depth=-1; // The room's depth in its stack, when there is no paradox.
    // What the room walked into takes back: the globals the room below had put aside when it was
    // left, or what a paradox room restores.
    GlobalState globals;
};
ExitTarget readExitTarget(uintptr_t host,uintptr_t sourceRoom,int from,uint32_t greens,const std::string& stack={});
// Where a cauldron into timeline leads. The switch (0x440860) puts the whole stack aside under the
// timeline being played and takes back the one saved under the named timeline, whose top room
// then takes its globals back and is reactivated. A timeline with no stack saved gets its first
// room, named after it, built fresh, dry and alone. Into the timeline being played, the same stack
// comes back. A player in the room returned to that finds its way back gone sets off a paradox.
// through is the cauldron the player of the room being played goes in through, when known: that
// player needs it back when the switch returns to the same stack.
struct CauldronTarget {
    bool available=false;
    bool same=false;    // The timeline being played: the room being played, with the globals it takes back.
    bool fresh=false;   // Built fresh as the first room of its timeline, alone on its stack.
    bool paradox=false; // Built fresh as the paradox room, because the room returned to lost its way back.
    std::string room,error;
    int depth=-1;       // The room's depth in its timeline's stack.
    // What the room takes back: the globals restored into a room returned to, or those saved
    // under the name of a room built fresh.
    GlobalState globals;
};
CauldronTarget readCauldronTarget(uintptr_t host,uintptr_t sourceRoom,const std::string& timeline,uintptr_t through=0);
// The timeline being played: the room it started from, which names its colours.
std::string readTimeline(uintptr_t host);
Snapshot readRoomSnapshot(uintptr_t host,uintptr_t sourceRoom,int ancestors,const Snapshot& appearance,const std::string& stack={});
bool wetAt(const Snapshot& room, float x, float y);
void applyGlobals(Snapshot& room, const GlobalState& state);
}
