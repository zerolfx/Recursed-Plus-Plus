#include "play_record.h"
#include <algorithm>
namespace peek {
bool startsAction(uint16_t before,uint16_t now){
 for(auto c:{ControlJump,ControlUse})if(held(now,c)&&!held(before,c))return true;
 for(int c=ControlUp;c<=ControlUse;c++)if(held(before,(Control)c))return false;
 for(int c=ControlUp;c<=ControlRight;c++)if(held(now,(Control)c))return true;
 return false;
}
size_t beforeLastAction(const std::vector<PlayTick>& ticks,size_t now){
 for(size_t t=std::min(now,ticks.size());t-->0;)if(startsAction(ticks[t].heldBefore,ticks[t].controls))return t;
 return now;
}
size_t secondsBefore(const std::vector<PlayTick>& ticks,size_t now,double seconds){
 double taken=0;
 for(size_t t=std::min(now,ticks.size());t-->0;){taken+=ticks[t].step;if(taken>=seconds)return t;}
 return 0;
}
}
