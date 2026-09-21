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
static std::vector<std::pair<uintptr_t,float>> rotating;
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
static bool entity(const Object& o,bool settle){
 uintptr_t ctor=0;size_t bytes=0;
 const bool portal=o.kind=="player"||o.kind=="yield";
 if(portal){ctor=0x412e00;bytes=0x5c;}else if(o.kind=="chest"){ctor=0x410fe0;bytes=0x7c;}else if(o.kind=="box"){ctor=0x410210;bytes=0x50;}else if(o.kind=="key"){ctor=0x416210;bytes=0x50;}else if(o.kind=="lock"){ctor=0x416790;bytes=0x50;}else if(o.kind=="crystal"||o.kind=="diamond"||o.kind=="ruby"){ctor=0x412580;bytes=0x5c;}else return false;
 void* p=allocate(bytes);if(!p)return false;
 if(portal){
  // Every chest destination has a parent room. Spawn("player") also creates
  // a regular Exit there in the original game; Spawn("yield") creates its
  // green variant. Construct only Exit, never Player or its gameplay context.
  using ExitCtor=void*(__thiscall*)(void*,void*,bool);
  fn<ExitCtor>(ctor)(p,exitContext.data(),o.kind=="yield");
 }else if(o.kind=="chest"){
  alignas(8) unsigned char name[24]{};using StringCtor=void*(__thiscall*)(void*,const char*);fn<StringCtor>(0x401e20)(name,o.target.c_str());
  using ChestCtor=void*(__thiscall*)(void*,void*);fn<ChestCtor>(ctor)(p,name);fn<Method>(0x401e80)(name);
 }else if(bytes==0x5c){using CrystalCtor=void*(__thiscall*)(void*,int);fn<CrystalCtor>(ctor)(p,o.kind=="diamond"?1:o.kind=="ruby"?2:0);}else fn<Construct>(ctor)(p);
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
 if(o.kind=="key")rotating.push_back({e,at<float>(e,0x30)});
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
 for(const auto& o:s.objects)if(o.kind!="player"&&o.kind!="yield"&&o.kind!="chest"&&o.kind!="box"&&o.kind!="key"&&o.kind!="lock"&&o.kind!="crystal"&&o.kind!="diamond"&&o.kind!="ruby"){error="Native scene does not yet support "+o.kind;return false;}
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
 for(const auto& o:s.objects)if(!entity(o,!s.live)){error="Entity allocation failed";clearNativeScene();return false;}
 using RendererCtor=void(__thiscall*)(void*,void*);fn<RendererCtor>(0x4335d0)(&renderer,host.data());
 return renderer!=0;
}
void* nativeSceneRenderer(){return renderer?&renderer:nullptr;}
const Snapshot& nativeSceneSnapshot(){return drawnSnapshot;}
float advanceNativeScene(float seconds){if(!room)return 0;elapsed+=std::clamp(seconds,0.f,.1f);at<double>(room,0x98)=elapsed;
 // Position/physics stay fixed. Only the original key spin and entity draw transforms advance.
 const float spin=at<float>(base+0x7d048);for(auto e:rotating)at<float>(e.first,0x30)=e.second+elapsed*spin;
 auto begin=at<uintptr_t>(room,0x14),end=at<uintptr_t>(room,0x18);for(auto p=begin;p<end;p+=4){auto e=at<uintptr_t>(p);((Method)at<uintptr_t>(at<uintptr_t>(e),12))((void*)e);}
 return elapsed;
}
const std::string& nativeSceneError(){return error;}
}
