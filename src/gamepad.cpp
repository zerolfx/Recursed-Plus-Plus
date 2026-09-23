#include <windows.h>
#include <mmsystem.h>
#include <array>
#include <cstring>
#include <vector>
#include "gamepad.h"
#include "rewind.h"
namespace peek { namespace {
static uintptr_t base=0;
static GamepadLog note=nullptr;
template<class T>T& at(uintptr_t p,size_t offset=0){return *(T*)(p+offset);}
using Poll=void(__thiscall*)(void*);
static Poll originalPoll=nullptr;
using Connected=bool(__cdecl*)(unsigned);
using ButtonCount=unsigned(__cdecl*)(unsigned);
using HasAxis=bool(__cdecl*)(unsigned,int);
using Pressed=bool(__cdecl*)(unsigned,unsigned);
using Position=float(__cdecl*)(unsigned,int);
static Connected connected;static ButtonCount buttonCount;static HasAxis hasAxis;static Pressed pressed;static Position position;
using Alloc=void*(__cdecl*)(size_t);using Free=void(__cdecl*)(void*);
// The game's input layer keeps two lists of 12-byte records, built once when it starts: every
// axis of every connected controller as joystick, axis and calibrated centre at +0x10, and every
// button as joystick and button at +0x1C. Its controls name entries of these lists by position.
struct Record {int joystick,index;float centre;};
constexpr size_t axisList=0x10,buttonList=0x1c;
static std::vector<Record> read(uintptr_t input,size_t list){
 auto first=at<uintptr_t>(input,list),last=at<uintptr_t>(input,list+4);
 if(last<first||(last-first)%12||last-first>12*512)return {};
 return std::vector<Record>((const Record*)first,(const Record*)last);
}
static void write(uintptr_t input,size_t list,const std::vector<Record>& records){
 auto alloc=at<Alloc>(base+0x7736c);auto release=at<Free>(base+0x77368);
 auto* fresh=(Record*)(records.empty()?nullptr:alloc(records.size()*sizeof(Record)));
 if(!records.empty()&&!fresh)return;
 if(fresh)memcpy(fresh,records.data(),records.size()*sizeof(Record));
 if(auto old=at<uintptr_t>(input,list))release((void*)old);
 at<uintptr_t>(input,list)=(uintptr_t)fresh;at<uintptr_t>(input,list+4)=(uintptr_t)(fresh+records.size());at<uintptr_t>(input,list+8)=(uintptr_t)(fresh+records.size());
}
// What the game's own start-up enumeration (0x4077B0) would build now: every connected joystick
// in order, its axes, then its buttons. An axis already listed keeps its calibrated centre; a new
// one is calibrated where it rests now, as the game does at start.
static void lists(const std::vector<Record>& oldAxes,std::vector<Record>& axes,std::vector<Record>& buttons){
 for(unsigned j=0;j<8;j++)if(connected(j)){
  for(int a=0;a<8;a++)if(hasAxis(j,a)){
   float centre=position(j,a);for(const auto& r:oldAxes)if(r.joystick==(int)j&&r.index==a)centre=r.centre;
   axes.push_back({(int)j,a,centre});
  }
  for(unsigned b=0,n=buttonCount(j);b<n&&b<32;b++)buttons.push_back({(int)j,(int)b,0});
 }
}
static bool same(const std::vector<Record>& a,const std::vector<Record>& b){
 if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();i++)if(a[i].joystick!=b[i].joystick||a[i].index!=b[i].index)return false;return true;
}
static void describe(unsigned j,bool on){
 JOYCAPSW caps{};if(!on||joyGetDevCapsW(j,&caps,sizeof caps)!=JOYERR_NOERROR){note("Gamepad %u disconnected",j);return;}
 char name[64]{};WideCharToMultiByte(CP_UTF8,0,caps.szPname,-1,name,sizeof name-1,nullptr,nullptr);
 note("Gamepad %u connected: %s, vendor %04X product %04X, %u buttons, %u axes",j,name,caps.wMid,caps.wPid,buttonCount(j),caps.wNumAxes);
}
static std::array<uint32_t,8> seen{};static bool seenOnce=false;
static bool wasBumper=false,wasTrigger=false;
static int changesLogged=0;static std::array<uint64_t,8> lastInput{};
static void __fastcall pollHook(void* self,void*){
 const auto input=(uintptr_t)self;
 // A controller that came or went since the last tick: rebuild the game's lists the way it
 // built them at start, so the controls it already has reach the new controller too. SFML finds
 // a newly connected controller by itself, checking each slot twice a second.
 std::array<uint32_t,8> now{};for(unsigned j=0;j<8;j++)if(connected(j)){now[j]=0x80000000u|buttonCount(j);for(int a=0;a<8;a++)if(hasAxis(j,a))now[j]|=1u<<(16+a);}
 if(!seenOnce||now!=seen){
  for(unsigned j=0;j<8;j++)if((seen[j]!=0)!=(now[j]!=0)||(!seenOnce&&now[j]))describe(j,now[j]!=0);
  auto oldAxes=read(input,axisList);std::vector<Record> axes,buttons;lists(oldAxes,axes,buttons);
  if(!same(axes,oldAxes)||!same(buttons,read(input,buttonList))){
   write(input,axisList,axes);write(input,buttonList,buttons);
   note("Gamepad lists rebuilt: %zu axes, %zu buttons",axes.size(),buttons.size());
  }
  seen=now;seenOnce=true;
 }
 // RB undoes and RT goes back five seconds, read here so they count only while the game has the
 // focus. RT is the Z axis below centre, where the Xbox driver reports the right trigger.
 bool bumper=false,trigger=false;
 for(unsigned j=0;j<8;j++)if(now[j]){
  bumper|=pressed(j,5);trigger|=position(j,2)<-64;
  // The first changes of each run go to the log, so a controller that does not answer can be read.
  uint64_t state=0;for(unsigned b=0;b<32;b++)if(pressed(j,b))state|=1ull<<b;
  for(int a=0;a<8;a++){float p=position(j,a);if(p<-64)state|=1ull<<(32+2*a);if(p>64)state|=1ull<<(33+2*a);}
  if(state!=lastInput[j]&&changesLogged<100){changesLogged++;note("Gamepad %u buttons %08X axes %04X",j,(unsigned)state,(unsigned)(state>>32));}
  lastInput[j]=state;
 }
 if(bumper&&!wasBumper){if(gameBindsButton(5))note("RB is one of the game's controls here, so it does not undo");else requestRewind(Rewind::Action);}
 if(trigger&&!wasTrigger){if(gameBindsAxis(2,true))note("RT is one of the game's controls here, so it does not undo");else requestRewind(Rewind::Seconds);}
 wasBumper=bumper;wasTrigger=trigger;
 originalPoll(self);
}
}
bool installGamepad(uintptr_t moduleBase,GamepadLog log){
 base=moduleBase;note=log;
 auto sfml=GetModuleHandleW(L"sfml-window-2.dll");if(!sfml)return false;
 connected=(Connected)GetProcAddress(sfml,"?isConnected@Joystick@sf@@SA_NI@Z");
 buttonCount=(ButtonCount)GetProcAddress(sfml,"?getButtonCount@Joystick@sf@@SAII@Z");
 hasAxis=(HasAxis)GetProcAddress(sfml,"?hasAxis@Joystick@sf@@SA_NIW4Axis@12@@Z");
 pressed=(Pressed)GetProcAddress(sfml,"?isButtonPressed@Joystick@sf@@SA_NII@Z");
 position=(Position)GetProcAddress(sfml,"?getAxisPosition@Joystick@sf@@SAMIW4Axis@12@@Z");
 if(!connected||!buttonCount||!hasAxis||!pressed||!position)return false;
 // The input poll opens with push ebp / mov ebp,esp / sub esp,24h: six bytes, three whole instructions.
 auto* p=(unsigned char*)(base+0x8540);const unsigned char signature[]={0x55,0x8b,0xec,0x83,0xec,0x24};if(memcmp(p,signature,6))return false;
 auto* t=(unsigned char*)VirtualAlloc(nullptr,11,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!t)return false;
 memcpy(t,p,6);t[6]=0xe9;*(int32_t*)(t+7)=(int32_t)((p+6)-(t+11));DWORD old;if(!VirtualProtect(t,11,PAGE_EXECUTE_READ,&old))return false;originalPoll=(Poll)t;
 if(!VirtualProtect(p,6,PAGE_EXECUTE_READWRITE,&old))return false;
 p[0]=0xe9;*(int32_t*)(p+1)=(int32_t)((unsigned char*)pollHook-(p+5));p[5]=0x90;DWORD unused;VirtualProtect(p,6,old,&unused);FlushInstructionCache(GetCurrentProcess(),p,6);
 return true;
}
}
