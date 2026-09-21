# Reverse-engineering notes

Validated locally on 2026-09-22 against the Windows x86 build below. Addresses are virtual addresses at preferred image base `0x400000`; all runtime calls relocate through the loaded module base.

```text
Recursed.exe SHA-256
0E47D5DF0F45152978777E5F8EC7F2435BAB79CD84D630CFAAD5106B90D74251
```

The executable uses MSVC 2013, SFML 2, Lua 5.2.4, GLEW, and OpenGL. It exposes no convenient game or Lua API. RTTI and identifiable imports make targeted hooks practical; rebuilding the entire decompiled game is unnecessary. Raw disassembly and game binaries are local-only investigation artifacts.

## Observed hooks

| Address or import | Role |
| --- | --- |
| Chest vtable slot `0x47AD80`, function `0x411730` | Observe chest transforms; position at +0x08/+0x0C, destination string at +0x4C |
| Door vtable slot `0x47AF24`, function `0x413600` | Observe red/green return portals and resolve the parent room |
| `0x440A20` | Observe real room construction and invalidate preview state |
| SFML `Window::display` | Draw the inspection UI |
| SFML `Window::pollEvent` and keyboard query | Navigation and separate-window input isolation |
| SFML `Window::setMouseCursorVisible` | Keep the system pointer visible during inspection; restore the game's requested visibility on F8 |
| CRT `fopen` | Observe mission files; host +0x08 is a tileset string, not the mission name |
| `SteamAPI_Init`, `SHGetFolderPathA` | Isolate the launched test copy from Steam and normal configuration |

Instruction signatures and executable fingerprints constrain these hooks to the supported build.

Window initialization calls `setMouseCursorVisible(false)` at `0x43E121` through IAT slot `0x4775F8`. The mod intercepts this request and preserves its original value. Inspection forces visibility through SFML itself, so its stored cursor state agrees with the display policy. F8 reapplies the saved game value; ordinary frames do not repeatedly change the Windows `ShowCursor` counter. The Win32 preview window keeps its own arrow cursor.

## Entry and global state

Ordinary entry at `0x43FCE0` allocates a new 0xA0-byte Room at `0x43FE66`, adds it to the stack, builds it at `0x44005B`, and restores globals at `0x440066`. This explains why initial room scripts remain part of the preview even after adopting the native renderer. Entry also mutates global state, room numbering, and achievement-related state, so previews do not call it.

- Entity +0x38 bit 0x10 indicates liquid. `0x41C975` clears it; `0x41C9E8-0x41C9FE` sets it for tile kinds 3 and 4. `0x419A55-0x419A6A` passes it as the entry wet condition.
- Host +0x54/+0x58 is the current stack begin/end. Each 28-byte record contains a 24-byte old MSVC string and a Room pointer. Readers verify the stack top against the source chest's owner.
- Host +0x6C is `map<string, vector<Entity*>>`. Tree-node key is at +0x10, value at +0x28. Read-only traversal avoids insertion.
- `0x440BD0` saves globals on exit; `0x440CD0` restores them on entry. A visited room's empty list must suppress its initial Global declarations.
- Room +0x14 is the entity vector. Entity +0x45 marks global objects; +0x44 marks pending destruction. The held item is referenced through host +0x04, context +0x04, and is excluded.
- Self-reference reads globals from the active Room because actual entry saves them before constructing the destination.

Readers use bounded container walks and read-only memory copies. Failed state reads report an unavailable preview. Carried-item script branches and future global changes along a hypothetical preview path remain approximate.

Existing ancestors are sampled by stack record, not by room name. Native tile indices come directly from their 12-byte tile entries; host +0x40 contains 20-byte tile definitions. RTTI identifies Door portals (variant byte +0x58), Surface entities (regenerated from tiles), and crystal variants (+0x54). These reads are repeated for open previews.

## Native rendering

`0x419C30` prepares the current view and invokes `0x4338A0` at `0x419E18`. The render context is 0x74 bytes. Its output FBO is at +0x20, time and delta at +0x24/+0x28, with additional internal buffer handles at +0x14/+0x18/+0x1C. Binding a framebuffer alone is insufficient: the pipeline rewrites and uses its own intermediate targets.

| Function | Stage |
| --- | --- |
| `0x42BCC0` | Background |
| `0x42D450` | Terrain |
| `0x431840` | Sprites |
| `0x42DCD0` | Models |
| `0x42C7F0` | Lighting |
| `0x430970` | Particles |
| `0x432D30` | Postprocessing |

The hook copies the context, redirects the output, and performs the extra draw before the normal draw. F7 first validated active-room replay to a separate target; the normal preview then integrated an isolated destination scene. Actual destination captures are in `docs/native-destination.png` and `docs/native-water.png`.

Renderer constructor `0x4335D0` creates an internal 0x10C-byte object; destructor `0x433760` releases it. Each preview scene has independent render caches and particle state. OpenGL readback restores pixel-pack and framebuffer state; the game's normal draw runs last.

## Native scene ownership

Room constructor is `0x440120`, destructor `0x41BBD0`. Tile entries are 12 bytes. The first field is a **native tile definition index**, not the PNG sprite frame. Snapshot tile names are resolved through host +0x4C, a `map<string,int>`. This distinction fixed incorrect pipe and wall textures in the initial experiment.

`0x41BD30` computes tile edges and creates native water-surface entities. Its mutation of the room serial at `0x48C580` is restored; the private renderer uses a private room ID. Host spawn at `0x4409A0` and global restoration at `0x440CD0` pass `(entity.flags & 0x21) != 0` to Room insertion at `0x41C130`. After checking initial overlap, insertion performs twenty downward moves of 0.05 tiles using collision movement at `0x41CC80`. Fresh preview entities now reuse that behavior; initially rejected objects are removed and destroyed. Live ancestor copies disable this placement and retain captured coordinates. The full Room update at `0x41C8C0` is never called for previews.

| Entity | Constructor | Bytes |
| --- | --- | --- |
| Chest | `0x410FE0` | 0x7C |
| Box | `0x410210` | 0x50 |
| Key | `0x416210` | 0x50 |
| Lock | `0x416790` | 0x50 |
| Crystal variants | `0x412580` | 0x5C |
| Door / return portal | `0x412E00` | 0x5C |

Old MSVC strings use the game's constructors/destructors; all native allocations use the matching game CRT. The preview owns its stack and entities, borrows immutable level metadata, and never destroys the borrowed host. Room destruction detaches entities and destroys containers; entity destruction is a separate ownership step.

Only key spin and draw transforms advance, alongside native renderer effects. A private RNG stream supplies original `rand` calls during preview work, avoiding consumption of the normal game's stream. The `player` script declaration creates only a Door in the preview; `yield` creates its green variant. A private empty entry context is sufficient for the audited constructor, attach, draw, and destructor paths. Gameplay portal update/interaction and Player construction are never invoked. Unsupported entity kinds use the resource-rendering fallback.

## Validation boundary

The native renderer compares the live stack, the Room's first 0xA0 bytes, and each entity's first 0x48 bytes before and after extra rendering. A mismatch disables native rendering for that process. This does not audit every entity-specific field, GPU cache, external subsystem, or all possible game states.

Local checks cover native keyroom and pool rendering, nesting, return, window switching, closure, and unchanged original save hashes. They do not prove complete compatibility with all puzzles or special objects. See `docs/validation.md` for the distinction between asset-free CI tests, local integration tests, and GUI checks.
