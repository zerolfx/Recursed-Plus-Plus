#include "../src/snapshot.h"
#include "../src/runtime_state.h"
#include "../src/particle_sim.h"
#include <cassert>
#include <cmath>
#include <iostream>
int main(){
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
 std::cout<<"PASS: authored fixtures, tile identities, wet branches, saved globals, Lua limits, deterministic particles\n";
}
