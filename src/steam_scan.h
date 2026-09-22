#pragma once
#include <string>
#include <vector>
namespace peek {
// Text parsing is kept pure so CI can cover it with fixtures, without Steam or the game installed.
// Handles both library file layouts: the old numbered entries that hold a path directly, and the
// newer numbered blocks that hold a "path" key.
std::vector<std::string> parseLibraryFolders(const std::string& vdf);
// The folder under steamapps/common that a Steam app manifest points at.
std::string parseInstallDir(const std::string& manifest);
// Folds a path for comparison. Windows paths are case-insensitive and the two Steam registry
// values disagree about case, so comparing them raw reports one install as two.
std::wstring foldPath(const std::wstring& path);
// Every Recursed.exe this machine appears to have, best guess first. Never throws.
std::vector<std::wstring> findRecursed();
// One Steam account's Recursed progress, as Steam Cloud keeps it on disk. `written` is the
// newest file time in the folder, so an account that has not been played in years cannot
// quietly outrank the one the player actually uses.
struct SteamSave {std::wstring folder,account;unsigned long long written=0;std::vector<std::string> files;};
// Steam Cloud keeps its own bookkeeping next to the saves. Only a save slot is worth copying,
// and only under a name the mod is allowed to store.
bool steamSaveFile(const std::string& name);
// Every Steam account on this machine with Recursed progress, most recently played first.
// Never throws; an unreadable or sleeping library is simply not reported.
std::vector<SteamSave> findSteamSaves();
}
