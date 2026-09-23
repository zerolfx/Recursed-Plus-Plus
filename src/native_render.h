#pragma once
#include "room_art.h"
namespace peek {
// Experimental replay of the active room through the original renderer.
bool installNativeRender(uintptr_t base);
void requestNativeMirror(bool enabled);
bool nativeMirrorRequested();
const RoomArt* nativeMirrorArt();
const std::string& nativeRenderStatus();
void requestNativeDestination(uintptr_t host,const Snapshot& snapshot,const std::string& key,int depth);
const RoomArt* nativeDestinationArt(const std::string& key);
const Snapshot* nativeDestinationSnapshot(const std::string& key);
// The destination was asked for and has neither been drawn nor failed yet.
bool nativeDestinationPending(const std::string& key);
bool nativeRenderWork();
// Sound starts refused while building, drawing or tearing down a preview scene.
uint32_t nativeSilencedSounds();
// Between these, rand() outside preview work draws from 'random' rather than the C runtime, and
// with 'silent' every sound start is refused. The rewind brackets each game tick with them.
void enterGameplay(uint32_t* random,bool silent);
void leaveGameplay();
// Sound starts refused while a rewind was catching up.
uint32_t replaySilencedSounds();
}
