#pragma once
#include <windows.h>
#include "room_art.h"
namespace peek {
enum class PreviewAction {None,Close,Back,Forward,Dock,Select};
struct WindowAction {PreviewAction action=PreviewAction::None;Object object{};};
bool showPreviewWindow(HWND owner);
void closePreviewWindow();
bool previewWindowOpen();
bool previewWindowFocused();
bool previewEscapeHeld();
WindowAction pumpPreviewWindow();
void updatePreviewWindow(const RoomArt& art,const Snapshot& snapshot,const std::string& room,int depth,const std::string& status);
}
