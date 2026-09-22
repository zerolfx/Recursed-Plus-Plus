#include <windows.h>
#include <cstdio>
#include <filesystem>
#include "game_launch.h"
// Console entry point for development against the prepared runtime copy.
// Players get the window launcher instead.
int wmain(){
    namespace fs=std::filesystem;
    wchar_t own[MAX_PATH];GetModuleFileNameW(nullptr,own,MAX_PATH);
    const auto folder=fs::path(own).parent_path();
    const auto plugin=(folder/L"recursed_peek.dll").wstring();
    const auto game=fs::absolute(folder.parent_path()/L"runtime/Recursed.exe").wstring();
    std::wstring error;
    const auto id=peek::launchModded(game,plugin,error);
    if(!id){fwprintf(stderr,L"%ls\n",error.c_str());return 2;}
    wprintf(L"Recursed++ started. PID=%lu. F8 toggles inspection.\n",id);
    return 0;
}
