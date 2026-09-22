#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <cstdio>
#include <filesystem>
#include "game_launch.h"
#include "support_folder.h"
namespace peek { namespace {
// The one build this mod reads offsets from. Anything else is refused before a process starts.
const char* kSupported="0E47D5DF0F45152978777E5F8EC7F2435BAB79CD84D630CFAAD5106B90D74251";
struct Remote {DWORD result;DWORD error;bool timedOut;};
Remote runRemote(HANDLE process,LPTHREAD_START_ROUTINE fn,void* arg){
    Remote out{0,0,false};
    HANDLE t=CreateRemoteThread(process,nullptr,0,fn,arg,0,nullptr);
    if(!t){out.error=GetLastError();return out;}
    // Measured at a few milliseconds warm; a timeout here means something is scanning the
    // game's thirty-odd unsigned DLLs, not that the mod is broken.
    if(WaitForSingleObject(t,30000)==WAIT_OBJECT_0)GetExitCodeThread(t,&out.result);
    else out.timedOut=true;
    CloseHandle(t);return out;
}
void stop(PROCESS_INFORMATION& pi){
    TerminateProcess(pi.hProcess,5);WaitForSingleObject(pi.hProcess,5000);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
}
}
bool supportedGame(const std::wstring& path){
    FILE* f=nullptr; _wfopen_s(&f,path.c_str(),L"rb"); if(!f)return false;
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
    unsigned char buf[65536], digest[32]; size_t n;
    while(ok&&(n=fread(buf,1,sizeof buf,f))!=0)ok=BCryptHashData(hash,buf,(ULONG)n,0)>=0;
    ok=ok&&!ferror(f); fclose(f);
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof digest,0)>=0;
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    if(!ok)return false;
    char hex[65]{};for(int i=0;i<32;i++)sprintf_s(hex+i*2,3,"%02X",digest[i]);
    return std::string(hex)==kSupported;
}
unsigned long launchModded(const std::wstring& exe,const std::wstring& plugin,SteamUse steam,
                           std::wstring& error,const std::function<void(const wchar_t*)>& stage){
    namespace fs=std::filesystem;
    error.clear();
    auto step=[&](const wchar_t* text){if(stage)stage(text);};
    std::error_code ec;
    step(L"Checking the game...");
    if(!fs::exists(exe,ec)){error=L"The game is not where the launcher expected it:\n"+exe;return 0;}
    if(!supportedGame(exe)){error=L"That file is not the build this mod was measured against, so it would read the wrong addresses.\n\nOnly the Steam Windows build of Recursed can be started.";return 0;}
    if(!fs::exists(plugin,ec)){error=L"recursed_peek.dll is missing next to the launcher.\n\nExtract the whole download into one folder and run it again.";return 0;}
    // The game reads data/ relative to its working directory, which is always its own folder
    // here. Without it the game still starts, then fails in ways that look like the mod's
    // fault, so refuse up front instead.
    if(!fs::is_directory(fs::path(exe).parent_path()/L"data",ec)){
        error=L"There is no data folder next to that file, so the game would have nothing to load.\n\nChoose the Recursed.exe inside your Recursed installation.";
        return 0;
    }
    HMODULE local=LoadLibraryExW(plugin.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    auto init=local?GetProcAddress(local,"RecursedPeekInitialize"):nullptr;
    if(!init){if(local)FreeLibrary(local);error=L"recursed_peek.dll could not be read. The download may be incomplete.";return 0;}
    const uintptr_t initRva=(uintptr_t)init-(uintptr_t)local;FreeLibrary(local);

    // Recursed resolves data/, custom/missions/ and recursed.conf against the process working
    // directory, not against its own folder, and starting it anywhere else kills it instantly
    // with no window and no message. The plugin resolves its copies of those assets from the
    // executable's folder, so the two agree only while this stays the exe's own directory.
    const auto folder=fs::path(exe).parent_path();
    // The child inherits this environment. Steam identifies a game it did not start itself by
    // the app id in the environment, so setting it is what lets the real API come up, and
    // clearing it is what keeps an isolated run from reaching a Steam this launcher was itself
    // started from. The plugin reads its own flag and decides what to do when Steam is absent.
    const bool useSteam=steam==SteamUse::Steam;
    SetEnvironmentVariableW(L"SteamAppId",useSteam?L"497780":nullptr);
    SetEnvironmentVariableW(L"SteamGameId",useSteam?L"497780":nullptr);
    SetEnvironmentVariableW(L"RECURSED_PEEK_STEAM",useSteam?L"1":L"0");
    std::wstring cmd=L"\""+exe+L"\"";
    STARTUPINFOW si{};si.cb=sizeof si;si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_SHOWNORMAL;PROCESS_INFORMATION pi{};
    step(L"Starting the game...");
    // A new, owned game process only. Never attach to a game the player already started.
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,folder.c_str(),&si,&pi)){
        error=L"Windows refused to start the game (error "+std::to_wstring(GetLastError())+L").";return 0;
    }
    step(L"Loading the mod...");
    const SIZE_T bytes=(plugin.size()+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(pi.hProcess,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    SIZE_T written=0;
    if(!remote||!WriteProcessMemory(pi.hProcess,remote,plugin.c_str(),bytes,&written)||written!=bytes){
        stop(pi);
        error=L"Security software is stopping the launcher from preparing the game.\n\nAllow these two files, then try again:\n"+plugin;
        return 0;
    }
    auto load=runRemote(pi.hProcess,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"),remote);
    if(load.timedOut){
        stop(pi);
        error=L"The game is taking unusually long to start, which normally means security software is scanning it. Try again; the second run is usually quick.";
        return 0;
    }
    if(!load.result){
        stop(pi);
        if(!fs::exists(plugin,ec))error=L"recursed_peek.dll was removed while the launcher was using it. Antivirus software usually did this; restore the file and add this folder to its exclusions.";
        else if(load.error==ERROR_ACCESS_DENIED)error=L"Windows refused to let the launcher load the mod into the game.\n\nThis is what security software blocks. Allow both of these, then try again:\n"+plugin;
        else error=L"The mod could not be loaded into the game"+(load.error?L" (error "+std::to_wstring(load.error)+L")":std::wstring())+L".";
        return 0;
    }
    // LoadLibraryW hands back the module base in the target, so the entry point is that plus
    // the offset measured in our own copy.
    auto ready=runRemote(pi.hProcess,(LPTHREAD_START_ROUTINE)(load.result+initRva),nullptr);
    if(ready.timedOut||!ready.result){
        stop(pi);
        error=ready.timedOut
            ? std::wstring(L"The mod did not finish starting and the game was closed again.")
            : L"The mod refused to run on this copy of the game and the game was closed again.\n\nThe log says why:\n"+supportFolder()+L"\\peek.log";
        return 0;
    }
    VirtualFreeEx(pi.hProcess,remote,0,MEM_RELEASE);
    ResumeThread(pi.hThread);

    // The game dies within milliseconds if anything about its start-up is wrong, so a process
    // id alone is not evidence that it is running. Wait for it to open a window, and treat an
    // early exit as the failure it is rather than reporting success.
    step(L"Waiting for the game...");
    WaitForInputIdle(pi.hProcess,10000);
    DWORD code=STILL_ACTIVE;
    if(GetExitCodeProcess(pi.hProcess,&code)&&code!=STILL_ACTIVE){
        wchar_t hex[16]{};swprintf_s(hex,L"0x%08lX",code);
        error=std::wstring(L"The game started and closed again immediately (")+hex+L").\n\nThis happens when the game cannot read its own data folder. Choose the Recursed.exe that sits next to a data folder.";
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
        return 0;
    }
    const auto id=pi.dwProcessId;
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    return id;
}
}
