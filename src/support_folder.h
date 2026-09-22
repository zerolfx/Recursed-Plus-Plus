#pragma once
#include <string>
namespace peek {
// Where this build keeps its log and the private game profile, created if missing.
// Beside the launcher is wrong: a download extracted into Program Files cannot write there,
// and the game is manifested asInvoker, so Windows does not redirect that failure to a
// VirtualStore copy either - it just fails. The launcher and the plugin both call this so
// the path in an error message is the path that was actually used.
std::wstring supportFolder();
// The same folder in the narrow encoding the game itself can consume, or an empty string when
// it cannot be represented. The game asks for its profile through SHGetFolderPathA and opens
// files through the ANSI CRT, so a path it cannot spell is worse than no redirect at all.
std::string narrowUsable(const std::wstring& path);
}
