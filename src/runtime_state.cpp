#include "runtime_state.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <map>
#include <stdexcept>

namespace peek {
namespace {
// ReadProcessMemory fails cleanly for stale/unmapped addresses. All sampling is
// on the game's render thread, outside object update/container mutations.
void readBytes(uintptr_t p, void* out, size_t n) {
    SIZE_T copied = 0;
    if (!p || !ReadProcessMemory(GetCurrentProcess(), (const void*)p, out, n, &copied) || copied != n)
        throw std::runtime_error("Game state no longer readable");
}
template<class T> T read(uintptr_t p) { T v{}; readBytes(p, &v, sizeof v); return v; }
uintptr_t pointer(uintptr_t p) { return read<uint32_t>(p); }
std::string oldString(uintptr_t p) {
    auto size = read<uint32_t>(p + 16), capacity = read<uint32_t>(p + 20);
    if (size > 240 || capacity < size || capacity > 1048576) throw std::runtime_error("Invalid game string");
    std::string result(size, '\0');
    if (size) readBytes(capacity < 16 ? p : pointer(p), result.data(), size);
    return result;
}
std::vector<uint32_t> pointers(uintptr_t p) {
    auto begin = pointer(p), end = pointer(p + 4), capacity = pointer(p + 8);
    if (end < begin || capacity < end || (end - begin) % 4 || (end - begin) / 4 > 2048)
        throw std::runtime_error("Invalid game object vector");
    std::vector<uint32_t> result((end - begin) / 4);
    if (!result.empty()) readBytes(begin, result.data(), result.size() * 4);
    return result;
}
std::string entityKind(uintptr_t entity) {
    auto vtable = pointer(entity), locator = pointer(vtable - 4);
    auto descriptor = pointer(locator + 12);
    char name[64]{}; readBytes(descriptor + 8, name, sizeof name - 1);
    if (strncmp(name, ".?AV", 4)) throw std::runtime_error("Unknown entity RTTI");
    std::string kind(name + 4); auto suffix = kind.find("@@");
    if (suffix == std::string::npos) throw std::runtime_error("Invalid entity RTTI");
    kind.resize(suffix);
    std::transform(kind.begin(), kind.end(), kind.begin(), [](char c) { return (char)tolower((unsigned char)c); });
    return kind;
}
void collect(GlobalState& result, const std::vector<uint32_t>& entities, uintptr_t held) {
    for (auto e : entities) {
        if (e == held || read<uint8_t>(e + 0x44) || !read<uint8_t>(e + 0x45)) continue;
        Object object;
        object.sourceId=e;
        object.kind = entityKind(e);
        if(object.kind=="jar"&&read<int>(e+0x64)==2)continue;
        object.x = read<float>(e + 8); object.y = read<float>(e + 12); object.global = true;
        if (!std::isfinite(object.x) || !std::isfinite(object.y)) throw std::runtime_error("Invalid object position");
        if (object.kind == "chest" || object.kind == "record" || object.kind == "cauldron" || object.kind == "jar")
            object.target = oldString(e + 0x4c);
        result.objects.push_back(std::move(object));
    }
}
// MSVC 2013 map<string, vector<Entity*>>: sentinel at +0x6c,
// node left/parent/right at +0/+4/+8, key +0x10, value +0x28.
// A room that has been left once has a list, even an empty one.
bool savedList(uintptr_t host, const std::string& name, std::vector<uint32_t>& out) {
    auto head = pointer(host + 0x6c), node = pointer(head + 4);
    unsigned depth = 0;
    while (node != head) {
        if (++depth > 128 || read<uint8_t>(node + 13)) throw std::runtime_error("Invalid global-state tree");
        auto key = oldString(node + 16);
        if (key == name) { out = pointers(node + 0x28); return true; }
        node = pointer(node + (name < key ? 0 : 8));
    }
    return false;
}
// Putting an entity into a room (0x41C130) refuses it when its box reaches a solid tile (kind 1
// or 5), or touches an entity already there that blocks (+0x34 bit 8, a locked lock). The tile
// scan clamps to the room's width and height rather than one less, as the game does.
bool refused(uintptr_t room, uint32_t e, const std::vector<uint32_t>& present) {
    const float x = read<float>(e + 8), y = read<float>(e + 12), hw = read<float>(e + 0x28), hh = read<float>(e + 0x2c);
    if (!std::isfinite(x) || !std::isfinite(y) || !(hw >= 0 && hw < 64) || !(hh >= 0 && hh < 64)) throw std::runtime_error("Invalid object bounds");
    const int w = read<int>(room), h = read<int>(room + 4);
    const auto tiles = pointer(room + 8), tilesEnd = pointer(room + 12);
    if (w < 1 || h < 1 || w > 256 || h > 256 || tilesEnd < tiles || (tilesEnd - tiles) / 12 < (uint32_t)(w * h)) throw std::runtime_error("Invalid room tiles");
    for (int ty = (int)(y - hh); (float)ty < y + hh; ty++)
        for (int tx = (int)(x - hw); (float)tx < x + hw; tx++) {
            const size_t i = (size_t)std::min(std::max(ty, 0), h) * w + std::min(std::max(tx, 0), w);
            if (i < (size_t)w * h) { const int kind = read<int>(tiles + i * 12 + 4); if (kind == 1 || kind == 5) return true; }
        }
    for (auto o : present) {
        if (!(read<uint32_t>(o + 0x34) & 8)) continue;
        const float ox = read<float>(o + 8), oy = read<float>(o + 12), ow = read<float>(o + 0x28), oh = read<float>(o + 0x2c);
        if (!(ox - ow > x + hw) && !(x - hw > ox + ow) && !(oy - oh > y + hh) && !(y - hh > oy + oh)) return true;
    }
    return false;
}
// A stack of rooms, as 28-byte records of a room name and a Room: the one being played, or one a
// timeline switch put aside.
struct Stack {
    uintptr_t begin = 0; int count = 0;
    std::string name(int i) const { return oldString(begin + 28 * i); }
    uintptr_t room(int i) const { auto r = pointer(begin + 28 * i + 24); if (!r) throw std::runtime_error("Missing ancestor room"); return r; }
};
// The stack being played, which must still end in the room the reading was made for.
Stack playedStack(uintptr_t host, uintptr_t sourceRoom) {
    if (!host || !sourceRoom) throw std::runtime_error("No active room");
    auto begin = pointer(host + 0x54), end = pointer(host + 0x58);
    if (end <= begin || (end - begin) % 28 || (end - begin) / 28 > 4096) throw std::runtime_error("Invalid room stack");
    if (pointer(end - 4) != sourceRoom) throw std::runtime_error("Scene changed");
    return {begin, (int)((end - begin) / 28)};
}
// Switching timeline (0x440860) puts the whole stack aside in host+0x7C, a
// map<string, vector<pair<string, Room*>>> by timeline name: key +0x10, vector +0x28. A timeline
// never left has no entry, and one the switch has taken back leaves an empty vector behind.
Stack savedStack(uintptr_t host, const std::string& timeline) {
    auto head = pointer(host + 0x7c), node = pointer(head + 4);
    unsigned depth = 0;
    while (node != head) {
        if (++depth > 128 || read<uint8_t>(node + 13)) throw std::runtime_error("Invalid timeline tree");
        const auto key = oldString(node + 0x10);
        if (key == timeline) {
            auto begin = pointer(node + 0x28), end = pointer(node + 0x2c), capacity = pointer(node + 0x30);
            if (end < begin || capacity < end || (end - begin) % 28 || (end - begin) / 28 > 4096) throw std::runtime_error("Invalid saved stack");
            return {begin, (int)((end - begin) / 28)};
        }
        node = pointer(node + (timeline < key ? 0 : 8));
    }
    return {};
}
// What leaving and entering rooms does to the saved global lists: each list is read once, and then
// moved the way the game would move it, without touching the game's own.
struct Moves {
    uintptr_t host = 0; uint32_t held = 0;
    struct Saved { bool present = false; std::vector<uint32_t> list; };
    std::map<std::string, Saved> saved;
    Saved& savedFor(const std::string& name) {
        auto found = saved.find(name); if (found != saved.end()) return found->second;
        auto& s = saved[name]; s.present = savedList(host, name, s.list); return s;
    }
    // Leaving a room (0x440BD0) moves its globals, except the held item and anything being
    // destroyed, onto its name's list, which it creates if it has to.
    void leave(const std::string& name, std::vector<uint32_t>& live) {
        auto& out = savedFor(name); out.present = true; std::vector<uint32_t> kept;
        for (auto e : live) if (e != held && read<uint8_t>(e + 0x45) && !read<uint8_t>(e + 0x44)) out.list.push_back(e); else kept.push_back(e);
        live.swap(kept);
    }
    // A room takes its globals back (0x440CD0), all but the held item, one at a time and in order;
    // what the room refuses stays on the list. Answers how many entities the room had before.
    size_t restore(const std::string& name, uintptr_t room, std::vector<uint32_t>& back) {
        const size_t stayed = back.size();
        auto& restored = savedFor(name);
        std::vector<uint32_t> left;
        for (auto e : restored.list) if (e != held) { if (refused(room, e, back)) left.push_back(e); else back.push_back(e); }
        restored.list.swap(left);
        return stayed;
    }
    // Then the room is reactivated (0x41C6A0), which gives each entity the room back in vector order
    // and attaches it at once. The player (0x416EA0) needs what it went in through to be the room's
    // already: an entity ahead of it, or a global put back just before. When it is not, this names
    // the paradox room the game leaves for.
    std::string paradox(const std::vector<uint32_t>& back, size_t stayed) const {
        uint32_t anchor = 0; size_t player = stayed;
        for (size_t k = 0; k < stayed; k++) if (entityKind(back[k]) == "player") { player = k; anchor = pointer(back[k] + 0x5c); break; }
        const auto at = (size_t)(std::find(back.begin(), back.end(), anchor) - back.begin());
        if (!anchor || anchor == held || at < player || (at >= stayed && at < back.size())) return {};
        // Named by what it went in through: a chest's name starts with "chest-" (0x411970), a jar's
        // is its own and a cauldron's starts with "cauldron-".
        const auto kind = entityKind(anchor);
        const bool chest = kind == "chest" || (kind == "jar" && oldString(anchor + 0x4c).compare(0, 6, "chest-") == 0);
        return chest ? "reject" : "threadless";
    }
    // A room built fresh (0x43FCE0) restores what is saved under its name.
    GlobalState savedGlobals(const std::string& name) {
        GlobalState result; auto& s = savedFor(name);
        result.available = true; result.initialized = s.present; collect(result, s.list, held);
        return result;
    }
};
// Switching to a timeline that has a stack put aside (0x440860): the room being played puts its
// globals aside first, then the top room of the saved stack takes its own back and is reactivated.
struct Switched { Stack stack; std::vector<uint32_t> live; size_t stayed = 0; std::string paradox; };
Switched switchTo(Moves& moves, const Stack& played, const std::string& timeline) {
    Switched s;
    auto current = pointers(played.room(played.count - 1) + 0x14);
    moves.leave(played.name(played.count - 1), current);
    s.stack = savedStack(moves.host, timeline);
    if (!s.stack.count) return s;
    const int top = s.stack.count - 1;
    s.live = pointers(s.stack.room(top) + 0x14);
    s.stayed = moves.restore(s.stack.name(top), s.stack.room(top), s.live);
    s.paradox = moves.paradox(s.live, s.stayed);
    return s;
}
}

GlobalState readGlobals(uintptr_t host, uintptr_t sourceRoom, const std::string& target) {
    GlobalState result;
    try {
        if (!host || !sourceRoom) throw std::runtime_error("No active room");
        auto begin = pointer(host + 0x54), end = pointer(host + 0x58);
        if (end <= begin || (end - begin) % 28 || (end - begin) / 28 > 4096)
            throw std::runtime_error("Invalid room stack");
        auto current = end - 28;
        if (pointer(current + 24) != sourceRoom) throw std::runtime_error("Scene changed");
        auto game = pointer(host + 4), held = pointer(game + 4);
        // Entering a self-reference first saves the currently active globals.
        if (oldString(current) == target) {
            collect(result, pointers(sourceRoom + 0x14), held);
            result.initialized = true;
        } else {
            std::vector<uint32_t> saved;
            if (savedList(host, target, saved)) { collect(result, saved, held); result.initialized = true; }
        }
        result.available = true;
    } catch (const std::exception& error) {
        result = {}; result.error = error.what();
    }
    return result;
}

RoomReference readRoomReference(uintptr_t host,uintptr_t sourceRoom,int ancestors,const std::string& stack){
    RoomReference result;
    try {
        if(ancestors<0||ancestors>4095)throw std::runtime_error("Invalid ancestor request");
        auto rooms=playedStack(host,sourceRoom);
        if(!stack.empty())rooms=savedStack(host,stack);
        if(ancestors>=rooms.count)throw std::runtime_error("Already at the outermost room");
        const int i=rooms.count-ancestors-1;
        result.name=rooms.name(i);result.room=rooms.room(i);result.depth=i;
    }catch(const std::exception& e){result={};result.error=e.what();}
    return result;
}

ExitTarget readExitTarget(uintptr_t host,uintptr_t sourceRoom,int from,uint32_t greens,const std::string& stack){
    ExitTarget result;
    try {
        if(from<0||from>4095)throw std::runtime_error("Invalid exit request");
        auto rooms=playedStack(host,sourceRoom);
        Moves moves;moves.host=host;moves.held=pointer(pointer(host+4)+4);
        std::vector<uint32_t> live=pointers(sourceRoom+0x14);
        if(!stack.empty()){
            // A flame of a room a cauldron leads back to: the switch there comes first.
            auto switched=switchTo(moves,rooms,stack);
            if(!switched.stack.count||!switched.paradox.empty())throw std::runtime_error("Scene changed");
            rooms=switched.stack;live.swap(switched.live);
            // The player arriving there puts what it holds into the room (0x416FC7), after the
            // globals the room took back.
            if(moves.held&&std::find(live.begin(),live.end(),moves.held)==live.end())live.push_back(moves.held);
        }
        const int count=rooms.count,last=count-1-from;
        // A room built alone on its stack gets no flame (0x43E8CF, 0x43EB74), so there is none to
        // take out of the outermost room.
        if(last<1)throw std::runtime_error("No flame in the outermost room");
        for(int i=count-1;;i--){
            // A green flame takes nothing through: what is held is put down first, for good.
            const int out=count-1-i;
            if(out<32&&(greens>>out&1))moves.held=0;
            moves.leave(rooms.name(i),live);
            const auto below=rooms.name(i-1);
            auto back=pointers(rooms.room(i-1)+0x14);
            const size_t stayed=moves.restore(below,rooms.room(i-1),back);
            if(auto paradox=moves.paradox(back,stayed);!paradox.empty()){
                result.paradox=true;result.room=paradox;
                // Switching timeline (0x440860) leaves this room as well.
                moves.leave(below,back);
                break;
            }
            if(i==last){
                result.room=below;result.ancestors=from+1;result.depth=i-1;
                result.globals.available=result.globals.initialized=true;
                collect(result.globals,std::vector<uint32_t>(back.begin()+stayed,back.end()),moves.held);
                break;
            }
            // Walking on out: the held item comes along.
            live.swap(back);
            if(moves.held&&std::find(live.begin(),live.end(),moves.held)==live.end())live.push_back(moves.held);
        }
        // The paradox room is built fresh (0x43FCE0) and restores what is saved under its name.
        if(result.paradox)result.globals=moves.savedGlobals(result.room);
        result.available=true;
    }catch(const std::exception& e){result={};result.error=e.what();}
    return result;
}

CauldronTarget readCauldronTarget(uintptr_t host,uintptr_t sourceRoom,const std::string& timeline,uintptr_t through){
    CauldronTarget result;
    try {
        if(timeline.empty()||timeline.size()>240)throw std::runtime_error("Invalid timeline");
        auto rooms=playedStack(host,sourceRoom);
        Moves moves;moves.host=host;moves.held=pointer(pointer(host+4)+4);
        // Into the timeline being played, the switch puts the stack aside and takes the same one
        // back, and the room being played puts its globals aside and takes back what is saved
        // under its name: what an earlier restore refused, then its own, less anything a solid tile
        // or a locked lock refuses now. The player comes back out after the cauldron it went in
        // through, so a local one is always ahead of it; a global one has to be taken back, and one
        // the room refuses is gone.
        if(timeline==oldString(host+0x84)){
            const int top=rooms.count-1;
            result.room=rooms.name(top);result.depth=top;result.same=true;
            auto live=pointers(sourceRoom+0x14);
            moves.leave(result.room,live);
            const size_t stayed=moves.restore(result.room,sourceRoom,live);
            if(through&&read<uint8_t>((uint32_t)through+0x45)&&std::find(live.begin(),live.end(),(uint32_t)through)==live.end()){
                // Paradox (0x440510), named by the cauldron's identity: threadless.
                moves.leave(result.room,live);
                result.same=false;result.fresh=result.paradox=true;result.room="threadless";result.depth=0;
                result.globals=moves.savedGlobals(result.room);
            }else{
                result.globals.available=result.globals.initialized=true;
                collect(result.globals,std::vector<uint32_t>(live.begin()+stayed,live.end()),moves.held);
            }
            result.available=true;
            return result;
        }
        auto switched=switchTo(moves,rooms,timeline);
        if(switched.stack.count&&switched.paradox.empty()){
            const int top=switched.stack.count-1;
            result.room=switched.stack.name(top);result.depth=top;
            result.globals.available=result.globals.initialized=true;
            collect(result.globals,std::vector<uint32_t>(switched.live.begin()+switched.stayed,switched.live.end()),moves.held);
        }else{
            // With no stack put aside, the switch builds the timeline's first room fresh, dry and
            // alone (0x43FCE0), whose player has no way back to lose. When the player of the room on
            // top of the stack put aside finds its way back gone, the paradox (0x440510) puts that
            // room's globals aside again and builds the paradox room fresh instead.
            if(!switched.paradox.empty()){
                moves.leave(switched.stack.name(switched.stack.count-1),switched.live);
                result.paradox=true;result.room=switched.paradox;
            }else result.room=timeline;
            result.fresh=true;result.depth=0;
            result.globals=moves.savedGlobals(result.room);
        }
        result.available=true;
    }catch(const std::exception& e){result={};result.error=e.what();}
    return result;
}

std::string readTimeline(uintptr_t host){
    try {return oldString(host+0x84);}catch(const std::exception&){return {};}
}

RoomReference readJarReference(uintptr_t host,uintptr_t sourceRoom,const std::string& jar){
    RoomReference result;
    try {
        auto current=readRoomReference(host,sourceRoom,0);
        if(!current.error.empty())throw std::runtime_error(current.error);
        result.name="glitch";result.depth=current.depth+1;
        if(jar.empty())return result;
        // map<string, pair<string, Room*>> at +0x74: key +0x10, room name +0x28,
        // instance +0x40. Entry (0x440380) consumes this node; never cache its pointer.
        auto head=pointer(host+0x74),node=pointer(head+4);unsigned depth=0;
        while(node!=head){
            if(++depth>128||read<uint8_t>(node+13))throw std::runtime_error("Invalid saved jar tree");
            const auto key=oldString(node+0x10);
            if(key==jar){
                result.name=oldString(node+0x28);result.room=pointer(node+0x40);
                if(!result.room||result.name.empty())throw std::runtime_error("Missing saved jar room");
                break;
            }
            node=pointer(node+(jar<key?0:8));
        }
    }catch(const std::exception& e){result={};result.error=e.what();}
    return result;
}

static Snapshot captureRoom(uintptr_t host,uintptr_t room,int depth,const Snapshot& appearance){
    Snapshot result=appearance;result.tiles={};result.objects.clear();result.error.clear();result.hasGlobals=false;result.live=true;
    try {
        result.nativeDepth=depth;
        if(read<int>(room)!=20||read<int>(room+4)!=15)throw std::runtime_error("Unsupported live room size");
        auto tiles=pointer(room+8),tilesEnd=pointer(room+12);
        if(tilesEnd<tiles||tilesEnd-tiles!=300*12)throw std::runtime_error("Invalid live tiles");
        auto defs=pointer(host+0x40),defsEnd=pointer(host+0x44);
        if(defsEnd<defs||(defsEnd-defs)%20||(defsEnd-defs)/20>4096)throw std::runtime_error("Invalid tile definitions");
        result.tileset=oldString(host+8);result.pattern=oldString(host+0x20);
        for(size_t i=0;i<300;i++){
            auto& tile=result.tiles[i];tile.nativeIndex=read<int>(tiles+i*12);tile.kind=read<int>(tiles+i*12+4);
            if(tile.nativeIndex<0||(unsigned)tile.nativeIndex>=(defsEnd-defs)/20||tile.kind<0||tile.kind>5)throw std::runtime_error("Invalid live tile index");
            auto definition=defs+tile.nativeIndex*20;
            auto frames=pointers(definition+4);tile.frame=frames.empty()?0:(int)frames.front();
        }
        auto held=pointer(pointer(host+4)+4);
        for(auto e:pointers(room+0x14)){
            if(e==held||read<uint8_t>(e+0x44))continue;
            auto kind=entityKind(e);
            if(kind=="jar"&&read<int>(e+0x64)==2)continue;
            // A fizzer is the invisible controller the engine attaches to acid for a few seconds.
            // Its draw transform is a bare ret, so it contributes nothing and must not block a scene.
            if(kind=="player"||kind=="surface"||kind=="fizzer")continue;
            if(kind=="door"){
                // A used flame removes itself on reactivation (0x41329B -> 0x4133B0).
                // In particular, the green flame used to seal a jar cannot be used again.
                if(read<uint8_t>(e+0x59))continue;
                kind=read<uint8_t>(e+0x58)?"yield":"player";
            }
            if(kind=="crystal"){
                auto variant=read<int>(e+0x54);if(variant==1)kind="diamond";else if(variant==2)kind="ruby";
            }
            Object object{kind,"",read<float>(e+8),read<float>(e+12),read<uint8_t>(e+0x45)!=0};
            object.sourceId=e;
            if(!std::isfinite(object.x)||!std::isfinite(object.y))throw std::runtime_error("Invalid live object position");
            // Chest/cauldron destinations, jar identities and record paths share +0x4c.
            if(kind=="chest"||kind=="record"||kind=="cauldron"||kind=="jar")object.target=oldString(e+0x4c);
            result.hasGlobals|=object.global;result.objects.push_back(std::move(object));
        }
    }catch(const std::exception& e){result.tiles={};result.objects.clear();result.error=e.what();}
    return result;
}

Snapshot readRoomSnapshot(uintptr_t host,uintptr_t sourceRoom,int ancestors,const Snapshot& appearance,const std::string& stack){
    auto reference=readRoomReference(host,sourceRoom,ancestors,stack);
    if(reference.error.empty())return captureRoom(host,reference.room,reference.depth,appearance);
    Snapshot result;result.error=reference.error;return result;
}

Snapshot readJarSnapshot(uintptr_t host,uintptr_t sourceRoom,const std::string& jar,const Snapshot& appearance){
    Snapshot result;
    try {
        auto reference=readJarReference(host,sourceRoom,jar);
        if(!reference.error.empty())throw std::runtime_error(reference.error);
        if(!reference.room)throw std::runtime_error("The jar no longer contains this room");
        result=captureRoom(host,reference.room,reference.depth,appearance);
        if(!result.error.empty())return result;
        // Re-entry keeps local objects and tiles, then restores globals by the saved room's
        // original name. Refused globals stay saved, just as in 0x440CD0.
        auto globals=readGlobals(host,sourceRoom,reference.name);
        if(!globals.available)throw std::runtime_error(globals.error);
        auto present=pointers(reference.room+0x14);
        for(auto object:globals.objects){
            if(std::find(present.begin(),present.end(),object.sourceId)!=present.end())continue;
            if(refused(reference.room,(uint32_t)object.sourceId,present))continue;
            present.push_back((uint32_t)object.sourceId);object.settle=true;
            result.objects.push_back(std::move(object));result.hasGlobals=true;
        }
    }catch(const std::exception& e){result={};result.error=e.what();}
    return result;
}

bool wetAt(const Snapshot& room, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    int ix = (int)std::clamp(x, 0.f, 19.f), iy = (int)std::clamp(y, 0.f, 14.f);
    int tile = room.tiles[iy * 20 + ix].kind;
    return tile == 3 || tile == 4;
}

void applyGlobals(Snapshot& room, const GlobalState& state) {
    if (!state.available || !state.initialized || !room.error.empty()) return;
    room.objects.erase(std::remove_if(room.objects.begin(), room.objects.end(), [](const Object& o) { return o.global; }), room.objects.end());
    room.objects.insert(room.objects.end(), state.objects.begin(), state.objects.end());
    room.hasGlobals = room.hasGlobals || !state.objects.empty();
}
}
