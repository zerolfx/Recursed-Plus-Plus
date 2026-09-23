#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace peek {
// One tick of play as Game::update saw it: the controls, the step it was given, and a digest of
// the room it left behind. A replay feeds the controls back in order and compares the digests.
// heldBefore is what was held on the tick of play before this one. That is the previous recorded
// tick, except where play resumed after a rewind: there it is what was held when the rewind was
// asked for, so a key kept down through a rewind does not start an action it never started.
struct PlayTick {uint16_t controls=0;float step=0;uint32_t digest=0;uint8_t heldBefore=0;};
// The game's input layer keeps two bytes per action: whether it is held, and whether that
// changed at the last poll. Controls pack the first set into the low byte, the second into the high.
enum Control {ControlUp,ControlDown,ControlLeft,ControlRight,ControlJump,ControlUse,ControlPause,ControlSpare};
constexpr bool held(uint16_t controls,Control c){return (controls>>c)&1;}
constexpr bool heldBefore(uint16_t controls,Control c){return (controls>>(8+c))&1;}
// An action starts where Jump or Use is pressed, or where any control is pressed from rest.
// Changing direction while already moving continues the same action. Each tick is compared with
// what was held before it rather than with the game's own changed byte, which stands still while
// the window is out of focus and would repeat one press on every tick of that.
bool startsAction(uint16_t before,uint16_t now);
// Ticks to replay to stand just before the latest action that started before tick 'now'.
// With no action before 'now' there is nothing to undo, and the answer is 'now' itself.
size_t beforeLastAction(const std::vector<PlayTick>& ticks,size_t now);
// Ticks to replay to stand 'seconds' of play before tick 'now', counting the steps actually taken;
// the start of the mission when less than that has been played.
size_t secondsBefore(const std::vector<PlayTick>& ticks,size_t now,double seconds);
}
