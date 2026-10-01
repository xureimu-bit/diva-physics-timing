#ifdef PHYSICS_SCOPE_OLD
#include "../../../outputs/DivaPhysicsTiming-0.1.9/source/DivaPhysicsTiming.cpp"
#else
#include "DivaPhysicsTiming.cpp"
#endif
#include <cstdlib>
static Block object=nullptr;
static Mat output;
static int stages=0,mains=0,ends=0,cloths=0;
static bool loading=false;
static float fakeDelta(){return 1.f;}
static float fakeAnimation(){return .5f;}
static void require(bool ok,const char* text){if(!ok){std::printf("FAIL %s\n",text);std::exit(1);}}
static void fakeStage(Block b,int stage,bool disabled){require(b==object&&stage>=0&&stage<6&&!disabled,"initialization stage arguments unchanged");++stages;}
static void fakeMain(Block b,bool changed,bool reset,float dt,float blend){
    require(b==object&&changed==loading&&reset==loading&&dt==(loading?.75f:.5f)&&blend==.25f,"five native Main arguments preserved");
    require(PhysicsAnimationDelta()==(loading?.5f:2.f),"initialization never receives fixed physics delta");
    output=*at<Mat*>(at<Block>(b,0x20),0x48);++mains;
}
static void fakeEnd(Block b){require(b==object,"End argument preserved");++ends;}
static void fakeCloth(Block b,bool changed,bool reset,float dt,float blend){require(b==object&&changed&&reset&&dt==.75f&&blend==.25f,"cloth initialization parameters preserved");require(PhysicsAnimationDelta()==.5f,"cloth initialization uses native time");++cloths;}
static void fakeItem(Block){for(int i=0;i<6;++i)hookOsageStep(object,i,false);hookOsageMain(object,false,false,.5f,.25f);hookOsageEnd(object);}
int main(){
    gameDelta=fakeDelta;animationDelta=fakeAnimation;osageStep=fakeStage;osageMain=fakeMain;osageEnd=fakeEnd;clothMain=fakeCloth;itemCtrl=fakeItem;
    std::vector<unsigned char> block(0x2500),nodes(0x1D0),parent(0x50),item(0x208),owner(0x950),blocks(8);
    Mat root=physics::identity();object=block.data();output=root;
    at<Block>(object,0x20)=parent.data();at<Mat*>(parent.data(),0x48)=&root;
    at<Block>(object,0x50)=item.data();at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;
    at<Block>(object,0xA0)=nodes.data();at<Block>(object,0xA8)=nodes.data()+nodes.size();at<Block>(object,0xB0)=nodes.data()+nodes.size();at<Mat*>(nodes.data(),0x40)=&output;
    at<Block>(item.data(),0xC0)=blocks.data();at<Block>(item.data(),0xC8)=blocks.data()+8;at<Block>(item.data(),0xD0)=blocks.data()+8;at<Block>(blocks.data(),0)=object;
    armed=true;mode=1;frame=100;
    for(int i=0;i<4;++i){++frame;hookItemCtrl(item.data());}
    require(mains==1&&states[object].valid,"established playback has one valid 30 Hz pose");
    // Loading/settling can run many virtual updates before OnFrame advances.
    // It must complete even when the last playback frame was a held frame.
    loading=true;stages=mains=ends=0;
    for(int cycle=0;cycle<20;++cycle){
        for(int stage=0;stage<6;++stage)hookOsageStep(object,stage,false);
        hookOsageMain(object,true,true,.75f,.25f);hookOsageEnd(object);
    }
    require(stages==120&&mains==20&&ends==20,"all repeated initialization calls complete without waiting for render clock");
    require(states.empty()&&!withinItem&&!fixedScope,"initialization clears stale display cache and leaves no scope");
    for(int i=0;i<20;++i)hookClothMain(object,true,true,.75f,.25f);
    require(cloths==20&&states.empty(),"cloth initialization is native passthrough even without playback layout");
    loading=false;stages=mains=ends=0;++frame;hookItemCtrl(item.data());
    require(mains==1&&stages==6&&states[object].valid,"returning to playback acquires a fresh physical pose");
    std::puts("PASS: repeated same-frame loading/settling bypasses render pacing, forwards all parameters/native delta, clears stale histories and resumes active-item physics");
}