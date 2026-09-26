#pragma once
#include "snapshot.h"
#include <cmath>

namespace peek {
// Model bounds around the entity position, in tiles, and the underline offset.
struct Reach {float half,above,below,underline;};
inline bool reachOf(const std::string& kind,Reach& out){
 // Bounds include an open chest lid and a jar's handles at their widest rotation.
 if(kind=="chest")out={.6f,.9f,.4f,.43f};
 else if(kind=="jar")out={.8f,.3f,.5f,.53f};
 else if(kind=="cauldron")out={.75f,.6f,.4f,.43f};
 else if(kind=="player"||kind=="yield")out={.5f,.4f,1.f,.98f};
 else return false;
 return true;
}
inline bool opensPreview(const Object& o){return ((o.kind=="chest"||o.kind=="cauldron")&&!o.target.empty())||o.kind=="jar"||o.kind=="player"||o.kind=="yield";}
constexpr float reachMargin=.15f;

// Prefer model bounds to pointer padding, then the closest bounds centre. Identity breaks
// exact ties so a change in draw order cannot switch between coincident live objects.
class PreviewHitTest {
 float pointerX,pointerY,margin;
 bool found=false,body=false;
 float distance=0;
 uintptr_t identity=0;
public:
 PreviewHitTest(float x,float y,float padding=reachMargin):pointerX(x),pointerY(y),margin(padding){}
 bool consider(float x,float y,const Reach& r,uintptr_t id){
  const float dx=pointerX-x,dy=pointerY-y;
  if(!std::isfinite(dx)||!std::isfinite(dy)||std::fabs(dx)>r.half+margin||dy < -r.above-margin||dy>r.below+margin)return false;
  const bool inBody=std::fabs(dx)<=r.half&&dy>=-r.above&&dy<=r.below;
  const float cy=dy-(r.below-r.above)*.5f,d=dx*dx+cy*cy;
  if(found){
   if(inBody!=body){if(!inBody)return false;}
   else if(d>distance||(d==distance&&id>=identity))return false;
  }
  found=true;body=inBody;distance=d;identity=id;return true;
 }
};

inline const Object* previewObjectAt(const std::vector<Object>& objects,float x,float y){
 PreviewHitTest hit(x,y);const Object* selected=nullptr;
 for(size_t i=0;i<objects.size();++i){
  const auto& o=objects[i];Reach r{};
  if(opensPreview(o)&&reachOf(o.kind,r)&&hit.consider(o.x,o.y,r,o.sourceId?o.sourceId:i))selected=&o;
 }
 return selected;
}
}
