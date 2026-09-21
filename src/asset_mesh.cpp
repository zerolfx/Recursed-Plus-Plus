#include "asset_mesh.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <sstream>
namespace peek { namespace {
using Faces=std::vector<Face>;
struct Context {std::vector<Faces> stack{Faces{}};ParticleEffect effect;size_t memory=0;int instructions=100000;};
static void* alloc(void* ud,void* p,size_t old,size_t n){auto& c=*(Context*)ud;if(!p)old=0;if(!n){free(p);c.memory-=old;return nullptr;}if(n>4*1024*1024||c.memory-old+n>4*1024*1024)return nullptr;void* q=realloc(p,n);if(q)c.memory=c.memory-old+n;return q;}
static Context& ctx(lua_State* L){return *(Context*)lua_touserdata(L,lua_upvalueindex(1));}
static void limit(lua_State* L,lua_Debug*){lua_getfield(L,LUA_REGISTRYINDEX,"mesh");auto c=(Context*)lua_touserdata(L,-1);lua_pop(L,1);if((c->instructions-=1000)<=0)luaL_error(L,"Mesh instruction limit");}
static Vec3 vector(lua_State* L,int idx){Vec3 v;float* out=&v.x;luaL_checktype(L,idx,LUA_TTABLE);for(int i=0;i<3;i++){lua_rawgeti(L,idx,i+1);out[i]=(float)luaL_optnumber(L,-1,0);lua_pop(L,1);if(!std::isfinite(out[i])||std::fabs(out[i])>10000)luaL_error(L,"Invalid vertex");}return v;}
static std::vector<Vec3> profile(lua_State* L,int idx,float z){std::vector<Vec3> p;luaL_checktype(L,idx,LUA_TTABLE);size_t n=lua_rawlen(L,idx);if(n<2||n>128)luaL_error(L,"Invalid profile");for(size_t i=1;i<=n;i++){lua_rawgeti(L,idx,(int)i);auto v=vector(L,lua_gettop(L));v.z=z;p.push_back(v);lua_pop(L,1);}return p;}
static void tri(lua_State* L,Vec3 a,Vec3 b,Vec3 c){auto& f=ctx(L).stack.back();if(f.size()>=8192)luaL_error(L,"Mesh too large");f.push_back({{a,b,c},{.7f,.7f,.7f}});}
static int cap(lua_State* L){auto p=profile(L,2,(float)luaL_checknumber(L,1));if(p.front().x==p.back().x&&p.front().y==p.back().y)p.pop_back();
 // Ear clipping preserves the concave teeth in the original key profile.
 float area=0;for(size_t i=0;i<p.size();i++){auto a=p[i],b=p[(i+1)%p.size()];area+=a.x*b.y-b.x*a.y;}float sign=area<0?-1.f:1.f;
 auto cross=[](Vec3 a,Vec3 b,Vec3 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
 while(p.size()>=3){bool found=false;for(size_t i=0;i<p.size();i++){auto a=p[(i+p.size()-1)%p.size()],b=p[i],c=p[(i+1)%p.size()];if(cross(a,b,c)*sign<=.000001f)continue;bool inside=false;for(size_t j=0;j<p.size();j++){if(j==i||j==(i+1)%p.size()||j==(i+p.size()-1)%p.size())continue;auto q=p[j];if(cross(a,b,q)*sign>=0&&cross(b,c,q)*sign>=0&&cross(c,a,q)*sign>=0){inside=true;break;}}if(inside)continue;if(lua_toboolean(L,lua_upvalueindex(2)))tri(L,c,b,a);else tri(L,a,b,c);p.erase(p.begin()+i);found=true;break;}if(!found)break;}return 0;}
static int extrude(lua_State* L){auto a=profile(L,3,(float)luaL_checknumber(L,1));auto b=profile(L,lua_istable(L,4)?4:3,(float)luaL_checknumber(L,2));
 // Corresponding vertices when profiles match; resample unequal bevel profiles.
 size_t n=std::max(a.size(),b.size());auto at=[](const std::vector<Vec3>& p,size_t i,size_t n){float t=(float)i*(p.size()-1)/(n-1);size_t j=std::min((size_t)t,p.size()-2);float f=t-j;return Vec3{p[j].x+(p[j+1].x-p[j].x)*f,p[j].y+(p[j+1].y-p[j].y)*f,p[j].z};};
 for(size_t i=1;i<n;i++){auto a0=at(a,i-1,n),a1=at(a,i,n),b0=at(b,i-1,n),b1=at(b,i,n);tri(L,a0,a1,b0);tri(L,b0,a1,b1);}return 0;}
static int push(lua_State* L){auto& s=ctx(L).stack;if(s.size()>=32)return luaL_error(L,"Mesh stack limit");s.emplace_back();return 0;}
static int duplicate(lua_State* L){auto& s=ctx(L).stack;if(s.size()>=32)return luaL_error(L,"Mesh stack limit");auto copy=s.back();s.push_back(std::move(copy));return 0;}
static int merge(lua_State* L){auto& s=ctx(L).stack;if(s.size()<2)return luaL_error(L,"Mesh stack underflow");auto top=std::move(s.back());s.pop_back();if(s.back().size()+top.size()>8192)return luaL_error(L,"Mesh too large");s.back().insert(s.back().end(),top.begin(),top.end());return 0;}
static int flip(lua_State* L){for(auto& f:ctx(L).stack.back())std::swap(f.p[0],f.p[2]);return 0;}
static int color(lua_State* L){auto c=vector(L,1);for(auto& f:ctx(L).stack.back())f.color=c;return 0;}
static int transform(lua_State* L){auto v=vector(L,1);bool scale=lua_toboolean(L,lua_upvalueindex(2))!=0;for(auto& f:ctx(L).stack.back())for(auto& p:f.p){if(scale){p.x*=v.x;p.y*=v.y;p.z*=v.z;}else{p.x+=v.x;p.y+=v.y;p.z+=v.z;}}return 0;}
static int rotate(lua_State* L){auto v=vector(L,1);float n=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);if(n<.00001f)return 0;v.x/=n;v.y/=n;v.z/=n;float angle=(float)luaL_checknumber(L,2)*.01745329252f,c=std::cos(angle),s=std::sin(angle);for(auto& f:ctx(L).stack.back())for(auto& p:f.p){float dot=p.x*v.x+p.y*v.y+p.z*v.z;Vec3 q{v.y*p.z-v.z*p.y,v.z*p.x-v.x*p.z,v.x*p.y-v.y*p.x};p={p.x*c+q.x*s+v.x*dot*(1-c),p.y*c+q.y*s+v.y*dot*(1-c),p.z*c+q.z*s+v.z*dot*(1-c)};}return 0;}
static int wind(lua_State* L){for(auto& f:ctx(L).stack.back())for(auto& p:f.p){float a=p.z*.01745329252f;p={p.x*std::cos(a),p.y,p.x*std::sin(a)};}return 0;}
static int mutate(lua_State* L){luaL_checktype(L,1,LUA_TFUNCTION);for(auto& f:ctx(L).stack.back())for(auto& p:f.p){lua_pushvalue(L,1);lua_pushnumber(L,p.x);lua_pushnumber(L,p.y);lua_pushnumber(L,p.z);lua_call(L,3,3);p={(float)luaL_checknumber(L,-3),(float)luaL_checknumber(L,-2),(float)luaL_checknumber(L,-1)};lua_pop(L,3);}return 0;}
static int properties(lua_State* L){auto& e=ctx(L).effect;e.texture=luaL_checkstring(L,1);e.count=std::clamp((int)luaL_checkinteger(L,2),1,128);e.interval=std::clamp((float)luaL_checknumber(L,3),.01f,10.f);return 0;}
static int colorRange(lua_State* L){auto& e=ctx(L).effect;e.colorMin=vector(L,1);e.colorMax=lua_istable(L,2)?vector(L,2):e.colorMin;return 0;}
static int positionRange(lua_State* L){auto& e=ctx(L).effect;e.positionMin=vector(L,1);e.positionMax=lua_istable(L,2)?vector(L,2):e.positionMin;return 0;}
static int velocityRange(lua_State* L){auto& e=ctx(L).effect;e.velocityMin=vector(L,1);e.velocityMax=lua_istable(L,2)?vector(L,2):e.velocityMin;return 0;}
static int range(lua_State* L){auto& e=ctx(L).effect;float a=(float)luaL_checknumber(L,1),b=(float)luaL_optnumber(L,2,a);if(!std::isfinite(a)||!std::isfinite(b))return luaL_error(L,"Invalid particle range");int kind=(int)lua_tointeger(L,lua_upvalueindex(2));if(kind==0){e.sizeMin=a;e.sizeMax=b;}else if(kind==1){e.lifeMin=a;e.lifeMax=b;}else if(kind==2){e.rollMin=a;e.rollMax=b;}else{e.sphereMin=a;e.sphereMax=b;}return 0;}
static int acceleration(lua_State* L){ctx(L).effect.acceleration=vector(L,1);return 0;}
static int target(lua_State* L){auto& e=ctx(L).effect;e.targetSpeed=(float)luaL_checknumber(L,1);if(lua_istable(L,2))e.target=vector(L,2);return 0;}
static int alpha(lua_State* L){auto& e=ctx(L).effect;luaL_checktype(L,1,LUA_TTABLE);for(int i=0;i<4;i++){lua_rawgeti(L,1,i+1);e.envelope[i]=(float)luaL_checknumber(L,-1);lua_pop(L,1);}e.alpha=(float)luaL_checknumber(L,2);return 0;}
static int detach(lua_State*){return 0;} // Each preview emitter stays at its captured object position.
static int easeSize(lua_State* L){auto& e=ctx(L).effect;luaL_checktype(L,1,LUA_TTABLE);for(int i=0;i<4;i++){lua_rawgeti(L,1,i+1);e.sizeEnvelope[i]=(float)luaL_checknumber(L,-1);lua_pop(L,1);}e.sizePeak=(float)luaL_checknumber(L,2);return 0;}
}
Mesh loadMesh(const std::string& root,const std::string& asset,const std::string& entry){Mesh out;
 if(asset.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos||entry.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos){out.error="Invalid asset";return out;}
 auto path=root+"/data/assets/"+asset+".lua";std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size>65536){out.error="Missing or oversized asset";return out;}std::ifstream f(path,std::ios::binary);if(!f){out.error="Missing asset";return out;}std::ostringstream source;source<<f.rdbuf();auto text=source.str();if(text.size()>65536){out.error="Asset too large";return out;}
 Context c;lua_State* L=lua_newstate(alloc,&c);if(!L){out.error="Mesh allocation failed";return out;}lua_pushlightuserdata(L,&c);lua_setfield(L,LUA_REGISTRYINDEX,"mesh");lua_sethook(L,limit,LUA_MASKCOUNT,1000);
 auto bind=[&](const char* name,lua_CFunction fn,bool flag=false){lua_pushlightuserdata(L,&c);lua_pushboolean(L,flag);lua_pushcclosure(L,fn,2);lua_setglobal(L,name);};
 bind("opencap",cap,true);bind("closecap",cap);bind("extrude",extrude);bind("push",push);bind("duplicate",duplicate);bind("merge",merge);bind("flip",flip);bind("setparams",color);bind("transform",transform);bind("scale",transform,true);bind("rotate",rotate);bind("wind",wind);bind("mutate",mutate);
 int result=luaL_loadbufferx(L,text.data(),text.size(),asset.c_str(),"t");if(!result)result=lua_pcall(L,0,0,0);if(!result){lua_getglobal(L,entry.c_str());result=lua_pcall(L,0,0,0);}if(result){const char* e=lua_tostring(L,-1);out.error=e?e:"Mesh failed";}else if(c.stack.size()!=1)out.error="Unbalanced mesh";else out.faces=std::move(c.stack.back());lua_close(L);return out;
}
ParticleEffect loadParticleEffect(const std::string& root,const std::string& asset,const std::string& entry,float strength){Context c;
 if(!std::isfinite(strength)||strength<=0||strength>4){c.effect.error="Invalid particle strength";return c.effect;}
 if(asset.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos||entry.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos){c.effect.error="Invalid particle asset";return c.effect;}
 auto path=root+"/data/assets/"+asset+".lua";std::error_code ec;auto size=std::filesystem::file_size(path,ec);if(ec||size>65536){c.effect.error="Missing or oversized particle script";return c.effect;}std::ifstream file(path,std::ios::binary);std::ostringstream source;source<<file.rdbuf();auto text=source.str();
 lua_State* L=lua_newstate(alloc,&c);if(!L){c.effect.error="Particle allocation failed";return c.effect;}lua_pushlightuserdata(L,&c);lua_setfield(L,LUA_REGISTRYINDEX,"mesh");lua_sethook(L,limit,LUA_MASKCOUNT,1000);
 auto bind=[&](const char* name,lua_CFunction fn,int kind=0){lua_pushlightuserdata(L,&c);lua_pushinteger(L,kind);lua_pushcclosure(L,fn,2);lua_setglobal(L,name);};
 bind("properties",properties);bind("initcolor",colorRange);bind("initsize",range,0);bind("initlifespan",range,1);bind("initroll",range,2);bind("positionsphere",range,3);bind("positionbox",positionRange);bind("velocitybox",velocityRange);bind("velocitytarget",target);bind("movement",acceleration);bind("easealpha",alpha);bind("detach",detach);
 bind("easesize",easeSize);
 int result=luaL_loadbufferx(L,text.data(),text.size(),asset.c_str(),"t");if(!result)result=lua_pcall(L,0,0,0);if(!result){lua_getglobal(L,entry.c_str());lua_pushnumber(L,strength);result=lua_pcall(L,1,0,0);}if(result){const char* e=lua_tostring(L,-1);c.effect.error=e?e:"Particle script failed";}lua_close(L);
 auto& e=c.effect;
 if(e.texture.rfind("particles/",0)!=0||e.texture.substr(10).find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos)e.error="Invalid particle texture";
 if(!std::isfinite(e.alpha)||!std::isfinite(e.targetSpeed)||e.lifeMin<=0||e.lifeMax>10||e.lifeMax<e.lifeMin||e.sizeMin<=0||e.sizeMax>8||e.sizeMax<e.sizeMin)e.error="Invalid particle parameters";
 for(int i=0;i<4;i++)if(!std::isfinite(e.envelope[i])||e.envelope[i]<0||e.envelope[i]>1||(i&&e.envelope[i]<e.envelope[i-1]))e.error="Invalid particle envelope";
 if(!std::isfinite(e.sizePeak)||e.sizePeak<0||e.sizePeak>8)e.error="Invalid particle size";
 for(int i=0;i<4;i++)if(!std::isfinite(e.sizeEnvelope[i])||e.sizeEnvelope[i]<0||e.sizeEnvelope[i]>1||(i&&e.sizeEnvelope[i]<e.sizeEnvelope[i-1]))e.error="Invalid particle size envelope";
 return c.effect;
}
}
