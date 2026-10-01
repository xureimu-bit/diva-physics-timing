#include "physics_core.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace physics;
void check(bool ok,const char* name) { if(!ok) { std::printf("FAIL %s\n",name); std::exit(1); } }
bool near(float a,float b) { return std::abs(a-b)<1e-4; }
int main() {
    for(int fps:{30,60,120,144,240}) {
        Clock c; int ticks=0;
        for(int i=0;i<fps*10;++i) if(c.advance(60.0f/fps)) ++ticks;
        check(ticks>=300&&ticks<=301,"30 Hz at every render rate");
        check(!c.advance(0),"pause does not advance");
        check(!c.advance(std::numeric_limits<float>::quiet_NaN()),"bad delta ignored");
    }
    Clock c; check(c.advance(0.5f),"first pose initializes");
    c.advance(1000); check(c.remainder<2,"seek backlog bounded");
    c.reset(); check(c.advance(0.5f),"reset initializes again");
    Mat old=identity(),now=identity(),d;
    old.v[12]=4;now.v[12]=7;
    check(rebase(old,now,d),"translation rebase");
    auto p=transform({6,2,3},d); check(near(p.x,9)&&near(p.y,2),"pose follows parent translation");
    auto v=transform({1,2,3},d,false);check(near(v.x,1),"directions exclude translation");
    old=identity(); old.v[0]=0;old.v[1]=1;old.v[4]=-1;old.v[5]=0;old.v[12]=4;
    now=identity();now.v[0]=2;now.v[5]=3;now.v[12]=8;
    check(rebase(old,now,d),"rotation and scale rebase");
    Mat local=identity();local.v[12]=1;local.v[13]=2;
    auto obtained=mul(mul(local,old),d);auto expected=mul(local,now);
    for(int i=0;i<16;++i) check(near(obtained.v[i],expected.v[i]),"row matrix attachment");
    Mat singular{};check(!rebase(singular,now,d),"singular parent rejected");
    Mat rotation90=identity();rotation90.v[0]=0;rotation90.v[1]=1;rotation90.v[4]=-1;rotation90.v[5]=0;
    auto midpoint=interpolate(identity(),rotation90,0.5f);
    check(near(midpoint.v[0],std::sqrt(0.5f))&&near(midpoint.v[1],std::sqrt(0.5f)),"45 degree rotation midpoint");
    check(near(midpoint.v[0]*midpoint.v[0]+midpoint.v[1]*midpoint.v[1],1),"rotation interpolation preserves bone length");
    Mat reflection=rotation90;reflection.v[0]*=-2;reflection.v[1]*=-2;
    midpoint=interpolate(reflection,reflection,0.5f);
    for(int i=0;i<16;++i)check(near(midpoint.v[i],reflection.v[i]),"reflection and scale preserved");
    auto positive=rotation(Quat{0,0,std::sin(179.0f*0.5f*0.0174532925f),std::cos(179.0f*0.5f*0.0174532925f)});
    auto negative=rotation(Quat{0,0,std::sin(-179.0f*0.5f*0.0174532925f),std::cos(-179.0f*0.5f*0.0174532925f)});
    midpoint=interpolate(positive,negative,0.5f);check(near(midpoint.v[0],-1),"rotation takes shortest path across 180 degrees");
    Mat shear=identity();shear.v[1]=0.4f;
    midpoint=interpolate(shear,shear,0.5f);
    for(int i=0;i<16;++i)check(near(midpoint.v[i],shear.v[i]),"sheared affine data kept intact");
    // Literal native layout: x/y/z translation in the final column, matching
    // this executable's 0x47FF88 stores and 0x1A6C50 translation routine.
    const Mat native={ {0,-1,0,4, 1,0,0,5, 0,0,1,6, 0,0,0,1} };
    auto canonical=fromGame(native);p=transform({1,2,3},canonical);
    check(near(p.x,2)&&near(p.y,6)&&near(p.z,9),"literal native column matrix transforms a point correctly");
    auto nativeRoundTrip=toGame(canonical);
    for(int i=0;i<16;++i)check(near(nativeRoundTrip.v[i],native.v[i]),"native bytes round trip without translation loss");
    auto nativeMidpoint=toGame(interpolate(identity(),canonical,0.5f));
    check(near(nativeMidpoint.v[3],2)&&near(nativeMidpoint.v[7],2.5f)&&near(nativeMidpoint.v[11],3),"native translation stays in final column through interpolation");
    check(near(nativeMidpoint.v[12],0)&&near(nativeMidpoint.v[13],0)&&near(nativeMidpoint.v[14],0),"native affine bottom row never gets world coordinates");
    Mat samples[4];
    const float angles[4]={0,0.2f,-0.1f,0.5f},positions[4]={0,2,-1,5};
    for(int i=0;i<4;++i){samples[i]=rotation(Quat{0,0,std::sin(angles[i]*0.5f),std::cos(angles[i]*0.5f)});samples[i].v[12]=positions[i];}
    auto end=smooth(samples[0],samples[1],samples[2],1),start=smooth(samples[1],samples[2],samples[3],0);
    const float h=0.001f;
    auto left=smooth(samples[0],samples[1],samples[2],1-h),right=smooth(samples[1],samples[2],samples[3],h);
    for(int i=0;i<16;++i){
        check(near(end.v[i],start.v[i]),"three-pose display position is continuous at physics boundary");
        check(std::abs((end.v[i]-left.v[i])/h-(right.v[i]-start.v[i])/h)<0.02f,"three-pose display velocity is continuous across a turn");
    }
    for(int i=0;i<=100;++i){
        auto smoothed=smooth(samples[0],samples[1],samples[2],i/100.0f);
        check(smoothed.v[12]>=-1&&smoothed.v[12]<=2,"position smoothing has no overshoot outside its samples");
        check(near(smoothed.v[0]*smoothed.v[0]+smoothed.v[1]*smoothed.v[1],1),"smoothed rotation retains unit bone length");
    }
    auto steady=smooth(canonical,canonical,canonical,0.63f);
    for(int i=0;i<16;++i)check(near(steady.v[i],canonical.v[i]),"steady smoothed pose preserves native translation and rotation");
    auto wrapped=smooth(positive,negative,negative,0.5f);
    check(wrapped.v[0]<-0.99f,"three-pose quaternion blending crosses 180 degrees without flipping");
    auto sheared=smooth(shear,shear,shear,0.4f);
    for(int i=0;i<16;++i)check(near(sheared.v[i],shear.v[i]),"smooth affine fallback preserves steady shear");
    std::puts("PASS: 30 Hz pacing, native matrix transforms, continuous three-pose display velocity, bounded translation and rotations");
}
