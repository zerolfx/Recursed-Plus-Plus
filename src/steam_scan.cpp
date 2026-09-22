#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "save_store.h"
#include "steam_scan.h"
namespace peek { namespace {
// Valve's text format is quoted tokens; only the quoting rules matter for what we read here.
std::vector<std::string> tokens(const std::string& text){
    std::vector<std::string> out;std::string current;bool inside=false;
    for(size_t i=0;i<text.size()&&out.size()<8192;i++){
        char c=text[i];
        if(!inside){if(c=='"')inside=true;continue;}
        if(c=='\\'&&i+1<text.size()){char n=text[++i];current+=(n=='n'?'\n':n=='t'?'\t':n);continue;}
        if(c=='"'){out.push_back(current);current.clear();inside=false;continue;}
        if(current.size()<1024)current+=c;
    }
    return out;
}
bool looksLikePath(const std::string& s){
    if(s.size()<3||s.size()>800)return false;
    if(s[1]==':'&&(s[2]=='\\'||s[2]=='/'))return true;
    return s[0]=='\\'&&s[1]=='\\';
}
bool digits(const std::string& s){return !s.empty()&&s.size()<12&&std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';});}
std::string readFile(const std::filesystem::path& p){
    std::error_code ec;auto size=std::filesystem::file_size(p,ec);
    if(ec||size>4u*1024*1024)return {};
    std::ifstream f(p,std::ios::binary);if(!f)return {};
    std::ostringstream text;text<<f.rdbuf();return text.str();
}
std::wstring registryText(HKEY root,const wchar_t* key,const wchar_t* value){
    wchar_t buffer[1024];DWORD bytes=sizeof buffer;
    if(RegGetValueW(root,key,value,RRF_RT_REG_SZ,nullptr,buffer,&bytes)!=ERROR_SUCCESS)return {};
    std::wstring text(buffer);
    // Steam writes SteamPath with forward slashes. Windows does not care, but normalising makes
    // the duplicate check below meaningful when both registry values point at the same folder.
    std::replace(text.begin(),text.end(),L'/',L'\\');
    while(!text.empty()&&(text.back()==L'\\'||text.back()==L' '))text.pop_back();
    return text;
}
std::wstring widen(const std::string& s){return std::wstring(s.begin(),s.end());}
std::string narrow(const std::wstring& text){std::string out;for(wchar_t c:text)out+=(c<128?(char)c:'?');return out;}
// Steam itself, not its libraries: userdata only ever sits next to the client. This launcher
// is x86, so a read of HKLM\SOFTWARE is already redirected to WOW6432Node.
std::vector<std::filesystem::path> steamRoots(){
    std::vector<std::filesystem::path> roots;
    for(auto root:{registryText(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath"),
                   registryText(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Valve\\Steam",L"InstallPath")}){
        if(root.empty())continue;
        const auto key=foldPath(root);
        bool known=false;for(const auto& seen:roots)if(foldPath(seen.wstring())==key)known=true;
        if(!known)roots.push_back(std::filesystem::path(root));
    }
    return roots;
}
// Steam records the moment a cloud file last changed; the newest one tells which account was
// actually being played, which is the only honest way to order them.
unsigned long long writtenAt(const std::filesystem::path& file){
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if(!GetFileAttributesExW(file.c_str(),GetFileExInfoStandard,&info))return 0;
    return ((unsigned long long)info.ftLastWriteTime.dwHighDateTime<<32)|info.ftLastWriteTime.dwLowDateTime;
}
}
std::wstring foldPath(const std::wstring& path){
    std::wstring out=path;
    // The two Steam registry values disagree about both separators and case, so a raw compare
    // reports a single install as two copies.
    std::replace(out.begin(),out.end(),L'/',L'\\');
    for(auto& c:out)if(c>=L'A'&&c<=L'Z')c=(wchar_t)(c-L'A'+L'a');
    while(!out.empty()&&(out.back()==L'\\'||out.back()==L' '))out.pop_back();
    return out;
}
std::vector<std::string> parseLibraryFolders(const std::string& vdf){
    auto list=tokens(vdf);std::vector<std::string> out;
    for(size_t i=0;i+1<list.size();i++){
        // A library is either a numbered key holding a path directly, as older Steam wrote it,
        // or the "path" key inside a numbered block, as current Steam writes it. The appid map
        // in a modern block also has numeric keys, but its values are byte counts, not paths.
        const bool key=list[i]=="path"||digits(list[i]);
        if(!key||!looksLikePath(list[i+1]))continue;
        if(std::find(out.begin(),out.end(),list[i+1])==out.end())out.push_back(list[i+1]);
    }
    return out;
}
std::string parseInstallDir(const std::string& manifest){
    auto list=tokens(manifest);
    for(size_t i=0;i+1<list.size();i++)if(list[i]=="installdir")return list[i+1];
    return {};
}
std::vector<std::wstring> findRecursed(){
    namespace fs=std::filesystem;
    std::vector<std::wstring> found;
    auto consider=[&](const fs::path& exe){
        std::error_code ec;
        if(!fs::is_regular_file(exe,ec))return;
        auto text=fs::absolute(exe,ec).wstring();
        if(ec)return;
        const auto key=foldPath(text);
        for(const auto& seen:found)if(foldPath(seen)==key)return;
        found.push_back(text);
    };
    std::vector<fs::path> libraries;
    auto addLibrary=[&](const fs::path& p){
        if(p.empty())return;
        const auto key=foldPath(p.wstring());
        for(const auto& seen:libraries)if(foldPath(seen.wstring())==key)return;
        libraries.push_back(p);
    };
    for(const auto& steam:steamRoots()){
        addLibrary(steam);
        for(const auto& entry:parseLibraryFolders(readFile(steam/"steamapps"/"libraryfolders.vdf")))
            addLibrary(fs::path(widen(entry)));
    }
    for(const auto& library:libraries){
        const auto apps=library/"steamapps";
        const auto declared=parseInstallDir(readFile(apps/"appmanifest_497780.acf"));
        if(!declared.empty())consider(apps/"common"/widen(declared)/"Recursed.exe");
        // A manifest can be missing or stale while the folder is still there, and the reverse.
        consider(apps/"common"/"Recursed"/"Recursed.exe");
    }
    return found;
}
bool steamSaveFile(const std::string& name){
    // Steam Cloud keeps its own remotecache.vdf beside the saves. Only a numbered slot is the
    // game's progress, and only a name the mod may store is worth offering to copy.
    if(!validSaveName(name)||name.size()<5||name.compare(0,4,"save")!=0)return false;
    return name[4]>='0'&&name[4]<='9';
}
std::vector<SteamSave> findSteamSaves(){
    namespace fs=std::filesystem;
    std::vector<SteamSave> found;std::error_code ec;
    for(const auto& steam:steamRoots()){
        // Steam names each account folder after its id, and only the account that owns Recursed
        // has an app folder for it.
        fs::directory_iterator accounts(steam/"userdata",ec),done;
        for(;!ec&&accounts!=done;accounts.increment(ec)){
            const auto remote=accounts->path()/"497780"/"remote";
            std::error_code inner;
            fs::directory_iterator files(remote,inner);
            SteamSave save{remote.wstring(),accounts->path().filename().wstring(),0,{}};
            for(;!inner&&files!=done&&save.files.size()<32;files.increment(inner)){
                std::error_code kind;
                if(!files->is_regular_file(kind)||kind)continue;
                const auto name=narrow(files->path().filename().wstring());
                if(!steamSaveFile(name))continue;
                save.files.push_back(name);
                const auto written=writtenAt(files->path());
                if(written>save.written)save.written=written;
            }
            if(!save.files.empty())found.push_back(save);
        }
        ec.clear();
    }
    // Most recently played first: an account left behind years ago must not outrank the one in
    // use, because the import replaces the mod's progress with whichever this reports first.
    std::sort(found.begin(),found.end(),[](const SteamSave& a,const SteamSave& b){return a.written>b.written;});
    return found;
}
}
