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

static HMODULE selfModule;
static uintptr_t gameBase;
static FILE* logFile;
static char profilePath[MAX_PATH];
static bool enabled=true,glReady=false;
static bool bufferedTestInput=false;
static bool nativeTest=false,queuedNative=false;
static bool queuedClick=false,queuedBack=false,queuedWet=false,queuedToggle=false;
static bool queuedOpen=false,queuedClose=false,clickPopout=false,suppressEscape=false;
static HWND gameWindow=nullptr;
static uint64_t keyUntil[128]{};
static bool keyDown[128]{};
static uint64_t frame=0;
static void log(const char* fmt,...);
static std::string missionPath,gameRoot;
static uintptr_t roomHost=0;
struct ChestView{uintptr_t id;float x,y;std::string room;uintptr_t owner;bool wet;};
static std::vector<ChestView> chests;
static bool pinned=false,previewWet=false;
static int wetMode=0; // 0 automatic, 1 dry comparison, 2 wet comparison
static uintptr_t pinnedId=0;
static uintptr_t hoveredId=0;
struct PreviewStep {std::string room;bool wet=false;};
static std::vector<PreviewStep> previewPath;
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
using ChestTransform=void(__thiscall*)(void*);
static ChestTransform originalChestTransform;
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
    roomHost=(uintptr_t)host;wetMode=0;
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
    if(key==36){bool held=originalIsKeyPressed(key);if(!held)suppressEscape=false;if(suppressEscape||queuedClose||peek::previewEscapeHeld())return false;}
    return originalIsKeyPressed(key)||(bufferedTestInput&&key>=0&&key<128&&GetTickCount64()<keyUntil[key]);
}
static bool __fastcall pollEventHook(void* window,void*,void* event){
    bool result=originalPollEvent(window,event);if(!result)return false;
    int* e=(int*)event;
    if(bufferedTestInput&&(e[0]==5||e[0]==6||e[0]==9))log("Input event type=%d code=%d",e[0],e[1]);
    // SFML 2 Event: type, then the event union (key code or mouse button).
    if(e[0]==2){memset(keyDown,0,sizeof keyDown);memset(keyUntil,0,sizeof keyUntil);}
    if(e[0]==6&&e[1]>=0&&e[1]<128)keyDown[e[1]]=false;
    if(e[0]==5){int key=e[1];if(key>=0&&key<128){keyUntil[key]=GetTickCount64()+100;if(keyDown[key])return result;keyDown[key]=true;}
        if(key==14)queuedOpen=true;   // O: move the same preview between inset and window
        if(key==36&&(nativeTest||pinned||!previewPath.empty())){queuedClose=true;suppressEscape=true;memset(keyUntil,0,sizeof keyUntil);return pollEventHook(window,nullptr,event);}
        if(key==91)queuedNative=true; // F7: original-renderer diagnostic
        if(key==92)queuedToggle=true; // F8
        if(key==59)queuedBack=true;   // Backspace
        if(key==22)queuedWet=true;    // W
    }
    if(e[0]==9&&e[1]==0){queuedClick=true;clickPopout=(GetKeyState(VK_SHIFT)&0x8000)!=0;}
    return result;
}
static FolderFn originalFolder;
static bool __cdecl offlineSteam(){log("Steam disabled for isolated testing");return false;}
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
static void clearPreview(){nativeTest=false;peek::requestNativeMirror(false);pinned=false;previewPath.clear();previewKey.clear();wetMode=0;dismissedHover=true;peek::closePreviewWindow();}
static void enterPreview(const peek::Object& o){if(previewPath.size()<8&&!o.target.empty()){previewPath.push_back({o.target,peek::wetAt(preview,o.x,o.y)});wetMode=0;log("Nested preview %s depth=%zu",o.target.c_str(),previewPath.size());}}
static const peek::RoomArt* currentArt=nullptr;
static void drawPreview(POINT mouse,bool clicked,bool back,bool wetToggle,bool open,bool close){
    auto action=peek::pumpPreviewWindow();
    if(action.action!=peek::PreviewAction::None)log("Preview window action=%d depth=%zu",(int)action.action,previewPath.size());
    if(close||action.action==peek::PreviewAction::Close){clearPreview();return;}
    if(nativeTest){currentArt=peek::nativeMirrorArt();if(currentArt){peek::Snapshot empty;peek::updatePreviewWindow(*currentArt,empty,"ACTIVE ROOM - NATIVE TEST",0,"LIVE",peek::nativeRenderStatus());}return;}
    if(action.action==peek::PreviewAction::Back)back=true;
    if(action.action==peek::PreviewAction::Wet)wetToggle=true;
    if(action.action==peek::PreviewAction::Dock)peek::closePreviewWindow();
    if(action.action==peek::PreviewAction::Select&&action.object>=0&&action.object<(int)preview.objects.size())enterPreview(preview.objects[action.object]);
    float u=std::max(1.0f,viewH/750.0f),cell=std::min(viewW/20,viewH/15);
    float left=(viewW-cell*20)*.5f,top=(viewH-cell*15)*.5f;
    ChestView* hovered=nullptr;ChestView* pinnedChest=nullptr;
    for(auto& c:chests){float x=left+(c.x-.5f)*cell,y=top+(c.y-.8f)*cell;
        if(GetForegroundWindow()==gameWindow&&mouse.x>=x-8*u&&mouse.x<=x+cell+8*u&&mouse.y>=y-8*u&&mouse.y<=y+cell+8*u)hovered=&c;
        if(c.id==pinnedId)pinnedChest=&c;
    }
    if(pinned&&!pinnedChest){clearPreview();return;}
    if(peek::previewWindowOpen()&&!pinned){peek::closePreviewWindow();}
    if(back){wetMode=0;if(previewPath.size()>1)previewPath.pop_back();else{clearPreview();return;}}
    if(wetToggle)wetMode=(wetMode+1)%3;
    if(hoveredId!=(hovered?hovered->id:0))dismissedHover=false;
    if(!pinned){if(!hovered||hoveredId!=hovered->id)wetMode=0;hoveredId=hovered?hovered->id:0;
        previewPath.clear();if(hovered&&(!dismissedHover||clicked||open))previewPath.push_back({hovered->room,hovered->wet});}
    if((clicked||open)&&hovered&&(!pinned||clickPopout)){if(pinnedId!=hovered->id){previewPath.clear();wetMode=0;}pinned=true;pinnedId=hovered->id;pinnedChest=hovered;dismissedHover=false;if(previewPath.empty())previewPath.push_back({hovered->room,hovered->wet});}
    ChestView* active=pinned?pinnedChest:hovered;
    if(previewPath.empty()){peek::requestNativeMirror(false);return;}
    if(active&&previewPath[0].wet!=active->wet){previewPath.resize(1);previewPath[0].wet=active->wet;wetMode=0;}
    if(open||(clicked&&clickPopout)){if(open&&peek::previewWindowOpen())peek::closePreviewWindow();else if(!peek::showPreviewWindow(gameWindow))log("Preview window creation failed: %lu",GetLastError());}
    previewWet=wetMode==0?previewPath.back().wet:wetMode==2;
    const auto key=missionPath+"|"+previewPath.back().room+"|"+(previewWet?"wet":"dry");
    if(key!=previewKey){templatePreview=peek::loadSnapshot(gameRoot,missionPath,previewPath.back().room,previewWet);previewKey=key;log("Preview %s objects=%zu error=%s",key.c_str(),templatePreview.objects.size(),templatePreview.error.c_str());}
    auto globals=peek::readGlobals(roomHost,active?active->owner:0,previewPath.back().room);
    preview=templatePreview;peek::applyGlobals(preview,globals);
    peek::requestNativeDestination(roomHost,preview,key,(int)previewPath.size());currentArt=peek::nativeDestinationArt(key);bool nativeArt=currentArt!=nullptr;
    if(!currentArt)currentArt=&peek::renderRoomArt(gameRoot,preview);
    const std::string stateStatus=!globals.available?"GLOBAL STATE UNAVAILABLE":globals.initialized?"GLOBAL STATE - APPROXIMATE":"INITIAL LAYOUT - APPROXIMATE";
    const std::string status=!preview.error.empty()?"PREVIEW UNAVAILABLE: "+preview.error:nativeArt?"ORIGINAL RENDERER / "+stateStatus:"FALLBACK / "+stateStatus;
    const std::string condition=std::string(wetMode?"MANUAL ":"AUTO ")+(previewWet?"WET":"DRY");
    if(active)rect(left+(active->x-.18f)*cell,top+(active->y+.43f)*cell,cell*.36f,2*u,1,.85f,.35f);
    if(peek::previewWindowOpen()){peek::updatePreviewWindow(*currentArt,preview,previewPath.back().room,(int)previewPath.size(),condition,status+(nativeArt?"":"\n"+peek::nativeRenderStatus()));return;}
    float w=std::min(viewW-32*u,440*u),s=(w-24*u)/20,h=15*s+112*u;
    float x=viewW-w-16*u,y=76*u;if(active&&left+active->x*cell>viewW*.6f)x=16*u;
    if(y+h>viewH-8*u)y=std::max(68*u,viewH-h-8*u);
    rect(x,y,w,h,.045f,.06f,.1f);outline(x,y,w,h,.72f,.81f,.95f);
    text(x+12*u,y+12*u,"ROOM: "+previewPath.back().room.substr(0,32),1.6f*u);
    text(x+12*u,y+29*u,std::string(pinned?"PINNED":"HOVER")+" "+condition+" DEPTH "+std::to_string(previewPath.size()),1.2f*u);
    float gx=x+12*u,gy=y+48*u;imageQuad(gx,gy,20*s,15*s);
    for(const auto& o:preview.objects)if(o.kind=="chest"){
        float ox=gx+(o.x-.65f)*s,oy=gy+(o.y-.9f)*s;
        if(mouse.x>=ox&&mouse.x<=ox+1.3f*s&&mouse.y>=oy&&mouse.y<=oy+1.55f*s){rect(gx+(o.x-.18f)*s,gy+(o.y+.43f)*s,.36f*s,2*u,1,.85f,.35f);if(pinned&&clicked){enterPreview(o);break;}}
    }
    if(!preview.error.empty())text(gx,gy+6*s,"PREVIEW UNAVAILABLE",1.5f*u);
    text(gx,gy+15*s+10*u,"BACKSPACE: BACK  W: AUTO-DRY-WET  ESC: CLOSE",u);
    text(gx,gy+15*s+25*u,"O: NEW WINDOW  SHIFT+CLICK: NEW WINDOW",u);
    text(gx,gy+15*s+40*u,status.substr(0,62),u);
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
    if(queuedNative){queuedNative=false;bool next=!nativeTest;clearPreview();nativeTest=next;if(next){enabled=true;peek::requestNativeMirror(true);peek::showPreviewWindow(hwnd);log("Native replay requested");}}
    // Events preserve short clicks without firing twice across consecutive frames.
    if(queuedToggle){enabled=!enabled;if(!enabled)clearPreview();}
    bool clicked=queuedClick,backed=queuedBack,wetToggle=queuedWet,open=queuedOpen,close=queuedClose;
    queuedToggle=queuedClick=queuedBack=queuedWet=queuedOpen=queuedClose=false;
    if(!enabled)return;
    POINT mouse{};GetCursorPos(&mouse);ScreenToClient(hwnd,&mouse);
    float u=std::max(1.0f,viewH/750.0f);
    vertices.clear();rect(12*u,12*u,std::min(viewW-24*u,500.0f*u),45*u,.055f,.075f,.13f);text(24*u,22*u,"RECURSED++  F8: INSPECT",2*u);
    text(24*u,43*u,"CLICK: PIN  SHIFT+CLICK: WINDOW  O: SWITCH",u);
    drawPreview(mouse,clicked,backed,wetToggle,open,close);clickPopout=false;
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
static void __fastcall displayHook(void* window,void*){frame++;if(frame<5)log("display frame=%llu this=%p original=%p",frame,window,(void*)originalDisplay);if(frame%300==0&&!chests.empty()){for(const auto& c:chests)log("Chest %p %0.2f,%0.2f room=%s",(void*)c.id,c.x,c.y,c.room.c_str());}drawOverlay();originalDisplay(window);chests.clear();}
extern "C" __declspec(dllexport) DWORD WINAPI RecursedPeekInitialize(void*){
    gameBase=(uintptr_t)GetModuleHandleW(nullptr);
    char own[MAX_PATH];GetModuleFileNameA(selfModule,own,MAX_PATH);char* sep=strrchr(own,'\\');if(!sep)return 0;*sep=0;
    char logPath[MAX_PATH];sprintf_s(logPath,"%s\\peek.log",own);logFile=_fsopen(logPath,"w",_SH_DENYNO);
    sprintf_s(profilePath,"%s\\test-profile",own);CreateDirectoryA(profilePath,nullptr);
    char exe[MAX_PATH];GetModuleFileNameA(nullptr,exe,MAX_PATH);char* slash=strrchr(exe,'\\');if(!slash)return 0;*slash=0;gameRoot=exe;
    const unsigned char expected[]={0x55,0x8b,0xec,0x83,0xec,0x20};
    if(memcmp(address(0x440A20),expected,sizeof expected)){log("Build signature mismatch");return 0;}
    if(!hookImport("SHELL32.dll","SHGetFolderPathA",(void*)isolatedFolder,(void**)&originalFolder)){log("Profile hook failed; refusing to start");return 0;}
    if(!hookImport("steam_api.dll","SteamAPI_Init",(void*)offlineSteam,nullptr)){log("Steam isolation failed; refusing to start");return 0;}
    if(!hookImport("MSVCR120.dll","fopen",(void*)fopenHook,(void**)&originalFopen)){log("Lua file hook missing");return 0;}
    if(!hookImport("sfml-window-2.dll","?pollEvent@Window@sf@@QAE_NAAVEvent@2@@Z",(void*)pollEventHook,(void**)&originalPollEvent))return 0;
    char testFlag[8];bufferedTestInput=GetEnvironmentVariableA("RECURSED_PEEK_TEST_INPUT",testFlag,sizeof testFlag)>0;
    if(!hookImport("sfml-window-2.dll","?isKeyPressed@Keyboard@sf@@SA_NW4Key@12@@Z",(void*)keyHook,(void**)&originalIsKeyPressed))return 0;
    log("Automated test input buffering: %s",bufferedTestInput?"on":"off");
    if(*(uintptr_t*)address(0x47ad80)!=(uintptr_t)address(0x411730)){log("Chest vtable mismatch");return 0;}
    if(!patchPointer((void**)address(0x47ad80),(void*)chestTransformHook,(void**)&originalChestTransform)||!hookRoomBuilder()){log("Game hooks failed");return 0;}
    if(!peek::installNativeRender(gameBase)){log("Native renderer signature mismatch");return 0;}
    if(!hookImport("sfml-window-2.dll","?display@Window@sf@@QAEXXZ",(void*)displayHook,(void**)&originalDisplay)){log("Display import missing");return 0;}
    log("Initialized base=%p profile=%s",(void*)gameBase,profilePath);return 1;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){selfModule=module;DisableThreadLibraryCalls(module);}return TRUE;}
