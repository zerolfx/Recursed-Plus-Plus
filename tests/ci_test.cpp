#include "../src/snapshot.h"
#include "../src/runtime_state.h"
#include "../src/particle_sim.h"
#include "../src/save_store.h"
#include "../src/steam_scan.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "live_state_fixture.h"
// The game never sees the storage object; it calls entries of the interface table Steam would
// have handed it. Declaring the same entries in the same order here tests the offsets the game
// actually uses, not just the code behind them.
struct SteamRemoteStorage {
 virtual bool FileWrite(const char*,const void*,int)=0;
 virtual int FileRead(const char*,void*,int)=0;
 virtual unsigned long long FileWriteAsync(const char*,const void*,unsigned)=0;
 virtual unsigned long long FileReadAsync(const char*,unsigned,unsigned)=0;
 virtual bool FileReadAsyncComplete(unsigned long long,void*,unsigned)=0;
 virtual bool FileForget(const char*)=0;
 virtual bool FileDelete(const char*)=0;
 virtual unsigned long long FileShare(const char*)=0;
 virtual bool SetSyncPlatforms(const char*,int)=0;
 virtual unsigned long long FileWriteStreamOpen(const char*)=0;
 virtual bool FileWriteStreamWriteChunk(unsigned long long,const void*,int)=0;
 virtual bool FileWriteStreamClose(unsigned long long)=0;
 virtual bool FileWriteStreamCancel(unsigned long long)=0;
 virtual bool FileExists(const char*)=0;
 virtual bool FilePersisted(const char*)=0;
 virtual int GetFileSize(const char*)=0;
 virtual long long GetFileTimestamp(const char*)=0;
};
int main(){
 testLiveRoomRead();
 const char* root="tests/fixtures";
 auto dry=peek::loadSnapshot(root,"missions/test","start",false);
 auto wet=peek::loadSnapshot(root,"missions/test","start",true);
 assert(dry.error.empty()&&wet.error.empty());
 // A mission saved with a UTF-8 byte-order mark loads here as it does in the game.
 {namespace fs=std::filesystem;const auto marked=fs::temp_directory_path()/"recursed-peek-bom";fs::remove_all(marked);
  fs::create_directories(marked/"custom"/"missions");fs::create_directories(marked/"data"/"tiles");
  fs::copy_file(fs::path(root)/"data"/"tiles"/"test.lua",marked/"data"/"tiles"/"test.lua");
  {std::ifstream in(fs::path(root)/"custom"/"missions"/"test.lua",std::ios::binary);std::ofstream out(marked/"custom"/"missions"/"marked.lua",std::ios::binary);out<<"\xEF\xBB\xBF"<<in.rdbuf();}
  auto withMark=peek::loadSnapshot(marked.string(),"missions/marked","start",false);
  assert(withMark.error.empty()&&withMark.objects.size()==dry.objects.size());fs::remove_all(marked);}
 assert(dry.objects.size()==3&&dry.objects[1].target=="start"&&dry.hasGlobals);
 assert(dry.tiles[3*20+2].kind==1&&dry.tiles[3*20+2].frame==7&&dry.tiles[3*20+2].definition=="floor");
 assert(!peek::wetAt(dry,2,2)&&peek::wetAt(wet,2,2));
 peek::GlobalState globals;globals.available=globals.initialized=true;
 peek::applyGlobals(dry,globals);assert(dry.objects.size()==2);
 globals.objects.push_back({"key","",7,3,true});
 peek::applyGlobals(dry,globals);peek::applyGlobals(dry,globals);
 assert(dry.objects.size()==3&&dry.objects.back().x==7);
 assert(!peek::readGlobals(1,1,"start").available);
 for(const char* room:{"missing","forbidden","endless","overflow"}){
  auto bad=peek::loadSnapshot(root,"missions/test",room,false);
  assert(!bad.error.empty()&&bad.objects.empty()&&!bad.hasGlobals);
 }
 assert(!peek::loadSnapshot(root,"../outside","start",false).error.empty());
 peek::ParticleEffect effect;effect.count=8;effect.velocityMin=effect.velocityMax={0,1,0};
 auto a=peek::sampleParticles(effect,1.01,42),b=peek::sampleParticles(effect,1.11,42),same=peek::sampleParticles(effect,1.01,42);
 assert(!a.empty()&&!b.empty()&&a.size()==same.size()&&a.size()<=8);
 assert(a[0].position.y==same[0].position.y&&a[0].position.y!=b[0].position.y);
 for(auto& p:b)assert(std::isfinite(p.position.y)&&p.alpha>=0&&p.alpha<=1);
 // Steam has written two shapes of library file over the years and both are still out there.
 auto flat=peek::parseLibraryFolders("\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\"1\"\n\t\"1\"\t\"D:\\\\SteamLibrary\"\n\t\"2\"\t\"E:\\\\Games\\\\Steam\"\n}\n");
 assert(flat.size()==2&&flat[0]=="D:\\SteamLibrary"&&flat[1]=="E:\\Games\\Steam");
 // The modern file nests the path, and carries an appid map whose numeric values are byte
 // counts. Reading one of those as a library folder would send the launcher hunting in nowhere.
 auto nested=peek::parseLibraryFolders("\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"497780\"\t\t\"74000000\"\n\t\t}\n\t}\n}\n");
 assert(nested.size()==1&&nested[0]=="C:\\Program Files (x86)\\Steam");
 assert(peek::parseInstallDir("\"AppState\"\n{\n\t\"appid\"\t\"497780\"\n\t\"installdir\"\t\"Recursed\"\n}\n")=="Recursed");
 assert(peek::parseLibraryFolders("").empty()&&peek::parseInstallDir("").empty());
 assert(peek::parseInstallDir("\"AppState\"\n{\n\t\"appid\"\t\"497780\"\n}\n").empty());
 // Steam's two registry values disagree about case and separators, so the same install has to
 // fold to the same key or the launcher offers to start one copy twice.
 assert(peek::foldPath(L"c:/program files (x86)/steam")==peek::foldPath(L"C:\\Program Files (x86)\\Steam\\"));
 assert(peek::foldPath(L"D:\\Games")!=peek::foldPath(L"E:\\Games"));
 // The game keeps its progress in Steam Cloud and nowhere else, so an isolated run only keeps
 // what this storage keeps for it: what one run writes, the next one has to read back.
 namespace fs=std::filesystem;
 const auto folder=fs::temp_directory_path()/"recursed-peek-saves";
 fs::remove_all(folder);
 peek::SaveStorage store{folder.wstring()};
 auto* remote=(SteamRemoteStorage*)((void**)peek::steamContext(store))[9];
 const std::string progress="complete missions/basement1\ncomplete missions/basic5\n";
 assert(!remote->FileExists("save0")&&remote->GetFileSize("save0")==0);
 assert(remote->FileWrite("save0",progress.data(),(int)progress.size()));
 assert(remote->FileExists("save0")&&remote->GetFileSize("save0")==(int)progress.size());
 std::string reread(progress.size(),'\0');
 assert(remote->FileRead("save0",&reread[0],(int)reread.size())==(int)progress.size()&&reread==progress);
 // The game names its own slots. Anything else would write outside the save folder.
 assert(!remote->FileWrite("../escape",progress.data(),4)&&!remote->FileExists("../escape"));
 assert(peek::validSaveName("save0-dlc2")&&!peek::validSaveName("..")&&!peek::validSaveName("a/b")&&!peek::validSaveName("c:\\x"));
 // Steam keeps its own bookkeeping beside the saves; only the slots are worth importing.
 assert(peek::steamSaveFile("save0")&&peek::steamSaveFile("save0-dlc")&&!peek::steamSaveFile("remotecache.vdf")&&!peek::steamSaveFile("save"));
 // An import replaces progress, so whatever it replaced has to stay recoverable.
 const auto cloud=folder/"cloud";
 fs::create_directories(cloud);
 {std::ofstream out(cloud/"save0",std::ios::binary);out<<"complete missions/wood1\n";}
 std::wstring error;
 assert(peek::importSaves(cloud.wstring(),{"save0"},folder.wstring(),error)==1&&error.empty());
 assert(remote->GetFileSize("save0")==24);
 int replaced=0;
 for(const auto& entry:fs::directory_iterator(folder))
  if(entry.is_directory()&&entry.path().filename().wstring().rfind(L"replaced-",0)==0)
   replaced+=fs::exists(entry.path()/"save0")?1:0;
 assert(replaced==1);
 assert(!peek::importSaves(cloud.wstring(),{},folder.wstring(),error)&&!error.empty());
 fs::remove_all(folder);
 std::cout<<"PASS: authored fixtures, tile identities, wet branches, saved globals, Lua limits, deterministic particles, Steam library parsing, save storage and import\n";
}
