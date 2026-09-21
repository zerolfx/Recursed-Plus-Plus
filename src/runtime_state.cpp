#include "runtime_state.h"
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
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
        object.kind = entityKind(e);
        object.x = read<float>(e + 8); object.y = read<float>(e + 12); object.global = true;
        if (!std::isfinite(object.x) || !std::isfinite(object.y)) throw std::runtime_error("Invalid object position");
        if (object.kind == "chest") object.target = oldString(e + 0x4c);
        result.objects.push_back(std::move(object));
    }
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
            // MSVC 2013 map<string, vector<Entity*>>: sentinel at +0x6c,
            // node left/parent/right at +0/+4/+8, key +0x10, value +0x28.
            auto head = pointer(host + 0x6c), node = pointer(head + 4);
            unsigned depth = 0;
            while (node != head) {
                if (++depth > 128 || read<uint8_t>(node + 13)) throw std::runtime_error("Invalid global-state tree");
                auto name = oldString(node + 16);
                if (name == target) {
                    collect(result, pointers(node + 0x28), held);
                    result.initialized = true;
                    break;
                }
                node = pointer(node + (target < name ? 0 : 8));
            }
        }
        result.available = true;
    } catch (const std::exception& error) {
        result = {}; result.error = error.what();
    }
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
