#include "snapshot.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <map>
namespace peek {
namespace {
struct Budget {size_t used=0;};
static void* alloc(void* ud,void* ptr,size_t old,size_t size){
 auto* b=(Budget*)ud;if(!ptr)old=0;
 if(!size){free(ptr);b->used-=old;return nullptr;}
 if(size>16*1024*1024||b->used-old+size>16*1024*1024)return nullptr;
 auto p=realloc(ptr,size);if(p)b->used=b->used-old+size;return p;
}
struct State {Snapshot result;std::map<std::string,Tile> kinds;int remaining=2000000;};
static State* ctx(lua_State* L){lua_getfield(L,LUA_REGISTRYINDEX,"peek.context");auto* s=(State*)lua_touserdata(L,-1);lua_pop(L,1);return s;}
static void limit(lua_State* L,lua_Debug*){auto* s=ctx(L);if((s->remaining-=1000)<=0)luaL_error(L,"Preview script instruction limit");}
static int tiles(lua_State* L){
 auto* s=ctx(L);luaL_checktype(L,1,LUA_TTABLE);int ox=(int)luaL_checkinteger(L,2),oy=(int)luaL_checkinteger(L,3);size_t len;const char* grid=luaL_checklstring(L,4,&len);
 if(len>65536)return luaL_error(L,"Tile grid too large");int x=0,y=0;bool seen=false;
 for(size_t i=0;i<len;i++){char c=grid[i];if(c=='\r')continue;if(c=='\n'){if(seen){y++;x=0;}continue;}if(c==' '||c=='\t')continue;seen=true;
   if(x+ox>=0&&x+ox<20&&y+oy>=0&&y+oy<15){char key[2]={c,0};lua_getfield(L,1,key);if(lua_isstring(L,-1)){
     const char* name=lua_tostring(L,-1);auto k=s->kinds.find(name);
     if(k!=s->kinds.end())s->result.tiles[(y+oy)*20+x+ox]=k->second;
     else {lua_pop(L,1);return luaL_error(L,"Unknown tile mapping");}
   }lua_pop(L,1);}x++;
 }return 0;
}
static int spawn(lua_State* L){
 auto* s=ctx(L);const char* kind=luaL_checkstring(L,1);float x=(float)luaL_checknumber(L,2),y=(float)luaL_checknumber(L,3);
 if(!std::isfinite(x)||!std::isfinite(y)||s->result.objects.size()>=2048)return luaL_error(L,"Invalid or excessive objects");
 const char* target=lua_type(L,4)==LUA_TSTRING?lua_tostring(L,4):"";
 bool global=lua_toboolean(L,lua_upvalueindex(1))!=0;
 s->result.objects.push_back({kind,target,x,y,global});s->result.hasGlobals|=global;return 0;
}
static bool runFile(lua_State* L,const std::filesystem::path& p,std::string& error){
 std::error_code ec;auto size=std::filesystem::file_size(p,ec);if(ec||size>1024*1024){error="Missing or oversized script";return false;}
 std::ifstream f(p,std::ios::binary);std::ostringstream text;text<<f.rdbuf();auto source=text.str();
 // The game loads the same file with luaL_loadfile, which skips a UTF-8 byte-order mark, and an
 // editor such as Notepad writes one. A buffer load does not skip it, so it is skipped here.
 const size_t mark=source.compare(0,3,"\xEF\xBB\xBF")==0?3:0;
 if(luaL_loadbufferx(L,source.data()+mark,source.size()-mark,p.filename().string().c_str(),"t")||lua_pcall(L,0,0,0)){
  const char* e=lua_tostring(L,-1);error=e?e:"Lua error";lua_pop(L,1);return false;
 }return true;
}
static bool safeRelative(const std::string& s){if(s.empty()||s.size()>240)return false;auto p=std::filesystem::path(s);if(p.is_absolute()||p.has_root_name())return false;for(auto& part:p)if(part=="..")return false;return true;}
}
Snapshot loadSnapshot(const std::string& root,const std::string& mission,const std::string& room,bool wet,const std::string& timeline,bool emptyWhenMissing){
 State state;Budget budget;
 if(!safeRelative(mission)||room.empty()||room.size()>128){state.result.error="Invalid preview target";return state.result;}
 lua_State* L=lua_newstate(alloc,&budget);if(!L){state.result.error="Preview allocation failed";return state.result;}
 // No io, os, package, debug, file loading or native module loading in this VM.
 luaL_requiref(L,"_G",luaopen_base,1);lua_pop(L,1);
 luaL_requiref(L,LUA_MATHLIBNAME,luaopen_math,1);lua_pop(L,1);
 luaL_requiref(L,LUA_STRLIBNAME,luaopen_string,1);lua_pop(L,1);
 luaL_requiref(L,LUA_TABLIBNAME,luaopen_table,1);lua_pop(L,1);
 for(auto name:{"dofile","loadfile","load","collectgarbage","print","pcall","xpcall"}){lua_pushnil(L);lua_setglobal(L,name);}
 lua_pushlightuserdata(L,&state);lua_setfield(L,LUA_REGISTRYINDEX,"peek.context");lua_sethook(L,limit,LUA_MASKCOUNT,1000);
 lua_newtable(L);const char* kinds[]={"Empty","Solid","Ledge","Water","Acid","Sticky","None"};for(int i=0;i<7;i++){lua_pushinteger(L,i==6?0:i);lua_setfield(L,-2,kinds[i]);}lua_setglobal(L,"Tile");
 lua_pushcfunction(L,tiles);lua_setglobal(L,"ApplyTiles");
 for(int i=0;i<2;i++){lua_pushboolean(L,i);lua_pushcclosure(L,spawn,1);lua_setglobal(L,i?"Global":"Spawn");}
 // Run the mission only in a fresh VM; captured local tables remain intact.
 std::string file=mission;if(file.size()<4||file.substr(file.size()-4)!=".lua")file+=".lua";
 std::filesystem::path missionPath=std::filesystem::path(root)/"custom"/file;
 if(!std::filesystem::exists(missionPath))missionPath=std::filesystem::path(root)/"data"/file;
 if(!runFile(L,missionPath,state.result.error)){lua_close(L);return state.result;}
 lua_getglobal(L,"tiles");std::string tilePath=lua_isstring(L,-1)?lua_tostring(L,-1):"tiles/wip";lua_pop(L,1);
 if(!safeRelative(tilePath)){state.result.error="Invalid tileset";lua_close(L);return state.result;}
 state.result.tileset=tilePath;
 lua_getglobal(L,"pattern");if(lua_isstring(L,-1)){std::string p=lua_tostring(L,-1);if(safeRelative(p))state.result.pattern=p;}lua_pop(L,1);
 // Level load (0x43F2A3, 0x43F3F2) keeps one colour pair per timeline, starting from a grey
 // "start". A table with a start entry is keyed by timeline, and any other table is the colours
 // of "start". A colour one table leaves out of a new entry is black. The renderer looks the
 // timeline up and, when it has no entry, uses the first name.
 {std::map<std::string,std::array<std::array<float,3>,2>> pairs;pairs["start"]={{{.2f,.2f,.2f},{.4f,.4f,.4f}}};
  for(int f=0;f<2;f++){lua_getglobal(L,f?"light":"dark");
   auto read=[&](const std::string& key){auto& c=pairs[key][f];for(int i=0;i<3;i++){lua_rawgeti(L,-1,i+1);float v=(float)lua_tonumber(L,-1);c[i]=std::isfinite(v)?std::fmax(0.f,std::fmin(1.f,v)):0.f;lua_pop(L,1);}};
   if(lua_istable(L,-1)){lua_getfield(L,-1,"start");bool keyed=!lua_isnil(L,-1);lua_pop(L,1);
    if(!keyed)read("start");
    else {lua_pushnil(L);while(lua_next(L,-2)){if(lua_type(L,-2)==LUA_TSTRING&&lua_istable(L,-1))read(lua_tostring(L,-2));lua_pop(L,1);}}}
   lua_pop(L,1);}
  auto p=pairs.find(timeline);if(p==pairs.end())p=pairs.begin();state.result.dark=p->second[0];state.result.light=p->second[1];}
 // Tileset declarations use their own environment so they cannot overwrite room functions.
 lua_State* T=lua_newstate(alloc,&budget);if(!T){state.result.error="Tileset allocation failed";lua_close(L);return state.result;}
 lua_pushlightuserdata(T,&state);lua_setfield(T,LUA_REGISTRYINDEX,"peek.context");lua_sethook(T,limit,LUA_MASKCOUNT,1000);
 lua_newtable(T);for(int i=0;i<7;i++){lua_pushinteger(T,i==6?0:i);lua_setfield(T,-2,kinds[i]);}lua_setglobal(T,"Tile");
 if(runFile(T,std::filesystem::path(root)/"data"/(tilePath+".lua"),state.result.error)){
  lua_pushglobaltable(T);lua_pushnil(T);while(lua_next(T,-2)){
   if(lua_type(T,-2)==LUA_TSTRING&&lua_istable(T,-1)){lua_getfield(T,-1,"type");if(lua_isnumber(T,-1)){Tile tile;tile.kind=(int)lua_tointeger(T,-1);lua_getfield(T,-2,"frame");tile.frame=(int)lua_tointeger(T,-1);lua_pop(T,1);tile.definition=lua_tostring(T,-3);state.kinds[tile.definition]=tile;}lua_pop(T,1);}lua_pop(T,1);
  }lua_pop(T,1);
 }
 lua_close(T);if(!state.result.error.empty()){lua_close(L);return state.result;}
 // The room builder 0x440A20 calls the function through lua_pcall and treats a failed call as a
 // built room, so a room with no function is empty: default tiles and nothing in it.
 lua_getglobal(L,room.c_str());if(!lua_isfunction(L,-1)){if(!emptyWhenMissing)state.result.error="Room function missing";}
 else {lua_pushboolean(L,wet);lua_pushstring(L,"");if(lua_pcall(L,2,0,0)){const char* e=lua_tostring(L,-1);state.result.error=e?e:"Room construction failed";}}
 lua_close(L);
 // Never display a partially constructed room as a successful preview.
 if(!state.result.error.empty()){state.result.tiles={};state.result.objects.clear();state.result.hasGlobals=false;}
 return state.result;
}
}

