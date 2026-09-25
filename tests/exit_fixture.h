#pragma once
#include "../src/runtime_state.h"
#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
// Authored old-ABI memory for walking out through a flame: a room stack, the rooms' entities, the
// players' anchors and the saved global lists. No game executable or private data needed.
namespace exitfixture {
using Block=std::array<unsigned char,0xa0>;
inline uint32_t ptr(const void* p){return (uint32_t)(uintptr_t)p;}
inline void word(void* p,size_t offset,uint32_t v){memcpy((unsigned char*)p+offset,&v,4);}
inline void real(void* p,size_t offset,float v){memcpy((unsigned char*)p+offset,&v,4);}
inline void string(void* p,const std::string& s){assert(s.size()<16);memset(p,0,24);memcpy(p,s.data(),s.size());word(p,16,(uint32_t)s.size());word(p,20,15);}
// begin, end and capacity of a vector<Entity*>.
inline void list(void* p,size_t offset,const std::vector<uint32_t>& v){word(p,offset,ptr(v.data()));word(p,offset+4,ptr(v.data()+v.size()));word(p,offset+8,ptr(v.data()+v.size()));}
// RTTI resolves through vtable[-1] -> locator + 12 -> descriptor + 8.
struct Kind {
 Block descriptor{},locator{},vtable{};
 explicit Kind(const char* rtti){memcpy(descriptor.data()+8,rtti,strlen(rtti)+1);word(locator.data(),12,ptr(descriptor.data()));word(vtable.data(),0,ptr(locator.data()));}
};
struct Entity {
 Block bytes{};
 Entity(const Kind& kind,bool global){word(bytes.data(),0,ptr(kind.vtable.data()+4));bytes[0x45]=global;}
 uint32_t id()const{return ptr(bytes.data());}
};
}

inline void testExitTarget(){
 using namespace exitfixture;
 Kind playerKind(".?AVPlayer@@"),chestKind(".?AVChest@@"),cauldronKind(".?AVCauldron@@"),keyKind(".?AVKey@@"),lockKind(".?AVLock@@");
 Block host{},context{},garden{},porch{},tower{},head{},porchNode{},rejectNode{};
 std::array<unsigned char,28*3> stack{};
 // Rooms a walk out returns to have a 20x15 grid of 12-byte tiles: index, kind, and one more word.
 std::array<int,900> gardenTiles{},porchTiles{};
 for(auto* room:{&garden,&porch}){word(room->data(),0,20);word(room->data(),4,15);}
 word(garden.data(),8,ptr(gardenTiles.data()));word(garden.data(),12,ptr(gardenTiles.data()+900));
 word(porch.data(),8,ptr(porchTiles.data()));word(porch.data(),12,ptr(porchTiles.data()+900));
 // garden, entered first; porch through chest x in garden; tower through chest a in porch.
 Entity gardenPlayer(playerKind,false),porchPlayer(playerKind,false),towerPlayer(playerKind,false);
 Entity x(chestKind,false),a(chestKind,true),cauldron(cauldronKind,false),key(keyKind,true),lock(lockKind,false);
 // A locked lock blocks (+0x34 bit 8) over a box half a tile wide and one and a half high.
 word(lock.bytes.data(),0x34,8);real(lock.bytes.data(),8,9);real(lock.bytes.data(),12,12.5f);real(lock.bytes.data(),0x28,.5f);real(lock.bytes.data(),0x2c,1.5f);
 string(a.bytes.data()+0x4c,"tower");string(x.bytes.data()+0x4c,"porch");
 real(key.bytes.data(),8,3);real(key.bytes.data(),12,4);
 word(gardenPlayer.bytes.data(),0x5c,x.id());word(porchPlayer.bytes.data(),0x5c,a.id());
 // A player comes back into its room after the chest it went into, so the chest is ahead of it.
 std::vector<uint32_t> gardenLive{x.id(),gardenPlayer.id()},porchLive{porchPlayer.id()},towerLive{towerPlayer.id()},porchSaved,rejectSaved{key.id()};
 auto set=[&]{
  list(garden.data(),0x14,gardenLive);list(porch.data(),0x14,porchLive);list(tower.data(),0x14,towerLive);
  list(porchNode.data(),0x28,porchSaved);list(rejectNode.data(),0x28,rejectSaved);
 };
 string(stack.data(),"garden");word(stack.data(),24,ptr(garden.data()));
 string(stack.data()+28,"porch");word(stack.data(),52,ptr(porch.data()));
 string(stack.data()+56,"tower");word(stack.data(),80,ptr(tower.data()));
 word(host.data(),4,ptr(context.data()));word(host.data(),0x54,ptr(stack.data()));word(host.data(),0x58,ptr(stack.data()+stack.size()));
 string(host.data()+0x84,"start");
 // Saved lists: a sorted chain, porch then reject, hanging off the sentinel.
 head[13]=1;word(head.data(),0,ptr(porchNode.data()));word(head.data(),4,ptr(porchNode.data()));word(head.data(),8,ptr(rejectNode.data()));
 word(host.data(),0x6c,ptr(head.data()));
 word(porchNode.data(),0,ptr(head.data()));word(porchNode.data(),4,ptr(head.data()));word(porchNode.data(),8,ptr(rejectNode.data()));string(porchNode.data()+0x10,"porch");
 word(rejectNode.data(),0,ptr(head.data()));word(rejectNode.data(),4,ptr(porchNode.data()));word(rejectNode.data(),8,ptr(head.data()));string(rejectNode.data()+0x10,"reject");
 const auto h=ptr(host.data()),t=ptr(tower.data());
 assert(peek::readTimeline(h)=="start");
 auto parent=[](const peek::ExitTarget& e,const char* room,int ancestors){return e.available&&!e.paradox&&e.room==room&&e.ancestors==ancestors;};
 auto paradox=[](const peek::ExitTarget& e,const char* room){return e.available&&e.paradox&&e.room==room;};
 // The chest the porch player went in through is still in the porch.
 porchLive={a.id(),porchPlayer.id()};set();
 auto stayed=peek::readExitTarget(h,t,0,0);
 assert(parent(stayed,"porch",1)&&stayed.globals.objects.empty());
 // The room is handed back to its entities in order, so one behind the player is not there yet.
 porchLive={porchPlayer.id(),a.id()};set();
 assert(paradox(peek::readExitTarget(h,t,0,0),"reject"));
 porchLive={a.id(),porchPlayer.id()};set();
 // Walking further out passes the porch player's check and meets the garden player's. The garden,
 // built alone on the stack, has no flame to walk out of.
 assert(parent(peek::readExitTarget(h,t,1,0),"garden",2));
 assert(!peek::readExitTarget(h,t,2,0).available);
 assert(!peek::readExitTarget(h,t,3,0).available);
 // A flame is read for the room being played, not any other.
 assert(!peek::readExitTarget(h,ptr(porch.data()),0,0).available);
 // A global chest away from its room comes back with the room's saved globals.
 porchLive={porchPlayer.id()};porchSaved={a.id()};set();
 auto returning=peek::readExitTarget(h,t,0,0);
 assert(parent(returning,"porch",1));
 // So walking out finds it back in the porch, which a preview of the porch has to show.
 assert(returning.globals.available&&returning.globals.objects.size()==1);
 assert(returning.globals.objects[0].kind=="chest"&&returning.globals.objects[0].target=="tower"&&returning.globals.objects[0].sourceId==a.id());
 // Unless the porch refuses it where it was saved: against a locked lock, or in a solid tile.
 real(a.bytes.data(),8,9);real(a.bytes.data(),12,12.5f);real(a.bytes.data(),0x28,.5f);real(a.bytes.data(),0x2c,.5f);
 porchLive={porchPlayer.id(),lock.id()};set();
 assert(paradox(peek::readExitTarget(h,t,0,0),"reject"));
 word(lock.bytes.data(),0x34,0);
 assert(parent(peek::readExitTarget(h,t,0,0),"porch",1));
 porchTiles[(12*20+9)*3+1]=1;
 assert(paradox(peek::readExitTarget(h,t,0,0),"reject"));
 porchTiles[(12*20+9)*3+1]=0;porchLive={porchPlayer.id()};set();
 // Carried into the room it leads to and put down there, it is saved under that room instead:
 // the porch player's way back is gone.
 porchSaved.clear();towerLive.push_back(a.id());set();
 auto rejected=peek::readExitTarget(h,t,0,0);
 assert(paradox(rejected,"reject"));
 // A paradox room restores what is saved under its own name.
 assert(rejected.globals.available&&rejected.globals.initialized&&rejected.globals.objects.size()==1);
 assert(rejected.globals.objects[0].kind=="key"&&rejected.globals.objects[0].x==3&&rejected.globals.objects[0].y==4);
 // Carried out instead, it comes along; but a green flame cannot be taken with it in hand.
 word(context.data(),4,a.id());
 assert(parent(peek::readExitTarget(h,t,0,0),"porch",1));
 assert(paradox(peek::readExitTarget(h,t,0,1),"reject"));
 word(context.data(),4,0);
 // Leaving a room of the same name saves it where the porch looks for its own.
 string(stack.data()+56,"porch");
 assert(parent(peek::readExitTarget(h,t,0,0),"porch",1));
 string(stack.data()+56,"tower");
 // What went in through anything but a chest ends up threadless.
 towerLive={towerPlayer.id()};porchLive={porchPlayer.id()};word(porchPlayer.bytes.data(),0x5c,cauldron.id());set();
 assert(paradox(peek::readExitTarget(h,t,0,0),"threadless"));
 // Walking further out moves globals on the way: x, a garden global the player brought into the
 // porch, is saved under the porch on the way out, so the garden player finds its chest gone.
 word(porchPlayer.bytes.data(),0x5c,a.id());porchLive={a.id(),porchPlayer.id(),x.id()};gardenLive={gardenPlayer.id()};x.bytes[0x45]=1;set();
 assert(parent(peek::readExitTarget(h,t,0,0),"porch",1));
 assert(paradox(peek::readExitTarget(h,t,1,0),"reject"));
 // Carried all the way instead, it is the garden player's way back in hand; but put down before a
 // green flame out of the porch, it stays behind with the porch.
 porchLive={a.id(),porchPlayer.id()};towerLive={towerPlayer.id(),x.id()};word(context.data(),4,x.id());set();
 assert(parent(peek::readExitTarget(h,t,1,0),"garden",2));
 assert(paradox(peek::readExitTarget(h,t,1,2),"reject"));
 assert(parent(peek::readExitTarget(h,t,0,2),"porch",1));
 word(context.data(),4,0);
}
