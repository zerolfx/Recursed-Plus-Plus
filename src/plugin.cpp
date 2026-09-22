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
struct ChestView{uintptr_t id;float x,y;std::string room;uintptr_t owner;bool wet;bool outward=false;};
static std::vector<ChestView> chests;
static bool pinned=false,previewWet=false;
static uintptr_t pinnedId=0;
static uintptr_t hoveredId=0;
struct PreviewStep {std::string room;bool wet=false;int ancestors=-1;int renderDepth=-1;uintptr_t chestId=0;peek::Snapshot parentSnapshot{};};
static std::vector<PreviewStep> previewPath;
// What going back stepped out of, so the side buttons can walk the path in both directions.
static std::vector<PreviewStep> forwardPath;
static peek::Snapshot preview,templatePreview;
static std::string previewKey;
using FopenFn=void*(__cdecl*)(const char*,const char*);
static FopenFn originalFopen;
static void* __cdecl fopenHook(const char* file,const char* mode){
    void* result=originalFopen(file,mode);
    if(result&&file&&mode&&mode[0]=='r'){
        std::string name=file;std::replace(name.begin(),name.end(),'\\','/');auto p=name.find("missions/");
        if(p!=std::string::npos&&name.size()>4&&name.substr(name.size()-4)==".lua"){
            auto mission=name.substr(p);if(mission!=missionPath){peek::requestNativeMirror(false);nativeTest=false;missionPath=mission;previewKey.clear();pinned=false;previewPath.clear();log("Mission file %s",missionPath.c_str());}
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
static ChestTransform originalExitTransform;
static void __fastcall exitTransformHook(void* object,void*){
    originalExitTransform(object);
    if(peek::nativeRenderWork()||!roomHost||chests.size()>128)return;
    const auto* b=(const unsigned char*)object;auto owner=*(uintptr_t*)(b+4);
    auto parent=peek::readRoomReference(roomHost,owner,1);if(!parent.error.empty())return;
    ChestView item{(uintptr_t)object,*(const float*)(b+8),*(const float*)(b+12),parent.name,owner,false,true};
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
using BuildRoom=uintptr_t(__thiscall*)(void*,void*,void*,uint32_t);
static BuildRoom originalBuildRoom;
static uintptr_t __fastcall buildRoomHook(void* host,void*,void* tiles,void* name,uint32_t wet){
    peek::requestNativeMirror(false);nativeTest=false;
    roomHost=(uintptr_t)host;
    auto tileset=oldString((char*)host+8);auto room=oldString(name);
    log("Build room tileset=%s room=%s wet=%u",tileset.c_str(),room.c_str(),wet&255);
    pinned=false;previewPath.clear();previewKey.clear();chests.clear();return originalBuildRoom(host,tiles,name,wet);
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
    }
    // Mouse buttons: the two side buttons walk the preview history the way they walk a browser's.
    if(e[0]==9){
        if(e[1]==0){queuedClick=true;clickPopout=(GetKeyState(VK_SHIFT)&0x8000)!=0;}
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
static void clearPreview(){nativeTest=false;peek::requestNativeMirror(false);pinned=false;previewPath.clear();forwardPath.clear();previewKey.clear();dismissedHover=true;peek::closePreviewWindow();}
static bool previewPortal(const peek::Object& o){return o.kind=="player"||o.kind=="yield";}
static void enterPreview(const peek::Object& o){
    if(previewPath.empty())return;
    if(previewPortal(o)){
        if(previewPath.back().ancestors<0&&previewPath.size()>1){previewPath.pop_back();return;}
        if(previewPath.size()>=8)return;
        int up=previewPath.back().ancestors+1;
        uintptr_t owner=0;for(const auto& c:chests)if(c.id==pinnedId)owner=c.owner;
        auto parent=peek::readRoomReference(roomHost,owner,up);
        if(parent.error.empty()){previewPath.push_back({parent.name,false,up,parent.depth});}
    }else if(o.kind=="chest"&&previewPath.size()<8&&!o.target.empty()){
        previewPath.push_back({o.target,peek::wetAt(preview,o.x,o.y),-1,preview.nativeDepth<0?-1:preview.nativeDepth+1,o.sourceId,preview});
        forwardPath.clear();
    }
}
static PreviewStep rootStep(const ChestView& c){return {c.room,c.wet,c.outward?1:-1,-1};}
static const peek::RoomArt* currentArt=nullptr;
static void drawPreview(POINT mouse,bool clicked,bool back,bool forward,bool open,bool close){
    auto action=peek::pumpPreviewWindow();
    if(action.action!=peek::PreviewAction::None)log("Preview window action=%d depth=%zu",(int)action.action,previewPath.size());
    if(close||action.action==peek::PreviewAction::Close){clearPreview();return;}
    if(nativeTest){currentArt=peek::nativeMirrorArt();if(currentArt){peek::Snapshot empty;peek::updatePreviewWindow(*currentArt,empty,"ACTIVE ROOM - NATIVE TEST",0,peek::nativeRenderStatus());}return;}
    if(action.action==peek::PreviewAction::Back)back=true;
    if(action.action==peek::PreviewAction::Forward)forward=true;
    if(action.action==peek::PreviewAction::Dock)peek::closePreviewWindow();
    if(action.action==peek::PreviewAction::Select)enterPreview(action.object);
    float u=std::max(1.0f,viewH/750.0f),cell=std::min(viewW/20,viewH/15);
    float left=(viewW-cell*20)*.5f,top=(viewH-cell*15)*.5f;
    ChestView* hovered=nullptr;ChestView* pinnedChest=nullptr;
    for(auto& c:chests){float x=left+(c.x-.5f)*cell,y=top+(c.y-(c.outward?.4f:.8f))*cell;
        if(GetForegroundWindow()==gameWindow&&mouse.x>=x-8*u&&mouse.x<=x+cell+8*u&&mouse.y>=y-8*u&&mouse.y<=y+cell*(c.outward?1.4f:1.f)+8*u)hovered=&c;
        if(c.id==pinnedId)pinnedChest=&c;
    }
    if(pinned&&!pinnedChest){clearPreview();return;}
    if(peek::previewWindowOpen()&&!pinned){peek::closePreviewWindow();}
    if(back){
        if(previewPath.size()>1){forwardPath.push_back(previewPath.back());previewPath.pop_back();}
        else{clearPreview();return;}
    }
    if(forward&&pinned&&!forwardPath.empty()){previewPath.push_back(forwardPath.back());forwardPath.pop_back();}
    if(hoveredId!=(hovered?hovered->id:0))dismissedHover=false;
    if(!pinned){hoveredId=hovered?hovered->id:0;
        previewPath.clear();forwardPath.clear();if(hovered&&(!dismissedHover||clicked||open))previewPath.push_back(rootStep(*hovered));}
    if((clicked||open)&&hovered&&(!pinned||clickPopout)){if(pinnedId!=hovered->id){previewPath.clear();}pinned=true;pinnedId=hovered->id;pinnedChest=hovered;dismissedHover=false;if(previewPath.empty())previewPath.push_back(rootStep(*hovered));}
    ChestView* active=pinned?pinnedChest:hovered;
    if(previewPath.empty()){peek::requestNativeMirror(false);return;}
    if(active&&(previewPath[0].wet!=active->wet||previewPath[0].room!=active->room)){
        previewPath.assign(1,rootStep(*active));forwardPath.clear();
    }
    // Revalidate live/global chests along the path, not just the visible room.
    // A removed entry returns to its parent; movement across water updates its branch.
    for(size_t i=1;i<previewPath.size();i++){
        auto& child=previewPath[i];if(!child.chestId)continue;
        const auto& parent=previewPath[i-1];auto state=child.parentSnapshot;
        if(parent.ancestors>=0)state=peek::readRoomSnapshot(roomHost,active?active->owner:0,parent.ancestors,state);
        else {
            auto globals=peek::readGlobals(roomHost,active?active->owner:0,parent.room);
            if(!globals.available)state.error=globals.error;else peek::applyGlobals(state,globals);
        }
        auto entry=std::find_if(state.objects.begin(),state.objects.end(),[&](const peek::Object& o){return o.sourceId==child.chestId&&o.kind=="chest";});
        if(!state.error.empty()||entry==state.objects.end()){previewPath.resize(i);forwardPath.clear();break;}
        bool wet=peek::wetAt(state,entry->x,entry->y);
        if(child.room!=entry->target||child.wet!=wet){child.room=entry->target;child.wet=wet;previewPath.resize(i+1);forwardPath.clear();break;}
    }
    if(open||(clicked&&clickPopout)){if(open&&peek::previewWindowOpen())peek::closePreviewWindow();else if(!peek::showPreviewWindow(gameWindow))log("Preview window creation failed: %lu",GetLastError());}
    const auto step=previewPath.back();bool live=step.ancestors>=0;
    previewWet=step.wet;
    const auto key=missionPath+"|"+step.room+"|"+(live?"live:"+std::to_string(step.ancestors):previewWet?"wet":"dry");
    if(key!=previewKey){templatePreview=peek::loadSnapshot(gameRoot,missionPath,previewPath.back().room,previewWet);previewKey=key;log("Preview %s objects=%zu error=%s",key.c_str(),templatePreview.objects.size(),templatePreview.error.c_str());}
    const auto source=peek::readRoomReference(roomHost,active?active->owner:0,0);
    auto globals=peek::readGlobals(roomHost,active?active->owner:0,previewPath.back().room);
    if(live)preview=peek::readRoomSnapshot(roomHost,active?active->owner:0,step.ancestors,templatePreview);
    else {
        preview=templatePreview;peek::applyGlobals(preview,globals);
        if(!globals.available&&preview.error.empty())preview.error=globals.error;
        preview.nativeDepth=step.renderDepth>=0?step.renderDepth:source.depth+1;
    }
    peek::requestNativeDestination(roomHost,preview,key,(int)previewPath.size());currentArt=peek::nativeDestinationArt(key);bool nativeArt=currentArt!=nullptr;
    // Reported after the scene exists, because attaching its entities is what would have
    // started a sound. A preview that stays silent is the point, so record that it did.
    static uint32_t reportedSilenced=0;
    if(auto refused=peek::nativeSilencedSounds();refused!=reportedSilenced){reportedSilenced=refused;log("Sound starts refused during preview work: %u",refused);}
    if(nativeArt)if(auto rendered=peek::nativeDestinationSnapshot(key))preview=*rendered;
    if(!currentArt)currentArt=&peek::renderRoomArt(gameRoot,preview);
    const std::string status=preview.error.empty()?"":"Preview unavailable: "+preview.error;
    int shownDepth=preview.nativeDepth-source.depth;
    if(active)rect(left+(active->x-.18f)*cell,top+(active->y+(active->outward?.98f:.43f))*cell,cell*.36f,2*u,1,.85f,.35f);
    if(peek::previewWindowOpen()){peek::updatePreviewWindow(*currentArt,preview,previewPath.back().room,shownDepth,status);return;}
    float w=std::min(viewW-32*u,440*u),s=(w-24*u)/20,h=15*s+66*u;
    float x=viewW-w-16*u,y=76*u;if(active&&left+active->x*cell>viewW*.6f)x=16*u;
    if(y+h>viewH-8*u)y=std::max(68*u,viewH-h-8*u);
    rect(x,y,w,h,.045f,.06f,.1f);outline(x,y,w,h,.72f,.81f,.95f);
    text(x+12*u,y+12*u,"ROOM: "+previewPath.back().room.substr(0,32),1.6f*u);
    text(x+12*u,y+29*u,std::string(pinned?"PINNED":"HOVER")+" DEPTH "+std::to_string(shownDepth),1.2f*u);
    float gx=x+12*u,gy=y+48*u;imageQuad(gx,gy,20*s,15*s);
    for(const auto& o:preview.objects)if(o.kind=="chest"||previewPortal(o)){
        bool portal=previewPortal(o);float ox=gx+(o.x-.65f)*s,oy=gy+(o.y-(portal?.4f:.9f))*s;
        if(mouse.x>=ox&&mouse.x<=ox+1.3f*s&&mouse.y>=oy&&mouse.y<=oy+(portal?1.4f:1.55f)*s){rect(gx+(o.x-.18f)*s,gy+(o.y+(portal?.98f:.43f))*s,.36f*s,2*u,1,.85f,.35f);if(pinned&&clicked){enterPreview(o);break;}}
    }
    if(!preview.error.empty())text(gx,gy+6*s,"PREVIEW UNAVAILABLE",1.5f*u);
    text(gx,gy+15*s+10*u,status.substr(0,62),u);
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
    if(*(uintptr_t*)address(0x47af24)!=(uintptr_t)address(0x413600)){log("Exit vtable mismatch");return 0;}
    if(!patchPointer((void**)address(0x47af24),(void*)exitTransformHook,(void**)&originalExitTransform))return 0;
    if(!patchPointer((void**)address(0x47ad80),(void*)chestTransformHook,(void**)&originalChestTransform)||!hookRoomBuilder()){log("Game hooks failed");return 0;}
    if(!peek::installNativeRender(gameBase)){log("Native renderer signature mismatch");return 0;}
    if(!hookImport("sfml-window-2.dll","?display@Window@sf@@QAEXXZ",(void*)displayHook,(void**)&originalDisplay)){log("Display import missing");return 0;}
    log("Initialized base=%p profile=%s saves=%ls steam=%s",(void*)gameBase,profilePath,saveFiles.folder.c_str(),steamAsked?"requested":"isolated");return 1;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){selfModule=module;DisableThreadLibraryCalls(module);}return TRUE;}
