#pragma once
#include "asset_mesh.h"
#include <cstdint>
namespace peek {
struct Particle {Vec3 position,color;float size=0,alpha=0,angle=0;};
// Stateless sampling: does not advance the game's RNG, physics, or Lua state.
std::vector<Particle> sampleParticles(const ParticleEffect& effect,double seconds,uint32_t seed);
}
