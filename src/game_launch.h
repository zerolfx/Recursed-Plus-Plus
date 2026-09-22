#pragma once
#include <functional>
#include <string>
#include "support_folder.h"
namespace peek {
// One executable fingerprint and one injection path, shared by the console launcher and the
// window launcher so a player and a developer can never be running different logic.
bool supportedGame(const std::wstring& exe);
// How the launched game reaches Steam. `Steam` lets the real API through, so achievements,
// statistics and cloud saves are the ones the player already has; `Isolated` answers Steam
// inside the process and keeps progress in this build's own save folder. Steam mode falls back
// to that folder by itself when Steam turns out not to be running.
enum class SteamUse { Steam, Isolated };
// Starts the game suspended, loads the plugin into it, runs the plugin entry point, resumes,
// and then confirms the process actually survived. Returns the new process id, or 0 with a
// reason in `error` naming the step that failed. `stage` is called on the calling thread.
// `developer` turns on what only a development run should have - the diagnostics behind
// RECURSED_PEEK_DEV - and lets the rest of that family of variables through from this process.
// A player's run states them instead, so nothing set in a shell can reach the game.
unsigned long launchModded(const std::wstring& exe,const std::wstring& plugin,SteamUse steam,bool developer,
                           std::wstring& error,const std::function<void(const wchar_t*)>& stage={});
}
