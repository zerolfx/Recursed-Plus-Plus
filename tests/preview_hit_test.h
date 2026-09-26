#pragma once
#include "../src/preview_hit.h"
#include <algorithm>
#include <cassert>
#include <limits>

inline void testPreviewHit(){
 using namespace peek;
 // The crowded jars reported in Athens: both nearly coincident jars must be selectable
 // at their own centres, whatever order the game drew them in.
 std::vector<Object> jars={
  {"jar","jar-1",15.f,13.5f,false,1},
  {"jar","jar-2",16.27f,13.5f,false,2},
  {"jar","jar-3",18.49f,13.5f,false,3},
  {"jar","jar-4",17.31f,13.5f,false,4},
  {"jar","jar-5",17.42f,13.5f,false,5}};
 do {
  assert(previewObjectAt(jars,17.31f,13.5f)->sourceId==4);
  assert(previewObjectAt(jars,17.42f,13.5f)->sourceId==5);
  assert(previewObjectAt(jars,18.f,13.5f)->sourceId==3);
  // Main-window padding at the reported 2560 x 1920 resolution uses the same ranking.
  PreviewHitTest hit(17.31f,13.5f,.16f);uintptr_t selected=0;
  for(const auto& o:jars){Reach r{};reachOf(o.kind,r);if(hit.consider(o.x,o.y,r,o.sourceId))selected=o.sourceId;}
  assert(selected==4);
 }while(std::next_permutation(jars.begin(),jars.end(),[](const Object& a,const Object& b){return a.sourceId<b.sourceId;}));
 // A chest lid takes priority over a closer jar's padding. Bounds are asymmetric vertically.
 std::vector<Object> mixed={{"jar","a",0,0,false,1},{"chest","b",0,.5f,false,2}};
 assert(previewObjectAt(mixed,0,-.39f)->sourceId==2);
 std::reverse(mixed.begin(),mixed.end());
 assert(previewObjectAt(mixed,0,-.39f)->sourceId==2);
 // Padding still works in empty space, but the rest of the room is not clickable.
 std::vector<Object> single={{"jar","a",0,0,false,1}};
 assert(previewObjectAt(single,.9f,0));
 assert(!previewObjectAt(single,.96f,0));
 assert(!previewObjectAt(single,0,.66f));
 assert(!previewObjectAt(single,0,-.46f));
 assert(!previewObjectAt(single,std::numeric_limits<float>::quiet_NaN(),0));
 assert(!previewObjectAt({},0,0));
 // Stable live identities break exact ties rather than the order of the objects.
 std::vector<Object> tied={{"jar","a",0,0,false,20},{"jar","b",0,0,false,10}};
 assert(previewObjectAt(tied,0,0)->sourceId==10);
 std::reverse(tied.begin(),tied.end());
 assert(previewObjectAt(tied,0,0)->sourceId==10);
 // Non-openable props and empty chest targets cannot steal a hit.
 std::vector<Object> props={{"key","",0,0},{"chest","",0,0},{"cauldron","",0,0},{"jar","",.2f,0,false,4}};
 assert(previewObjectAt(props,0,0)->sourceId==4);
 for(const char* kind:{"chest","jar","cauldron","player","yield"}){
  std::vector<Object> object={{kind,"target",2,3}};
  assert(previewObjectAt(object,2,3));
 }
}
