#pragma once
#include <array>
#include <string>
#include <vector>
namespace peek {
struct Tile {int kind=0;int frame=0;std::string definition;};
struct Object {std::string kind,target;float x=0,y=0;bool global=false;};
struct Snapshot {
 std::array<Tile,300> tiles{};
 std::vector<Object> objects;
 std::string error;
 bool hasGlobals=false;
 std::string tileset="tiles/wip",pattern;
 std::array<float,3> dark{.17f,.19f,.38f},light{.22f,.24f,.5f};
};
Snapshot loadSnapshot(const std::string& gameRoot,const std::string& mission,const std::string& room,bool wet);
}
