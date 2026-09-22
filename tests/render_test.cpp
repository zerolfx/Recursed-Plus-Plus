#include <windows.h>
#include "../src/room_art.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
static void save(const std::string& path,const peek::RoomArt& image){
 BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+(DWORD)(image.pixels.size()*4);info.biSize=sizeof(info);info.biWidth=image.width;info.biHeight=-image.height;info.biPlanes=1;info.biBitCount=32;info.biCompression=BI_RGB;
 std::ofstream out(path,std::ios::binary);out.write((char*)&file,sizeof(file));out.write((char*)&info,sizeof(info));out.write((char*)image.pixels.data(),image.pixels.size()*4);
}
int main(){
 auto snapshot=peek::loadSnapshot("runtime","missions/peek-lab","pool",false);assert(snapshot.error.empty()&&snapshot.tileset=="tiles/castle");
 auto a=peek::renderRoomArt("runtime",snapshot,1.01);auto cached=peek::renderRoomArt("runtime",snapshot,1.02);assert(a.revision==cached.revision&&a.pixels==cached.pixels);
 auto b=peek::renderRoomArt("runtime",snapshot,1.5);size_t differences=0;for(size_t i=0;i<a.pixels.size();i++)differences+=a.pixels[i]!=b.pixels[i];assert(differences>100&&differences<100000);
 assert(a.pixels[590*800+400]==b.pixels[590*800+400]); // solid terrain is static
 assert(snapshot.objects.size()==2&&snapshot.tiles[11*20+9].kind==3); // input unchanged
 // Every mesh-backed kind must reach its asset and entry. drawMesh is the only place the
 // kind -> asset/entry split happens, and a miss silently degrades to the schematic dot.
 auto meshed=[](const std::string& room,const char* kind,double when){
  auto s=peek::loadSnapshot("runtime","missions/peek-lab",room,false);assert(s.error.empty());
  s.objects.erase(std::remove_if(s.objects.begin(),s.objects.end(),[&](const peek::Object& o){return o.kind!=kind;}),s.objects.end());
  assert(s.objects.size()==1);
  return peek::renderRoomArt("runtime",s,when).note.find("schematic")==std::string::npos;
 };
 assert(meshed("keyroom","record",2.01));
 assert(meshed("props","fan",2.11));
 assert(meshed("props","generic",2.21));
 assert(meshed("props","cauldron",2.31));
 // crystal.lua ships a mesh for each variant, so none of the three may fall back.
 for(const char* gem:{"crystal","diamond","ruby"}){
  peek::Snapshot one;one.tileset="tiles/castle";one.objects.push_back({gem,"",10,12.5f});
  assert(peek::renderRoomArt("runtime",one,2.41).note.find("schematic")==std::string::npos);
 }
 std::filesystem::create_directories("build/effects-frames");
 auto start=GetTickCount64();for(int i=0;i<48;i++){auto& frame=peek::renderRoomArt("runtime",snapshot,1+i*.08);save("build/effects-frames/"+std::to_string(i)+".bmp",frame);}
 std::cout<<"PASS: animation changes "<<differences<<" pixels; 30 Hz cache and static terrain preserved. 48 frames incl. BMP output: "<<GetTickCount64()-start<<" ms\n";
}
