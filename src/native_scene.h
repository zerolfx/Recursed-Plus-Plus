#pragma once
#include "snapshot.h"
#include <cstdint>
namespace peek {
void initNativeScene(uintptr_t base);
// All entities and draw resources are owned by the preview, never by the live room.
bool prepareNativeScene(uintptr_t liveHost,const Snapshot& snapshot,const std::string& key,int depth);
void* nativeSceneRenderer();
const Snapshot& nativeSceneSnapshot();
float advanceNativeScene(float seconds);
void clearNativeScene();
const std::string& nativeSceneError();
}
