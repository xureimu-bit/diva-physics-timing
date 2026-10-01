#ifdef PHYSICS_TRANSLATION_OLD
#include "../before-v16/DivaPhysicsTiming.cpp"
#else
#include "DivaPhysicsTiming.cpp"
#endif
#include <cstdlib>
#include <thread>
static void require(bool ok,const char* message){if(!ok){std::printf("FAIL %s\n",message);std::exit(1);}}
static void equal(const Mat& a,const Mat& b,const char* message){for(int i=0;i<16;++i)require(std::abs(a.v[i]-b.v[i])<.0002f,message);}
static float deltaValue=1,animationValue=.75f;static double animationTime=0;
static float fakeDelta(){return deltaValue;}static float fakeAnimation(){return animationValue;}
static Block objects[4]{};static Mat body,output[4],nativeOutput[4],previousRoot[4];static int updates[4]{};
static bool enabledTrial=true;static Mat expectedBody,renderBody;static Vec hsc{1,1,1};
static int indexOf(Block work){for(int i=0;i<4;++i)if(work==objects[i]+0x68)return i;require(false,"known native work");return 0;}
static Mat expectedRoot(int i){return i==1?physics::fromGame(nativeOutput[0]):i==3||!enabledTrial?renderBody:expectedBody;}
static void verifyNative(Block work,const Mat* root){
    int i=indexOf(work);auto expected=expectedRoot(i);
    equal(physics::fromGame(*root),expected,"constant translation reaches the exact 30 Hz input time without double-shifting branches");
    equal(physics::fromGame(*at<Mat*>(objects[i],0x1F30)),i<3&&enabledTrial?expectedBody:renderBody,"dependent private ancestor agrees with the main body's sampled translation");
    if(updates[i])equal(physics::fromGame(at<Mat>(objects[i],0x1F38)),previousRoot[i],"native prior root retains its sampled tick position");
    require(at<Vec>(at<Block>(objects[i],0xA0),4).x==123,"adapter never moves the solver's internal node positions");
    auto colliderBody=i<3&&enabledTrial?expectedBody:renderBody;
    for(int j=0;j<5;++j){auto c=work+0x2B0+j*0x38;auto a=at<Vec>(c,8);auto b=at<Vec>(c,0x14);
        require(std::abs(a.x-colliderBody.v[12]-.1f*j)<.0002f,"collider anchor uses the same sampled translation as the hair");
        require(std::abs(a.y-colliderBody.v[13])<.0002f&&std::abs(a.z-colliderBody.v[14])<.0002f,"all axes use the same private collider translation");
        if(j==1||j==3)require(std::abs(b.x-colliderBody.v[12]-.2f*j)<.0002f,"capsule and ellipse endpoints both follow the body");
        else require(b.x==9&&b.y==8&&b.z==7,"plane normals, AABB dimensions and unused ball fields never translate");
        require(at<Vec>(c,0x20).x==3&&at<float>(c,0x2C)==4&&at<float>(c,0x34)==.9f,"collision lengths, relative axes and friction remain native");
    }
    require(at<Vec>(work+0x10B0,8).x==999,"static stage/ring collider never follows the character");
    equal(physics::fromGame(body),renderBody,"shared skinning and body input remains at render time");
}
static void fakeBegin(Block work,const Mat* root,const Vec*,bool){verifyNative(work,root);}
static void fakeApply(Block work,const Mat* root,const Vec* scale,float step,bool,bool,bool){
    require(step==2,"native solver stays at 30 Hz strength and timestep");verifyNative(work,root);
    hookRootBegin(work,root,scale,false);verifyNative(work,root);
}
static void fakeTarget(Block work,const Mat* root,const Vec*,float,bool){verifyNative(work,root);}
static void fakeCtrl(Block work,const Mat* root,const Vec* scale,float step){
    int i=indexOf(work);hookRootApply(work,root,scale,step,false,false,false);hookRootTarget(work,root,scale,step,true);
    auto pose=physics::fromGame(*root);pose.v[12]+=.05f*(i+1);output[i]=physics::toGame(pose);nativeOutput[i]=output[i];
    at<Mat>(objects[i],0x1F38)=*at<Mat*>(objects[i],0x1F30);previousRoot[i]=physics::fromGame(*at<Mat*>(objects[i],0x1F30));++updates[i];
}
static void fakeMain(Block b,bool,bool,float,float){
    int i=indexOf(b+0x68);if(updates[i])equal(output[i],nativeOutput[i],"display matrices restored before any new native solve");
    auto work=b+0x68;
    for(int j=0;j<5;++j){auto c=work+0x2B0+j*0x38;at<int>(c,0)=j+1;at<float>(c,4)=.05f;
        at<Vec>(c,8)={renderBody.v[12]+.1f*j,renderBody.v[13],renderBody.v[14]};
        at<Vec>(c,0x14)=j==1||j==3?Vec{renderBody.v[12]+.2f*j,renderBody.v[13],renderBody.v[14]}:Vec{9,8,7};
        at<Vec>(c,0x20)={3,2,1};at<float>(c,0x2C)=4;at<float>(c,0x34)=.9f;
    }
    at<int>(work+0x2B0+5*0x38,0)=0;at<Vec>(work+0x10B0,8)={999,998,997};
    unsigned char collisions[64*0x38];std::memcpy(collisions,work+0x2B0,sizeof collisions);
    auto parent=at<Block>(b,0x20);hookRootCtrl(work,at<Mat*>(parent,0x48),&hsc,PhysicsAnimationDelta());
    require(std::memcmp(collisions,work+0x2B0,sizeof collisions)==0,"private collider scratch restored byte-for-byte at callback exit");
}
#ifndef PHYSICS_TRANSLATION_OLD
static void fallbackCtrl(Block work,const Mat* root,const Vec*,float){
    equal(*root,body,"unsupported colliders forward the unchanged native root argument");
    require(at<Mat*>(work,0x1EC8)==&body,"unsupported collider trial restores the native private ancestor");
}
#endif
static void fakeStep(Block,int,bool){}static void fakeEnd(Block){}
static void fakeItem(Block){
    for(int stage=0;stage<6;++stage)for(auto b:objects)hookOsageStep(b,stage,false);
    for(auto b:objects){hookOsageMain(b,false);require(at<Mat*>(b,0x1F30)==&body,"private ancestor pointer restored after each main callback");}
    for(auto b:objects)hookOsageEnd(b);
}
int main(){
    gameDelta=fakeDelta;animationDelta=fakeAnimation;osageMain=fakeMain;osageStep=fakeStep;osageEnd=fakeEnd;itemCtrl=fakeItem;
    rootCtrl=fakeCtrl;rootBegin=fakeBegin;rootApply=fakeApply;rootTarget=fakeTarget;
    std::vector<unsigned char> item(0x208),owner(0x950),blocks(32),storage[4],nodes[4],parents[4];
    char labels[4][64]={"c_tail_l_osg","c_tail_l_u_osg","c_tail_r_osg","c_skirt_osg"};
    for(int i=0;i<4;++i){storage[i].resize(0x2500);nodes[i].resize(0x1D0);parents[i].resize(0x50);auto b=storage[i].data();objects[i]=b;
        at<int>(b,0x10)=1;at<const char*>(b,0x18)=labels[i];at<Block>(b,0x20)=parents[i].data();at<Mat*>(parents[i].data(),0x48)=i==1?&output[0]:&body;
        at<Block>(b,0x48)=i==1?objects[0]:nullptr;at<Block>(b,0x50)=item.data();at<Mat*>(b,0x1F30)=&body;
        at<Block>(b,0xA0)=nodes[i].data();at<Block>(b,0xA8)=nodes[i].data()+nodes[i].size();at<Block>(b,0xB0)=nodes[i].data()+nodes[i].size();
        at<Mat*>(nodes[i].data(),0x40)=&output[i];at<Vec>(nodes[i].data(),4)={123,456,789};at<Block>(blocks.data(),i*8)=b;
    }
    at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;at<Block>(item.data(),0xC0)=blocks.data();
    at<Block>(item.data(),0xC8)=blocks.data()+32;at<Block>(item.data(),0xD0)=blocks.data()+32;
    armed=true;mode=1;frame=100;captureRoots=1;resampleRoots=0;smoothOsage=0;interpolateOsage=1;timeAwareInterpolation=1;
    for(bool trial:{true,false})for(int fps:{60,75,80,83,120,144,240})for(bool uneven:{false,true}){
        enabledTrial=trial;
#ifndef PHYSICS_TRANSLATION_OLD
        translationInputs=trial;
#endif
        states.clear();++epoch;std::memset(updates,0,sizeof updates);animationTime=0;deltaValue=1;
        for(int i=0;i<4;++i){output[i]=physics::identity();nativeOutput[i]=output[i];}
        physics::Clock oracle;
        for(int i=0;i<fps*2;++i){
            animationValue=60.f/fps*(uneven?(i%3==0?1.35f:i%3==1?.65f:1.f):1.f);
            if(i)animationTime+=animationValue;
            renderBody=physics::rotation(physics::Quat{0,0,std::sin(static_cast<float>(animationTime)*.01f),std::cos(static_cast<float>(animationTime)*.01f)});
            renderBody.v[12]=static_cast<float>(animationTime*.005);renderBody.v[13]=static_cast<float>(animationTime*.0025);renderBody.v[14]=static_cast<float>(animationTime*-.001);
            body=physics::toGame(renderBody);oracle.advance(animationValue);expectedBody=renderBody;
            expectedBody.v[12]=static_cast<float>((animationTime-oracle.remainder)*.005);expectedBody.v[13]=static_cast<float>((animationTime-oracle.remainder)*.0025);expectedBody.v[14]=static_cast<float>((animationTime-oracle.remainder)*-.001);
            ++frame;if(i%2){std::thread worker([&]{hookItemCtrl(item.data());});worker.join();}else hookItemCtrl(item.data());
        }
        for(int i=0;i<4;++i)require(updates[i]>=59&&updates[i]<=61,"all native updates remain at 30 Hz including the unchanged skirt");
        auto count=updates[0];deltaValue=0;for(int i=0;i<5;++i){++frame;hookItemCtrl(item.data());}require(updates[0]==count,"paused scene does not advance physics");
    }
#ifndef PHYSICS_TRANSLATION_OLD
    translationInputs=1;State rejected;rejected.translationReady=true;
    auto raw=physics::fromGame(body);rejected.translationInput={raw.v[12],raw.v[13],raw.v[14]};
    rejected.translationOutput={raw.v[12]-.01f,raw.v[13],raw.v[14]};
    auto c=objects[0]+0x318;at<int>(c,0)=6;
    unsigned char rejectedBytes[64*0x38];std::memcpy(rejectedBytes,c,sizeof rejectedBytes);
    rootCtrl=fallbackCtrl;
    {RootScope scope(objects[0],rejected,true);hookRootCtrl(objects[0]+0x68,&body,&hsc,2);
        require(!rejected.rootApplied,"unsupported collider correction never marks a sampled solve");}
    require(at<Mat*>(objects[0],0x1F30)==&body,"rejected private pointer restored after the outer scope");
    require(std::memcmp(rejectedBytes,c,sizeof rejectedBytes)==0,"unsupported collider scratch is never partially translated");
    rootCtrl=fakeCtrl;
    physics::TranslationSampler sampler;Vec result;
    sampler.sample({0,0,0},.75f,0,true,result,2);sampler.sample({3,0,0},.75f,.5,true,result,2);
    require(result.x==3,"scene jump is acquired directly instead of stretching across it");
    sampler.reset();sampler.sample({5,0,0},.75f,.5,true,result,2);require(result.x==5,"seek resets translation history");
    sampler.sample({NAN,0,0},.75f,.5,true,result,2);require(!sampler.initialized,"invalid input clears history");
#endif
    std::puts("PASS: coherent fixed-grid Miku-tail translations at 60/75/80/83/120/144/240 FPS and uneven frames; physical-parent inputs not shifted twice; private ancestor and body collisions agree; plane normals/AABB sizes/ring unchanged; byte-exact scratch and pointer restoration, native 30 Hz strength, skirt passthrough, workers, pause and seek");
}