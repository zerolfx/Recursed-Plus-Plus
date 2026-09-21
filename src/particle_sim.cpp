#include "particle_sim.h"
#include <algorithm>
#include <cmath>
namespace peek {
std::vector<Particle> sampleParticles(const ParticleEffect& e,double seconds,uint32_t seed){std::vector<Particle> out;if(!e.error.empty()||!std::isfinite(seconds)||seconds<0)return out;
 double cycle=std::max((double)e.lifeMax,(double)e.count*e.interval);if(cycle<=0)return out;
 for(int i=0;i<e.count;i++){
  double epoch=std::floor((seconds-i*e.interval)/cycle),age=seconds-(epoch*cycle+i*e.interval);uint32_t state=seed+0x9e3779b9u*(i+1)+(uint32_t)std::fmod(std::fabs(epoch),4294967295.)*0x85ebca6bu;
  auto random=[&](){state^=state>>16;state*=0x7feb352du;state^=state>>15;state*=0x846ca68bu;state^=state>>16;return (state&0xffffffu)/16777216.f;};
  auto between=[&](float a,float b){return a+(b-a)*random();};
  float life=between(e.lifeMin,e.lifeMax);if(age>life)continue;float t=(float)age,u=t/life;
  Particle p;p.position={between(e.positionMin.x,e.positionMax.x),between(e.positionMin.y,e.positionMax.y),between(e.positionMin.z,e.positionMax.z)};
  if(e.sphereMax>0){float z=between(-1,1),angle=between(0,6.2831853f),r=between(e.sphereMin,e.sphereMax),xz=std::sqrt(std::max(0.f,1-z*z));p.position.x+=r*xz*std::cos(angle);p.position.y+=r*z;p.position.z+=r*xz*std::sin(angle);}
  Vec3 v{between(e.velocityMin.x,e.velocityMax.x),between(e.velocityMin.y,e.velocityMax.y),between(e.velocityMin.z,e.velocityMax.z)};
  if(e.targetSpeed){Vec3 d{e.target.x-p.position.x,e.target.y-p.position.y,e.target.z-p.position.z};float n=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);if(n>.00001f){v.x+=d.x*e.targetSpeed/n;v.y+=d.y*e.targetSpeed/n;v.z+=d.z*e.targetSpeed/n;}}
  p.position.x+=v.x*t+e.acceleration.x*t*t*.5f;p.position.y+=v.y*t+e.acceleration.y*t*t*.5f;p.position.z+=v.z*t+e.acceleration.z*t*t*.5f;
  p.size=between(e.sizeMin,e.sizeMax);p.angle=between(e.rollMin,e.rollMax)*.01745329252f;p.color={between(e.colorMin.x,e.colorMax.x),between(e.colorMin.y,e.colorMax.y),between(e.colorMin.z,e.colorMax.z)};
  auto smooth=[](float a,float b,float x){if(b<=a)return x>=b?1.f:0.f;float q=std::clamp((x-a)/(b-a),0.f,1.f);return q*q*(3-2*q);};
  if(e.sizePeak>0)p.size=e.sizePeak*smooth(e.sizeEnvelope[0],e.sizeEnvelope[1],u)*(1-smooth(e.sizeEnvelope[2],e.sizeEnvelope[3],u));
  p.alpha=std::clamp(e.alpha*smooth(e.envelope[0],e.envelope[1],u)*(1-smooth(e.envelope[2],e.envelope[3],u)),0.f,1.f);if(p.alpha>.001f)out.push_back(p);
 }return out;
}
}
