#include <windows.h>
#include "../src/room_art.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
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
 std::filesystem::create_directories("build/effects-frames");
 auto start=GetTickCount64();for(int i=0;i<48;i++){auto& frame=peek::renderRoomArt("runtime",snapshot,1+i*.08);save("build/effects-frames/"+std::to_string(i)+".bmp",frame);}
 std::cout<<"PASS: animation changes "<<differences<<" pixels; 30 Hz cache and static terrain preserved. 48 frames incl. BMP output: "<<GetTickCount64()-start<<" ms\n";
}
