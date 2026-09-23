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
| SFML `Window::setMouseCursorVisible` | Keep the system pointer visible, which is what chests are hovered with |
| CRT `fopen` | Observe mission files; host +0x08 is a tileset string, not the mission name |
| Every `steam_api.dll` import, `SHGetFolderPathA` | Answer the game's Steam calls inside the process and isolate its configuration |

Instruction signatures and executable fingerprints constrain these hooks to the supported build.

Window initialization calls `setMouseCursorVisible(false)` at `0x43E121` through IAT slot `0x4775F8`. The mod intercepts this request and forces visibility through SFML itself, so its stored cursor state agrees with what is on screen. Ordinary frames do not repeatedly change the Windows `ShowCursor` counter. The Win32 preview window keeps its own arrow cursor.

## Progress and Steam storage

`SteamAPI_Init` is called at `0x43B5B0` and its result kept in the byte at `0x48C585`. Every later use of Steam tests that byte first, and the storage calls sit behind it: the save write is inline at `0x4367D1`-`0x436817` and the read is the wrapper at `0x43B750`. Neither has a local fallback, so with Steam absent the game loads nothing and, more to the point, writes nothing. Progress made in an isolated run was lost when the process ended.

The game reaches its interfaces through `SteamInternal_ContextInit(0x48A088)` and reads four entries of what it returns: `+0x04` user, `+0x0C` utilities, `+0x14` user stats, and `+0x24` remote storage. Each dereference is guarded by a null check of the entry it uses, or of the user entry in the case of utilities. Handing the game a table whose only populated entry is remote storage therefore keeps its stats, achievement, and overlay paths unexecuted while its own save and load paths run unchanged.

Remote storage is `STEAMREMOTESTORAGE_INTERFACE_VERSION014`. The game calls `FileWrite` at vtable `+0x00`, `FileRead` at `+0x04`, `FileExists` at `+0x34`, and `GetFileSize` at `+0x3C`, which a C++ class declaring those entries in interface order matches exactly. The names it asks for are the ones Steam Cloud stores in `userdata\<account>\497780\remote`: `save0`, and `save0-dlc` and `save0-dlc2` for the additional content. The contents are lines of `complete <mission>`, `alt <mission>`, `alt2 <mission>`, and `final 1`, written by the serializer at `0x436560`, parsed by the loader at `0x436020`, and written again from `0x4364A0` as the save object is destroyed, which is why closing the game records a save.

Configuration is separate: `0x4519F0` builds `%APPDATA%\recursed.ini` through `SHGetFolderPathA(CSIDL_APPDATA)`, falling back to `./recursed.conf`, and `0x425D90` rewrites the whole file whenever one setting changes. That path is redirected into the private profile folder; progress does not travel with it.

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
| Record | `0x418120` | 0x70 |
| Fan | `0x4138B0` | 0x5C |
| Generic | `0x414780` | 0x60 |
| Cauldron | `0x410650` | 0x78 |
| Bird | `0x40EDE0` | 0x6C |
| Jar | `0x415770` | 0x74 |
| Froth | `0x4144D0` | 0x50 |
| Crystal variants | `0x412580` | 0x5C |
| Door / return portal | `0x412E00` | 0x5C |

The record constructor takes the voice-clip path as a `const char*` and stores it at +0x4C, where a chest stores its destination room; live reads use that offset for both. Construction only builds the `assets/record` model and copies that string, so no audio subsystem is touched. Cauldrons and jars also keep a room name at +0x4C, so live reads cover four kinds.

Argument shapes differ enough that the construction switch dispatches on the kind, never on the size: Exit, Crystal and Fan are all 0x5C. A cauldron takes its destination as a game string by pointer, like a chest, and the caller owns it. A jar takes the same string **by value** and frees it itself before its `ret 0x18`, so that path must not destroy the copy it passes. Generic and Bird store an entry-context pointer at +0x4C and +0x48 without dereferencing it, exactly as Exit does; Bird's second argument is a `vector<string>` of gameplay hints copied in by `0x40F910`, which sizes the copy from `last - first`, so three null words are a valid empty vector.

One kind is rejected on purpose: `Crux::attach` unconditionally starts the looping `sounds/core-idle` through `0x439070`, and a preview stays silent. Two are handled with a known limit rather than rejected. `Bird::draw` returns immediately while +0x4C is null, and only the gameplay update builds that sprite, by matching the bird's hints against the room's entity list, so a bird is constructed and placed but not drawn. A fizzer is skipped when reading a live room: it is the invisible controller the engine attaches to acid, it has no constructor of its own, and its draw transform is a bare `ret`. An unnamed jar would auto-name itself and advance the game's jar counter at `0x48A050`, so scene construction saves and restores that counter as it already does the room serial.

Old MSVC strings use the game's constructors/destructors; all native allocations use the matching game CRT. The preview owns its stack and entities, borrows immutable level metadata, and never destroys the borrowed host. Room destruction detaches entities and destroys containers; entity destruction is a separate ownership step.

Only original spin rates and draw transforms advance, alongside native renderer effects. Key adds `dt * 3`, Generic `dt * 2` and Crystal `dt * 0.6` unconditionally in their own updates, before their first branch, and Fan integrates the rate at +0x54, which its constructor sets to the same 10.0 its ramp clamps to. The preview advances each registered angle at its own rate. Record, cauldron and jar reach the shared body step `0x415490`, whose rotation is gated on +0x48; their constructors clear it, so a resting one does not spin in the original game either. A private RNG stream supplies original `rand` calls during preview work, avoiding consumption of the normal game's stream. The `player` script declaration creates only a Door in the preview; `yield` creates its green variant. A private empty entry context is sufficient for the audited constructor, attach, draw, and destructor paths. Gameplay portal update/interaction and Player construction are never invoked. Unsupported entity kinds use the resource-rendering fallback.

## Rewind

Host::run `0x434860` is a fixed-step loop: the step is `sf::microseconds(16666)` at `0x48C668`, at most ten ticks are caught up per frame, and each tick polls events, samples input, and calls the component's vtable `+8` with `dt = [0x48A084] * [0x47CE64]` (`1/60`). `0x48A084` is the game speed, 1.0 unless Shift+F5/F6/F7/F8/F10 set it to 0.25, 0.5, 0.75, 1.0 or 1.8, and it survives a Restart. The game reacts to no key event without Shift.

The player's controls are 16-byte records at App+4+0x28..+0x2C: action, kind (0 key, 1 joystick button, 2 axis below centre, 3 axis above, threshold 64) and code. A button's code indexes 12-byte records of joystick and button at App+4+0x1C, and an axis's indexes records of joystick, axis and calibrated centre at App+4+0x10. The defaults are the arrows, Z or Y to jump and X to use, gamepad button 0 to jump, 2 to use and 7 to pause, the first two axes to move, and the fixed Escape for Pause and Return for Confirm. Q, W, button 5 (RB) and the Z axis, where the Xbox driver reports the triggers through the joystick API SFML uses, are free unless the player binds them, so undo takes them and gives way when the list says otherwise. An Xbox Wireless Controller over Bluetooth (045E:0B22) was read through winmm to confirm it: RB is button 5, LB button 4, A button 0, RT drives Z from 32767 to 128 and LT to 65408.

The input layer's constructor `0x4077B0` fills the axis and button lists once: for each of the eight joystick slots that is connected, every axis `hasAxis` reports, then every button up to `getButtonCount`, and afterwards it calibrates each axis centre from its current position. Nothing rebuilds them, which is why a controller connected later never answered. Neither Windows nor this SFML is the obstacle: winmm reported a controller switched on after the process had first asked, and this SFML, which imports only `joyGetDevCapsW` and `joyGetPosEx`, re-checks each slot twice a second. The mod therefore takes over the input poll `0x408540` (prologue `55 8B EC 83 EC 24`, six bytes, three whole instructions) and, whenever the set of connected controllers changes, rebuilds both lists in the same order with the game's own `operator new` and `delete`, keeping the centre of every axis already listed.

| Address | Role |
| --- | --- |
| Game vtable `0x47B4D4` | `+4` attach `0x4199B0` sets Game+8 = App+4; `+8` update `0x4199C0`; `+0x10` pause `0x419E40` |
| `0x419820` | Game constructor (mission name); only called at `0x41E344` and `0x422FFC` |
| `0x408540` | Input poll into App+4: eight actions (Up, Down, Left, Right, Jump, Use, Pause, Confirm), a held byte and a changed byte each |
| `0x48C580`, `0x48A050` | Room serial and unnamed-jar counter; only ever incremented |
| `0x48C585` | Steam answered; every Steam call tests it first |
| `0x48C67C`/`0x48C680` | Active sounds: `unique_ptr` to 0x34-byte instances, `sf::Sound` at +0x24, handle at +0x30 |

Update runs the top Room's update `0x41C8C0` and then drains the room's event queue; entity transforms (vtable `+0xC`) run there too, not in draw. Game::draw writes nothing that update reads. Gameplay reads input only through `[[Player+0x48]]`, which is Game+8, and only the bytes recorded after the poll: while the window is out of focus the poll is skipped and the bytes stand still, and while the Steam overlay is open it leaves every action released.

Pause's Restart sets App+0xB8 = 1 and App+0xB4 = 1, and Host::run returns 1. Both callers that run a Game then destroy it, zero the same stack slot, and construct a new one there for the same mission name: `0x42300F` for missions from the level map, which first runs the save serializer `0x436560`, the achievement check `0x423CA0` and a door refresh, and `0x41E360` for a mission named on the command line, which runs nothing in between. Host::run calls attach once per run, clears App+0xB4 after it, and only then creates the clock it measures ticks against. The pause flag is latched before update and handled after it, so a Pause edge on the tick that asks for Restart would open the menu and clear the request.

rand() is the C runtime's, seeded once by `srand(time)` at `0x41E17F`. Gameplay draws from it in entity constructors (yaw), Bird `0x40F57B`/`0x40F82C`, the oobleck's time to set `0x414EE9`, Fizzer and froth, and the lock glint; the renderer constructor draws 1536 values. Draw-phase particles, the music picker `0x41EBD5`, and every sound start (pitch at `0x439132`) draw from it at rates set by the frame rate and the audio device, and the footstep sound restarts only when the previous one has left a list the draw phase prunes. None of the entity code orders anything by pointer or reads uninitialised physics fields; physics is scalar SSE and the game never changes MXCSR. Mission scripts run in a Lua state with no standard libraries, so they cannot draw random numbers themselves.

Entity bytes +0x08..+0x45 hold no pointer: position, velocity, acceleration, impact speed, half extents, angle (Player facing, Bird heading), property, contact, category and mask bits, and the pending-destruction and global bytes. The held item is Game+0x0C. Player+0x54 is wetness and +0x60..+0x7F its re-entry cooldown and move state; Player+0x58, Record+0x64 and Crux+0x5C hold sound handles, which a silent replay leaves empty.

## Validation boundary

The native renderer compares the live stack, the Room's first 0xA0 bytes, and each entity's first 0x48 bytes before and after extra rendering. A mismatch disables native rendering for that process. This does not audit every entity-specific field, GPU cache, external subsystem, or all possible game states.

Local checks cover native keyroom and pool rendering, nesting, return, window switching, closure, and unchanged original save hashes. They do not prove complete compatibility with all puzzles or special objects. See `docs/validation.md` for the distinction between asset-free CI tests, local integration tests, and GUI checks.
