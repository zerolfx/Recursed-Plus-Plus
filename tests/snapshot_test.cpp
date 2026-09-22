#include "../src/snapshot.h"
#include "../src/runtime_state.h"
#include "../src/asset_mesh.h"
#include "../src/particle_sim.h"
#include <cmath>
#include <cassert>
#include <iostream>
#include <filesystem>
#include <utility>
int main(){
 auto a=peek::loadSnapshot("runtime","missions/peek-lab","keyroom",false);
 assert(a.error.empty());assert(a.objects.size()==5);assert(a.tiles[9*20+10].kind==1);
 bool key=false,chest=false,ring=false;for(auto& o:a.objects){key|=o.kind=="key"&&o.x==12;chest|=o.kind=="chest"&&o.target=="pool";ring|=o.kind=="record"&&o.target=="sounds/voices/c5";}assert(key&&chest&&ring);
 auto b=peek::loadSnapshot("runtime","missions/peek-lab","pool",false);assert(b.error.empty());assert(b.tiles[11*20+9].kind==3);
 auto c=peek::loadSnapshot("runtime","missions/basic5","start",false);assert(c.error.empty());assert(c.objects.size()==5);
 // Stock room holding a record: its fourth argument is a voice-clip path, not a room.
 auto under=peek::loadSnapshot("runtime","missions/basic5","under",false);
 assert(under.error.empty()&&under.objects.size()==3);
 bool voice=false;for(auto& o:under.objects)voice|=o.kind=="record"&&o.target=="sounds/voices/c5";
 assert(voice);
 assert(c.tileset=="tiles/cave"&&c.pattern=="backgrounds/checker");
 assert(c.tiles[0].frame==8&&c.tiles[0].kind==1);
 assert(a.tiles[9*20+10].definition=="brick_u"&&b.tiles[11*20+9].definition=="watersurface");
 // Every kind the native scene learned after chests and keys, in one authored room.
 auto props=peek::loadSnapshot("runtime","missions/peek-lab","props",false);
 assert(props.error.empty()&&props.objects.size()==6&&props.hasGlobals);
 bool fan=false,generic=false,cauldron=false,bird=false,crux=false;
 for(auto& o:props.objects){fan|=o.kind=="fan";generic|=o.kind=="generic";
   cauldron|=o.kind=="cauldron"&&o.target=="keyroom";bird|=o.kind=="bird";crux|=o.kind=="crux"&&o.global;}
 assert(fan&&generic&&cauldron&&bird&&crux);
 auto portals=peek::loadSnapshot("runtime","missions/peek-lab","portals",false);
 assert(portals.error.empty()&&portals.objects.size()==3);
 assert(portals.objects[0].kind=="player"&&portals.objects[0].x==3&&portals.objects[0].y==12);
 assert(portals.objects[1].kind=="yield"&&portals.objects[1].x==10&&portals.objects[2].target=="pool");
 // Asset file first, then the mesh entry inside it; they differ for jar and record.
 for(const auto& model:{std::pair<const char*,const char*>{"chest","chest"},{"key","key"},{"box","box"},{"crystal","crystal"},{"cauldron","cauldron"},{"yield","jar"},{"record","ring"},{"fan","fan"},{"oobleck","oobleck"}}){
   auto kind=model.second;auto mesh=peek::loadMesh("runtime",model.first,model.second);
   if(!mesh.error.empty())std::cerr<<kind<<": "<<mesh.error<<"\n";
   assert(mesh.error.empty()&&mesh.faces.size()>12&&mesh.faces.size()<8192);
   for(const auto& face:mesh.faces)for(const auto& p:face.p)assert(std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::fabs(p.x)<3&&std::fabs(p.y)<3&&std::fabs(p.z)<3);
 }
 assert(!peek::loadMesh("runtime","../chest","chest").error.empty());
 auto glow=peek::loadParticleEffect("runtime","chest","glow");
 assert(glow.error.empty()&&glow.texture=="particles/dot"&&glow.count==12);
 assert(std::fabs(glow.interval-.1f)<.00001f&&glow.colorMax.z==1&&glow.positionMin.y==2);
 auto p0=peek::sampleParticles(glow,1.01,42),p1=peek::sampleParticles(glow,1.11,42),again=peek::sampleParticles(glow,1.01,42);
 assert(!p0.empty()&&!p1.empty()&&p0.size()<=12&&p0.size()==again.size());
 assert(p0[0].position.y==again[0].position.y&&p0[0].position.y!=p1[0].position.y);
 for(const auto& p:p0)assert(std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&p.alpha>=0&&p.alpha<=1);
 auto sparkle=peek::loadParticleEffect("runtime","key","glow");assert(sparkle.error.empty()&&sparkle.texture=="particles/twinkle");
 for(const char* entry:{"over","under"}){auto water=peek::loadParticleEffect("runtime","water",entry);assert(water.error.empty()&&water.sizePeak>0);auto particles=peek::sampleParticles(water,1.2,42);assert(!particles.empty());for(const auto& p:particles)assert(std::isfinite(p.size)&&p.size>=0&&p.size<=water.sizePeak);}
 auto dry=peek::loadSnapshot("runtime","missions/sewer1","cupboard",false);
 auto wet=peek::loadSnapshot("runtime","missions/sewer1","cupboard",true);
 assert(dry.error.empty()&&wet.error.empty());assert(dry.tiles[2*20+5].kind==0);assert(wet.tiles[2*20+5].kind==3);
 auto invalid=peek::loadSnapshot("runtime","../../outside","start",false);assert(!invalid.error.empty());
 auto missing=peek::loadSnapshot("runtime","missions/peek-lab","missing",false);assert(!missing.error.empty());
 auto vault=peek::loadSnapshot("runtime","missions/state-lab","vault",false);
 assert(vault.error.empty()&&vault.hasGlobals&&vault.objects.size()==4);
 assert(!peek::wetAt(vault,6,12.5f));
 auto flooded=peek::loadSnapshot("runtime","missions/state-lab","vault",true);
 assert(flooded.error.empty()&&peek::wetAt(flooded,6,12.5f));
 // A visited room's empty saved list must remove declarations; otherwise a
 // collected key would misleadingly reappear in every preview.
 peek::GlobalState visited;visited.available=true;visited.initialized=true;
 auto empty=vault;peek::applyGlobals(empty,visited);assert(empty.objects.size()==2);
 visited.objects.push_back({"key","",12,12.6f,true});
 peek::applyGlobals(vault,visited);assert(vault.objects.size()==3);
 assert(vault.objects.back().kind=="key"&&vault.objects.back().y==12.6f);
 peek::applyGlobals(vault,visited);assert(vault.objects.size()==3); // no duplication
 auto stale=peek::readGlobals(1,1,"vault");assert(!stale.available&&!stale.error.empty());
 size_t count=0,failed=0;
 for(const auto& f:std::filesystem::recursive_directory_iterator("runtime/data/missions"))if(f.path().extension()==".lua"){
   auto mission="missions/"+std::filesystem::relative(f.path(),"runtime/data/missions").generic_string();
   auto room=peek::loadSnapshot("runtime",mission,"start",false);
   ++count;if(!room.error.empty()){++failed;std::cerr<<f.path().filename()<<": "<<room.error<<"\n";}
 }
 std::cout<<"Stock starting rooms: "<<count-failed<<"/"<<count<<" loaded\n";
 assert(failed==0);
 std::cout<<"PASS: actual Lua room extraction, object targets, tile mappings, water, stock level, missing room and path rejection\n";
}
