#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>

static bool supported(const std::wstring& path) {
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
    return std::string(hex)=="0E47D5DF0F45152978777E5F8EC7F2435BAB79CD84D630CFAAD5106B90D74251";
}
static DWORD runRemote(HANDLE process,LPTHREAD_START_ROUTINE fn,void* arg){
    HANDLE t=CreateRemoteThread(process,nullptr,0,fn,arg,0,nullptr);
    if(!t)return 0;
    const auto wait=WaitForSingleObject(t,15000);DWORD result=0;
    if(wait==WAIT_OBJECT_0)GetExitCodeThread(t,&result);
    CloseHandle(t);return result;
}
int wmain(){
    namespace fs=std::filesystem;
    wchar_t own[MAX_PATH];GetModuleFileNameW(nullptr,own,MAX_PATH);
    const auto dll=(fs::path(own).parent_path()/L"recursed_peek.dll").wstring();
    const auto game=fs::absolute(fs::path(own).parent_path().parent_path()/L"runtime/Recursed.exe");
    if(!supported(game.wstring())){fwprintf(stderr,L"Unsupported or missing game: %ls\n",game.c_str());return 2;}
    HMODULE local=LoadLibraryExW(dll.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES);
    auto init=local?GetProcAddress(local,"RecursedPeekInitialize"):nullptr;
    if(!init){fprintf(stderr,"Plugin missing or invalid. Build first.\n");return 3;}
    const uintptr_t initRva=(uintptr_t)init-(uintptr_t)local;FreeLibrary(local);
    std::wstring cmd=L"\""+game.wstring()+L"\"";
    STARTUPINFOW si{};si.cb=sizeof si;si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_SHOWNORMAL;PROCESS_INFORMATION pi{};
    // A new, owned game process only. Never attach to an existing user's session.
    if(!CreateProcessW(game.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,game.parent_path().c_str(),&si,&pi)){
        fprintf(stderr,"CreateProcess failed: %lu\n",GetLastError());return 4;
    }
    const SIZE_T bytes=(dll.size()+1)*sizeof(wchar_t);
    void* remote=VirtualAllocEx(pi.hProcess,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    SIZE_T written=0;DWORD module=0;
    if(remote&&WriteProcessMemory(pi.hProcess,remote,dll.c_str(),bytes,&written)&&written==bytes)
        module=runRemote(pi.hProcess,(LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"),remote);
    const DWORD ready=module?runRemote(pi.hProcess,(LPTHREAD_START_ROUTINE)(module+initRva),nullptr):0;
    if(!ready){fprintf(stderr,"Plugin initialization failed; cancelling this test process.\n");TerminateProcess(pi.hProcess,5);WaitForSingleObject(pi.hProcess,5000);}
    else { if(remote)VirtualFreeEx(pi.hProcess,remote,0,MEM_RELEASE);ResumeThread(pi.hThread);printf("Recursed++ started. PID=%lu. F8 toggles inspection.\n",pi.dwProcessId);}
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return ready?0:5;
}
