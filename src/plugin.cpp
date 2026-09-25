#include <windows.h>
#include <shlobj.h>
#include <gl/GL.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <cctype>
#include <share.h>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include "snapshot.h"
#include "runtime_state.h"
#include "room_art.h"
#include "preview_window.h"
#include "native_render.h"
#include "support_folder.h"
#include "save_store.h"
#include "rewind.h"
#include "gamepad.h"

static HMODULE selfModule;
static uintptr_t gameBase;
static FILE* logFile;
static char profilePath[MAX_PATH];
static bool glReady=false;
static bool bufferedTestInput=false,developerMode=false;
static bool nativeTest=false,queuedNative=false;
static bool queuedClick=false,queuedBack=false,queuedForward=false;
static bool queuedOpen=false,queuedClose=false,clickPopout=false,suppressEscape=false,escapeSwallowed=false;
static HWND gameWindow=nullptr;
static HWND titledWindow=nullptr;
static uint64_t keyUntil[128]{};
static bool keyDown[128]{};
static uint64_t frame=0;
static void log(const char* fmt,...);
static std::string missionPath,gameRoot;
static uintptr_t roomHost=0;
// A flame's room is left empty until it is looked at: where it leads is worked out for the one in use.
struct ChestView{uintptr_t id;float x,y;std::string room;uintptr_t owner;bool wet;bool outward=false;bool yield=false;bool paradox=false;bool jar=false;std::string jarName;};
static std::vector<ChestView> chests;
static bool pinned=false,previewWet=false;
static uintptr_t pinnedId=0;
static uintptr_t hoveredId=0;
// depth is relative to the room being played, not the engine's absolute depth: carrying a pinned
// chest out through a flame builds no room, so the pin survives a change of the room it counts from.
// A live step (ancestors>=0) takes its depth from ancestors instead.
// A paradox leaves the whole stack behind for a timeline named after its room, so a step in one
// keeps that name in timeline and counts depth from the paradox room, which is at depth 0.
// A step through a flame of the stack keeps which room's flame it was, counted out from the room
// being played, and which flames on the way out were green, so it can follow where that walk leads
// as things move.
struct PreviewStep {std::string room;bool wet=false;int ancestors=-1;int depth=1;uintptr_t chestId=0;peek::Snapshot parentSnapshot{};std::string timeline;int exitFrom=-1;uint32_t greens=0;bool jar=false;std::string jarName;};
static std::vector<PreviewStep> previewPath;
// What going back stepped out of, so the side buttons can walk the path in both directions.
static std::vector<PreviewStep> forwardPath;
static peek::Snapshot preview,templatePreview;
static std::string previewKey;
// Frame the inset was last drawn or held on, and for which chest. Zero once a preview ends, so
// a preview that was closed is never held over whatever opens next.
static uint64_t heldFrame=0;
static uintptr_t heldChest=0;
using FopenFn=void*(__cdecl*)(const char*,const char*);
static FopenFn originalFopen;
static void* __cdecl fopenHook(const char* file,const char* mode){
    void* result=originalFopen(file,mode);
    if(result&&file&&mode&&mode[0]=='r'){
        std::string name=file;std::replace(name.begin(),name.end(),'\\','/');auto p=name.find("missions/");
        if(p!=std::string::npos&&name.size()>4&&name.substr(name.size()-4)==".lua"){
            auto mission=name.substr(p);if(mission!=missionPath){peek::requestNativeMirror(false);nativeTest=false;missionPath=mission;previewKey.clear();pinned=false;previewPath.clear();heldFrame=0;log("Mission file %s",missionPath.c_str());}
        }
    }return result;
}
static std::string oldString(const void* p){
    const auto* b=(const unsigned char*)p;uint32_t n=*(const uint32_t*)(b+16),capacity=*(const uint32_t*)(b+20);
    if(n>240||capacity<n||capacity>1048576)return {};
    const char* data=capacity<16?(const char*)p:*(const char*const*)p;
    if(!data)return {};return std::string(data,n);
}
static void log(const char* fmt,...){if(!logFile)return;va_list args;va_start(args,fmt);vfprintf(logFile,fmt,args);va_end(args);fputc('\n',logFile);fflush(logFile);}
static void* address(uintptr_t va){return (void*)(gameBase+va-0x400000);}
static bool patchPointer(void** location,void* replacement,void** original){
    DWORD old;if(!VirtualProtect(location,sizeof(void*),PAGE_READWRITE,&old))return false;
    if(original)*original=*location;InterlockedExchangePointer(location,replacement);
    DWORD unused;VirtualProtect(location,sizeof(void*),old,&unused);return true;
}
static bool hookImport(const char* dll,const char* name,void* hook,void** original){
    auto dos=(IMAGE_DOS_HEADER*)gameBase;auto nt=(IMAGE_NT_HEADERS*)(gameBase+dos->e_lfanew);
    auto d=(IMAGE_IMPORT_DESCRIPTOR*)(gameBase+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for(;d->Name;d++)if(_stricmp((char*)(gameBase+d->Name),dll)==0){
        auto names=(IMAGE_THUNK_DATA*)(gameBase+d->OriginalFirstThunk);auto slots=(IMAGE_THUNK_DATA*)(gameBase+d->FirstThunk);
        for(;names->u1.AddressOfData;names++,slots++)if(!IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)){
            auto entry=(IMAGE_IMPORT_BY_NAME*)(gameBase+names->u1.AddressOfData);
            if(strcmp((char*)entry->Name,name)==0)return patchPointer((void**)&slots->u1.Function,hook,original);
        }
    }return false;
}
using CursorVisibility=void(__thiscall*)(void*,bool);
static CursorVisibility originalCursorVisibility;
static void* cursorWindow=nullptr;
static bool requestedCursorVisible=true,appliedCursorVisible=true;
static void __fastcall cursorVisibilityHook(void* window,void*,bool visible){
    // Inspection is always on, so the pointer the player aims at chests with is always shown,
    // whatever the game asks for. What it asked for is still recorded, for the log.
    cursorWindow=window;requestedCursorVisible=visible;
    appliedCursorVisible=true;
    originalCursorVisibility(window,true);
}
static void syncCursorVisibility(void* window){
    const bool known=cursorWindow==window;
    if(!known){cursorWindow=window;requestedCursorVisible=true;}
    // Update SFML's stored visibility, including its mouse-enter/focus handling.
    // Do not modify the thread's ShowCursor counter on every rendered frame.
    if(!known||!appliedCursorVisible){
        originalCursorVisibility(window,true);
        if(bufferedTestInput){CURSORINFO info{sizeof info};GetCursorInfo(&info);log("Cursor requested=%d systemVisible=%d",requestedCursorVisible,(info.flags&CURSOR_SHOWING)!=0);}
    }
    appliedCursorVisible=true;
}
using ChestTransform=void(__thiscall*)(void*);
static ChestTransform originalChestTransform;
static ChestTransform originalJarTransform;
static ChestTransform originalExitTransform;
static void __fastcall exitTransformHook(void* object,void*){
    originalExitTransform(object);
    if(peek::nativeRenderWork()||!roomHost||chests.size()>128)return;
    const auto* b=(const unsigned char*)object;auto owner=*(uintptr_t*)(b+4);
    // Door +0x58 marks the green flame.
    ChestView item{(uintptr_t)object,*(const float*)(b+8),*(const float*)(b+12),"",owner,false,true,b[0x58]!=0};
    if(!std::isfinite(item.x)||!std::isfinite(item.y))return;
    auto it=std::find_if(chests.begin(),chests.end(),[&](const ChestView& c){return c.id==item.id;});
    if(it==chests.end())chests.push_back(item);else *it=item;
}
static void __fastcall chestTransformHook(void* object,void*){
    originalChestTransform(object);
    if(peek::nativeRenderWork())return;
    const auto* b=(const unsigned char*)object;
    ChestView item{(uintptr_t)object,*(const float*)(b+8),*(const float*)(b+12),oldString(b+0x4c),*(uintptr_t*)(b+4),(*(uint32_t*)(b+0x38)&0x10)!=0};
    if(item.room.empty()||!std::isfinite(item.x)||!std::isfinite(item.y)||chests.size()>128)return;
    auto it=std::find_if(chests.begin(),chests.end(),[&](const ChestView& c){return c.id==item.id;});
    if(it==chests.end())chests.push_back(item);else *it=item;
}
static void __fastcall jarTransformHook(void* object,void*){
    originalJarTransform(object);
    if(peek::nativeRenderWork()||!roomHost||chests.size()>128)return;
    const auto* b=(const unsigned char*)object;
    if(b[0x44]||*(const int*)(b+0x64)==2)return;
    ChestView item{(uintptr_t)object,*(const float*)(b+8),*(const float*)(b+12),"",*(uintptr_t*)(b+4),false};
    item.jar=true;item.jarName=oldString(b+0x4c);
    if(!item.owner||!std::isfinite(item.x)||!std::isfinite(item.y))return;
    auto it=std::find_if(chests.begin(),chests.end(),[&](const ChestView& c){return c.id==item.id;});
    if(it==chests.end())chests.push_back(item);else *it=item;
}
using BuildRoom=uintptr_t(__thiscall*)(void*,void*,void*,uint32_t);
static BuildRoom originalBuildRoom;
static uintptr_t __fastcall buildRoomHook(void* host,void*,void* tiles,void* name,uint32_t wet){
    peek::requestNativeMirror(false);nativeTest=false;
    roomHost=(uintptr_t)host;
    auto tileset=oldString((char*)host+8);auto room=oldString(name);
    log("Build room tileset=%s room=%s wet=%u",tileset.c_str(),room.c_str(),wet&255);
    pinned=false;previewPath.clear();previewKey.clear();chests.clear();heldFrame=0;return originalBuildRoom(host,tiles,name,wet);
}
static bool hookRoomBuilder(){
    auto* target=(unsigned char*)address(0x440a20);
    // Exact-build prologue consists of three complete, non-relative instructions.
    auto* trampoline=(unsigned char*)VirtualAlloc(nullptr,11,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!trampoline)return false;memcpy(trampoline,target,6);trampoline[6]=0xe9;*(int32_t*)(trampoline+7)=(int32_t)((target+6)-(trampoline+11));
    DWORD old;if(!VirtualProtect(trampoline,11,PAGE_EXECUTE_READ,&old))return false;
    originalBuildRoom=(BuildRoom)trampoline;
    if(!VirtualProtect(target,6,PAGE_EXECUTE_READWRITE,&old))return false;
    target[0]=0xe9;*(int32_t*)(target+1)=(int32_t)((unsigned char*)buildRoomHook-(target+5));target[5]=0x90;
    DWORD unused;VirtualProtect(target,6,old,&unused);FlushInstructionCache(GetCurrentProcess(),target,6);return true;
}
using Display=void(__thiscall*)(void*);
using FolderFn=HRESULT(WINAPI*)(HWND,int,HANDLE,DWORD,LPSTR);
static Display originalDisplay;
using PollEvent=bool(__thiscall*)(void*,void*);
using IsKeyPressed=bool(__cdecl*)(int);
static PollEvent originalPollEvent;
static IsKeyPressed originalIsKeyPressed;
static bool __cdecl keyHook(int key){
    if(peek::previewWindowFocused())return false;
    // Escape closes an open preview, and only that. The game reads the key state directly and
    // does so before the event that would announce the press, so waiting for that event let the
    // pause menu open in the same press. While a preview is open the key is answered here, and
    // it keeps being answered here until it is released, so closing a preview cannot also pause.
    if(key==36){
        const bool held=originalIsKeyPressed(key)||(bufferedTestInput&&GetTickCount64()<keyUntil[36]);
        if(!held){suppressEscape=false;return false;}
        if(suppressEscape||nativeTest||pinned||!previewPath.empty()||peek::previewEscapeHeld()){
            suppressEscape=true;queuedClose=true;return false;
        }
        return true;
    }
    return originalIsKeyPressed(key)||(bufferedTestInput&&key>=0&&key<128&&GetTickCount64()<keyUntil[key]);
}
// Q undoes the last action and W goes back five seconds, unless the player has made either one
// of the game's own controls, in which case it stays that control.
static void undoKey(int key){
    static bool said[2]{};const bool seconds=key==22;
    if(peek::gameBindsKey(key)){if(!said[seconds]){said[seconds]=true;log("%c is one of the game's controls here, so it does not undo",seconds?'W':'Q');}return;}
    peek::requestRewind(seconds?peek::Rewind::Seconds:peek::Rewind::Action);
}
static bool __fastcall pollEventHook(void* window,void*,void* event){
    bool result=originalPollEvent(window,event);if(!result)return false;
    int* e=(int*)event;
    if(bufferedTestInput&&(e[0]==5||e[0]==6||e[0]==9))log("Input event type=%d code=%d",e[0],e[1]);
    // SFML 2 Event: type, then the event union (key code or mouse button).
    // Losing focus ends every key the game thinks is down, including the Escape this was about to
    // swallow the release of; keeping that latch would eat the next press instead.
    if(e[0]==2){memset(keyDown,0,sizeof keyDown);memset(keyUntil,0,sizeof keyUntil);escapeSwallowed=suppressEscape=false;}
    // An Escape that closed a preview is the player's answer to the preview, not to the game.
    // Swallowing the press alone is not enough: the pause menu opens on the release, which
    // arrives after the key is physically up and every held-key test has already gone quiet.
    if(e[0]==6&&e[1]==36&&escapeSwallowed){escapeSwallowed=false;keyDown[36]=false;suppressEscape=false;return pollEventHook(window,nullptr,event);}
    if(e[0]==6&&e[1]>=0&&e[1]<128)keyDown[e[1]]=false;
    if(e[0]==5){int key=e[1];if(key>=0&&key<128){keyUntil[key]=GetTickCount64()+100;if(keyDown[key])return result;keyDown[key]=true;}
        if(key==14)queuedOpen=true;   // O: move the same preview between inset and window
        if(key==36&&(suppressEscape||nativeTest||pinned||!previewPath.empty())){queuedClose=true;suppressEscape=escapeSwallowed=true;memset(keyUntil,0,sizeof keyUntil);return pollEventHook(window,nullptr,event);}
        if(key==91&&developerMode)queuedNative=true; // F7: original-renderer diagnostic, development runs only
        if(key==59)queuedBack=true;   // Backspace
        if(key==16||key==22)undoKey(key);   // Q, W
        // F8: replay everything and compare. Shift+F8 is the game's own normal-speed key.
        if(key==92&&developerMode&&!((const char*)event)[10])peek::requestRewind(peek::Rewind::Verify);
    }
    // Mouse buttons: the two side buttons walk the preview history the way they walk a browser's.
    if(e[0]==9){
        if(e[1]==0){queuedClick=true;clickPopout=(GetKeyState(VK_SHIFT)&0x8000)!=0;}
        if(e[1]==1&&pinned)queuedClose=true; // Right click lets go of a pinned preview.
        if(e[1]==3)queuedBack=true;
        if(e[1]==4)queuedForward=true;
    }
    return result;
}
static FolderFn originalFolder;
// Playing through Steam is the ordinary case, and the launcher asks for it by default: the
// achievements, statistics and cloud saves are the ones the player already has. Steam is only
// stood in for when an isolated run was asked for, or when Steam turns out not to be running.
// That stand-in is what keeps such a run from losing everything: the game writes its progress
// to Steam Cloud and nowhere else, and skips the write entirely when Steam did not answer.
static peek::SaveStorage saveFiles;
static void* saveContext;
static bool steamAsked=false,steamLive=false;
using SteamInitFn=bool(__cdecl*)();
using ContextInitFn=void*(__cdecl*)(void*);
using SteamWorkFn=void(__cdecl*)();
static SteamInitFn originalSteamInit;
static ContextInitFn originalContextInit;
static SteamWorkFn originalRunCallbacks,originalSteamShutdown;
static void noteSave(const char* what,const char* file,int bytes){log("Progress %s: %s (%d bytes)",what,file,bytes);}
static bool __cdecl steamInitHook(){
    steamLive=steamAsked&&originalSteamInit&&originalSteamInit();
    log("Steam %s",steamLive?"connected; achievements and cloud saves are the game's own"
        :steamAsked?"did not answer; progress kept in this build's save folder"
        :"not used; progress kept in this build's save folder");
    // The game saves only when it believes Steam answered, so this reports success either way
    // and the context below decides where the progress actually lands.
    return true;
}
static void* __cdecl contextInitHook(void* data){return steamLive?originalContextInit(data):saveContext;}
static void __cdecl runCallbacksHook(){if(steamLive&&originalRunCallbacks)originalRunCallbacks();}
static void __cdecl steamShutdownHook(){if(steamLive&&originalSteamShutdown)originalSteamShutdown();}
static bool installSteamStand(){
    char flag[8];steamAsked=GetEnvironmentVariableA("RECURSED_PEEK_STEAM",flag,sizeof flag)>0&&flag[0]=='1';
    saveFiles.folder=peek::saveFolder();saveFiles.note=noteSave;
    if(saveFiles.folder.empty()){log("No save folder; refusing to start");return false;}
    saveContext=peek::steamContext(saveFiles);
    // Callback registration happens before the game reaches its entry point, so those imports
    // are left alone; an uninitialised Steam accepts them and does nothing with them.
    return hookImport("steam_api.dll","SteamAPI_Init",(void*)steamInitHook,(void**)&originalSteamInit)
        &&hookImport("steam_api.dll","SteamInternal_ContextInit",(void*)contextInitHook,(void**)&originalContextInit)
        &&hookImport("steam_api.dll","SteamAPI_RunCallbacks",(void*)runCallbacksHook,(void**)&originalRunCallbacks)
        &&hookImport("steam_api.dll","SteamAPI_Shutdown",(void*)steamShutdownHook,(void**)&originalSteamShutdown);
}
static HRESULT WINAPI isolatedFolder(HWND hwnd,int folder,HANDLE token,DWORD flags,LPSTR out){
    if((folder&0xff)==CSIDL_APPDATA){strcpy_s(out,MAX_PATH,profilePath);log("Redirected APPDATA");return S_OK;}
    return originalFolder(hwnd,folder,token,flags,out);
}
// Minimal modern OpenGL overlay. It preserves every state it changes.
using GLchar=char;using GLsizeiptr=ptrdiff_t;
#define GLFN(ret,name,...) using name##Fn=ret(APIENTRY*)(__VA_ARGS__);static name##Fn name
GLFN(GLuint,CreateShader,GLenum);GLFN(void,ShaderSource,GLuint,GLsizei,const GLchar*const*,const GLint*);
GLFN(void,CompileShader,GLuint);GLFN(void,GetShaderiv,GLuint,GLenum,GLint*);GLFN(void,GetShaderInfoLog,GLuint,GLsizei,GLsizei*,GLchar*);
GLFN(GLuint,CreateProgram);GLFN(void,AttachShader,GLuint,GLuint);GLFN(void,LinkProgram,GLuint);GLFN(void,GetProgramiv,GLuint,GLenum,GLint*);
GLFN(void,DeleteShader,GLuint);GLFN(void,UseProgram,GLuint);GLFN(void,GenVertexArrays,GLsizei,GLuint*);GLFN(void,BindVertexArray,GLuint);
GLFN(void,GenBuffers,GLsizei,GLuint*);GLFN(void,BindBuffer,GLenum,GLuint);GLFN(void,BufferData,GLenum,GLsizeiptr,const void*,GLenum);
GLFN(void,EnableVertexAttribArray,GLuint);GLFN(void,VertexAttribPointer,GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
GLFN(void,BindFramebuffer,GLenum,GLuint);GLFN(void,ActiveTexture,GLenum);
#undef GLFN
static GLuint program,vao,vbo,artTexture;
static uint64_t artRevision=0;
struct Vertex{float x,y,r,g,b,a,u=0,v=0,texture=0;};
static std::vector<Vertex> vertices;
static float viewW=1,viewH=1;
static void rect(float x,float y,float w,float h,float r,float g,float b,float a=1){
    float l=x/viewW*2-1,rr=(x+w)/viewW*2-1,t=1-y/viewH*2,bb=1-(y+h)/viewH*2;
    Vertex v[6]={{l,t,r,g,b,a},{rr,t,r,g,b,a},{l,bb,r,g,b,a},{l,bb,r,g,b,a},{rr,t,r,g,b,a},{rr,bb,r,g,b,a}};
    vertices.insert(vertices.end(),v,v+6);
}
static void outline(float x,float y,float w,float h,float r,float g,float b){rect(x,y,w,2,r,g,b);rect(x,y+h-2,w,2,r,g,b);rect(x,y,2,h,r,g,b);rect(x+w-2,y,2,h,r,g,b);}
static const unsigned char font[][7]={
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14}};
static void text(float x,float y,const std::string& str,float scale=2){for(char ch:str){char c=(char)toupper((unsigned char)ch);int i=c>='A'&&c<='Z'?c-'A':c>='0'&&c<='9'?26+c-'0':-1;if(i>=0){for(int yy=0;yy<7;yy++)for(int xx=0;xx<5;xx++)if(font[i][yy]&(1<<(4-xx)))rect(x+xx*scale,y+yy*scale,scale,scale,.94f,.95f,1);}else if(c=='-'||c=='_')rect(x,y+4*scale,5*scale,scale,.94f,.95f,1);else if(c=='.'||c==':'){rect(x+2*scale,y+5*scale,scale,scale,1,1,1);if(c==':')rect(x+2*scale,y+2*scale,scale,scale,1,1,1);}else if(c=='+'){rect(x,y+3*scale,5*scale,scale,1,1,1);rect(x+2*scale,y+scale,scale,5*scale,1,1,1);}x+=6*scale;}}
static void imageQuad(float x,float y,float w,float h){
    float l=x/viewW*2-1,r=(x+w)/viewW*2-1,t=1-y/viewH*2,b=1-(y+h)/viewH*2;
    Vertex v[6]={{l,t,1,1,1,1,0,0,1},{r,t,1,1,1,1,1,0,1},{l,b,1,1,1,1,0,1,1},{l,b,1,1,1,1,0,1,1},{r,t,1,1,1,1,1,0,1},{r,b,1,1,1,1,1,1,1}};vertices.insert(vertices.end(),v,v+6);
}
static bool dismissedHover=false;
static void clearPreview(){nativeTest=false;peek::requestNativeMirror(false);pinned=false;previewPath.clear();forwardPath.clear();previewKey.clear();dismissedHover=true;heldFrame=0;peek::closePreviewWindow();}
static bool previewPortal(const peek::Object& o){return o.kind=="player"||o.kind=="yield";}
static int relativeDepth(const PreviewStep& s){return s.ancestors>=0?-s.ancestors:s.depth;}
static bool paradoxRoom(const PreviewStep& s){return !s.timeline.empty()&&!s.depth;}
// How deep a step is, as the preview says it.
static std::string depthLabel(const PreviewStep& s){
    if(s.timeline.empty())return "Depth "+std::to_string(relativeDepth(s));
    return s.depth?"Paradox depth "+std::to_string(s.depth):"Paradox";
}
// The timeline being played, read each frame: it names the colours a room is drawn in.
static std::string timelinePlayed;
// What a step shows. A click in the separate window carries the view it was aimed at, so a
// click that arrives after the window has moved on is not applied to a different room.
static std::string stepKey(const PreviewStep& s){return missionPath+"|"+(s.timeline.empty()?timelinePlayed:s.timeline)+"|"+s.room+"|"+(s.jar?"jar:"+s.jarName:s.ancestors>=0?"live:"+std::to_string(s.ancestors):s.wet?"wet":"dry")+(paradoxRoom(s)?"|paradox":"");}
static std::string stepView(const PreviewStep& s){return stepKey(s)+"|"+std::to_string(relativeDepth(s));}
// A step through a flame: out to a room of the stack, or into a paradox room.
static PreviewStep exitStep(const std::string& room,bool paradox,int ancestors,int from,uint32_t greens){
    PreviewStep s;s.room=room;s.exitFrom=from;s.greens=greens;
    if(paradox){s.timeline=room;s.depth=0;}else s.ancestors=ancestors;
    return s;
}
// What a step's room restores: a paradox room reached through a flame of the stack takes what that
// walk out leaves saved under its name; anything else what is saved now.
static peek::GlobalState stepGlobals(const PreviewStep& s,uintptr_t owner){
    if(paradoxRoom(s)&&s.exitFrom>=0){
        auto target=peek::readExitTarget(roomHost,owner,s.exitFrom,s.greens);
        if(target.available&&target.paradox&&target.room==s.room)return target.globals;
        peek::GlobalState changed;changed.error=target.available?"Scene changed":target.error;return changed;
    }
    return peek::readGlobals(roomHost,owner,s.room);
}
// An outer room as walking out finds it. A room puts its globals aside when it is left and takes
// them back when it is returned to, so through a flame it has them again.
static peek::Snapshot outerRoom(const PreviewStep& s,uintptr_t owner,const peek::Snapshot& appearance){
    auto room=peek::readRoomSnapshot(roomHost,owner,s.ancestors,appearance);
    if(s.exitFrom<0||!room.error.empty())return room;
    auto target=peek::readExitTarget(roomHost,owner,s.exitFrom,s.greens);
    if(!target.available||target.paradox){room.tiles={};room.objects.clear();room.error=target.available?"Scene changed":target.error;return room;}
    // The restore places each one the way a spawn is placed (0x440CD0 then 0x41C130).
    for(auto o:target.globals.objects){o.settle=true;room.objects.push_back(std::move(o));}
    room.hasGlobals|=!target.globals.objects.empty();
    return room;
}
static peek::Snapshot jarRoom(const PreviewStep& s,uintptr_t owner,const peek::Snapshot& appearance){
    auto reference=peek::readJarReference(roomHost,owner,s.jarName);
    if(!reference.error.empty()||reference.name!=s.room){peek::Snapshot changed;changed.error=reference.error.empty()?"Scene changed":reference.error;return changed;}
    if(reference.room)return peek::readJarSnapshot(roomHost,owner,s.jarName,appearance);
    auto room=appearance;auto globals=stepGlobals(s,owner);peek::applyGlobals(room,globals);
    if(!globals.available)room.error=globals.error;
    return room;
}
enum class PreviewMove {None,Return,Enter};
// Where clicking o inside the preview leads: back to the step before, or into next.
// The click and the pinned hover line both ask here, so the line says what the click does.
static PreviewMove previewMove(const peek::Object& o,PreviewStep& next){
    if(previewPath.empty())return PreviewMove::None;
    const auto& here=previewPath.back();
    if(previewPortal(o)){
        const bool yield=o.kind=="yield";
        // A paradox room is built alone on its stack, which gives it no flame.
        if(paradoxRoom(here))return PreviewMove::None;
        if(here.ancestors<0&&previewPath.size()>1)return PreviewMove::Return;
        if(previewPath.size()>=8)return PreviewMove::None;
        uintptr_t owner=0;for(const auto& c:chests)if(c.id==pinnedId)owner=c.owner;
        // Out of a chest's room is back in the room being played.
        if(here.ancestors<0){
            auto current=peek::readRoomReference(roomHost,owner,0);
            if(!current.error.empty())return PreviewMove::None;
            next={current.name,false,0,0};return PreviewMove::Enter;
        }
        const uint32_t greens=here.greens|(yield&&here.ancestors<32?1u<<here.ancestors:0u);
        auto target=peek::readExitTarget(roomHost,owner,here.ancestors,greens);
        if(!target.available)return PreviewMove::None;
        next=exitStep(target.room,target.paradox,target.ancestors,here.ancestors,greens);return PreviewMove::Enter;
    }
    if((o.kind!="chest"&&o.kind!="jar")||previewPath.size()>=8||(o.kind=="chest"&&o.target.empty()))return PreviewMove::None;
    next={o.target,peek::wetAt(preview,o.x,o.y),-1,relativeDepth(here)+1,o.sourceId};next.timeline=here.timeline;
    if(o.kind=="jar"){
        uintptr_t owner=0;for(const auto& c:chests)if(c.id==pinnedId)owner=c.owner;
        auto target=peek::readJarReference(roomHost,owner,o.target);
        if(!target.error.empty())return PreviewMove::None;
        next.room=target.name;next.wet=false;next.jar=true;next.jarName=o.target;
    }
    return PreviewMove::Enter;
}
// A flame back to the step before is Back, so Forward can undo it. Any new step starts a new
// branch, like a link in a browser: Forward must not graft on a step from the one left behind.
static void enterPreview(const peek::Object& o){
    PreviewStep next;auto move=previewMove(o,next);
    if(move==PreviewMove::Return){forwardPath.push_back(std::move(previewPath.back()));previewPath.pop_back();}
    else if(move==PreviewMove::Enter){if(!previewPortal(o))next.parentSnapshot=preview;forwardPath.clear();previewPath.push_back(std::move(next));}
}
static PreviewStep rootStep(const ChestView& c){auto s=c.outward?exitStep(c.room,c.paradox,1,0,c.yield?1u:0u):PreviewStep{c.room,c.wet};s.jar=c.jar;s.jarName=c.jarName;return s;}
// Where a flame of the room being played leads depends on what is where right now.
static void resolveExit(ChestView& c){
    auto target=peek::readExitTarget(roomHost,c.owner,0,c.yield?1u:0u);
    c.room=target.available?target.room:"";c.paradox=target.paradox;
}
static const peek::RoomArt* currentArt=nullptr;
// The inset as it was last drawn, and where. Its image quad samples the texture, which keeps
// the last uploaded art for as long as nothing new is uploaded.
static std::vector<Vertex> heldInset;
static float heldLeft,heldTop,heldRight,heldBottom;
static void drawPreview(POINT mouse,bool clicked,bool back,bool forward,bool open,bool close){
    auto action=peek::pumpPreviewWindow();
    if(action.action!=peek::PreviewAction::None)log("Preview window action=%d depth=%zu",(int)action.action,previewPath.size());
    // Undo belongs to the game, but the separate window takes the keys while it has the focus.
    if(action.action==peek::PreviewAction::Undo)undoKey(16);
    if(action.action==peek::PreviewAction::UndoSeconds)undoKey(22);
    float u=std::max(1.0f,viewH/750.0f),cell=std::min(viewW/20,viewH/15);
    float left=(viewW-cell*20)*.5f,top=(viewH-cell*15)*.5f;
    // A pinned inset is opaque to the pointer: the game chests it covers are not hovered through it.
    const bool overInset=pinned&&heldFrame+1==frame&&mouse.x>=heldLeft&&mouse.x<=heldRight&&mouse.y>=heldTop&&mouse.y<=heldBottom;
    // pointed ignores focus: after a close from the separate window, it is what the pointer rests on.
    const bool gameFocused=GetForegroundWindow()==gameWindow;
    ChestView* hovered=nullptr;ChestView* pinnedChest=nullptr;ChestView* pointed=nullptr;
    for(auto& c:chests){float x=left+(c.x-.5f)*cell,y=top+(c.y-(c.outward?.4f:.8f))*cell;
        if(mouse.x>=x-8*u&&mouse.x<=x+cell+8*u&&mouse.y>=y-8*u&&mouse.y<=y+cell*(c.outward?1.4f:1.f)+8*u){pointed=&c;if(gameFocused&&!overInset)hovered=&c;}
        if(c.id==pinnedId)pinnedChest=&c;
    }
    timelinePlayed=peek::readTimeline(roomHost);
    // A flame that leads nowhere a preview can show is not there to hover or keep pinned.
    for(auto* c:{hovered,pinnedChest})if(c&&c->outward&&c->room.empty())resolveExit(*c);
    for(auto* c:{hovered,pinnedChest})if(c&&c->jar){auto ref=peek::readJarReference(roomHost,c->owner,c->jarName);c->room=ref.error.empty()?ref.name:"";}
    if(hovered&&hovered->room.empty())hovered=nullptr;
    if(pinnedChest&&pinnedChest->room.empty())pinnedChest=nullptr;
    // Closing leaves the chest under the pointer dismissed until the pointer moves to another one,
    // so a close is not answered by a new preview of whatever the pointer happened to rest on.
    auto closeAll=[&]{clearPreview();hoveredId=pointed?pointed->id:0;};
    if(close||action.action==peek::PreviewAction::Close){closeAll();return;}
    if(nativeTest){currentArt=peek::nativeMirrorArt();if(currentArt){peek::Snapshot empty;peek::updatePreviewWindow(*currentArt,empty,"ACTIVE ROOM - NATIVE TEST","Depth 0",peek::nativeRenderStatus(),"");}return;}
    if(action.action==peek::PreviewAction::Back)back=true;
    if(action.action==peek::PreviewAction::Forward)forward=true;
    if(action.action==peek::PreviewAction::Dock)peek::closePreviewWindow();
    if(action.action==peek::PreviewAction::Select){
        if(!previewPath.empty()&&action.view==stepView(previewPath.back()))enterPreview(action.object);
        else log("Preview window click dropped: aimed at %s",action.view.c_str());
    }
    // The click that pins a preview is spent on pinning, even where the inset covers the chest.
    const bool wasPinned=pinned;
    if(pinned&&!pinnedChest){clearPreview();return;}
    if(peek::previewWindowOpen()&&!pinned){peek::closePreviewWindow();}
    if(back){
        if(previewPath.size()>1){forwardPath.push_back(previewPath.back());previewPath.pop_back();}
        else{closeAll();return;}
    }
    if(forward&&pinned&&!forwardPath.empty()){previewPath.push_back(forwardPath.back());forwardPath.pop_back();}
    if(hoveredId!=(hovered?hovered->id:0))dismissedHover=false;
    if(!pinned){hoveredId=hovered?hovered->id:0;
        previewPath.clear();forwardPath.clear();if(hovered&&(!dismissedHover||clicked||open))previewPath.push_back(rootStep(*hovered));}
    if((clicked||open)&&hovered&&(!pinned||clickPopout)){if(pinnedId!=hovered->id){previewPath.clear();forwardPath.clear();}pinned=true;pinnedId=hovered->id;pinnedChest=hovered;dismissedHover=false;if(previewPath.empty())previewPath.push_back(rootStep(*hovered));}
    ChestView* active=pinned?pinnedChest:hovered;
    if(previewPath.empty()){peek::requestNativeMirror(false);return;}
    // A flame's first step follows the flame, into a paradox and back out of one as things move.
    if(active&&(previewPath[0].wet!=active->wet||previewPath[0].room!=active->room||previewPath[0].jarName!=active->jarName||previewPath[0].timeline.empty()==active->paradox)){
        previewPath.assign(1,rootStep(*active));forwardPath.clear();
    }
    // Revalidate live/global chests along the path, not just the visible room.
    // A removed entry returns to its parent; movement across water updates its branch.
    const uintptr_t owner=active?active->owner:0;
    for(size_t i=1;i<previewPath.size();i++){
        auto& child=previewPath[i];
        // A step through a flame of the stack follows that flame the same way.
        if(child.exitFrom>=0){
            auto target=peek::readExitTarget(roomHost,owner,child.exitFrom,child.greens);
            if(!target.available){previewPath.resize(i);forwardPath.clear();break;}
            if(target.paradox!=!child.timeline.empty()||(target.paradox&&target.room!=child.room)){
                child=exitStep(target.room,target.paradox,target.ancestors,child.exitFrom,child.greens);previewPath.resize(i+1);forwardPath.clear();break;
            }
            // An outer step names whatever room is that far out now: carrying the pinned chest out
            // through a flame builds no room, so the same step can come to mean another one.
            child.room=target.room;continue;
        }
        if(child.ancestors>=0){auto ref=peek::readRoomReference(roomHost,owner,child.ancestors);if(ref.error.empty()&&ref.name!=child.room)child.room=ref.name;continue;}
        if(child.jar){
            auto ref=peek::readJarReference(roomHost,owner,child.jarName);
            if(!ref.error.empty()){previewPath.resize(i);forwardPath.clear();break;}
            if(ref.name!=child.room){child.room=ref.name;previewPath.resize(i+1);forwardPath.clear();break;}
        }
        if(!child.chestId)continue;
        const auto& parent=previewPath[i-1];auto state=child.parentSnapshot;
        if(parent.ancestors>=0)state=outerRoom(parent,owner,state);
        else if(parent.jar)state=jarRoom(parent,owner,state);
        else {
            auto globals=stepGlobals(parent,owner);
            if(!globals.available)state.error=globals.error;else peek::applyGlobals(state,globals);
        }
        auto entry=std::find_if(state.objects.begin(),state.objects.end(),[&](const peek::Object& o){return o.sourceId==child.chestId&&o.kind==(child.jar?"jar":"chest");});
        if(!state.error.empty()||entry==state.objects.end()){previewPath.resize(i);forwardPath.clear();break;}
        if(child.jar){if(entry->target!=child.jarName){previewPath.resize(i);forwardPath.clear();break;}continue;}
        bool wet=peek::wetAt(state,entry->x,entry->y);
        if(child.room!=entry->target||child.wet!=wet){child.room=entry->target;child.wet=wet;previewPath.resize(i+1);forwardPath.clear();break;}
    }
    if(open||(clicked&&clickPopout)){if(open&&peek::previewWindowOpen())peek::closePreviewWindow();else if(!peek::showPreviewWindow(gameWindow))log("Preview window creation failed: %lu",GetLastError());}
    const auto step=previewPath.back();bool live=step.ancestors>=0;
    previewWet=step.wet;
    const auto key=stepKey(step);
    if(key!=previewKey){
        // A paradox room the script leaves out is built empty, as the game builds it.
        templatePreview=peek::loadSnapshot(gameRoot,missionPath,step.room,previewWet,step.timeline.empty()?timelinePlayed:step.timeline,paradoxRoom(step)||step.jar);
        // Spawn("player") and Spawn("yield") make a flame only in a room built with another below
        // it (0x43E8CF, 0x43EB74), and a paradox room is built alone.
        if(paradoxRoom(step))templatePreview.objects.erase(std::remove_if(templatePreview.objects.begin(),templatePreview.objects.end(),previewPortal),templatePreview.objects.end());
        templatePreview.timeline=step.timeline;previewKey=key;log("Preview %s objects=%zu error=%s",key.c_str(),templatePreview.objects.size(),templatePreview.error.c_str());
    }
    const auto source=peek::readRoomReference(roomHost,owner,0);
    if(live)preview=outerRoom(step,owner,templatePreview);
    else if(step.jar){preview=jarRoom(step,owner,templatePreview);preview.nativeDepth=!step.timeline.empty()?step.depth:source.depth<0?-1:source.depth+step.depth;}
    else {
        auto globals=stepGlobals(step,owner);
        preview=templatePreview;peek::applyGlobals(preview,globals);
        if(!globals.available&&preview.error.empty())preview.error=globals.error;
        // A paradox room is the only room left on its stack, and the rooms inside it count from it.
        preview.nativeDepth=!step.timeline.empty()?step.depth:source.depth<0?-1:source.depth+step.depth;
    }
    peek::requestNativeDestination(roomHost,preview,key,(int)previewPath.size());currentArt=peek::nativeDestinationArt(key);bool nativeArt=currentArt!=nullptr;
    // Reported after the scene exists, because attaching its entities is what would have
    // started a sound. A preview that stays silent is the point, so record that it did.
    static uint32_t reportedSilenced=0;
    if(auto refused=peek::nativeSilencedSounds();refused!=reportedSilenced){reportedSilenced=refused;log("Sound starts refused during preview work: %u",refused);}
    if(nativeArt)if(auto rendered=peek::nativeDestinationSnapshot(key))preview=*rendered;
    // The fallback is for rooms the original renderer cannot draw, not for the frame or two
    // before it draws one: showing it there flashed schematic shapes on every new preview.
    const bool waiting=!nativeArt&&preview.error.empty()&&peek::nativeDestinationPending(key);
    if(!currentArt&&!waiting)currentArt=&peek::renderRoomArt(gameRoot,preview);
    const std::string status=preview.error.empty()?"":"Preview unavailable: "+preview.error;
    // From the step itself, so a rendered snapshot swapped in above cannot carry an older depth.
    const auto shownDepth=depthLabel(step);
    if(active)rect(left+(active->x-.18f)*cell,top+(active->y+(active->outward?.98f:.43f))*cell,cell*.36f,2*u,1,.85f,.35f);
    if(peek::previewWindowOpen()){if(currentArt)peek::updatePreviewWindow(*currentArt,preview,step.room,shownDepth,status,stepView(step));return;}
    // Meanwhile a panel that is up stays as it was, and one that is not up yet waits. The held
    // panel takes no clicks: they would be aimed at the room it still shows, not the one opening.
    if(waiting){if(heldFrame+1==frame&&active&&active->id==heldChest){vertices.insert(vertices.end(),heldInset.begin(),heldInset.end());heldFrame=frame;}return;}
    float w=std::min(viewW-32*u,440*u),s=(w-24*u)/20,h=15*s+66*u;
    float x=viewW-w-16*u,y=76*u;if(active&&left+active->x*cell>viewW*.6f)x=16*u;
    if(y+h>viewH-8*u)y=std::max(68*u,viewH-h-8*u);
    const size_t panel=vertices.size();
    rect(x,y,w,h,.045f,.06f,.1f);outline(x,y,w,h,.72f,.81f,.95f);
    text(x+12*u,y+12*u,"ROOM: "+previewPath.back().room.substr(0,32),1.6f*u);
    float gx=x+12*u,gy=y+48*u;imageQuad(gx,gy,20*s,15*s);
    // Only the first match is underlined, because that is the one a click enters.
    const peek::Object* under=nullptr;
    for(const auto& o:preview.objects)if(o.kind=="chest"||o.kind=="jar"||previewPortal(o)){
        bool portal=previewPortal(o);float ox=gx+(o.x-.65f)*s,oy=gy+(o.y-(portal?.4f:.9f))*s;
        if(mouse.x>=ox&&mouse.x<=ox+1.3f*s&&mouse.y>=oy&&mouse.y<=oy+(portal?1.4f:1.55f)*s){rect(gx+(o.x-.18f)*s,gy+(o.y+(portal?.98f:.43f))*s,.36f*s,2*u,1,.85f,.35f);under=&o;break;}
    }
    // Pinned, the depth line also answers for what is hovered inside: its depth and room.
    auto depthLine=std::string(pinned?"PINNED ":"HOVER ")+shownDepth;
    PreviewStep next;auto move=pinned&&under?previewMove(*under,next):PreviewMove::None;
    if(move!=PreviewMove::None){
        const auto& to=move==PreviewMove::Return?previewPath[previewPath.size()-2]:next;
        auto hover="   HOVER "+depthLabel(to)+": ";
        size_t fit=(size_t)std::max(0.f,(w-24*u)/(7.2f*u));
        if(depthLine.size()+hover.size()<fit)depthLine+=hover+to.room.substr(0,fit-depthLine.size()-hover.size());
    }
    text(x+12*u,y+29*u,depthLine,1.2f*u);
    if(wasPinned&&clicked&&under)enterPreview(*under);
    if(!preview.error.empty())text(gx,gy+6*s,"PREVIEW UNAVAILABLE",1.5f*u);
    text(gx,gy+15*s+10*u,status.substr(0,62),u);
    heldInset.assign(vertices.begin()+(ptrdiff_t)panel,vertices.end());heldFrame=frame;heldChest=active?active->id:0;
    heldLeft=x;heldTop=y;heldRight=x+w;heldBottom=y+h;
}
static bool initGL(){
#define LOAD(n) n=(n##Fn)wglGetProcAddress("gl" #n);if(!n)return false
    LOAD(CreateShader);LOAD(ShaderSource);LOAD(CompileShader);LOAD(GetShaderiv);LOAD(GetShaderInfoLog);LOAD(CreateProgram);LOAD(AttachShader);LOAD(LinkProgram);LOAD(GetProgramiv);LOAD(DeleteShader);LOAD(UseProgram);LOAD(GenVertexArrays);LOAD(BindVertexArray);LOAD(GenBuffers);LOAD(BindBuffer);LOAD(BufferData);LOAD(EnableVertexAttribArray);LOAD(VertexAttribPointer);LOAD(BindFramebuffer);LOAD(ActiveTexture);
#undef LOAD
    const char* vs="#version 330\nlayout(location=0)in vec2 p;layout(location=1)in vec4 c;layout(location=2)in vec3 uv;out vec4 color;out vec3 tex;void main(){gl_Position=vec4(p,0,1);color=c;tex=uv;}";
    const char* fs="#version 330\nin vec4 color;in vec3 tex;uniform sampler2D art;out vec4 result;void main(){result=mix(color,texture(art,tex.xy),tex.z);}";
    GLuint shaders[2];const char* sources[2]={vs,fs};
    for(int i=0;i<2;i++){shaders[i]=CreateShader(i?0x8B30:0x8B31);ShaderSource(shaders[i],1,&sources[i],nullptr);CompileShader(shaders[i]);GLint ok;GetShaderiv(shaders[i],0x8B81,&ok);if(!ok){char error[1024];GetShaderInfoLog(shaders[i],1024,nullptr,error);log("shader: %s",error);return false;}}
    program=CreateProgram();AttachShader(program,shaders[0]);AttachShader(program,shaders[1]);LinkProgram(program);DeleteShader(shaders[0]);DeleteShader(shaders[1]);GLint ok;GetProgramiv(program,0x8B82,&ok);if(!ok)return false;
    GenVertexArrays(1,&vao);GenBuffers(1,&vbo);log("OpenGL overlay ready: %s",glGetString(GL_VERSION));return true;
}
static void drawOverlay(){
    if(!wglGetCurrentContext())return;
    HWND hwnd=WindowFromDC(wglGetCurrentDC());RECT client{};if(!hwnd||!GetClientRect(hwnd,&client)||client.right<=0||client.bottom<=0)return;
    if(!IsWindowVisible(hwnd)||client.right<100||client.bottom<100)return;
    if(!glReady){glReady=initGL();if(!glReady)return;}
    gameWindow=hwnd;viewW=(float)client.right;viewH=(float)client.bottom;currentArt=nullptr;
    // This is not the copy of the game the player normally runs, and the window says which one
    // it is. SFML sets the title when it creates the window, so a new handle needs it again.
    if(hwnd!=titledWindow){titledWindow=hwnd;SetWindowTextW(hwnd,L"Recursed++");}
    if(queuedNative){queuedNative=false;bool next=!nativeTest;clearPreview();nativeTest=next;if(next){peek::requestNativeMirror(true);peek::showPreviewWindow(hwnd);log("Native replay requested");}}
    // Events preserve short clicks without firing twice across consecutive frames.
    bool clicked=queuedClick,backed=queuedBack,went=queuedForward,open=queuedOpen,close=queuedClose;
    queuedClick=queuedBack=queuedForward=queuedOpen=queuedClose=false;
    POINT mouse{};GetCursorPos(&mouse);ScreenToClient(hwnd,&mouse);
    // Nothing is drawn over the game except a preview the player asked for. The controls are
    // printed in the launcher window, and a permanent banner reciting them is not gameplay.
    vertices.clear();
    drawPreview(mouse,clicked,backed,went,open,close);clickPopout=false;
    // A rewind that worked shows itself. One that could not do what was asked says so, briefly.
    if(auto said=peek::rewindNotice(GetTickCount64());!said.empty()){
        float u=std::max(1.0f,viewH/750.0f),w=said.size()*9.6f*u+24*u;
        rect((viewW-w)*.5f,16*u,w,36*u,.045f,.06f,.1f);text((viewW-w)*.5f+12*u,27*u,said,1.6f*u);
    }
    GLint oldRowLength,oldSkipRows,oldSkipPixels;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&oldRowLength);glGetIntegerv(GL_UNPACK_SKIP_ROWS,&oldSkipRows);glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&oldSkipPixels);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);
    GLint oldActiveTexture,oldTexture,oldUnpack,oldPBO;glGetIntegerv(0x84E0,&oldActiveTexture);ActiveTexture(0x84C0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&oldTexture);glGetIntegerv(GL_UNPACK_ALIGNMENT,&oldUnpack);glGetIntegerv(0x88EF,&oldPBO);BindBuffer(0x88EC,0);glPixelStorei(GL_UNPACK_ALIGNMENT,4);
    if(!artTexture){glGenTextures(1,&artTexture);glBindTexture(GL_TEXTURE_2D,artTexture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,0x812F);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,0x812F);unsigned int pixel=0xffffffff;glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,0x80E1,GL_UNSIGNED_BYTE,&pixel);}glBindTexture(GL_TEXTURE_2D,artTexture);
    if(currentArt&&!currentArt->pixels.empty()&&currentArt->revision!=artRevision){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,currentArt->width,currentArt->height,0,0x80E1,GL_UNSIGNED_BYTE,currentArt->pixels.data());artRevision=currentArt->revision;}
    GLint oldProgram,oldVAO,oldBuffer,oldDrawFBO,oldViewport[4];GLboolean oldColor[4];
    glGetIntegerv(0x8B8D,&oldProgram);glGetIntegerv(0x85B5,&oldVAO);glGetIntegerv(0x8894,&oldBuffer);glGetIntegerv(0x8CA6,&oldDrawFBO);glGetIntegerv(GL_VIEWPORT,oldViewport);glGetBooleanv(GL_COLOR_WRITEMASK,oldColor);
    const GLenum caps[]={GL_DEPTH_TEST,GL_STENCIL_TEST,GL_SCISSOR_TEST,GL_CULL_FACE,GL_BLEND,0x8C89};GLboolean states[6];for(int i=0;i<6;i++){states[i]=glIsEnabled(caps[i]);glDisable(caps[i]);}
    BindFramebuffer(0x8CA9,0);glViewport(0,0,client.right,client.bottom);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);UseProgram(program);BindVertexArray(vao);BindBuffer(0x8892,vbo);BufferData(0x8892,vertices.size()*sizeof(Vertex),vertices.data(),0x88E0);EnableVertexAttribArray(0);VertexAttribPointer(0,2,GL_FLOAT,FALSE,sizeof(Vertex),nullptr);EnableVertexAttribArray(1);VertexAttribPointer(1,4,GL_FLOAT,FALSE,sizeof(Vertex),(void*)8);EnableVertexAttribArray(2);VertexAttribPointer(2,3,GL_FLOAT,FALSE,sizeof(Vertex),(void*)24);glDrawArrays(GL_TRIANGLES,0,(GLsizei)vertices.size());
    BindVertexArray(oldVAO);BindBuffer(0x8892,oldBuffer);UseProgram(oldProgram);BindFramebuffer(0x8CA9,oldDrawFBO);glViewport(oldViewport[0],oldViewport[1],oldViewport[2],oldViewport[3]);glColorMask(oldColor[0],oldColor[1],oldColor[2],oldColor[3]);for(int i=0;i<6;i++)if(states[i])glEnable(caps[i]);
    glBindTexture(GL_TEXTURE_2D,oldTexture);glPixelStorei(GL_UNPACK_ALIGNMENT,oldUnpack);glPixelStorei(GL_UNPACK_ROW_LENGTH,oldRowLength);glPixelStorei(GL_UNPACK_SKIP_ROWS,oldSkipRows);glPixelStorei(GL_UNPACK_SKIP_PIXELS,oldSkipPixels);BindBuffer(0x88EC,oldPBO);ActiveTexture(oldActiveTexture);
}
static void __fastcall displayHook(void* window,void*){frame++;if(frame<5)log("display frame=%llu this=%p original=%p",frame,window,(void*)originalDisplay);if(frame%300==0&&!chests.empty()){for(const auto& c:chests)log("Chest %p %0.2f,%0.2f room=%s",(void*)c.id,c.x,c.y,c.room.c_str());}drawOverlay();syncCursorVisibility(window);originalDisplay(window);chests.clear();}
extern "C" __declspec(dllexport) DWORD WINAPI RecursedPeekInitialize(void*){
    gameBase=(uintptr_t)GetModuleHandleW(nullptr);
    // Not beside the DLL: a download extracted into Program Files cannot write there, and the
    // game is manifested asInvoker, so a failed write is a real failure rather than one Windows
    // quietly redirects into a VirtualStore copy.
    const auto support=peek::supportFolder();
    if(support.empty())return 0;
    logFile=_wfsopen((support+L"\\peek.log").c_str(),L"w",_SH_DENYNO);
    const auto profile=support+L"\\profile";
    CreateDirectoryW(profile.c_str(),nullptr);
    // The game asks for this through SHGetFolderPathA and opens it with the ANSI CRT, so it has
    // to be a path the active code page can spell. A user name outside that code page would
    // otherwise arrive as question marks and every save would fail somewhere unpredictable.
    const auto narrow=peek::narrowUsable(profile);
    if(narrow.empty()){log("This account's profile path cannot be spelled in the active code page, and the game opens its files by that name; refusing to start");return 0;}
    strcpy_s(profilePath,MAX_PATH,narrow.c_str());
    char exe[MAX_PATH];GetModuleFileNameA(nullptr,exe,MAX_PATH);char* slash=strrchr(exe,'\\');
    if(!slash){log("The game's own path came back without a folder; refusing to start");return 0;}
    *slash=0;gameRoot=exe;
    const unsigned char expected[]={0x55,0x8b,0xec,0x83,0xec,0x20};
    if(memcmp(address(0x440A20),expected,sizeof expected)){log("Build signature mismatch");return 0;}
    if(!hookImport("SHELL32.dll","SHGetFolderPathA",(void*)isolatedFolder,(void**)&originalFolder)){log("Profile hook failed; refusing to start");return 0;}
    if(!installSteamStand()){log("Steam isolation failed; refusing to start");return 0;}
    if(!hookImport("MSVCR120.dll","fopen",(void*)fopenHook,(void**)&originalFopen)){log("Lua file hook missing");return 0;}
    if(!hookImport("sfml-window-2.dll","?setMouseCursorVisible@Window@sf@@QAEX_N@Z",(void*)cursorVisibilityHook,(void**)&originalCursorVisibility)){log("Cursor visibility hook missing");return 0;}
    if(!hookImport("sfml-window-2.dll","?pollEvent@Window@sf@@QAE_NAAVEvent@2@@Z",(void*)pollEventHook,(void**)&originalPollEvent))return 0;
    char testFlag[8];bufferedTestInput=GetEnvironmentVariableA("RECURSED_PEEK_TEST_INPUT",testFlag,sizeof testFlag)>0;
    char devFlag[8];developerMode=GetEnvironmentVariableA("RECURSED_PEEK_DEV",devFlag,sizeof devFlag)>0&&devFlag[0]=='1';
    if(!hookImport("sfml-window-2.dll","?isKeyPressed@Keyboard@sf@@SA_NW4Key@12@@Z",(void*)keyHook,(void**)&originalIsKeyPressed))return 0;
    log("Automated test input buffering: %s; developer diagnostics: %s",bufferedTestInput?"on":"off",developerMode?"on":"off");
    if(*(uintptr_t*)address(0x47ad80)!=(uintptr_t)address(0x411730)){log("Chest vtable mismatch");return 0;}
    if(*(uintptr_t*)address(0x47b0d8)!=(uintptr_t)address(0x415f30)){log("Jar vtable mismatch");return 0;}
    if(!patchPointer((void**)address(0x47b0d8),(void*)jarTransformHook,(void**)&originalJarTransform))return 0;
    if(*(uintptr_t*)address(0x47af24)!=(uintptr_t)address(0x413600)){log("Exit vtable mismatch");return 0;}
    if(!patchPointer((void**)address(0x47af24),(void*)exitTransformHook,(void**)&originalExitTransform))return 0;
    if(!patchPointer((void**)address(0x47ad80),(void*)chestTransformHook,(void**)&originalChestTransform)||!hookRoomBuilder()){log("Game hooks failed");return 0;}
    if(!peek::installNativeRender(gameBase)){log("Native renderer signature mismatch");return 0;}
    // Rewind is an addition on top of the preview, so a build it does not recognise keeps the rest.
    if(!peek::installRewind(gameBase,log))log("Rewind hooks unavailable; undo is off");
    if(!peek::installGamepad(gameBase,log))log("Gamepad hook unavailable; controllers connected later and gamepad undo are off");

    if(!hookImport("sfml-window-2.dll","?display@Window@sf@@QAEXXZ",(void*)displayHook,(void**)&originalDisplay)){log("Display import missing");return 0;}
    log("Initialized base=%p profile=%s saves=%ls steam=%s",(void*)gameBase,profilePath,saveFiles.folder.c_str(),steamAsked?"requested":"isolated");return 1;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){selfModule=module;DisableThreadLibraryCalls(module);}return TRUE;}
