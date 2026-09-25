#pragma once
#include "../src/runtime_state.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cmath>
// Authored old-ABI memory fixture. No game executable or private data needed.
inline void testLiveRoomRead(){
 using Bytes=std::array<unsigned char,0xa0>;
 Bytes host{},outside{},inside{},context{},chest{},descriptor{},locator{},vtable{};
 std::array<unsigned char,56> stack{};
 std::array<int,900> tiles{};
 std::array<uint32_t,10> definitions{};
 uint32_t tileFrame=7;
 auto ptr=[](const void* p){return (uint32_t)(uintptr_t)p;};
 auto word=[](void* p,size_t offset,uint32_t v){memcpy((unsigned char*)p+offset,&v,4);};
 auto real=[](void* p,size_t offset,float v){memcpy((unsigned char*)p+offset,&v,4);};
 auto string=[&](void* p,const char* s){auto n=strlen(s);assert(n<16);memcpy(p,s,n+1);word(p,16,(uint32_t)n);word(p,20,15);};
 string(host.data()+8,"tiles/test");string(host.data()+0x20,"test");
 word(host.data(),4,ptr(context.data()));word(host.data(),0x54,ptr(stack.data()));word(host.data(),0x58,ptr(stack.data()+56));
 word(host.data(),0x40,ptr(definitions.data()));word(host.data(),0x44,ptr(definitions.data()+10));
 definitions[5]=1;definitions[6]=ptr(&tileFrame);definitions[7]=definitions[8]=ptr(&tileFrame+1);
 string(stack.data(),"outside");word(stack.data(),24,ptr(outside.data()));string(stack.data()+28,"inside");word(stack.data(),52,ptr(inside.data()));
 word(outside.data(),0,20);word(outside.data(),4,15);word(outside.data(),8,ptr(tiles.data()));word(outside.data(),12,ptr(tiles.data()+900));
 for(int x=0;x<20;x++){tiles[(13*20+x)*3]=1;tiles[(13*20+x)*3+1]=1;}
 // RTTI resolves through vtable[-1] -> locator + 12 -> descriptor + 8.
 memcpy(descriptor.data()+8,".?AVChest@@",12);word(locator.data(),12,ptr(descriptor.data()));word(vtable.data(),0,ptr(locator.data()));word(chest.data(),0,ptr(vtable.data()+4));
 real(chest.data(),8,9.f);real(chest.data(),12,12.6f);string(chest.data()+0x4c,"inside");
 uint32_t entity=ptr(chest.data());word(outside.data(),0x14,ptr(&entity));word(outside.data(),0x18,ptr(&entity+1));word(outside.data(),0x1c,ptr(&entity+1));
 auto reference=peek::readRoomReference(ptr(host.data()),ptr(inside.data()),1);
 assert(reference.error.empty()&&reference.name=="outside"&&reference.depth==0&&reference.room==ptr(outside.data()));
 peek::Snapshot appearance;appearance.error="A missing script must not erase a live room";
 auto live=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(live.error.empty()&&live.live&&live.nativeDepth==0&&live.objects.size()==1);
 assert(live.objects[0].x==9.f&&live.objects[0].target=="inside"&&live.objects[0].sourceId==entity);
 assert(live.tiles[13*20].nativeIndex==1&&live.tiles[13*20].frame==7&&live.tiles[13*20].kind==1);
 real(chest.data(),8,11.f);
 auto moved=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(moved.error.empty()&&moved.objects[0].x==11.f); // Resample actual state, not a template.
 assert(live.objects[0].x==9.f); // Prior captured frames remain immutable.
 tiles[0]=1;tiles[1]=3;
 auto flooded=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(flooded.error.empty()&&peek::wetAt(flooded,0,0));
 word(context.data(),4,entity);
 auto held=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(held.error.empty()&&held.objects.empty());
 word(context.data(),4,0);chest[0x44]=1;
 auto destroyed=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(destroyed.error.empty()&&destroyed.objects.empty());
 chest[0x44]=0;
 // Jar identities map to preserved instances, with names unrelated to the jar key.
 Bytes jarHead{},jarNode{},globalHead{},globalNode{},globalBox{},boxDescriptor{},boxLocator{},boxVtable{};
 word(host.data(),0x74,ptr(jarHead.data()));word(jarHead.data(),4,ptr(jarNode.data()));
 word(jarNode.data(),0,ptr(jarHead.data()));word(jarNode.data(),8,ptr(jarHead.data()));
 string(jarNode.data()+0x10,"jar-7");string(jarNode.data()+0x28,"kept");word(jarNode.data(),0x40,ptr(outside.data()));
 word(host.data(),0x6c,ptr(globalHead.data()));word(globalHead.data(),4,ptr(globalNode.data()));
 word(globalNode.data(),0,ptr(globalHead.data()));word(globalNode.data(),8,ptr(globalHead.data()));string(globalNode.data()+0x10,"kept");
 memcpy(boxDescriptor.data()+8,".?AVBox@@",10);word(boxLocator.data(),12,ptr(boxDescriptor.data()));word(boxVtable.data(),0,ptr(boxLocator.data()));word(globalBox.data(),0,ptr(boxVtable.data()+4));
 globalBox[0x45]=1;real(globalBox.data(),8,6.f);real(globalBox.data(),12,12.f);real(globalBox.data(),0x28,.3f);real(globalBox.data(),0x2c,.3f);
 uint32_t boxId=ptr(globalBox.data());word(globalNode.data(),0x28,ptr(&boxId));word(globalNode.data(),0x2c,ptr(&boxId+1));word(globalNode.data(),0x30,ptr(&boxId+1));
 auto jar=peek::readJarReference(ptr(host.data()),ptr(inside.data()),"jar-7");
 assert(jar.error.empty()&&jar.name=="kept"&&jar.room==ptr(outside.data())&&jar.depth==2);
 auto savedJar=peek::readJarSnapshot(ptr(host.data()),ptr(inside.data()),"jar-7",appearance);
 assert(savedJar.error.empty()&&savedJar.live&&savedJar.objects.size()==2&&savedJar.objects[0].x==11.f);
 assert(savedJar.objects[1].sourceId==boxId&&savedJar.objects[1].settle&&savedJar.hasGlobals);
 assert(savedJar.tiles[0].kind==3); // Stored water is independent of the jar's surroundings.
 real(chest.data(),8,7.f);
 auto changedJar=peek::readJarSnapshot(ptr(host.data()),ptr(inside.data()),"jar-7",appearance);
 assert(changedJar.error.empty()&&changedJar.objects[0].x==7.f&&savedJar.objects[0].x==11.f);
 real(globalBox.data(),12,13.5f);
 auto blockedJar=peek::readJarSnapshot(ptr(host.data()),ptr(inside.data()),"jar-7",appearance);
 assert(blockedJar.error.empty()&&blockedJar.objects.size()==1); // A global inside solid tiles stays saved.
 real(globalBox.data(),12,12.f);word(context.data(),4,boxId);
 assert(peek::readJarSnapshot(ptr(host.data()),ptr(inside.data()),"jar-7",appearance).objects.size()==1);
 word(context.data(),4,0);globalBox[0x44]=1;
 assert(peek::readJarSnapshot(ptr(host.data()),ptr(inside.data()),"jar-7",appearance).objects.size()==1);
 globalBox[0x44]=0;
 auto unused=peek::readJarReference(ptr(host.data()),ptr(inside.data()),"unused");
 assert(unused.error.empty()&&!unused.room&&unused.name=="glitch");
 word(jarHead.data(),4,ptr(jarHead.data())); // Consuming the jar removes the saved node.
 assert(!peek::readJarReference(ptr(host.data()),ptr(inside.data()),"jar-7").room);
 auto consumed=peek::readJarSnapshot(ptr(host.data()),ptr(inside.data()),"jar-7",appearance);
 assert(!consumed.error.empty()&&consumed.objects.empty());
 word(jarHead.data(),4,ptr(jarNode.data()));word(jarNode.data(),0x40,0);
 assert(!peek::readJarReference(ptr(host.data()),ptr(inside.data()),"jar-7").error.empty());
 assert(!peek::readJarReference(ptr(host.data()),ptr(outside.data()),"jar-7").error.empty());
 word(jarNode.data(),0x40,ptr(outside.data()));word(jarNode.data(),0,ptr(jarNode.data()));
 assert(!peek::readJarReference(ptr(host.data()),ptr(inside.data()),"aaa").error.empty());
 memcpy(descriptor.data()+8,".?AVDoor@@",11);chest[0x58]=0;
 auto red=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(red.error.empty()&&red.objects.size()==1&&red.objects[0].kind=="player");
 chest[0x58]=1;
 auto green=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(green.error.empty()&&green.objects[0].kind=="yield");
 chest[0x59]=1;
 auto spentFlame=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(spentFlame.error.empty()&&spentFlame.objects.empty());
 // The flame that sealed a jar is still in the saved vector, but disappears on re-entry.
 word(jarNode.data(),0,ptr(jarHead.data()));
 auto sealed=peek::readJarSnapshot(ptr(host.data()),ptr(inside.data()),"jar-7",appearance);
 assert(sealed.error.empty()&&sealed.objects.size()==1&&sealed.objects[0].kind=="box");
 chest[0x59]=0;
 // Record stores its voice-clip path where Chest stores its destination room.
 memcpy(descriptor.data()+8,".?AVRecord@@",13);
 auto record=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(record.error.empty()&&record.objects.size()==1&&record.objects[0].kind=="record"&&record.objects[0].target=="inside");
 // Cauldron destinations and jar identities share the same string offset.
 for(const char* rtti:{".?AVCauldron@@",".?AVJar@@"}){
  memcpy(descriptor.data()+8,rtti,strlen(rtti)+1);
  auto vessel=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
  assert(vessel.error.empty()&&vessel.objects.size()==1&&vessel.objects[0].target=="inside");
 }
 word(chest.data(),0x64,2);
 auto spentJar=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(spentJar.error.empty()&&spentJar.objects.empty());
 word(chest.data(),0x64,1);
 // A fizzer is an invisible transient controller: it must be skipped, not reported.
 memcpy(descriptor.data()+8,".?AVFizzer@@",13);
 auto fizzer=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(fizzer.error.empty()&&fizzer.objects.empty());
 memcpy(descriptor.data()+8,".?AVDoor@@",11);
 assert(!peek::readRoomReference(ptr(host.data()),ptr(outside.data()),1).error.empty());
 assert(!peek::readRoomReference(ptr(host.data()),ptr(inside.data()),2).error.empty());
 tiles[0]=1000;
 auto invalid=peek::readRoomSnapshot(ptr(host.data()),ptr(inside.data()),1,appearance);
 assert(!invalid.error.empty()&&invalid.objects.empty());
}
