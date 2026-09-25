#pragma once
#include <array>
#include <string>
#include <vector>
#include <cstdint>
namespace peek {
struct Tile {int kind=0;int frame=0;std::string definition;int nativeIndex=-1;};
// settle: placed the way a spawn is placed even in a captured room, as a global a room takes back is.
struct Object {std::string kind,target;float x=0,y=0;bool global=false;uintptr_t sourceId=0;bool settle=false;};
struct Snapshot {
 std::array<Tile,300> tiles{};
 std::vector<Object> objects;
 std::string error;
 bool hasGlobals=false;
 bool live=false; // Captured room instance: preserve positions without settling.
 int nativeDepth=-1; // Absolute engine depth, if known; root is zero.
 std::string tileset="tiles/wip",pattern;
 std::array<float,3> dark{.17f,.19f,.38f},light{.22f,.24f,.5f};
 // Set when the room belongs to another timeline than the one being played, as a paradox room
 // does: the game draws a timeline in the colours it names.
 std::string timeline;
};
// timeline picks the colours, as the game does by the name of the timeline being played. The game
// builds an empty room where a script has no function for it; a paradox room is where that
// happens in play, so emptyWhenMissing builds one instead of reporting the gap.
Snapshot loadSnapshot(const std::string& gameRoot,const std::string& mission,const std::string& room,bool wet,const std::string& timeline="start",bool emptyWhenMissing=false);
}
