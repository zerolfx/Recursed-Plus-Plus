#pragma once
#include <windows.h>
#include "room_art.h"
namespace peek {
enum class PreviewAction {None,Close,Back,Forward,Dock,Select,Undo,UndoSeconds};
// view names what the window was showing when the object was clicked.
struct WindowAction {PreviewAction action=PreviewAction::None;Object object{};std::string view;};
bool showPreviewWindow(HWND owner);
void closePreviewWindow();
bool previewWindowOpen();
bool previewWindowFocused();
bool previewEscapeHeld();
WindowAction pumpPreviewWindow();
void updatePreviewWindow(const RoomArt& art,const Snapshot& snapshot,const std::string& room,int depth,const std::string& status,const std::string& view);
}
