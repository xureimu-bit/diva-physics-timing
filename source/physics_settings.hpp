#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>
namespace physics_settings {
enum class Group { Other, Hair, Clothing };
inline Group namedGroup(const char* name) {
    if(!name)return Group::Other;
    for(auto prefix:{"c_hair_","c_fhair_","c_mhair_","c_tail_"})
        if(!std::strncmp(name,prefix,std::strlen(prefix)))return Group::Hair;
    for(auto prefix:{"c_skirt_","c_level_","c_jacket_","c_sleeve_","c_belt","c_necktie_","c_hood_","c_tag_","c_boots_","c_dress_","c_ribbon_"})
        if(!std::strncmp(name,prefix,std::strlen(prefix)))return Group::Clothing;
    return Group::Other;
}
constexpr int minHz=20,maxHz=60;
inline int validOverride(int value){return value>=minHz&&value<=maxHz?value:0;}
struct Controls {
    uint32_t size=sizeof(Controls),version=1;
    int32_t mode=1,hairHz=30,clothingHz=30,hairEnabled=1,clothingEnabled=1;
    int32_t hairInterpolation=1,hairSmoothing=0,clothingInterpolation=1;
    int32_t armed=0,pending=0,lastSaveError=0;
    uint32_t revision=0;
};
static_assert(sizeof(Controls)==56,"stable controls ABI");
inline bool valid(const Controls& s){
    auto flag=[](int n){return n==0||n==1;};
    return s.size==sizeof(Controls)&&s.version==1&&validOverride(s.hairHz)==s.hairHz
        &&validOverride(s.clothingHz)==s.clothingHz&&s.hairHz!=0&&s.clothingHz!=0
        &&flag(s.mode)&&flag(s.hairEnabled)&&flag(s.clothingEnabled)
        &&flag(s.hairInterpolation)&&flag(s.hairSmoothing)&&flag(s.clothingInterpolation);
}
using GetControls=bool(__cdecl*)(Controls*);
using SetControls=bool(__cdecl*)(const Controls*);
}
