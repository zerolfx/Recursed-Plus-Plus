#pragma once
#include <windows.h>
#include "room_art.h"
namespace peek {
enum class PreviewAction {None,Close,Back,Forward,Dock,Select,Undo,UndoSeconds};
// Where something a preview opens from is drawn around its position, in tiles, as the game's
// models draw it: how far it reaches to either side, above and below, and where the short line
// beneath it goes. kind is an object kind: chest, jar, cauldron, or player and yield for flames.
struct Reach {float half,above,below,underline;};
bool reachOf(const std::string& kind,Reach& out);
// What inside a preview a click can open: a chest or a cauldron that leads somewhere, a jar, or a
// flame. The pointer is given a margin of reachMargin tiles around it.
bool opensPreview(const Object& o);
constexpr float reachMargin=.15f;
// view names what the window was showing when the object was clicked.
struct WindowAction {PreviewAction action=PreviewAction::None;Object object{};std::string view;};
bool showPreviewWindow(HWND owner);
void closePreviewWindow();
bool previewWindowOpen();
bool previewWindowFocused();
bool previewEscapeHeld();
WindowAction pumpPreviewWindow();
// depth is how deep the room is as the preview says it, such as "Depth 2" or "Paradox".
void updatePreviewWindow(const RoomArt& art,const Snapshot& snapshot,const std::string& room,const std::string& depth,const std::string& status,const std::string& view);
}
