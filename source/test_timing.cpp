#ifdef PHYSICS_TIMING_OLD
#include "../before-v13/DivaPhysicsTiming.cpp"
#else
#include "DivaPhysicsTiming.cpp"
#endif
#include <cstdlib>
#include <thread>
static void require(bool ok,const char* name){if(!ok){std::printf("FAIL %s\n",name);std::exit(1);}}
static float deltaValue=1,animationValue=.75f;
static double animationTime=0;
static float fakeDelta(){return deltaValue;}
static float fakeAnimation(){return animationValue;}
static Mat body,output,nativePose;
static Block object=nullptr;
static int updates=0;
static void fakeMain(Block b,bool,bool,float,float){
    if(updates)for(int i=0;i<16;++i)require(std::abs(output.v[i]-nativePose.v[i])<.0001f,"display history restored before native solver");
    // A moving anchor, with a tip remaining still in world space, isolates
    // artificial movement caused by assigning the wrong time to captured poses.
    output=physics::identity();output.v[3]=4;output.v[7]=-2;output.v[11]=1;
    nativePose=output;++updates;
    auto nodes=at<Block>(b,0xA0);
    require(at<Vec>(nodes,4).x==123,"presentation never rewrites internal physical positions");
}
static void fakeEnd(Block){}
static void fakeItem(Block){hookOsageMain(object,true,false,.5f,.25f);hookOsageEnd(object);}
int main(){
    gameDelta=fakeDelta;animationDelta=fakeAnimation;osageMain=fakeMain;osageEnd=fakeEnd;itemCtrl=fakeItem;
    std::vector<unsigned char> storage(0x2500),nodes(0x1D0),parent(0x50),item(0x208),owner(0x950),blocks(8);
    object=storage.data();at<Block>(object,0x20)=parent.data();at<Mat*>(parent.data(),0x48)=&body;
    at<Block>(object,0x50)=item.data();at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;
    at<Block>(object,0xA0)=nodes.data();at<Block>(object,0xA8)=nodes.data()+nodes.size();at<Block>(object,0xB0)=nodes.data()+nodes.size();
    at<Mat*>(nodes.data(),0x40)=&output;at<Vec>(nodes.data(),4)={123,456,789};
    at<Block>(blocks.data(),0)=object;at<Block>(item.data(),0xC0)=blocks.data();at<Block>(item.data(),0xC8)=blocks.data()+8;at<Block>(item.data(),0xD0)=blocks.data()+8;
    armed=true;mode=1;interpolateOsage=1;smoothOsage=0;resampleRoots=0;captureRoots=0;frame=100;
#ifndef PHYSICS_TIMING_OLD
    timeAwareInterpolation=1;
#endif
    double maximumError=0;
    for(int rate:{30,40})for(int fps:{60,75,80,83,120,144,240})for(bool uneven:{false,true}){
        physicsHz=rate;const float period=60.f/rate;
        states.clear();++epoch;updates=0;animationTime=0;deltaValue=1;
        for(int i=0;i<fps*3;++i){
            animationValue=60.f/fps*(uneven?(i%3==0?1.35f:i%3==1?.65f:1.f):1.f);
            if(i)animationTime+=animationValue;
            body=physics::identity();body.v[3]=static_cast<float>(animationTime*.4);body.v[7]=static_cast<float>(animationTime*-.25);body.v[11]=static_cast<float>(animationTime*.1);
            ++frame;
            if(i%2){std::thread worker([&]{hookItemCtrl(item.data());});worker.join();}else hookItemCtrl(item.data());
            if(i>fps/2){
                const float expected[3]={4+period*.4f,-2-period*.25f,1+period*.1f};const int offsets[3]={3,7,11};
                for(int axis=0;axis<3;++axis){double error=std::abs(output.v[offsets[axis]]-expected[axis]);maximumError=std::max(maximumError,error);}
                require(maximumError<.0001,"nonuniform capture times create no false tip translation");
            }
        }
        require(updates>=rate*3-1&&updates<=rate*3+1,"native solver retains the configured 30/40 Hz rate");
        auto paused=output;auto before=updates;deltaValue=0;
        for(int i=0;i<10;++i){++frame;hookItemCtrl(item.data());}
        require(updates==before,"pause does not integrate forces");
        for(int i=0;i<16;++i)require(std::abs(output.v[i]-paused.v[i])<.0001f,"pause preserves timed presentation exactly");
    }
#ifndef PHYSICS_TIMING_OLD
    physics::PoseTimeline timeline;timeline.capture(0,false);timeline.capture(2.25,true);timeline.capture(3.75,true);
    auto blend=timeline.sample(3.75);
    require(blend.older&&std::abs(blend.alpha-1.75f/2.25f)<.0001f,"short following tick chooses earlier pair rather than jumping forward");
    timeline.capture(6,true);blend=timeline.sample(6);
    require(!blend.older&&std::abs(blend.alpha-.25f/2.25f)<.0001f,"long following tick uses measured interval rather than ideal spacing");
    timeline.capture(0,false);require(timeline.earlier==0&&timeline.previous==0&&timeline.current==0,"seek/topology reset discards stale timestamps");
    timeline.capture(0,true);require(timeline.sample(0).alpha==0,"repeated paused capture never divides by zero");
#endif
    std::printf("PASS: 30/40 Hz actual-time display at 60/75/80/83/120/144/240 FPS and uneven steps; stationary world tip with moving body; older-pair boundary, pause, reset and native input restoration; max error %.8f\n",maximumError);
}
