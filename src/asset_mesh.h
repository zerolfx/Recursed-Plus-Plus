#pragma once
#include <string>
#include <vector>
#include <array>
namespace peek {
struct Vec3 {float x=0,y=0,z=0;};
struct Face {Vec3 p[3];Vec3 color{.7f,.7f,.7f};};
struct Mesh {std::vector<Face> faces;std::string error;};
// Executes only the mesh-building entry point in an isolated, bounded Lua VM.
Mesh loadMesh(const std::string& root,const std::string& asset,const std::string& entry);
struct ParticleEffect {
 std::string texture,error;
 int count=1;
 float interval=.1f,sizeMin=.2f,sizeMax=.2f,lifeMin=1,lifeMax=1;
 Vec3 colorMin{1,1,1},colorMax{1,1,1},positionMin{},positionMax{},velocityMin{},velocityMax{},acceleration{},target{};
 float targetSpeed=0,sphereMin=0,sphereMax=0,rollMin=0,rollMax=0,alpha=1;
 std::array<float,4> envelope{0,.1f,.8f,1};
 std::array<float,4> sizeEnvelope{0,0,1,1};
 float sizePeak=0;
};
ParticleEffect loadParticleEffect(const std::string& root,const std::string& asset,const std::string& entry,float strength=1);
}
