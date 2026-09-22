#include <windows.h>
#include <filesystem>
#include "save_store.h"
#include "support_folder.h"
namespace peek { namespace {
std::filesystem::path fileFor(const std::wstring& folder,const std::string& file){
    if(folder.empty()||!validSaveName(file))return {};
    return std::filesystem::path(folder)/std::wstring(file.begin(),file.end());
}
// Steam's ISteamRemoteStorage, in the order of the interface version the game asks for
// (STEAMREMOTESTORAGE_INTERFACE_VERSION014). The game calls FileWrite, FileRead, FileExists,
// and GetFileSize; the rest keep the vtable slots and the stack right if it ever reaches them.
// MSVC gives these entries the __thiscall convention the game's virtual calls expect.
struct RemoteStorage {
    virtual bool FileWrite(const char* file,const void* data,int bytes){return files&&file&&files->write(file,data,bytes);}
    virtual int FileRead(const char* file,void* data,int bytes){return files&&file?files->read(file,data,bytes):0;}
    virtual unsigned long long FileWriteAsync(const char*,const void*,unsigned){return 0;}
    virtual unsigned long long FileReadAsync(const char*,unsigned,unsigned){return 0;}
    virtual bool FileReadAsyncComplete(unsigned long long,void*,unsigned){return false;}
    virtual bool FileForget(const char*){return false;}
    virtual bool FileDelete(const char* file){return files&&file&&files->erase(file);}
    virtual unsigned long long FileShare(const char*){return 0;}
    virtual bool SetSyncPlatforms(const char*,int){return false;}
    virtual unsigned long long FileWriteStreamOpen(const char*){return 0;}
    virtual bool FileWriteStreamWriteChunk(unsigned long long,const void*,int){return false;}
    virtual bool FileWriteStreamClose(unsigned long long){return false;}
    virtual bool FileWriteStreamCancel(unsigned long long){return false;}
    virtual bool FileExists(const char* file){return files&&file&&files->exists(file);}
    virtual bool FilePersisted(const char* file){return files&&file&&files->exists(file);}
    virtual int GetFileSize(const char* file){return files&&file?files->size(file):0;}
    virtual long long GetFileTimestamp(const char*){return 0;}
    const SaveStorage* files=nullptr;
};
SaveStorage storedFiles;
RemoteStorage storage;
// The shape Steam hands over is a table of interface pointers. The game reads the user,
// utilities, stats, and remote storage entries from it and checks each one before use.
void* context[32]{};
}
std::wstring saveFolder(){
    const auto support=supportFolder();
    if(support.empty())return {};
    auto folder=std::filesystem::path(support)/L"saves";
    std::error_code ec;std::filesystem::create_directories(folder,ec);
    return folder.wstring();
}
bool validSaveName(const std::string& file){
    if(file.empty()||file.size()>64||file.front()=='.')return false;
    for(unsigned char c:file)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;
    return file.find("..")==std::string::npos;
}
bool SaveStorage::write(const std::string& file,const void* data,int bytes) const {
    const auto target=fileFor(folder,file);
    if(target.empty()||bytes<0||(bytes&&!data)){if(note)note("refused",file.c_str(),bytes);return false;}
    std::error_code ec;std::filesystem::create_directories(folder,ec);
    auto pending=target;pending+=L".new";
    HANDLE handle=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE){if(note)note("not written",file.c_str(),bytes);return false;}
    DWORD written=0;
    bool ok=WriteFile(handle,data,(DWORD)bytes,&written,nullptr)!=0&&written==(DWORD)bytes&&FlushFileBuffers(handle)!=0;
    CloseHandle(handle);
    if(ok)ok=MoveFileExW(pending.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok)DeleteFileW(pending.c_str());
    if(note)note(ok?"saved":"not written",file.c_str(),bytes);
    return ok;
}
int SaveStorage::read(const std::string& file,void* data,int bytes) const {
    const auto source=fileFor(folder,file);
    if(source.empty()||!data||bytes<=0)return 0;
    HANDLE handle=CreateFileW(source.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return 0;
    DWORD got=0;
    if(!ReadFile(handle,data,(DWORD)bytes,&got,nullptr))got=0;
    CloseHandle(handle);
    if(note)note("loaded",file.c_str(),(int)got);
    return (int)got;
}
int SaveStorage::size(const std::string& file) const {
    const auto source=fileFor(folder,file);
    if(source.empty())return 0;
    std::error_code ec;auto bytes=std::filesystem::file_size(source,ec);
    if(ec||bytes>0x7fffffffull)return 0;
    return (int)bytes;
}
bool SaveStorage::exists(const std::string& file) const {
    const auto source=fileFor(folder,file);
    if(source.empty())return false;
    std::error_code ec;return std::filesystem::is_regular_file(source,ec);
}
bool SaveStorage::erase(const std::string& file) const {
    const auto target=fileFor(folder,file);
    if(target.empty())return false;
    const bool ok=DeleteFileW(target.c_str())!=0;
    if(note)note(ok?"deleted":"not deleted",file.c_str(),0);
    return ok;
}
int importSaves(const std::wstring& from,const std::vector<std::string>& files,const std::wstring& into,std::wstring& error){
    namespace fs=std::filesystem;
    error.clear();
    if(from.empty()||into.empty()||files.empty()){error=L"There is nothing to copy.";return 0;}
    std::error_code ec;fs::create_directories(into,ec);
    if(!fs::is_directory(into,ec)){error=L"This build's save folder could not be created:\n"+into;return 0;}
    // Whatever is already here is about to be replaced, so it is copied out first under the
    // time it was replaced. Nothing is deleted; a wrong import stays undoable by hand.
    fs::path kept;
    fs::directory_iterator existing(into,ec),done;
    for(;!ec&&existing!=done;existing.increment(ec)){
        std::error_code kind;
        if(!existing->is_regular_file(kind)||kind)continue;
        const auto name=existing->path().filename().string();
        if(!validSaveName(name))continue;
        if(kept.empty()){
            SYSTEMTIME now{};GetLocalTime(&now);
            wchar_t stamp[32]{};
            swprintf_s(stamp,L"replaced-%04u%02u%02u-%02u%02u%02u",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);
            kept=fs::path(into)/stamp;
            // Two imports within the same second would otherwise share a folder, and the second
            // would bury what the first set aside.
            for(int attempt=2;fs::exists(kept,ec)&&attempt<100;attempt++)kept=fs::path(into)/(std::wstring(stamp)+L"-"+std::to_wstring(attempt));
            fs::create_directories(kept,ec);
            if(ec){error=L"The progress already here could not be copied aside, so nothing was replaced.";return 0;}
        }
        std::error_code copied;
        fs::copy_file(existing->path(),kept/existing->path().filename(),fs::copy_options::overwrite_existing,copied);
        if(copied){error=L"The progress already here could not be copied aside, so nothing was replaced.";return 0;}
    }
    // Everything that was here is in `kept` now, so what the imported account does not have can
    // go: a folder holding one account's base game and another's extra chapters is nobody's save.
    if(!kept.empty()){
        fs::directory_iterator leftover(into,ec),stop;
        for(;!ec&&leftover!=stop;leftover.increment(ec)){
            std::error_code kind;
            if(!leftover->is_regular_file(kind)||kind)continue;
            if(validSaveName(leftover->path().filename().string()))fs::remove(leftover->path(),kind);
        }
    }
    int arrived=0,missed=0;
    for(const auto& file:files){
        if(!validSaveName(file)){missed++;continue;}
        const std::wstring wide(file.begin(),file.end());
        std::error_code copied;
        fs::copy_file(fs::path(from)/wide,fs::path(into)/wide,fs::copy_options::overwrite_existing,copied);
        if(copied)missed++;else arrived++;
    }
    if(!arrived)error=L"The save files could not be read:\n"+from;
    else if(missed)error=L"Only part of that Steam save could be copied: "+std::to_wstring(arrived)+L" of "
        +std::to_wstring(arrived+missed)+L" files arrived.\n\nWhat was here before is in the dated folder beside them; use it rather than playing on a half-copied save.";
    return arrived;
}
void* steamContext(const SaveStorage& files){
    // Copied, so the table stays valid for as long as the game holds the context.
    storedFiles=files;storage.files=&storedFiles;
    context[9]=&storage; // The remote storage entry, at the offset the game reads it from.
    return context;
}
}
