#include <windows.h>
#include <array>
#include <cstring>
#include <vector>
#include "rewind.h"
#include "play_record.h"
#include "native_render.h"
namespace peek { namespace {
static uintptr_t base=0;
static RewindLog note=nullptr;
template<class T>T& at(uintptr_t p,size_t offset=0){return *(T*)(p+offset);}
static uintptr_t va(uintptr_t address){return base+address-0x400000;}
using Method=void(__thiscall*)(void*);
using Update=void(__thiscall*)(void*,float);
using Construct=void*(__thiscall*)(void*,const void*);
static Method originalAttach=nullptr,originalPause=nullptr;
static Update originalUpdate=nullptr;
static Construct originalConstruct=nullptr;
// Counters outside the Game that building rooms advances: the room serial and the number the
// next unnamed jar takes. A replay starts them where the recording did, or it would build
// the same rooms under other numbers.
static const uintptr_t counterAddresses[]={0x48c580,0x48a050};
struct Recording {uintptr_t game=0;std::string mission;uint32_t seed=0;std::array<uint32_t,2> counters{};std::vector<PlayTick> ticks;};
static Recording play;
static uint32_t random=0;
// Presses are counted: several can arrive in one batch of events before the next tick.
static Rewind requested=Rewind::None;
static int requestedSteps=0;
// Held controls of the last tick of play, for the next tick's heldBefore.
static uint8_t lastHeld=0;
// The pause menu runs its own loop over the same events; a rewind is not asked for from there.
static bool paused=false;
// Host::run hands every Game the App, whose input layer holds the player's own controls.
static uintptr_t gameApp=0;
// A rewind has asked for Restart; the next Game built for the same mission replays 'replayTicks'.
static bool restarting=false,replayPending=false,replaying=false;
static size_t replayTicks=0;
static Rewind replayKind=Rewind::None;
static std::string notice;
static uint64_t noticeUntil=0;
static void say(const char* text){notice=text;noticeUntil=GetTickCount64()+1800;}
static std::string oldString(const void* p){
 const auto* b=(const unsigned char*)p;uint32_t n=*(const uint32_t*)(b+16),capacity=*(const uint32_t*)(b+20);
 if(n>240||capacity<n||capacity>1048576)return {};
 const char* data=capacity<16?(const char*)p:*(const char*const*)p;
 return data?std::string(data,n):std::string();
}
// Game+4 is the App, whose input layer sits at App+4: two bytes per action, held and changed at the last poll.
static uintptr_t binder(uintptr_t game){auto app=at<uintptr_t>(game,4);return app?app+4:0;}
static uint16_t controls(uintptr_t input){
 uint16_t packed=0;for(int i=0;i<8;i++){if(at<uint8_t>(input,2*i))packed|=1u<<i;if(at<uint8_t>(input,2*i+1))packed|=1u<<(8+i);}
 return packed;
}
static void setControls(uintptr_t input,uint16_t packed){for(int i=0;i<8;i++){at<uint8_t>(input,2*i)=(packed>>i)&1;at<uint8_t>(input,2*i+1)=(packed>>(8+i))&1;}}
// Kind-specific state past the shared entity fields, none of it a pointer: the player's wetness
// and move state; an item's in-flight byte; the fan's spin; the oobleck's time to set and what
// it sets into; a crystal's kind; a door's variant and whether it was used; a chest's open flag.
struct KindField {uintptr_t vtable;uint32_t offset,bytes;};
static const KindField kindFields[]={
 {0x47b3cc,0x54,1},{0x47b3cc,0x60,0x20},
 {0x47ad74,0x48,1},{0x47ad74,0x64,4},{0x47accc,0x48,1},{0x47b0cc,0x48,1},{0x47ad9c,0x48,1},{0x47b434,0x48,1},
 {0x47af70,0x48,1},{0x47af70,0x54,4},{0x47b058,0x48,1},{0x47b058,0x58,8},
 {0x47ae8c,0x54,4},{0x47af18,0x58,2},
};
// What the tick left behind, without a heap pointer in it: those differ between the play and
// its replay, and everything hashed here must not. Only the top room is ever updated. Vtables
// are hashed as they are, since a replay runs in the same process as the play it repeats.
static uint32_t digest(uintptr_t game){
 uint32_t h=2166136261u;auto add=[&](const void* p,size_t n){for(size_t i=0;i<n;i++){h^=((const unsigned char*)p)[i];h*=16777619u;}};
 auto begin=at<uintptr_t>(game,0x68),end=at<uintptr_t>(game,0x6c);
 if(end<=begin||(end-begin)%28||end-begin>28*256)return 0;
 uint32_t depth=(uint32_t)((end-begin)/28);add(&depth,4);
 for(auto r=begin;r<end;r+=28){auto name=oldString((void*)r);auto n=(uint32_t)name.size();add(&n,4);add(name.data(),n);}
 auto room=at<uintptr_t>(end-4);if(!room)return 0;
 add((void*)room,8);add((void*)(room+0x94),12); // size; crystal state and room clock
 auto first=at<uintptr_t>(room,0x14),last=at<uintptr_t>(room,0x18);if(last<first||last-first>4*4096)return 0;
 auto count=(uint32_t)((last-first)/4);add(&count,4);
 const auto held=at<uintptr_t>(game,0xc);int32_t heldAt=held?-2:-1;
 for(auto p=first;p<last;p+=4){
  auto e=at<uintptr_t>(p);if(!e)continue;if(e==held)heldAt=(int32_t)((p-first)/4);
  // Everything from position to the global byte: velocity, acceleration, impact speed, extents,
  // angle, property and contact bits. Only the vtable and owner before it are pointers.
  add((void*)e,4);uint8_t owned=at<uintptr_t>(e,4)==room;add(&owned,1);add((void*)(e+8),0x3e);
  for(const auto& f:kindFields)if(at<uintptr_t>(e)==va(f.vtable))add((void*)(e+f.offset),f.bytes);
 }
 add(&heldAt,4);return h;
}
static uint32_t freshSeed(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return (uint32_t)t.QuadPart^(uint32_t)(t.QuadPart>>32)^GetTickCount();}
static void replay(uintptr_t game){
 auto input=binder(game),app=at<uintptr_t>(game,4);if(!input)return;
 unsigned char live[16];memcpy(live,(void*)input,sizeof live);
 LARGE_INTEGER frequency,start,finish;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&start);
 const auto silencedBefore=replaySilencedSounds();
 size_t done=0;bool diverged=false,ended=false;
 // App+0xB4 still says how the last run ended; Host::run clears it only after attach returns.
 // Cleared here, it tells whether a replayed tick ended the mission.
 auto& exit=at<int>(app,0xb4);const int exitBefore=exit;exit=0;
 // Every Steam call tests this byte first. The ticks being caught up on already counted their
 // chest entries in the rooms statistic and unlocked what they unlocked; they must not again.
 auto& steam=at<uint8_t>(va(0x48c585));const uint8_t steamBefore=steam;steam=0;
 replaying=true;enterGameplay(&random,true);
 while(done<replayTicks){
  auto& t=play.ticks[done++];setControls(input,t.controls);originalUpdate((void*)game,t.step);
  auto d=digest(game);if(d!=t.digest){t.digest=d;diverged=true;break;}
  if(exit){ended=true;break;}
 }
 leaveGameplay();replaying=false;memcpy((void*)input,live,sizeof live);exit=exitBefore;steam=steamBefore;
 QueryPerformanceCounter(&finish);const double ms=(finish.QuadPart-start.QuadPart)*1000.0/frequency.QuadPart;
 if(replayKind!=Rewind::Verify||diverged||ended)play.ticks.resize(done);
 note("%s: replayed %zu of %zu ticks in %.1f ms, %u sounds held back%s%s",replayKind==Rewind::Verify?"Rewind check":"Rewind",done,replayTicks,ms,replaySilencedSounds()-silencedBefore,diverged?"; the room differed from the recording here, so play resumes from this tick":"",ended?"; the mission ended inside the recording":"");
 if(diverged)say("COULD NOT UNDO EXACTLY");
}
// Asks for the game's own Restart, exactly as the pause menu does, and remembers how far to replay.
static bool restartFor(uintptr_t game,Rewind kind,int steps){
 const size_t now=play.ticks.size();size_t target=now;
 if(kind==Rewind::Action)for(int i=0;i<steps;i++){auto before=beforeLastAction(play.ticks,target);if(before>=target)break;target=before;}
 if(kind==Rewind::Seconds)target=secondsBefore(play.ticks,now,5.0*steps);
 if(kind!=Rewind::Verify&&(now==0||target>=now)){say("NOTHING TO UNDO");return false;}
 auto app=at<uintptr_t>(game,4);if(!app)return false;
 // What the player holds now carries through the rewind; play resumes measured against it.
 if(auto input=binder(game))lastHeld=(uint8_t)controls(input);
 play.ticks.resize(target);replayTicks=target;replayKind=kind;restarting=true;
 note("Rewind requested: %s x%d, from tick %zu to %zu",kind==Rewind::Action?"before the last action":kind==Rewind::Seconds?"five seconds":"replay check",steps,now,target);
 at<int>(app,0xb8)=1;at<int>(app,0xb4)=1;return true;
}
static void __fastcall updateHook(void* self,void*,float step){
 const auto game=(uintptr_t)self;
 if(game!=play.game||replaying){originalUpdate(self,step);return;}
 if(requested!=Rewind::None){auto kind=requested;requested=Rewind::None;if(restartFor(game,kind,requestedSteps))return;}
 PlayTick tick{0,step,0,lastHeld};if(auto input=binder(game))tick.controls=controls(input);
 enterGameplay(&random,false);originalUpdate(self,step);leaveGameplay();
 tick.digest=digest(game);play.ticks.push_back(tick);lastHeld=(uint8_t)tick.controls;
}
// Host::run decides to open the pause menu before the tick and opens it after, even when the
// tick asked for Restart; the menu's own run would then clear that request. A rewind in flight wins.
static void __fastcall pauseHook(void* self,void*){
 if(restarting&&(uintptr_t)self==play.game)return;
 paused=true;originalPause(self);paused=false;
}
// Host::run calls attach once per Game, before it starts the clock its ticks are measured by.
// Catching up here keeps the time the replay took from being made up afterwards.
static void __fastcall attachHook(void* self,void*){
 originalAttach(self);gameApp=at<uintptr_t>((uintptr_t)self,4);
 if(replayPending&&(uintptr_t)self==play.game){replayPending=false;replay((uintptr_t)self);}
}
// What is still sounding belongs to the moment being left, and the game's Restart alone lets it
// play on, subtitles and all. By the time the next Game is built the old one is gone, and anything
// the new one starts, such as a crux's hum in its first room, comes after this. Each entry of the
// active-sound list holds its sf::Sound at +0x24; the next frame's cleanup frees the stopped ones.
static void stopSounds(){
 using Stop=void(__thiscall*)(void*);const auto stop=at<Stop>(va(0x477520));
 for(auto p=at<uintptr_t>(va(0x48c67c)),e=at<uintptr_t>(va(0x48c680));p<e;p+=4)if(auto s=at<uintptr_t>(p))stop((void*)(s+0x24));
}
static void* __fastcall constructHook(void* self,void*,const void* name){
 const auto mission=oldString(name);
 if(restarting&&mission==play.mission){
  for(size_t i=0;i<play.counters.size();i++)at<uint32_t>(va(counterAddresses[i]))=play.counters[i];
  stopSounds();replayPending=true;
 }else{
  play=Recording{};play.mission=mission;play.seed=freshSeed();replayPending=false;lastHeld=0;
  for(size_t i=0;i<play.counters.size();i++)play.counters[i]=at<uint32_t>(va(counterAddresses[i]));
 }
 restarting=false;requested=Rewind::None;random=play.seed;
 enterGameplay(&random,false);auto result=originalConstruct(self,name);leaveGameplay();
 play.game=(uintptr_t)self;return result;
}
static bool patchPointer(void** location,void* replacement,void** original){
 DWORD old;if(!VirtualProtect(location,sizeof(void*),PAGE_READWRITE,&old))return false;
 *original=*location;*location=replacement;DWORD unused;VirtualProtect(location,sizeof(void*),old,&unused);return true;
}
}
bool installRewind(uintptr_t moduleBase,RewindLog log){
 base=moduleBase;note=log;
 // Game's vtable: +4 attach, +8 update, +0x10 pause. All must still be the functions measured.
 auto* attachSlot=(void**)va(0x47b4d8);auto* updateSlot=(void**)va(0x47b4dc);auto* pauseSlot=(void**)va(0x47b4e4);
 if(*attachSlot!=(void*)va(0x4199b0)||*updateSlot!=(void*)va(0x4199c0)||*pauseSlot!=(void*)va(0x419e40))return false;
 // The Game constructor opens with push ebp / mov ebp,esp / push -1: five bytes, three whole
 // instructions, the same shape as the sound start the renderer takes over.
 auto* c=(unsigned char*)va(0x419820);const unsigned char signature[]={0x55,0x8b,0xec,0x6a,0xff};if(memcmp(c,signature,5))return false;
 auto* trampoline=(unsigned char*)VirtualAlloc(nullptr,10,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!trampoline)return false;
 memcpy(trampoline,c,5);trampoline[5]=0xe9;*(int32_t*)(trampoline+6)=(int32_t)((c+5)-(trampoline+10));
 DWORD old;if(!VirtualProtect(trampoline,10,PAGE_EXECUTE_READ,&old))return false;originalConstruct=(Construct)trampoline;
 if(!patchPointer(attachSlot,(void*)attachHook,(void**)&originalAttach)||!patchPointer(updateSlot,(void*)updateHook,(void**)&originalUpdate)||!patchPointer(pauseSlot,(void*)pauseHook,(void**)&originalPause))return false;
 if(!VirtualProtect(c,5,PAGE_EXECUTE_READWRITE,&old))return false;
 c[0]=0xe9;*(int32_t*)(c+1)=(int32_t)((unsigned char*)constructHook-(c+5));DWORD unused;VirtualProtect(c,5,old,&unused);FlushInstructionCache(GetCurrentProcess(),c,5);
 return true;
}
// The game's controls are 16-byte records at App+4+0x28: action, kind (0 key, 1 joystick button,
// 2 axis below centre, 3 axis above) and code. A button's code indexes 12-byte records of joystick
// and button at +0x1C; an axis's indexes records of joystick, axis and centre at +0x10.
template<class Match>static bool anyBinding(Match match){
 if(!gameApp)return false;const auto input=gameApp+4;auto first=at<uintptr_t>(input,0x28),last=at<uintptr_t>(input,0x2c);
 if(last<first||last-first>16*256)return false;
 for(auto r=first;r<last;r+=16)if(match(at<int>(r,4),at<int>(r,8),input))return true;
 return false;
}
static int device(uintptr_t input,size_t list,int index){
 auto first=at<uintptr_t>(input,list),last=at<uintptr_t>(input,list+4);
 if(index<0||last<first||(size_t)index>=(last-first)/12)return -1;return at<int>(first+12*index,4);
}
bool gameBindsKey(int key){return anyBinding([&](int kind,int code,uintptr_t){return kind==0&&code==key;});}
bool gameBindsButton(int button){return anyBinding([&](int kind,int code,uintptr_t input){return kind==1&&device(input,0x1c,code)==button;});}
bool gameBindsAxis(int axis,bool below){return anyBinding([&](int kind,int code,uintptr_t input){return kind==(below?2:3)&&device(input,0x10,code)==axis;});}
void requestRewind(Rewind kind){if(!play.game||paused)return;requestedSteps=kind==requested?requestedSteps+1:1;requested=kind;}
std::string rewindNotice(uint64_t now){return now<noticeUntil?notice:std::string();}
bool rewindReplaying(){return replaying;}
}
