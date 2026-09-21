#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "room_art.h"
#include "asset_mesh.h"
#include "particle_sim.h"
#include <map>
#include <memory>
#include <algorithm>
#include <cmath>
#include <sstream>
namespace peek { namespace {
using namespace Gdiplus;
static ULONG_PTR token=0;
static std::map<std::string,std::unique_ptr<Bitmap>> images;
static std::map<std::string,Mesh> meshes;
static Bitmap* bitmap(const std::string& path){auto i=images.find(path);if(i!=images.end())return i->second.get();std::wstring wide(path.begin(),path.end());auto b=std::make_unique<Bitmap>(wide.c_str());if(b->GetLastStatus()!=Ok||b->GetWidth()>4096||b->GetHeight()>4096)b.reset();auto* p=b.get();images.emplace(path,std::move(b));return p;}
static Color rgb(Vec3 c,float shade=1){auto channel=[&](float n){return (BYTE)std::clamp(n*shade*255.f,0.f,255.f);};return Color(255,channel(c.x),channel(c.y),channel(c.z));}
static bool drawLock(Graphics& g,const std::string& root,const Object& o,float cell){
 auto* sprite=bitmap(root+"/data/sprites/lock.png");if(!sprite)return false;
 // sprites/lock.lua: five horizontal frames; sequence ends on closed frame 0.
 float width=sprite->GetWidth()/5.f,height=(float)sprite->GetHeight();if(width<1||height<1)return false;
 ImageAttributes attrs;attrs.SetColorKey(Color(255,255,0,255),Color(255,255,0,255));
 g.DrawImage(sprite,RectF(o.x*cell-width*cell/32,o.y*cell-height*cell/32,width*cell/16,height*cell/16),0,0,width,height,UnitPixel,&attrs);return true;
}
static std::string signature(const std::string& root,const Snapshot& s){std::ostringstream k;k<<root<<'|'<<s.tileset<<'|'<<s.pattern<<'|'<<s.error;for(auto c:s.dark)k<<'|'<<c;for(auto c:s.light)k<<'|'<<c;for(auto t:s.tiles)k<<','<<t.kind<<':'<<t.frame;for(auto& o:s.objects)k<<'|'<<o.kind<<':'<<o.target<<':'<<o.x<<':'<<o.y<<':'<<o.global;return k.str();}
static bool drawMesh(Graphics& g,RoomArt& art,const std::string& root,const Object& o,float cell){
 std::string asset=o.kind,entry=o.kind;if(o.kind=="jar"){asset="yield";entry="jar";}if(o.kind!="chest"&&o.kind!="box"&&o.kind!="key"&&o.kind!="crystal"&&o.kind!="jar"&&o.kind!="cauldron")return false;
 auto key=root+"/"+asset+"/"+entry;auto i=meshes.find(key);if(i==meshes.end())i=meshes.emplace(key,loadMesh(root,asset,entry)).first;if(!i->second.error.empty()||i->second.faces.empty())return false;
 struct Projected {PointF p[3];Color color;float depth[3];};std::vector<Projected> faces;
 for(const auto& f:i->second.faces){Projected p{};Vec3 v[3];for(int j=0;j<3;j++){auto a=f.p[j];v[j]={a.x*.96f+a.z*.28f,a.y,-a.x*.28f+a.z*.96f};p.p[j]={cell*(o.x+v[j].x),cell*(o.y+((o.kind=="chest"||o.kind=="jar"||o.kind=="cauldron")?.5f:0)-v[j].y+v[j].z*.20f)};p.depth[j]=v[j].z+v[j].y*.20f;}
 auto a=Vec3{v[1].x-v[0].x,v[1].y-v[0].y,v[1].z-v[0].z},b=Vec3{v[2].x-v[0].x,v[2].y-v[0].y,v[2].z-v[0].z};Vec3 n{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};float len=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);if(len<.000001f)continue;
 // A fixed inspection camera/light, separate from the game's animated shader.
 if(n.z+n.y*.20f<0)continue;
 float shade=.60f+.40f*std::max(0.f,(-.35f*n.x+.65f*n.y+.68f*n.z)/len);p.color=rgb(f.color,shade);faces.push_back(p);}
 // A depth buffer keeps the small gold latch in front of the chest face.
 // Sorting whole triangles by average depth incorrectly hides that detail.
 g.Flush(FlushIntentionSync);std::vector<float> depth(art.width*art.height,-1e30f);
 for(const auto& f:faces){auto a=f.p[0],b=f.p[1],c=f.p[2];float d=(b.Y-c.Y)*(a.X-c.X)+(c.X-b.X)*(a.Y-c.Y);if(std::fabs(d)<.00001f)continue;
  int left=std::max(0,(int)std::floor(std::min({a.X,b.X,c.X}))),right=std::min(art.width,(int)std::ceil(std::max({a.X,b.X,c.X}))),top=std::max(0,(int)std::floor(std::min({a.Y,b.Y,c.Y}))),bottom=std::min(art.height,(int)std::ceil(std::max({a.Y,b.Y,c.Y})));
  for(int y=top;y<bottom;y++)for(int x=left;x<right;x++){float u=((b.Y-c.Y)*(x+.5f-c.X)+(c.X-b.X)*(y+.5f-c.Y))/d,v=((c.Y-a.Y)*(x+.5f-c.X)+(a.X-c.X)*(y+.5f-c.Y))/d,w=1-u-v;if(u<0||v<0||w<0)continue;float z=u*f.depth[0]+v*f.depth[1]+w*f.depth[2];int at=y*art.width+x;if(z>=depth[at]){depth[at]=z;art.pixels[at]=f.color.GetValue();}}
 }return true;
}
struct Mask {int w=0,h=0;std::vector<float> alpha;};
static std::map<std::string,Mask> masks;
static std::map<std::string,ParticleEffect> effects;
static const Mask& mask(const std::string& path){auto i=masks.find(path);if(i!=masks.end())return i->second;Mask m;if(auto* b=bitmap(path)){m.w=b->GetWidth();m.h=b->GetHeight();m.alpha.resize(m.w*m.h);for(int y=0;y<m.h;y++)for(int x=0;x<m.w;x++){Color c;b->GetPixel(x,y,&c);m.alpha[y*m.w+x]=(c.GetA()/255.f)*(std::max({c.GetR(),c.GetG(),c.GetB()})/255.f);}}return masks.emplace(path,std::move(m)).first->second;}
static void addPixel(RoomArt& art,int x,int y,Vec3 color,float a){if(x<0||y<0||x>=art.width||y>=art.height)return;auto& p=art.pixels[y*art.width+x];auto c=[&](int shift,float v){return (uint32_t)std::clamp(((p>>shift)&255)+v*a*255.f,0.f,255.f);};p=0xff000000u|(c(16,color.x)<<16)|(c(8,color.y)<<8)|c(0,color.z);}
static void drawEffects(RoomArt& art,const std::string& root,const Snapshot& snapshot,double seconds){float cell=art.width/20.f;
 // Use the game's crest/ripple textures and water.lua size envelopes on exposed water.
 for(int y=1;y<15;y++)for(int x=0;x<20;x++)if(snapshot.tiles[y*20+x].kind==3&&snapshot.tiles[(y-1)*20+x].kind==0){
  for(const char* entry:{"over","under"}){auto key=root+"/water/"+entry;auto it=effects.find(key);if(it==effects.end())it=effects.emplace(key,loadParticleEffect(root,"water",entry)).first;const auto& e=it->second;const auto& sprite=mask(root+"/data/"+e.texture+".png");if(sprite.alpha.empty())continue;
   for(const auto& p:sampleParticles(e,seconds,(y*20+x)*1597334677u+(entry[0]=='o'?17:91))){float size=p.size*cell;if(size<1)continue;float cx=cell*(x+p.position.x),cy=cell*(y-p.position.y);
    for(int py=std::max(0,(int)(cy-size/2));py<std::min(art.height,(int)(cy+size/2+1));py++)for(int px=std::max(0,(int)(cx-size/2));px<std::min(art.width,(int)(cx+size/2+1));px++){float u=(px+.5f-cx)/size+.5f,v=(py+.5f-cy)/size+.5f;if(u<0||u>=1||v<0||v>=1)continue;int tx=std::clamp((int)(px/cell),0,19),ty=std::clamp((int)(py/cell),0,14);if(snapshot.tiles[ty*20+tx].kind==1)continue;float a=sprite.alpha[(int)(v*sprite.h)*sprite.w+(int)(u*sprite.w)]*p.alpha;addPixel(art,px,py,p.color,a);}
   }
  }
 }
 for(size_t i=0;i<snapshot.objects.size();i++){const auto& o=snapshot.objects[i];if(o.x<-4||o.x>24||o.y<-8||o.y>23)continue;if(o.kind!="chest"&&o.kind!="key")continue;auto key=root+"/"+o.kind;auto it=effects.find(key);if(it==effects.end())it=effects.emplace(key,loadParticleEffect(root,o.kind,"glow")).first;const auto& e=it->second;if(!e.error.empty())continue;const auto& sprite=mask(root+"/data/"+e.texture+".png");if(sprite.alpha.empty())continue;
  // Subtle local bloom approximates the native glow pass without obscuring geometry.
  if(o.kind=="chest"){float cx=o.x*cell,cy=(o.y-.4f)*cell,r=cell*.85f,pulse=.045f+.015f*std::sin((float)seconds*2.1f+o.x);for(int y=(int)(cy-r);y<cy+r;y++)for(int x=(int)(cx-r);x<cx+r;x++){float d=((x-cx)*(x-cx)+(y-cy)*(y-cy))/(r*r);if(d<1)addPixel(art,x,y,{1,.04f,.55f},pulse*(1-d)*(1-d));}}
  auto particles=sampleParticles(e,seconds,(uint32_t)(i+1)*1597334677u+(uint32_t)(std::fabs(o.x)*1024));
  for(const auto& p:particles){float cx=cell*(o.x+p.position.x*.96f+p.position.z*.28f),cy=cell*(o.y+(o.kind=="chest"?.5f:0)-p.position.y+p.position.z*.2f);float size=std::max(1.f,p.size*cell),half=size*.71f;float sn=std::sin(p.angle),cs=std::cos(p.angle);
   int left=std::max(0,(int)(cx-half)),right=std::min(art.width,(int)(cx+half+1)),top=std::max(0,(int)(cy-half)),bottom=std::min(art.height,(int)(cy+half+1));
   for(int y=top;y<bottom;y++)for(int x=left;x<right;x++){float dx=(x+.5f-cx)/size,dy=(y+.5f-cy)/size;float u=dx*cs-dy*sn+.5f,v=dx*sn+dy*cs+.5f;if(u>=0&&u<1&&v>=0&&v<1){float a=sprite.alpha[(int)(v*sprite.h)*sprite.w+(int)(u*sprite.w)]*p.alpha;addPixel(art,x,y,p.color,a);}}
  }
 }
}
}
const RoomArt& renderRoomArt(const std::string& root,const Snapshot& s,double seconds){static RoomArt out;static std::vector<uint32_t> basePixels;static std::string old;static uint64_t lastTick=0;
 if(seconds<0)seconds=GetTickCount64()/1000.0;uint64_t tick=(uint64_t)(seconds*30);auto key=signature(root,s);bool changed=old!=key||out.pixels.empty();bool animated=std::any_of(s.objects.begin(),s.objects.end(),[](const Object& o){return o.kind=="chest"||o.kind=="key";})||std::any_of(s.tiles.begin(),s.tiles.end(),[](const Tile& t){return t.kind==3;});if(!changed&&(!animated||tick==lastTick))return out;lastTick=tick;
 if(changed){old=key;
 if(!token){GdiplusStartupInput input;if(GdiplusStartup(&token,&input,nullptr)!=Ok){out.note="Image renderer unavailable";return out;}}
 out.pixels.assign(out.width*out.height,0xff202038);out.note="Original textures, meshes and particle assets; lighting/motion approximate";
 Bitmap canvas(out.width,out.height,out.width*4,PixelFormat32bppARGB,(BYTE*)out.pixels.data());Graphics g(&canvas);g.SetCompositingMode(CompositingModeSourceOver);g.SetInterpolationMode(InterpolationModeNearestNeighbor);g.SetPixelOffsetMode(PixelOffsetModeHalf);
 g.Clear(rgb({s.dark[0],s.dark[1],s.dark[2]}));float cell=(float)out.width/20;
 // Background sheets contain square pattern frames; use the geometric frame when available.
 if(!s.pattern.empty())if(auto* bg=bitmap(root+"/data/"+s.pattern+".png")){ColorMatrix matrix{};for(int j=0;j<3;j++){matrix.m[0][j]=s.light[j]-s.dark[j];matrix.m[4][j]=s.dark[j];}matrix.m[3][3]=matrix.m[4][4]=1;ImageAttributes tint;tint.SetColorMatrix(&matrix);int n=(int)std::min(bg->GetWidth(),bg->GetHeight());for(int y=0;y<15;y+=2)for(int x=0;x<20;x+=2)g.DrawImage(bg,RectF(x*cell,y*cell,2*cell,2*cell),0,(REAL)(bg->GetHeight()>=2*bg->GetWidth()?n:0),(REAL)n,(REAL)n,UnitPixel,&tint);}
 auto* sheet=bitmap(root+"/data/"+s.tileset+".png");ImageAttributes tileAttrs;tileAttrs.SetColorKey(Color(255,255,0,255),Color(255,255,0,255));
 for(int y=0;y<15;y++)for(int x=0;x<20;x++){auto t=s.tiles[y*20+x];if(t.frame>0&&sheet&&sheet->GetWidth()>=16){int cols=sheet->GetWidth()/16,tx=(t.frame%cols)*16,ty=(t.frame/cols)*16;if(ty+16<=(int)sheet->GetHeight()){g.DrawImage(sheet,RectF(x*cell,y*cell,cell,cell),(REAL)tx,(REAL)ty,16.f,16.f,UnitPixel,&tileAttrs);continue;}}
  if(t.kind){SolidBrush brush(t.kind==3?Color(180,45,140,200):t.kind==4?Color(180,130,190,50):Color(255,120,120,155));g.FillRectangle(&brush,x*cell,y*cell,cell,t.kind==2?cell*.15f:cell);}}
 g.SetSmoothingMode(SmoothingModeNone);int fallback=0;
 for(const auto& o:s.objects){if(o.kind=="bird")continue;float x=o.x*cell,y=o.y*cell;
  if(!drawMesh(g,out,root,o,cell)&&!(o.kind=="lock"&&drawLock(g,root,o,cell))){fallback++;SolidBrush brush(Color(255,220,210,220));if(o.kind=="player"||o.kind=="yield"){SolidBrush door(Color(130,255,95,210));g.FillEllipse(&door,x-cell*.38f,y-cell*.65f,cell*.76f,cell*1.1f);Pen rim(Color(255,255,145,224),2);g.DrawEllipse(&rim,x-cell*.38f,y-cell*.65f,cell*.76f,cell*1.1f);}else {g.FillEllipse(&brush,x-cell*.2f,y-cell*.2f,cell*.4f,cell*.4f);}}
  if(o.global){Pen global(Color(255,75,246,127),2);g.DrawRectangle(&global,x-cell*.62f,y-cell*.66f,cell*1.24f,cell*1.26f);}
 }
 if(fallback)out.note+="; "+std::to_string(fallback)+" schematic objects";
 g.Flush(FlushIntentionSync);basePixels=out.pixels;}
 out.pixels=basePixels;if(animated)drawEffects(out,root,s,seconds);out.revision++;return out;
}
}
