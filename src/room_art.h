#pragma once
#include "snapshot.h"
#include <cstdint>
namespace peek {
struct RoomArt {int width=800,height=600;std::vector<uint32_t> pixels;uint64_t revision=0;std::string note;};
// Shared raster image for the OpenGL inset and the native preview window.
const RoomArt& renderRoomArt(const std::string& root,const Snapshot& snapshot,double seconds=-1);
}
