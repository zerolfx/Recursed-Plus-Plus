#pragma once
#include <cstdint>
#include <string>
namespace peek {
// Going back in a mission. The game has no state to restore, so a rewind asks for the game's own
// Restart and then runs the recorded ticks again, in one frame, up to the moment asked for.
enum class Rewind {None,Action,Seconds,Verify};
using RewindLog=void(*)(const char*,...);
bool installRewind(uintptr_t base,RewindLog log);
// Taken up on the next tick of play. Verify replays every tick and is for development runs.
void requestRewind(Rewind kind);
// Something the player should know after a rewind, such as there being nothing to go back to,
// and how long it has left on screen. Empty when there is nothing to say.
std::string rewindNotice(uint64_t now);
// True while recorded ticks are being run again.
bool rewindReplaying();
// Whether the game's own controls, as the player has set them, use this SFML key, this joystick
// button, or this joystick axis pushed below or above its centre. An undo key gives way to them.
bool gameBindsKey(int key);
bool gameBindsButton(int button);
bool gameBindsAxis(int axis,bool below);
}
