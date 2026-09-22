#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <vector>
#include "support_folder.h"
namespace peek {
std::wstring supportFolder(){
    std::filesystem::path base;
    PWSTR local=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&local))&&local){
        base=local;CoTaskMemFree(local);
    }
    if(base.empty()){
        wchar_t fallback[MAX_PATH]{};
        if(GetTempPathW(MAX_PATH,fallback))base=fallback;
    }
    if(base.empty())return {};
    auto folder=base/L"Recursed++";
    std::error_code ec;std::filesystem::create_directories(folder,ec);
    return folder.wstring();
}
std::string narrowUsable(const std::wstring& path){
    if(path.empty())return {};
    auto convert=[](const std::wstring& text)->std::string{
        BOOL lossy=FALSE;
        int n=WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,text.c_str(),-1,nullptr,0,"?",&lossy);
        if(n<=0||lossy)return {};
        std::vector<char> buffer((size_t)n);
        if(WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,text.c_str(),-1,buffer.data(),n,"?",&lossy)<=0||lossy)return {};
        std::string out(buffer.data());
        return out.size()<MAX_PATH?out:std::string();
    };
    if(auto direct=convert(path);!direct.empty())return direct;
    // A user name outside the active code page turns into question marks. The 8.3 alias is
    // always representable, when the volume still generates them.
    std::wstring shortened(MAX_PATH,L'\0');
    auto n=GetShortPathNameW(path.c_str(),shortened.data(),MAX_PATH);
    if(n==0||n>=MAX_PATH)return {};
    shortened.resize(n);
    return convert(shortened);
}
}
