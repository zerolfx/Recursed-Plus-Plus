#include "../src/snapshot.h"
#include "../src/runtime_state.h"
#include "../src/particle_sim.h"
#include "../src/steam_scan.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include "live_state_fixture.h"
int main(){
 testLiveRoomRead();
 const char* root="tests/fixtures";
 auto dry=peek::loadSnapshot(root,"missions/test","start",false);
 auto wet=peek::loadSnapshot(root,"missions/test","start",true);
 assert(dry.error.empty()&&wet.error.empty());
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
 std::cout<<"PASS: authored fixtures, tile identities, wet branches, saved globals, Lua limits, deterministic particles, Steam library parsing\n";
}
