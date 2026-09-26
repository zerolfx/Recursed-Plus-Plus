#include "../src/snapshot.h"
#include "../src/runtime_state.h"
#include "../src/particle_sim.h"
#include "../src/save_store.h"
#include "../src/steam_scan.h"
#include "../src/play_record.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "live_state_fixture.h"
#include "exit_fixture.h"
#include "cauldron_fixture.h"
#include "preview_hit_test.h"
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
 testPreviewHit();
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
 testExitTarget();
 testCauldronTarget();
 // Colours are kept per timeline once a table has a start entry; a timeline without its own
 // entry takes the first name's, and a colour one table leaves out is zero.
 {auto colours=[](const peek::Snapshot& s,std::array<float,3> dark,std::array<float,3> light){for(int i=0;i<3;i++)if(std::fabs(s.dark[i]-dark[i])>1e-6f||std::fabs(s.light[i]-light[i])>1e-6f)return false;return true;};
  auto first=peek::loadSnapshot(root,"missions/timelines","start",false);
  assert(first.error.empty()&&colours(first,{.1f,.2f,.3f},{.2f,.3f,.4f}));
  assert(colours(peek::loadSnapshot(root,"missions/timelines","start",false,"reject"),{.4f,.5f,.6f},{.7f,.8f,.9f}));
  assert(colours(peek::loadSnapshot(root,"missions/timelines","start",false,"threadless"),{1,0,0},{0,0,0}));
  assert(colours(peek::loadSnapshot(root,"missions/timelines","start",false,"elsewhere"),{.4f,.5f,.6f},{.7f,.8f,.9f}));
  // Without a colour table a level keeps the game's grey.
  assert(colours(dry,{.2f,.2f,.2f},{.4f,.4f,.4f}));
  // A paradox room the script leaves out is built empty, in its timeline's colours.
  auto rejected=peek::loadSnapshot(root,"missions/timelines","reject",false,"reject",true);
  assert(rejected.error.empty()&&rejected.objects.size()==2&&rejected.hasGlobals);
  auto missing=peek::loadSnapshot(root,"missions/timelines","threadless",false,"threadless",true);
  assert(missing.error.empty()&&missing.objects.empty()&&!missing.hasGlobals&&colours(missing,{1,0,0},{0,0,0}));
  for(const auto& tile:missing.tiles)assert(tile.kind==0&&tile.definition.empty());
  assert(!peek::loadSnapshot(root,"missions/timelines","threadless",false,"threadless").error.empty());}
 peek::ParticleEffect effect;effect.count=8;effect.velocityMin=effect.velocityMax={0,1,0};
 auto a=peek::sampleParticles(effect,1.01,42),b=peek::sampleParticles(effect,1.11,42),same=peek::sampleParticles(effect,1.01,42);
 assert(!a.empty()&&!b.empty()&&a.size()==same.size()&&a.size()<=8);
 assert(a[0].position.y==same[0].position.y&&a[0].position.y!=b[0].position.y);
 for(auto& p:b)assert(std::isfinite(p.position.y)&&p.alpha>=0&&p.alpha<=1);
 // Undo lands just before the latest action: a jump or a use, or a move started from rest.
 {using namespace peek;auto press=[](std::initializer_list<Control> cs){uint16_t v=0;for(auto c:cs)v|=1u<<c;return v;};
  std::vector<PlayTick> ticks(40,PlayTick{0,1.f/60,0});
  for(int t=5;t<20;t++)ticks[t].controls=press({ControlRight});   // walk from rest: an action at 5
  for(int t=12;t<20;t++)ticks[t].controls=press({ControlLeft});   // turning while moving is the same action
  ticks[15].controls|=press({ControlJump});                        // a jump is its own action at 15
  ticks[16].controls|=press({ControlJump});
  ticks[30].controls=press({ControlUse});                          // a use at 30, from rest
  auto chain=[](std::vector<PlayTick>& ts){for(size_t t=1;t<ts.size();t++)ts[t].heldBefore=(uint8_t)ts[t-1].controls;};
  chain(ticks);
  assert(startsAction(0,press({ControlRight}))&&!startsAction(press({ControlRight}),press({ControlLeft})));
  assert(startsAction(press({ControlRight}),press({ControlRight,ControlJump}))&&!startsAction(press({ControlJump}),press({ControlJump})));
  assert(!startsAction(0,press({ControlPause}))&&!startsAction(press({ControlUse}),0));
  assert(beforeLastAction(ticks,40)==30&&beforeLastAction(ticks,30)==15&&beforeLastAction(ticks,15)==5);
  // Before the first action there is nothing to undo, which the answer says by not moving.
  assert(beforeLastAction(ticks,5)==5&&beforeLastAction(ticks,31)==30&&beforeLastAction({},0)==0);
  // Seconds count the steps the game took, so a run at a different step rate goes back as far.
  assert(secondsBefore(ticks,40,1.f/6)==30&&secondsBefore(ticks,40,10)==0&&secondsBefore(ticks,20,0)==19);
  // Undo to just before the jump at 15 while Left and Jump are still held. Play resumes measured
  // against what was held when the undo was asked for, so the held Jump is not a new jump and
  // the next undo reaches the walk's start at 5 rather than landing on 15 again.
  const auto both=press({ControlLeft,ControlJump});
  auto resumed=ticks;resumed.resize(15);
  for(int t=15;t<21;t++)resumed.push_back(PlayTick{both,1.f/60,0,(uint8_t)both});
  assert(beforeLastAction(resumed,21)==5);
 }
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
 std::cout<<"PASS: authored fixtures, tile identities, wet branches, saved globals, Lua limits, deterministic particles, Steam library parsing, save storage and import, undo targets, flames into paradox rooms, cauldron timeline switches, timeline colours\n";
}
