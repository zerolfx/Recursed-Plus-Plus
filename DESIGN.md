# Preview design

## Experience

Make room nesting visible without changing Recursed's movement, physics, or entry rules. A chest is too small to contain a useful complete image, so hovering opens an enlarged preview. Clicking pins it; Shift + click opens a separate window. Both presentations share one bounded navigation path.

The current implementation supports eight levels, Backspace to go back, Esc to close, right click to close a pinned preview, and O to switch presentation without losing the path. A close leaves the chest under the pointer dismissed until the pointer reaches another one. The separate window displays depth and destination. A short underline identifies the hovered chest; persistent debugging rectangles are not part of the presentation. Inside a pinned inline preview, hovering a chest or flame adds the depth and room a click there opens to the depth line, computed by the same step the click takes, so the line cannot promise a different destination than the click reaches.

## State semantics

A preview answers what a future entry would construct, not what the player saw on the previous visit.

- Ordinary chests construct fresh room contents. Initial script declarations remain the baseline.
- Global objects retain their saved identity and positions. An initialized but empty saved list must suppress initial global declarations.
- Wetness can select a different Lua branch and layout. A visual blue overlay is insufficient.
- Self-referencing previews use the current room's global state because real entry saves it first.
- Return portals resolve existing ancestors in the real room stack; their tiles and object positions are resampled.
- Carried-item branches and successive hypothetical entries require additional entry-context modeling.
- Jars preserve instances, and cauldrons have different transition rules. A room name alone cannot represent their state.

Fresh destinations reuse native spawn collision placement. They do not run the full ongoing physics update. Outer snapshots preserve existing positions. Water follows game state without a manual override. The game remains active while previews are open; a focused separate preview window suppresses game keyboard queries.

## Data and rendering

```text
Chest hit test and target
  -> wet condition and bounded navigation path
  -> independent Lua snapshot
  -> read-only global-state merge
  -> isolated native scene
  -> original renderer into a private framebuffer
  -> in-game overlay or separate window
```

The original renderer is the preferred path. It provides native terrain, background depth, lighting, models, particles, and water. Snapshots retain tile definition names so native tile IDs can be resolved through the live level's metadata. Sprite frame numbers are not native tile IDs.

The preview owns its Room, entities, Renderer, and copied stack. It borrows level metadata only while that level is active. Native allocations use the game's allocator and matching destructors. Room numbering and preview random-number consumption are isolated. Visual transforms advance on a private clock while room physics is frozen.

Unsupported native entity kinds cause the entire scene to use the existing resource-based renderer. That renderer stands in only once a native draw has failed. A new destination is drawn on the game's next render, so until that draw lands or fails, bounded at 300 ms, an in-game panel that is not up yet is not shown and one that is up for the same chest keeps the image it has. The held panel takes no clicks, because they would be aimed at the room it still shows. A preview that ends is never held over the next one. The separate window keeps its last picture and title until the new room is drawn, and one just opened stays empty until then. Renderer details belong in diagnostics and documentation, not player-facing labels. Rendering fidelity does not imply exact state simulation.

## Boundaries

Do not invoke the real chest-entry path for a preview: it modifies the room stack, global objects, room numbering, and other game state. The independent Lua VM has memory and instruction limits, omits file/process APIs, and rejects invalid paths. Live memory readers validate supported layouts and bound traversal.

Each additional native draw audits the real stack, Room data, and entity common fields. A failed audit disables native rendering for the process. GPU caches and all entity-specific fields are not covered by that audit. Scene changes invalidate previews, and native resources are rebuilt when the target snapshot changes. Both inline and separate-window presentations use the same refreshed snapshot. Nested live/global entries retain source identity and are revalidated; deletion returns to the parent and changed liquid conditions refresh the branch. Window click actions retain the displayed object instead of a potentially stale vector index, together with the view it was clicked in; a click for a view no longer shown is dropped. Outer steps take their depth from the actual stack, and each inner step is one deeper than the step it was opened from, including paths that go outside and then inside again. Steps store depth relative to the room being played, not the engine's absolute depth, because walking out through a flame builds no room: a pinned chest carried out keeps its preview, and its depths stay right. A flame back to the previous step is Back, and any new step clears Forward.

## Undo

Undo is the one feature that changes play rather than showing it, and it does so without writing any game state of its own. The game keeps nothing it could return to, and copying a whole world back in would mean owning every field of every entity. What it does keep is determinism: Game::update runs at a fixed 1/60 s step, only the room on top of the stack is simulated, and the player's input reaches it as sixteen bytes sampled once per tick. So a mission is its start plus the sequence of those ticks, and any earlier moment is the start plus a prefix of them.

```text
Each tick of play: record controls, step, room digest
Undo: pick a target tick, ask for the game's own Restart
New Game for the same mission, before its clock starts:
  restore the counters it started with, reseed gameplay randomness
  run the recorded ticks up to the target, silently, comparing each digest
  resume live play from there
```

- A recording starts with each Game the game builds, so it covers the level from its start or from the player's own Restart, and ends when the level does. The Restart a rewind asks for is recognised by mission name, because the new Game lives at the old one's address.
- The replay runs inside Game::attach, before Host::run starts the clock its ticks are measured against, so the time it takes is not made up afterwards as a burst of extra ticks.
- rand() is shared by gameplay, rendering, particles, music and sound pitch, and the draws outside gameplay happen at a rate set by the frame rate and the audio device. Inside Game::update and the Game constructor, rand() therefore comes from a stream seeded per recording; everywhere else, including the pitch of a sound started during a tick, it stays on the C runtime. The oobleck's time to set and the bird's flight are the draws this protects.
- A replay starts no sound except the crux hum, the only one that loops, which must still be humming afterwards. Every sound still playing is stopped before the new Game is built, so a hum its first room starts survives. Steam is switched off for the replay's duration, so chest entries are not counted in the rooms statistic twice.
- An action is a press of Jump or Use, or any control pressed from rest; turning round while moving continues the same one. Undo lands on the tick before the latest action start. Each tick records what was held on the tick of play before it, and the first tick after a rewind records what was held when the rewind was asked for, so a key kept down through it is not a new action. Presses are counted, since several can arrive before the next tick, and the pause menu's own loop does not take them. Five seconds count the steps actually taken, so a changed game speed goes back as far in game time.
- Each recorded tick carries a digest of the room it left: the stack's room names, the room's size, state and clock, and every entity's shared physics fields and a few kind-specific ones, with no heap pointer in it. A replayed tick that disagrees ends the replay there, keeps play going from that tick, and tells the player the rewind was not exact. None has disagreed in testing.

The cost is a replay of everything since the level started, about 1 to 3 µs a tick depending on the room, plus a millisecond or two for each room it builds: three minutes of busy play took 32 ms, and an hour in one level would take about half a second. Checkpoints would only be worth their complexity beyond that.

## Remaining work

- Cruxes, whose attach step starts a looping sound, and bird sprites, which only the gameplay update creates.
- Carried-item context and successive hypothetical entry semantics.
- Independent simultaneous preview windows; the current UI has one shared preview.
- Preserved instances and cauldron semantics.
- Reliable pause-on-inspection and optional explored-only previews.
- Broader lifecycle, performance, and game-version coverage.
- Redo after an undo, until new input takes a different path.

Acceptance cases include dry/wet destinations, self-reference, collected global keys, two chests targeting the same room, target destruction, scene restart, repeated nesting and return, and closing either presentation without changing player state.
