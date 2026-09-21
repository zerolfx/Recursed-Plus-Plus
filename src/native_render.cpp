#include <windows.h>
#include <gl/GL.h>
#include <array>
#include <algorithm>
#include <cstring>
#include "native_render.h"
#include "native_scene.h"
namespace peek { namespace {
using Render=void(__thiscall*)(void*,void*);
static Render original=nullptr;
static bool requested=false,ready=false,disabled=false;
static RoomArt art=[](){RoomArt a;a.revision=1ull<<63;return a;}();
static std::string status="Native renderer not sampled";
static GLuint fbo=0,texture=0,depth=0;
static int width=0,height=0;
static uint64_t tick=0;
static bool destination=false;
static thread_local bool working=false;
static uintptr_t sourceHost=0;
static Snapshot scene;
static std::string sceneKey,renderedKey;
static int sceneDepth=1;
static uint64_t lastSceneTick=0;
using Rand=int(__cdecl*)();static Rand originalRand=nullptr;
static uint32_t privateRandom=0x1234abcd;
static int __cdecl isolatedRand(){if(!working)return originalRand();privateRandom=privateRandom*214013u+2531011u;return (privateRandom>>16)&0x7fff;}
#define FN(ret,name,...) using name##Fn=ret(APIENTRY*)(__VA_ARGS__);static name##Fn name
FN(void,GenFramebuffers,GLsizei,GLuint*);FN(void,BindFramebuffer,GLenum,GLuint);
FN(void,FramebufferTexture2D,GLenum,GLenum,GLenum,GLuint,GLint);FN(GLenum,CheckFramebufferStatus,GLenum);
FN(void,GenRenderbuffers,GLsizei,GLuint*);FN(void,BindRenderbuffer,GLenum,GLuint);
FN(void,RenderbufferStorage,GLenum,GLenum,GLsizei,GLsizei);FN(void,FramebufferRenderbuffer,GLenum,GLenum,GLenum,GLuint);
FN(void,BindBuffer,GLenum,GLuint);
#undef FN
static bool initialize(){
#define LOAD(n) n=(n##Fn)wglGetProcAddress("gl" #n);if(!n)return false
 LOAD(GenFramebuffers);LOAD(BindFramebuffer);LOAD(FramebufferTexture2D);LOAD(CheckFramebufferStatus);LOAD(GenRenderbuffers);LOAD(BindRenderbuffer);LOAD(RenderbufferStorage);LOAD(FramebufferRenderbuffer);LOAD(BindBuffer);
#undef LOAD
 return true;
}
static bool target(int w,int h){
 if(!ready){ready=initialize();if(!ready)return false;}
 if(fbo&&w==width&&h==height)return true;
 GLint read,draw,binding,rb;glGetIntegerv(0x8CAA,&read);glGetIntegerv(0x8CA6,&draw);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);glGetIntegerv(0x8CA7,&rb);
 if(!fbo){GenFramebuffers(1,&fbo);glGenTextures(1,&texture);GenRenderbuffers(1,&depth);}
 glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,0x8058,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 BindFramebuffer(0x8D40,fbo);FramebufferTexture2D(0x8D40,0x8CE0,GL_TEXTURE_2D,texture,0);
 BindRenderbuffer(0x8D41,depth);RenderbufferStorage(0x8D41,0x88F0,w,h);FramebufferRenderbuffer(0x8D40,0x821A,0x8D41,depth);
 bool complete=CheckFramebufferStatus(0x8D40)==0x8CD5;
 glBindTexture(GL_TEXTURE_2D,binding);BindRenderbuffer(0x8D41,rb);BindFramebuffer(0x8CA8,read);BindFramebuffer(0x8CA9,draw);
 if(complete){width=w;height=h;}return complete;
}
static void readback(){
 GLint read,buffer,pbo,align,row,rows,pixels;GLboolean swap;
 glGetIntegerv(0x8CAA,&read);glGetIntegerv(GL_READ_BUFFER,&buffer);glGetIntegerv(0x88ED,&pbo);glGetIntegerv(GL_PACK_ALIGNMENT,&align);glGetIntegerv(GL_PACK_ROW_LENGTH,&row);glGetIntegerv(GL_PACK_SKIP_ROWS,&rows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&pixels);glGetBooleanv(GL_PACK_SWAP_BYTES,&swap);
 BindFramebuffer(0x8CA8,fbo);glReadBuffer(0x8CE0);BindBuffer(0x88EB,0);glPixelStorei(GL_PACK_ALIGNMENT,4);glPixelStorei(GL_PACK_ROW_LENGTH,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SWAP_BYTES,GL_FALSE);
 art.width=width;art.height=height;art.pixels.resize(width*height);glReadPixels(0,0,width,height,0x80E1,GL_UNSIGNED_BYTE,art.pixels.data());
 for(int y=0;y<height/2;y++)for(int x=0;x<width;x++)std::swap(art.pixels[y*width+x],art.pixels[(height-1-y)*width+x]);
 for(auto& p:art.pixels)p|=0xff000000u;
 BindBuffer(0x88EB,pbo);glPixelStorei(GL_PACK_ALIGNMENT,align);glPixelStorei(GL_PACK_ROW_LENGTH,row);glPixelStorei(GL_PACK_SKIP_ROWS,rows);glPixelStorei(GL_PACK_SKIP_PIXELS,pixels);glPixelStorei(GL_PACK_SWAP_BYTES,swap);BindFramebuffer(0x8CA8,read);glReadBuffer(buffer);
 art.revision++;art.note="Original engine: active-room replay; this is not a chest destination preview";
}
// Audit gameplay-owned data around the extra draw; GPU caches are intentionally excluded.
static uint64_t coreHash(void* renderer){
 uint64_t hash=1469598103934665603ull;auto add=[&](const void* p,size_t n){for(size_t i=0;i<n;i++){hash^=((const unsigned char*)p)[i];hash*=1099511628211ull;}};
 auto internal=*(uintptr_t*)renderer,host=*(uintptr_t*)internal;add((void*)(host+0x54),12);add((void*)(host+0x6c),8);
 auto begin=*(uintptr_t*)(host+0x54),end=*(uintptr_t*)(host+0x58);if(end<=begin||end-begin>28*4096)return 0;add((void*)begin,end-begin);
 auto room=*(uintptr_t*)(end-4);add((void*)room,0xa0);auto eb=*(uintptr_t*)(room+0x14),ee=*(uintptr_t*)(room+0x18);if(ee<eb||ee-eb>8192)return 0;
 for(auto p=eb;p<ee;p+=4){auto entity=*(uintptr_t*)p;add((void*)entity,0x48);}return hash;
}
static void __fastcall renderHook(void* renderer,void*,void* context){
 if(!requested){working=true;clearNativeScene();working=false;}
 if(requested&&GetTickCount64()/33!=tick){
  tick=GetTickCount64()/33;int w=*(int*)context,h=*((int*)context+1);
  if(w>0&&h>0&&w<=2048&&h<=2048&&target(w,h)){
   // Context has two dimensions, view values, FBO handles, time, and a 4x4 matrix.
   alignas(16) std::array<unsigned char,0x74> copy{};memcpy(copy.data(),context,copy.size());*(GLuint*)(copy.data()+0x20)=fbo;
   auto before=coreHash(renderer);working=true;bool drawable=true;void* selected=renderer;
   if(destination){drawable=prepareNativeScene(sourceHost,scene,sceneKey,sceneDepth);if(drawable){selected=nativeSceneRenderer();auto now=GetTickCount64();float dt=lastSceneTick?std::min(.1f,(now-lastSceneTick)/1000.f):0.f;lastSceneTick=now;*(float*)(copy.data()+0x24)=advanceNativeScene(dt);*(float*)(copy.data()+0x28)=dt;}}
   else *(float*)(copy.data()+0x28)=0; // Do not advance the live particle simulation twice.
   if(drawable)original(selected,copy.data());working=false;auto after=coreHash(renderer);
   if(before!=after||!before){disabled=true;requested=false;art.pixels.clear();status="Native preview disabled: gameplay audit failed";}
   else if(!drawable){art.pixels.clear();status=nativeSceneError();}
   else if(before&&before==after){readback();renderedKey=sceneKey;status="Native renderer: live room stack and entity fields unchanged";if(destination)art.note="Original engine / isolated destination scene / physics frozen";}
  }else status="Native replay target unavailable";
 }
 // The normal draw runs last, restores the engine's expected render state and presents normally.
 original(renderer,context);
}
}
bool installNativeRender(uintptr_t base){
 initNativeScene(base);auto* randSlot=(void**)(base+0x77378);auto expectedRand=GetProcAddress(GetModuleHandleW(L"MSVCR120.dll"),"rand");if(*randSlot!=(void*)expectedRand)return false;DWORD randProtect;if(!VirtualProtect(randSlot,4,PAGE_READWRITE,&randProtect))return false;originalRand=(Rand)*randSlot;*randSlot=(void*)isolatedRand;DWORD randUnused;VirtualProtect(randSlot,4,randProtect,&randUnused);
 auto* p=(unsigned char*)(base+0x338a0);const unsigned char signature[]={0x55,0x8b,0xec,0x83,0xec,0x20};if(memcmp(p,signature,6))return false;
 auto* t=(unsigned char*)VirtualAlloc(nullptr,11,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!t)return false;memcpy(t,p,6);t[6]=0xe9;*(int32_t*)(t+7)=(int32_t)((p+6)-(t+11));DWORD old;if(!VirtualProtect(t,11,PAGE_EXECUTE_READ,&old))return false;original=(Render)t;
 if(!VirtualProtect(p,6,PAGE_EXECUTE_READWRITE,&old))return false;p[0]=0xe9;*(int32_t*)(p+1)=(int32_t)((unsigned char*)renderHook-(p+5));p[5]=0x90;DWORD unused;VirtualProtect(p,6,old,&unused);FlushInstructionCache(GetCurrentProcess(),p,6);return true;
}
void requestNativeMirror(bool enabled){requested=enabled&&!disabled;destination=false;sceneKey.clear();renderedKey.clear();if(!enabled)art.pixels.clear();}
bool nativeMirrorRequested(){return requested;}
const RoomArt* nativeMirrorArt(){return art.pixels.empty()?nullptr:&art;}
const std::string& nativeRenderStatus(){return status;}
void requestNativeDestination(uintptr_t host,const Snapshot& snapshot,const std::string& key,int pathDepth){if(key!=sceneKey||host!=sourceHost||pathDepth!=sceneDepth){art.pixels.clear();lastSceneTick=0;}sourceHost=host;scene=snapshot;sceneKey=key;sceneDepth=pathDepth;destination=true;requested=!disabled;}
const RoomArt* nativeDestinationArt(const std::string& key){return destination&&renderedKey==key&&!art.pixels.empty()?&art:nullptr;}
bool nativeRenderWork(){return working;}
}
