# Preview design

## Experience

Make room nesting visible without changing Recursed's movement, physics, or entry rules. A chest is too small to contain a useful complete image, so hovering opens an enlarged preview. Clicking pins it; Shift + click opens a separate window. Both presentations share one bounded navigation path.

The current implementation supports eight levels, Backspace to go back, Esc to close, and O to switch presentation without losing the path. The separate window displays depth and destination. A short underline identifies the hovered chest; persistent debugging rectangles are not part of the presentation.

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

Unsupported native entity kinds cause the entire scene to use the existing resource-based renderer. Renderer details belong in diagnostics and documentation, not player-facing labels. Rendering fidelity does not imply exact state simulation.

## Boundaries

Do not invoke the real chest-entry path for a preview: it modifies the room stack, global objects, room numbering, and other game state. The independent Lua VM has memory and instruction limits, omits file/process APIs, and rejects invalid paths. Live memory readers validate supported layouts and bound traversal.

Each additional native draw audits the real stack, Room data, and entity common fields. A failed audit disables native rendering for the process. GPU caches and all entity-specific fields are not covered by that audit. Scene changes invalidate previews, and native resources are rebuilt when the target snapshot changes. Both inline and separate-window presentations use the same refreshed snapshot. Nested live/global entries retain source identity and are revalidated; deletion returns to the parent and changed liquid conditions refresh the branch. Window click actions retain the displayed object instead of a potentially stale vector index. Relative depth is derived from actual stack depth, including paths that go outside and then inside again.

## Remaining work

- Cruxes, whose attach step starts a looping sound, and bird sprites, which only the gameplay update creates.
- Carried-item context and successive hypothetical entry semantics.
- Independent simultaneous preview windows; the current UI has one shared preview.
- Preserved instances and cauldron semantics.
- Reliable pause-on-inspection and optional explored-only previews.
- Broader lifecycle, performance, and game-version coverage.

Acceptance cases include dry/wet destinations, self-reference, collected global keys, two chests targeting the same room, target destruction, scene restart, repeated nesting and return, and closing either presentation without changing player state.
