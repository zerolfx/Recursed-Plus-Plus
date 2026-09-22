#include <windows.h>
#include <array>
#include <vector>
#include <cstring>
#include <cmath>
#include <sstream>
#include <algorithm>
#include "native_scene.h"
namespace peek { namespace {
static uintptr_t base=0,room=0,renderer=0,source=0;
static std::array<unsigned char,0xa0> host{};
// Exit stores an entry-context pointer. Its draw/attach/destructor paths do not
// use it; keep a private empty context instead of borrowing gameplay pointers.
static std::array<uintptr_t,3> exitContext{};
static std::vector<unsigned char> stack;
static std::vector<uintptr_t> entities;
// Kinds whose own update advances the draw angle every frame, with the rate that update uses.
struct Spin {uintptr_t entity;float base,rate;};
static std::vector<Spin> rotating;
static std::string signature,error;
static float elapsed=0;
static Snapshot drawnSnapshot;
template<class T>T& at(uintptr_t p,size_t offset=0){return *(T*)(p+offset);}
template<class T>T fn(uintptr_t va){return (T)(base+va-0x400000);}
using Method=void(__thiscall*)(void*);
using Construct=void*(__thiscall*)(void*);
using Alloc=void*(__cdecl*)(size_t);
using Free=void(__cdecl*)(void*);
static void* allocate(size_t n){void* p=at<Alloc>(base+0x7736c)(n);if(p)memset(p,0,n);return p;}
static void release(void* p){at<Free>(base+0x77368)(p);}
static int tileIndex(uintptr_t liveHost,const Tile& tile){
 if(tile.definition.empty())return 0;
 auto head=at<uintptr_t>(liveHost,0x4c),node=at<uintptr_t>(head,4);
 for(int n=0;node!=head&&n<128;n++){
  if(at<unsigned char>(node,13))return -1;auto p=node+16;auto len=at<uint32_t>(p,16),cap=at<uint32_t>(p,20);if(len>240||cap<len)return -1;
  std::string name((const char*)(cap<16?p:at<uintptr_t>(p)),len);int order=tile.definition.compare(name);if(!order)return at<int>(node,0x28);node=at<uintptr_t>(node,order<0?0:8);
 }return -1;
}
// The game has no generic entity factory: for everything a script can spawn, the only
// kind-to-constructor mapping is the chain inlined into its Lua Spawn binding at 0x43E6F0,
// and these are that chain's own allocation sizes and constructors. Jar and froth have no
// arm there; they reach a preview only through a live outer room, so their sizes and
// constructors come from their own call sites. One table keeps the scene guard and the
// construction switch from drifting apart; sizes are never used to pick a branch,
// because Exit, Crystal and Fan all happen to be 0x5C.
struct Native {uintptr_t ctor;size_t bytes;};
static bool nativeEntity(const std::string& kind,Native& out){
 static const struct {const char* kind;uintptr_t ctor;size_t bytes;} table[]={
  {"player",0x412e00,0x5c},{"yield",0x412e00,0x5c},{"chest",0x410fe0,0x7c},{"box",0x410210,0x50},
  {"key",0x416210,0x50},{"lock",0x416790,0x50},{"crystal",0x412580,0x5c},{"diamond",0x412580,0x5c},
  {"ruby",0x412580,0x5c},{"record",0x418120,0x70},{"fan",0x4138b0,0x5c},{"generic",0x414780,0x60},
  {"cauldron",0x410650,0x78},{"bird",0x40ede0,0x6c},{"jar",0x415770,0x74},{"froth",0x4144d0,0x50},
 };
 for(const auto& e:table)if(kind==e.kind){out={e.ctor,e.bytes};return true;}
 return false;
}
static bool entity(const Object& o,bool settle){
 Native n{};if(!nativeEntity(o.kind,n))return false;
 const uintptr_t ctor=n.ctor;
 const bool portal=o.kind=="player"||o.kind=="yield";
 void* p=allocate(n.bytes);if(!p)return false;
 if(portal){
  // Every chest destination has a parent room. Spawn("player") also creates
  // a regular Exit there in the original game; Spawn("yield") creates its
  // green variant. Construct only Exit, never Player or its gameplay context.
  using ExitCtor=void*(__thiscall*)(void*,void*,bool);
  fn<ExitCtor>(ctor)(p,exitContext.data(),o.kind=="yield");
 }else if(o.kind=="chest"||o.kind=="cauldron"){
  // Both take their destination room as a game string by pointer; the caller owns it.
  alignas(8) unsigned char name[24]{};using StringCtor=void*(__thiscall*)(void*,const char*);fn<StringCtor>(0x401e20)(name,o.target.c_str());
  using TargetCtor=void*(__thiscall*)(void*,void*);fn<TargetCtor>(ctor)(p,name);fn<Method>(0x401e80)(name);
 }else if(o.kind=="jar"){
  // Jar takes that string by value and frees it itself before its ret 0x18
  // (operator delete at 0x415B17), so this path must not destroy the copy it hands over.
  alignas(8) unsigned char name[24]{};using StringCtor=void*(__thiscall*)(void*,const char*);fn<StringCtor>(0x401e20)(name,o.target.c_str());
  struct GameString {unsigned char bytes[24];};using JarCtor=void*(__thiscall*)(void*,GameString);
  fn<JarCtor>(ctor)(p,*(const GameString*)name);
 }else if(o.kind=="generic"){
  // Stores the entry context at +0x4C without dereferencing it, exactly like Exit.
  using GenericCtor=void*(__thiscall*)(void*,void*);fn<GenericCtor>(ctor)(p,exitContext.data());
 }else if(o.kind=="bird"){
  // Second argument is a vector<string> of gameplay hints, copied in by 0x40F910,
  // which sizes the copy from last-first. Three null words are a valid empty vector,
  // and the update that would read those hints never runs in a preview.
  uintptr_t hints[3]{};
  using BirdCtor=void*(__thiscall*)(void*,void*,const void*);fn<BirdCtor>(ctor)(p,exitContext.data(),hints);
 }else if(o.kind=="record"){
  // Record keeps the voice-clip path it was spawned with. Construction only
  // stores that string; playback belongs to the gameplay update we never run.
  using RecordCtor=void*(__thiscall*)(void*,const char*);fn<RecordCtor>(ctor)(p,o.target.c_str());
 }else if(o.kind=="crystal"||o.kind=="diamond"||o.kind=="ruby"){using CrystalCtor=void*(__thiscall*)(void*,int);fn<CrystalCtor>(ctor)(p,o.kind=="diamond"?1:o.kind=="ruby"?2:0);}else fn<Construct>(ctor)(p);
 auto e=(uintptr_t)p;at<float>(e,8)=o.x;at<float>(e,12)=o.y;at<unsigned char>(e,0x45)=o.global?1:0;
 // Match Host::spawn (0x4409A0) and global restore (0x440CD0):
 // eligible bodies get 20 collision-aware downward moves of 0.05 tiles.
 // Existing outer-room instances must retain their exact captured positions.
 using Add=bool(__thiscall*)(void*,void*,bool);
 bool accepted=fn<Add>(0x41c130)((void*)room,p,settle&&(at<uint32_t>(e,0x34)&0x21)!=0);
 if(!accepted&&settle){
  using Remove=void(__thiscall*)(void*,void*,bool);fn<Remove>(0x41c4e0)((void*)room,p,false);
  using Destroy=void(__thiscall*)(void*,unsigned);((Destroy)at<uintptr_t>(at<uintptr_t>(e)))(p,1);
  return true;
 }
 // Key adds dt*3, Generic dt*2 and Crystal dt*0.6 unconditionally in their own updates,
 // before their first branch. Fan integrates the rate at +0x54, which its constructor sets
 // to the same 10.0 its ramp clamps to; the game spins one down only once it leaves the
 // floor, which a frozen preview cannot observe. Record, cauldron and jar reach the shared
 // body step 0x415490, whose rotation is gated on +0x48, and their constructors clear it,
 // so a resting one does not spin in the game either.
 const bool gem=o.kind=="crystal"||o.kind=="diamond"||o.kind=="ruby";
 float rate=o.kind=="key"?at<float>(base+0x7d048):o.kind=="generic"?at<float>(base+0x7cf88):gem?at<float>(base+0x7ce9c):o.kind=="fan"?at<float>(e,0x54):0.f;
 if(rate!=0.f)rotating.push_back({e,at<float>(e,0x30),rate});
 auto drawn=o;drawn.x=at<float>(e,8);drawn.y=at<float>(e,12);drawnSnapshot.objects.push_back(std::move(drawn));
 return true;
}
}
void initNativeScene(uintptr_t b){base=b;}
void clearNativeScene(){
 if(renderer){auto internal=renderer;fn<Method>(0x433760)((void*)internal);release((void*)internal);renderer=0;}
 if(room){
  // Room destructor detaches entities and releases its containers, but entities are externally owned.
  auto begin=at<uintptr_t>(room,0x14),end=at<uintptr_t>(room,0x18);entities.clear();if(begin&&end>begin)entities.assign((uintptr_t*)begin,(uintptr_t*)end);
  fn<Method>(0x41bbd0)((void*)room);
  for(auto e:entities){using Destroy=void(__thiscall*)(void*,unsigned);((Destroy)at<uintptr_t>(at<uintptr_t>(e)))((void*)e,1);}
  release((void*)room);room=0;
 }
 entities.clear();rotating.clear();stack.clear();signature.clear();source=0;elapsed=0;exitContext={};drawnSnapshot={};
}
bool prepareNativeScene(uintptr_t liveHost,const Snapshot& s,const std::string& key,int depth){
 error.clear();if(!liveHost||!s.error.empty()){error="Scene unavailable";return false;}
 if(depth<1||depth>8||s.objects.size()>2048){error="Scene exceeds preview limits";return false;}
 Native probe{};for(const auto& o:s.objects)if(!nativeEntity(o.kind,probe)){error="Native scene does not yet support "+o.kind;return false;}
 std::array<int,300> indices{};for(size_t i=0;i<300;i++){indices[i]=s.live?s.tiles[i].nativeIndex:tileIndex(liveHost,s.tiles[i]);if(indices[i]<0||indices[i]>4096){error="Native tile definition missing: "+s.tiles[i].definition;return false;}}
 std::ostringstream stamp;stamp.precision(9);stamp<<liveHost<<'|'<<key<<'|'<<depth<<'|'<<s.nativeDepth<<'|'<<s.live;for(size_t i=0;i<300;i++)stamp<<','<<s.tiles[i].kind<<':'<<indices[i];for(const auto& o:s.objects)stamp<<'|'<<o.kind<<o.target<<o.x<<','<<o.y<<o.global<<':'<<o.sourceId;
 if(room&&renderer&&signature==stamp.str())return true;
 clearNativeScene();source=liveHost;signature=stamp.str();memcpy(host.data(),(void*)liveHost,host.size());
 auto begin=at<uintptr_t>(liveHost,0x54),end=at<uintptr_t>(liveHost,0x58);if(end<=begin||end-begin>28*4096){error="Invalid source room stack";return false;}
 // Only metadata strings/maps are borrowed. The renderer never owns or destroys this host copy.
 int count=(end-begin)/28;int desired=s.nativeDepth>=0?s.nativeDepth+1:count+depth;
 if(desired<1||desired>4104){error="Invalid destination depth";return false;}
 stack.assign((unsigned char*)begin,(unsigned char*)begin+std::min(count,desired)*28);stack.resize(desired*28,0);
 room=(uintptr_t)allocate(0xa0);if(!room){error="Room allocation failed";return false;}fn<Construct>(0x440120)((void*)room);
 at<int>(room)=20;at<int>(room,4)=15;auto tiles=(uintptr_t)allocate(300*12);if(!tiles){error="Tile allocation failed";clearNativeScene();return false;}at<uintptr_t>(room,8)=tiles;at<uintptr_t>(room,12)=at<uintptr_t>(room,16)=tiles+300*12;
 for(size_t i=0;i<300;i++){at<int>(tiles,i*12)=indices[i];at<int>(tiles,i*12+4)=s.tiles[i].kind;}
 auto counter=base+0x8c580;auto serial=at<uint32_t>(counter);fn<Method>(0x41bd30)((void*)room);at<uint32_t>(counter)=serial;
 // The private renderer has independent caches, so a private nonzero room id is sufficient.
 at<uint32_t>(room,0x90)=0x70000001;at<uintptr_t>((uintptr_t)stack.data()+stack.size()-4)=room;
 auto h=(uintptr_t)host.data();at<uintptr_t>(h,0x54)=(uintptr_t)stack.data();at<uintptr_t>(h,0x58)=at<uintptr_t>(h,0x5c)=(uintptr_t)stack.data()+stack.size();
 drawnSnapshot=s;drawnSnapshot.objects.clear();
 // An unnamed jar auto-names itself and advances the game's jar counter at 0x48A050 while
 // doing so. A live jar always arrives named, so that branch should be unreachable, but a
 // preview must not be able to move a gameplay counter at all: restore it the way the room
 // serial above is restored.
 auto jarNames=base+0x8a050;auto jarSerial=at<uint32_t>(jarNames);
 for(const auto& o:s.objects)if(!entity(o,!s.live)){at<uint32_t>(jarNames)=jarSerial;error="Entity allocation failed";clearNativeScene();return false;}
 at<uint32_t>(jarNames)=jarSerial;
 using RendererCtor=void(__thiscall*)(void*,void*);fn<RendererCtor>(0x4335d0)(&renderer,host.data());
 return renderer!=0;
}
void* nativeSceneRenderer(){return renderer?&renderer:nullptr;}
const Snapshot& nativeSceneSnapshot(){return drawnSnapshot;}
float advanceNativeScene(float seconds){if(!room)return 0;elapsed+=std::clamp(seconds,0.f,.1f);at<double>(room,0x98)=elapsed;
 // Position/physics stay fixed. Only the original spin rates and draw transforms advance.
 for(const auto& s:rotating)at<float>(s.entity,0x30)=s.base+elapsed*s.rate;
 auto begin=at<uintptr_t>(room,0x14),end=at<uintptr_t>(room,0x18);for(auto p=begin;p<end;p+=4){auto e=at<uintptr_t>(p);((Method)at<uintptr_t>(at<uintptr_t>(e),12))((void*)e);}
 return elapsed;
}
const std::string& nativeSceneError(){return error;}
}
