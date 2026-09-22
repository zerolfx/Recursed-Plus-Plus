#pragma once
#include <functional>
#include <string>
#include "support_folder.h"
namespace peek {
// One executable fingerprint and one injection path, shared by the console launcher and the
// window launcher so a player and a developer can never be running different logic.
bool supportedGame(const std::wstring& exe);
// Starts the game suspended, loads the plugin into it, runs the plugin entry point, resumes,
// and then confirms the process actually survived. Returns the new process id, or 0 with a
// reason in `error` naming the step that failed. `stage` is called on the calling thread.
unsigned long launchModded(const std::wstring& exe,const std::wstring& plugin,std::wstring& error,
                           const std::function<void(const wchar_t*)>& stage={});
}
