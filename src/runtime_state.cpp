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

RoomReference readRoomReference(uintptr_t host,uintptr_t sourceRoom,int ancestors){
    RoomReference result;
    try {
        if(!host||!sourceRoom||ancestors<0||ancestors>4095)throw std::runtime_error("Invalid ancestor request");
        auto begin=pointer(host+0x54),end=pointer(host+0x58);
        if(end<=begin||(end-begin)%28||(end-begin)/28>4096)throw std::runtime_error("Invalid room stack");
        if(pointer(end-4)!=sourceRoom)throw std::runtime_error("Scene changed");
        int count=(end-begin)/28;
        if(ancestors>=count)throw std::runtime_error("Already at the outermost room");
        auto entry=end-28*(ancestors+1);
        result.name=oldString(entry);result.room=pointer(entry+24);result.depth=count-ancestors-1;
        if(!result.room)throw std::runtime_error("Missing ancestor room");
    }catch(const std::exception& e){result={};result.error=e.what();}
    return result;
}

ExitTarget readExitTarget(uintptr_t host,uintptr_t sourceRoom,int from,uint32_t greens){
    ExitTarget result;
    try {
        if(!host||!sourceRoom||from<0||from>4095)throw std::runtime_error("Invalid exit request");
        auto begin=pointer(host+0x54),end=pointer(host+0x58);
        if(end<=begin||(end-begin)%28||(end-begin)/28>4096)throw std::runtime_error("Invalid room stack");
        if(pointer(end-4)!=sourceRoom)throw std::runtime_error("Scene changed");
        const int count=(end-begin)/28,last=count-1-from;
        // A room built alone on its stack gets no flame (0x43E8CF, 0x43EB74), so there is none to
        // take out of the outermost room.
        if(last<1)throw std::runtime_error("No flame in the outermost room");
        auto held=pointer(pointer(host+4)+4);
        // Saved global lists by room name, read once and then moved the way each exit moves them.
        struct Saved {bool present=false;std::vector<uint32_t> list;};
        std::map<std::string,Saved> saved;
        auto savedFor=[&](const std::string& name)->Saved&{
            auto found=saved.find(name);if(found!=saved.end())return found->second;
            auto& s=saved[name];s.present=savedList(host,name,s.list);return s;
        };
        // Leaving a room (0x440BD0) moves its globals, except the held item and anything being
        // destroyed, onto its name's list, which it creates if it has to.
        auto leave=[&](const std::string& name,std::vector<uint32_t>& live){
            auto& out=savedFor(name);out.present=true;std::vector<uint32_t> kept;
            for(auto e:live)if(e!=held&&read<uint8_t>(e+0x45)&&!read<uint8_t>(e+0x44))out.list.push_back(e);else kept.push_back(e);
            live.swap(kept);
        };
        auto name=[&](int i){return oldString(begin+28*i);};
        auto roomAt=[&](int i){auto room=pointer(begin+28*i+24);if(!room)throw std::runtime_error("Missing ancestor room");return room;};
        auto entities=[&](int i){return pointers(roomAt(i)+0x14);};
        std::vector<uint32_t> live=entities(count-1);
        for(int i=count-1;;i--){
            // A green flame takes nothing through: what is held is put down first, for good.
            const int out=count-1-i;
            if(out<32&&(greens>>out&1))held=0;
            leave(name(i),live);
            // The room below takes its globals back (0x440CD0), all but the held item, one at a time
            // and in order; what the room refuses stays on the list.
            const auto below=name(i-1);
            auto back=entities(i-1);
            const size_t stayed=back.size();
            auto& restored=savedFor(below);
            std::vector<uint32_t> left;
            for(auto e:restored.list)if(e!=held){if(refused(roomAt(i-1),e,back))left.push_back(e);else back.push_back(e);}
            restored.list.swap(left);
            const std::vector<uint32_t> returned(back.begin()+stayed,back.end());
            // Then it is reactivated (0x41C6A0), which gives each entity the room back in vector
            // order and attaches it at once. The player (0x416EA0) needs what it went in through to
            // be the room's already: an entity ahead of it, or a global put back just before.
            uint32_t anchor=0;size_t player=stayed;
            for(size_t k=0;k<stayed;k++)if(entityKind(back[k])=="player"){player=k;anchor=pointer(back[k]+0x5c);break;}
            const auto at=(size_t)(std::find(back.begin(),back.end(),anchor)-back.begin());
            if(anchor&&anchor!=held&&at>=player&&(at<stayed||at==back.size())){
                // Named by what it went in through: a chest's name starts with "chest-" (0x411970),
                // a jar's is its own and a cauldron's starts with "cauldron-".
                const auto kind=entityKind(anchor);
                const bool chest=kind=="chest"||(kind=="jar"&&oldString(anchor+0x4c).compare(0,6,"chest-")==0);
                result.paradox=true;result.room=chest?"reject":"threadless";
                // Switching timeline (0x440860) leaves this room as well.
                leave(below,back);
                break;
            }
            if(i==last){
                result.room=below;result.ancestors=from+1;
                result.globals.available=result.globals.initialized=true;collect(result.globals,returned,held);
                break;
            }
            // Walking on out: the held item comes along.
            live.swap(back);
            if(held&&std::find(live.begin(),live.end(),held)==live.end())live.push_back(held);
        }
        if(result.paradox){
            // The paradox room is built fresh (0x43FCE0) and restores what is saved under its name.
            auto& s=savedFor(result.room);
            result.globals.available=true;result.globals.initialized=s.present;
            collect(result.globals,s.list,held);
        }
        result.available=true;
    }catch(const std::exception& e){result={};result.error=e.what();}
    return result;
}

std::string readTimeline(uintptr_t host){
    try {return oldString(host+0x84);}catch(const std::exception&){return {};}
}

Snapshot readRoomSnapshot(uintptr_t host,uintptr_t sourceRoom,int ancestors,const Snapshot& appearance){
    Snapshot result=appearance;result.tiles={};result.objects.clear();result.error.clear();result.hasGlobals=false;result.live=true;
    try {
        auto reference=readRoomReference(host,sourceRoom,ancestors);
        if(!reference.error.empty())throw std::runtime_error(reference.error);
        result.nativeDepth=reference.depth;auto room=reference.room;
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
            // A fizzer is the invisible controller the engine attaches to acid for a few seconds.
            // Its draw transform is a bare ret, so it contributes nothing and must not block a scene.
            if(kind=="player"||kind=="surface"||kind=="fizzer")continue;
            if(kind=="door")kind=read<uint8_t>(e+0x58)?"yield":"player";
            if(kind=="crystal"){
                auto variant=read<int>(e+0x54);if(variant==1)kind="diamond";else if(variant==2)kind="ruby";
            }
            Object object{kind,"",read<float>(e+8),read<float>(e+12),read<uint8_t>(e+0x45)!=0};
            object.sourceId=e;
            if(!std::isfinite(object.x)||!std::isfinite(object.y))throw std::runtime_error("Invalid live object position");
            // Chest, cauldron and jar keep a room name at +0x4c; a record keeps its voice-clip path there.
            if(kind=="chest"||kind=="record"||kind=="cauldron"||kind=="jar")object.target=oldString(e+0x4c);
            result.hasGlobals|=object.global;result.objects.push_back(std::move(object));
        }
    }catch(const std::exception& e){result.tiles={};result.objects.clear();result.error=e.what();}
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
