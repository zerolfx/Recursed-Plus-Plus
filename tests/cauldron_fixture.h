#pragma once
#include "exit_fixture.h"
// Authored old-ABI memory for a cauldron's timeline switch: the stack being played, a stack another
// timeline was left with, the players' anchors and the saved global lists. No game executable or
// private data needed.
namespace cauldronfixture {
using namespace exitfixture;
// A map node: left, parent, right, colour, isnil, then the key string at +0x10.
inline void node(Block& n,const Block& head,const std::string& key){word(n.data(),0,ptr(head.data()));word(n.data(),4,ptr(head.data()));word(n.data(),8,ptr(head.data()));string(n.data()+0x10,key);}
// A 20x15 room of 12-byte tiles.
inline void room(Block& r,std::array<int,900>& tiles){word(r.data(),0,20);word(r.data(),4,15);word(r.data(),8,ptr(tiles.data()));word(r.data(),12,ptr(tiles.data()+900));}
}

inline void testCauldronTarget(){
 using namespace cauldronfixture;
 Kind playerKind(".?AVPlayer@@"),chestKind(".?AVChest@@"),cauldronKind(".?AVCauldron@@"),keyKind(".?AVKey@@"),boxKind(".?AVBox@@"),doorKind(".?AVDoor@@");
 Block host{},context{},start{},two{},den{},globalsHead{},denList{},startList{},threadlessList{},timelines{},twoNode{},fourNode{};
 std::array<int,900> startTiles{},twoTiles{},denTiles{};
 room(start,startTiles);room(two,twoTiles);room(den,denTiles);
 // One 20-byte tile definition, with no frames, for the tiles a captured room reads.
 std::array<uint32_t,5> definitions{};
 word(host.data(),0x40,ptr(definitions.data()));word(host.data(),0x44,ptr(definitions.data()+5));
 std::array<unsigned char,28> played{};
 std::array<unsigned char,28*2> twoStack{};
 // Played: start, in the timeline start. Put aside: two, then den through chest x in two.
 string(played.data(),"start");word(played.data(),24,ptr(start.data()));
 string(twoStack.data(),"two");word(twoStack.data(),24,ptr(two.data()));
 string(twoStack.data()+28,"den");word(twoStack.data(),52,ptr(den.data()));
 word(host.data(),4,ptr(context.data()));word(host.data(),0x54,ptr(played.data()));word(host.data(),0x58,ptr(played.data()+played.size()));
 string(host.data()+0x84,"start");
 Entity startPlayer(playerKind,false),startCauldron(cauldronKind,false),box(boxKind,true);
 Entity twoPlayer(playerKind,false),x(chestKind,false);
 Entity denPlayer(playerKind,false),denCauldron(cauldronKind,false),flame(doorKind,false),key(keyKind,true);
 string(startCauldron.bytes.data()+0x4c,"two");string(denCauldron.bytes.data()+0x4c,"start");string(x.bytes.data()+0x4c,"den");
 real(denCauldron.bytes.data(),8,12);real(denCauldron.bytes.data(),12,12.5f);real(flame.bytes.data(),8,3);real(flame.bytes.data(),12,12);
 real(key.bytes.data(),8,6);real(key.bytes.data(),12,12);real(key.bytes.data(),0x28,.3f);real(key.bytes.data(),0x2c,.3f);
 real(box.bytes.data(),8,9);real(box.bytes.data(),12,12);real(box.bytes.data(),0x28,.3f);real(box.bytes.data(),0x2c,.3f);
 // Each player went in through what comes ahead of it: the den player last left through den's cauldron.
 word(twoPlayer.bytes.data(),0x5c,x.id());word(denPlayer.bytes.data(),0x5c,denCauldron.id());
 std::vector<uint32_t> startLive{startCauldron.id(),startPlayer.id()},twoLive{x.id(),twoPlayer.id()},denLive{denCauldron.id(),flame.id(),denPlayer.id()};
 std::vector<uint32_t> denSaved{key.id()},startSaved,threadlessSaved;
 // Saved global lists by room name: den, start and threadless, hanging off the sentinel in order.
 auto set=[&]{
  list(start.data(),0x14,startLive);list(two.data(),0x14,twoLive);list(den.data(),0x14,denLive);
  list(denList.data(),0x28,denSaved);list(startList.data(),0x28,startSaved);list(threadlessList.data(),0x28,threadlessSaved);
 };
 globalsHead[13]=1;word(host.data(),0x6c,ptr(globalsHead.data()));
 word(globalsHead.data(),4,ptr(startList.data()));
 node(denList,globalsHead,"den");node(startList,globalsHead,"start");node(threadlessList,globalsHead,"threadless");
 word(startList.data(),0,ptr(denList.data()));word(startList.data(),8,ptr(threadlessList.data()));
 word(denList.data(),4,ptr(startList.data()));word(threadlessList.data(),4,ptr(startList.data()));
 // Put-aside stacks by timeline: two holds its stack, and four was taken back, leaving an empty one.
 timelines[13]=1;word(host.data(),0x7c,ptr(timelines.data()));word(timelines.data(),4,ptr(fourNode.data()));
 node(fourNode,timelines,"four");node(twoNode,timelines,"two");word(fourNode.data(),8,ptr(twoNode.data()));word(twoNode.data(),4,ptr(fourNode.data()));
 word(twoNode.data(),0x28,ptr(twoStack.data()));word(twoNode.data(),0x2c,ptr(twoStack.data()+twoStack.size()));word(twoNode.data(),0x30,ptr(twoStack.data()+twoStack.size()));
 set();
 const auto h=ptr(host.data()),s=ptr(start.data());
 auto returned=[](const peek::CauldronTarget& t,const char* room,int depth){return t.available&&!t.same&&!t.fresh&&!t.paradox&&t.room==room&&t.depth==depth;};
 auto fresh=[](const peek::CauldronTarget& t,const char* room){return t.available&&t.fresh&&!t.paradox&&t.room==room&&t.depth==0;};
 auto paradox=[](const peek::CauldronTarget& t,const char* room){return t.available&&t.fresh&&t.paradox&&t.room==room&&t.depth==0;};
 // Into the timeline being played, the same stack comes back.
 auto same=peek::readCauldronTarget(h,s,"start");
 assert(same.available&&same.same&&same.room=="start"&&same.depth==0);
 // Its player comes back out after the cauldron it went in through, so a local one is always
 // there; a global one is put aside and taken back with the room's globals, and refused, it is gone.
 assert(peek::readCauldronTarget(h,s,"start",startCauldron.id()).same);
 startCauldron.bytes[0x45]=1;real(startCauldron.bytes.data(),8,10);real(startCauldron.bytes.data(),12,12.5f);real(startCauldron.bytes.data(),0x28,.4f);real(startCauldron.bytes.data(),0x2c,.4f);
 assert(peek::readCauldronTarget(h,s,"start",startCauldron.id()).same);
 startTiles[(12*20+10)*3+1]=1;
 auto home=peek::readCauldronTarget(h,s,"start",startCauldron.id());
 assert(paradox(home,"threadless")&&!home.same&&home.globals.available);
 assert(peek::readCauldronTarget(h,s,"start").same);
 startTiles[(12*20+10)*3+1]=0;startCauldron.bytes[0x45]=0;
 // The room being played takes back what is saved under its name: a leftover an earlier restore
 // refused comes back, and a global of its own that a solid tile now refuses does not.
 startSaved={box.id()};startLive={startCauldron.id(),startPlayer.id(),key.id()};set();
 startTiles[(12*20+6)*3+1]=1;
 auto roundTrip=peek::readCauldronTarget(h,s,"start");
 assert(roundTrip.same&&roundTrip.globals.available&&roundTrip.globals.objects.size()==1&&roundTrip.globals.objects[0].sourceId==box.id());
 startTiles[(12*20+6)*3+1]=0;startSaved.clear();startLive={startCauldron.id(),startPlayer.id()};set();
 // Into two, the room two was left in: den, which takes back the key saved under its name.
 auto back=peek::readCauldronTarget(h,s,"two");
 assert(returned(back,"den",1)&&back.globals.available&&back.globals.initialized);
 assert(back.globals.objects.size()==1&&back.globals.objects[0].sourceId==key.id()&&back.globals.objects[0].x==6);
 // Unless den refuses it where it was saved, or it is in hand.
 denTiles[(12*20+6)*3+1]=1;
 assert(returned(peek::readCauldronTarget(h,s,"two"),"den",1)&&peek::readCauldronTarget(h,s,"two").globals.objects.empty());
 denTiles[(12*20+6)*3+1]=0;
 word(context.data(),4,key.id());
 assert(peek::readCauldronTarget(h,s,"two").globals.objects.empty());
 word(context.data(),4,0);
 // The den player has to find the cauldron it left through ahead of it; behind it, the room is not
 // the player's yet, and the switch ends in a paradox named after what it went in through.
 denLive={flame.id(),denPlayer.id(),denCauldron.id()};set();
 auto lost=peek::readCauldronTarget(h,s,"two");
 assert(paradox(lost,"threadless")&&lost.globals.available&&lost.globals.initialized&&lost.globals.objects.empty());
 word(denPlayer.bytes.data(),0x5c,x.id());denLive={flame.id(),denPlayer.id()};set();
 assert(paradox(peek::readCauldronTarget(h,s,"two"),"reject"));
 // A global cauldron comes back with den's globals, unless another room of that name took it.
 denCauldron.bytes[0x45]=1;word(denPlayer.bytes.data(),0x5c,denCauldron.id());denSaved={denCauldron.id(),key.id()};set();
 auto carried=peek::readCauldronTarget(h,s,"two");
 assert(returned(carried,"den",1)&&carried.globals.objects.size()==2&&carried.globals.objects[0].kind=="cauldron");
 denSaved={key.id()};set();
 assert(paradox(peek::readCauldronTarget(h,s,"two"),"threadless"));
 // In hand, it is still the player's way back.
 word(context.data(),4,denCauldron.id());
 assert(returned(peek::readCauldronTarget(h,s,"two"),"den",1));
 word(context.data(),4,0);
 // The paradox room takes what is saved under its name, not what den took back.
 threadlessSaved={box.id()};set();
 auto rejected=peek::readCauldronTarget(h,s,"two");
 assert(paradox(rejected,"threadless")&&rejected.globals.objects.size()==1&&rejected.globals.objects[0].sourceId==box.id());
 // The room returned to puts aside again what it took back before the paradox room is built, so a
 // room of that name hands its globals on to it.
 string(twoStack.data()+28,"threadless");
 auto handed=peek::readCauldronTarget(h,s,"two");
 assert(paradox(handed,"threadless")&&handed.globals.objects.size()==1&&handed.globals.objects[0].sourceId==box.id());
 string(twoStack.data()+28,"den");
 threadlessSaved.clear();denCauldron.bytes[0x45]=0;denLive={denCauldron.id(),flame.id(),denPlayer.id()};set();
 // A timeline never left gets its first room, named after it, built fresh with no globals saved;
 // one taken back leaves an empty stack, which builds its first room fresh as well.
 auto three=peek::readCauldronTarget(h,s,"three");
 assert(fresh(three,"three")&&three.globals.available&&!three.globals.initialized);
 assert(fresh(peek::readCauldronTarget(h,s,"four"),"four"));
 // A fresh room takes what is saved under its name, and a room of that name being played puts
 // its own globals aside first: the leftovers the last restore refused, then the live ones.
 startSaved={key.id()};startLive={startCauldron.id(),startPlayer.id(),box.id()};set();
 string(played.data(),"four");
 auto four=peek::readCauldronTarget(h,s,"four");
 assert(fresh(four,"four")&&four.globals.initialized&&four.globals.objects.size()==1&&four.globals.objects[0].sourceId==box.id());
 string(played.data(),"start");
 auto fromStart=peek::readCauldronTarget(h,s,"four");
 assert(fresh(fromStart,"four")&&!fromStart.globals.initialized);
 string(twoStack.data()+28,"start");
 auto self=peek::readCauldronTarget(h,s,"two");
 assert(returned(self,"start",1)&&self.globals.objects.size()==2);
 assert(self.globals.objects[0].sourceId==key.id()&&self.globals.objects[1].sourceId==box.id());
 string(twoStack.data()+28,"den");startSaved.clear();startLive={startCauldron.id(),startPlayer.id()};set();
 // The switch reads the stack being played, and only for the room on top of it.
 assert(!peek::readCauldronTarget(h,ptr(den.data()),"two").available);
 assert(!peek::readCauldronTarget(h,s,"").available);
 // The rooms two was left with are read where they were put aside, counted out from its top.
 auto top=peek::readRoomReference(h,s,0,"two"),outer=peek::readRoomReference(h,s,1,"two");
 assert(top.error.empty()&&top.name=="den"&&top.depth==1&&top.room==ptr(den.data()));
 assert(outer.error.empty()&&outer.name=="two"&&outer.depth==0&&outer.room==ptr(two.data()));
 assert(!peek::readRoomReference(h,s,2,"two").error.empty()&&!peek::readRoomReference(h,s,0,"three").error.empty());
 assert(!peek::readRoomReference(h,s,0,"four").error.empty()&&!peek::readRoomReference(h,ptr(den.data()),0,"two").error.empty());
 peek::Snapshot appearance;appearance.timeline="two";
 auto denRoom=peek::readRoomSnapshot(h,s,0,appearance,"two");
 assert(denRoom.error.empty()&&denRoom.live&&denRoom.nativeDepth==1&&denRoom.timeline=="two"&&denRoom.objects.size()==2);
 assert(denRoom.objects[0].kind=="cauldron"&&denRoom.objects[0].target=="start"&&denRoom.objects[1].kind=="player");
 // A flame there walks out of two's stack after the switch: back to two, which takes its globals
 // back, or into reject when two's player finds its chest gone.
 auto out=peek::readExitTarget(h,s,0,0,"two");
 assert(out.available&&!out.paradox&&out.room=="two"&&out.ancestors==1&&out.depth==0&&out.globals.available);
 assert(!peek::readExitTarget(h,s,1,0,"two").available);
 twoLive={twoPlayer.id(),x.id()};set();
 auto chestGone=peek::readExitTarget(h,s,0,0,"two");
 assert(chestGone.available&&chestGone.paradox&&chestGone.room=="reject");
 twoLive={x.id(),twoPlayer.id()};set();
 // No flame of that stack is there to take when the switch itself ends in a paradox.
 denLive={flame.id(),denPlayer.id(),denCauldron.id()};set();
 assert(!peek::readExitTarget(h,s,0,0,"two").available);
 denLive={denCauldron.id(),flame.id(),denPlayer.id()};set();
 // What is held comes along into the room returned to. Put down before a green flame out of it,
 // it is saved under that room's name, and a room below of the same name takes it back.
 string(twoStack.data()+28,"two");word(context.data(),4,key.id());
 auto kept=peek::readExitTarget(h,s,0,1,"two");
 assert(kept.available&&!kept.paradox&&kept.room=="two"&&kept.globals.objects.size()==1&&kept.globals.objects[0].sourceId==key.id());
 assert(peek::readExitTarget(h,s,0,0,"two").globals.objects.empty());
 word(context.data(),4,0);string(twoStack.data()+28,"den");
 // A broken timeline tree is refused, not walked.
 fourNode[13]=1;
 assert(!peek::readCauldronTarget(h,s,"two").available&&!peek::readRoomReference(h,s,0,"two").error.empty());
 fourNode[13]=0;
 assert(returned(peek::readCauldronTarget(h,s,"two"),"den",1));
}
