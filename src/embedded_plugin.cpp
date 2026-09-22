#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <vector>
#include "embedded_plugin.h"
#include "support_folder.h"
namespace peek { namespace {
// One name, used by the resource script that puts the mod in the executable and by the lookup here.
const wchar_t* kResource=L"RECURSEDPEEKPLUGIN";
const wchar_t* kNotices=L"RECURSEDPEEKNOTICES";
// This project does not define UNICODE, so the stock resource type comes through as a narrow
// pointer; it is integer-encoded either way.
const wchar_t* kData=(const wchar_t*)RT_RCDATA;
const unsigned char* resource(const wchar_t* name,DWORD& bytes){
    HRSRC found=FindResourceW(nullptr,name,kData);
    HGLOBAL loaded=found?LoadResource(nullptr,found):nullptr;
    bytes=found?SizeofResource(nullptr,found):0;
    return (const unsigned char*)(loaded?LockResource(loaded):nullptr);
}
// Held for the life of the launcher: a file nobody may delete while it is open is one antivirus
// cannot take between here and the load, and one another launcher's sweep cannot take either.
HANDLE held=INVALID_HANDLE_VALUE;
bool hold(const std::filesystem::path& file){
    if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
    held=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    return held!=INVALID_HANDLE_VALUE;
}
std::wstring shortDigest(const unsigned char* data,size_t bytes){
    BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char digest[32]{};
    bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,(PUCHAR)data,(ULONG)bytes,0)>=0;
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof digest,0)>=0;
    if(hash)BCryptDestroyHash(hash);
    if(alg)BCryptCloseAlgorithmProvider(alg,0);
    if(!ok)return {};
    wchar_t text[17]{};
    for(int i=0;i<8;i++)swprintf_s(text+i*2,3,L"%02x",digest[i]);
    return text;
}
// Older copies are left behind by earlier versions of this launcher, and by a game that was still
// holding one open. Removing what is not in use keeps the folder from growing a file per build;
// whatever is locked stays, and is removed the next time nothing has it open.
void removeOthers(const std::filesystem::path& folder,const std::filesystem::path& keep){
    std::error_code ec;
    std::filesystem::directory_iterator entry(folder,ec),done;
    for(;!ec&&entry!=done;entry.increment(ec)){
        const auto name=entry->path().filename().wstring();
        if(name.rfind(L"recursed_peek-",0)!=0||entry->path()==keep)continue;
        DeleteFileW(entry->path().c_str());
    }
}
}
std::wstring embeddedPlugin(std::wstring& error){
    namespace fs=std::filesystem;
    error.clear();
    DWORD bytes=0;
    const auto* data=resource(kResource,bytes);
    if(!data||!bytes){
        error=L"This launcher does not carry the mod, so the download is incomplete.\n\nDownload Recursed-Plus-Plus.exe again.";
        return {};
    }
    const auto support=supportFolder();
    const auto digest=shortDigest(data,bytes);
    if(support.empty()||digest.empty()){
        error=L"The mod could not be unpacked, because this account has nowhere to write to.";
        return {};
    }
    std::error_code ec;
    const auto folder=fs::path(support)/L"bin";
    fs::create_directories(folder,ec);
    const auto target=folder/(L"recursed_peek-"+digest+L".dll");
    // The name says what is inside it, so a copy that is already there is usually the right one -
    // including the one a running game has open and Windows will not let anybody overwrite. Usually
    // is not enough for something about to be loaded into a game: read it back and compare.
    if(fs::file_size(target,ec)==bytes&&!ec&&hold(target)){
        std::vector<unsigned char> existing(bytes);
        DWORD got=0;
        if(ReadFile(held,existing.data(),bytes,&got,nullptr)&&got==bytes&&shortDigest(existing.data(),bytes)==digest)
            return target.wstring();
        CloseHandle(held);held=INVALID_HANDLE_VALUE;
    }
    auto pending=target;pending+=L".new";
    HANDLE handle=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD written=0;
    bool ok=handle!=INVALID_HANDLE_VALUE
        &&WriteFile(handle,data,bytes,&written,nullptr)!=0&&written==bytes&&FlushFileBuffers(handle)!=0;
    if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
    if(ok)ok=MoveFileExW(pending.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok){
        DeleteFileW(pending.c_str());
        // Another launcher writing the same contents at the same moment wins the race, and its
        // copy is the one this run wanted anyway.
        if(fs::file_size(target,ec)==bytes&&!ec)return target.wstring();
        error=L"The mod could not be unpacked to:\n"+target.wstring()+L"\n\nSecurity software usually stops this. Allow Recursed-Plus-Plus.exe and try again.";
        return {};
    }
    if(!hold(target)){
        error=L"The mod was unpacked and then disappeared:\n"+target.wstring()+L"\n\nSecurity software usually does this. Allow Recursed-Plus-Plus.exe and try again.";
        return {};
    }
    removeOthers(folder,target);
    return target.wstring();
}
std::wstring embeddedNotices(){
    DWORD bytes=0;
    const auto* data=resource(kNotices,bytes);
    if(!data||!bytes)return {};
    const int wide=MultiByteToWideChar(CP_UTF8,0,(const char*)data,(int)bytes,nullptr,0);
    if(wide<=0)return {};
    std::wstring text((size_t)wide,L'\0');
    MultiByteToWideChar(CP_UTF8,0,(const char*)data,(int)bytes,&text[0],wide);
    // The file is stored with whichever line endings the checkout produced, and a text control
    // draws a lone line feed as nothing at all.
    std::wstring out;out.reserve(text.size()+64);
    for(size_t i=0;i<text.size();i++){
        if(text[i]==L'\n'&&(i==0||text[i-1]!=L'\r'))out+=L'\r';
        out+=text[i];
    }
    return out;
}
}
