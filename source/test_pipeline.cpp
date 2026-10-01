#include "DivaPhysicsTiming.cpp"
#include <cstdlib>
#include <thread>
static int mainCount=0,endCount=0,stageCount=0,clothCount=0,opdCount=0;
static float deltaValue=1.0f,animationValue=.5f;
static bool staged=true,isCloth=false;
static Block object=nullptr;
static float fakeDelta(){return deltaValue;}
static float fakeAnimation(){return animationValue;}
static void require(bool ok,const char* text){if(!ok){std::printf("FAIL %s\n",text);std::exit(1);}}
static void fakeMain(Block b,bool,bool,float,float){
    require(!cacheRegions,"preflight memory permissions never survive into native Main"); require(ownerStep(b)==1,"shared owner step is never modified");
    require(PhysicsAnimationDelta()==(fixedScope?2.f:animationValue),"corrected time is local to physics");
    ++mainCount;auto parent=at<Block>(b,0x20);auto root=at<Mat*>(parent,0x48);
    auto node=at<Block>(b,0xA0),end=at<Block>(b,0xA8);
    for(;node<end;node+=0x1D0){auto dst=at<Mat*>(node,0x40);*dst=*root;if(dst!=root)dst->v[7]+=1;}
}
static void fakeEnd(Block){++endCount;}
static void fakeStage(Block b,int,bool){require(!cacheRegions,"preflight permissions never survive into native stages");require(ownerStep(b)==1,"staged time remains native");++stageCount;}
static void fakeOPD(Block){++opdCount;}
static void fakeReset(Block){}
static void fakeRoot(Block r){auto roots=at<Block>(r,0x1D48);auto parent=at<Block>(r-0x60,0x20);auto root=at<Mat*>(parent,0x48);for(int i=0;i<2;++i)at<Mat>(roots+i*0x158,0x98)=*root;}
static void fakeCloth(Block b,bool,bool,float,float){++clothCount;fakeRoot(b+0x60);auto n=at<Block>(b,0x80);for(int i=0;i<4;++i)at<Vec>(n+i*0xF0,4)={0,static_cast<float>(i),0};}
static void fakeItem(Block){
    if(isCloth){hookClothMain(object,false);return;}
    if(staged)for(int stage=0;stage<6;++stage)hookOsageStep(object,stage,false);
    hookOsageMain(object,false);hookOsageEnd(object);
}
int main(){
    gameDelta=fakeDelta;animationDelta=fakeAnimation;osageMain=fakeMain;osageEnd=fakeEnd;osageStep=fakeStage;
    osageOPD=fakeOPD;clothOPD=fakeOPD;osageReset=fakeReset;clothReset=fakeReset;clothMain=fakeCloth;clothRoot=fakeRoot;itemCtrl=fakeItem;
    std::vector<unsigned char> block(0x2500),nodes(3*0x1D0),parent(0x50),item(0x208),owner(0x950),blocks(8);
    Mat root=physics::identity(),outputs[3];auto b=block.data();object=b;
    at<Block>(b,0x20)=parent.data();at<Mat*>(parent.data(),0x48)=&root;
    at<Block>(b,0x50)=item.data();at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;
    at<Block>(item.data(),0xC0)=blocks.data();at<Block>(item.data(),0xC8)=blocks.data()+8;at<Block>(item.data(),0xD0)=blocks.data()+8;at<Block>(blocks.data(),0)=b;
    at<Block>(b,0xA0)=nodes.data();at<Block>(b,0xA8)=nodes.data()+nodes.size();at<Block>(b,0xB0)=nodes.data()+nodes.size();
    for(int i=0;i<3;++i){outputs[i]=root;at<Mat*>(nodes.data()+i*0x1D0,0x40)=&outputs[i];}
    armed=true;mode=0;frame=100;
    for(int i=0;i<120;++i){++frame;hookItemCtrl(item.data());}
    require(mainCount==120&&endCount==120&&stageCount==720,"diagnostic mode preserves every native call");
    mainCount=endCount=stageCount=0;mode=1;++epoch;
    for(int i=0;i<120;++i){
        ++frame;root.v[3]=static_cast<float>(i);hookItemCtrl(item.data());
        require(std::abs(outputs[1].v[3]-root.v[3])<1e-4,"display follows current body root at high FPS");
    }
    require(mainCount==30&&stageCount==180&&endCount==120,"six stages and Main share 30 Hz, display references stay high FPS");
    require(!fixedScope&&!withinItem&&!suspendPhysics,"item and time scopes end after native calls");
    require(ownerStep(b)==1&&gameDelta()==1.f,"owner and global game time unchanged");
    hookOsageOPD(b);require(opdCount==1&&states.empty(),"baked playback remains native and clears history");
    ++frame;hookItemCtrl(item.data());hookOsageReset(b);require(states.empty(),"reset clears all cached poses");
    std::vector<unsigned char> cloth(0x2500),vertices(4*0xF0),roots(2*0x158);auto c=cloth.data();
    at<Block>(c,0x20)=parent.data();at<Block>(c,0x50)=item.data();at<size_t>(c,0x70)=2;
    at<Block>(c,0x80)=vertices.data();at<Block>(c,0x88)=vertices.data()+vertices.size();at<Block>(c,0x90)=vertices.data()+vertices.size();
    at<Block>(c,0x1DA8)=roots.data();at<Block>(c,0x1DB0)=roots.data()+roots.size();at<Block>(c,0x1DB8)=roots.data()+roots.size();
    for(int i=0;i<2;++i)at<Mat>(roots.data()+i*0x158,0x98)=root;
    object=c;isCloth=true;at<Block>(blocks.data(),0)=c;
    for(int i=0;i<120;++i){++frame;hookItemCtrl(item.data());}
    require(clothCount==30,"cloth force updates remain 30 Hz inside active item");
    ++frame;hookItemCtrl(item.data());++frame;root.v[3]+=5;hookItemCtrl(item.data());
    auto& cs=states[c];auto before=cs.vertices[2].pos;
    require(std::abs(at<Vec>(vertices.data()+2*0xF0,4).x-before.x-5)<1e-4,"cloth render follows weighted body anchors");
    auto velocity=at<Vec>(vertices.data()+2*0xF0,0x28);clear(c);
    require(std::abs(at<Vec>(vertices.data()+2*0xF0,4).x-before.x)<1e-4,"cloth display position restored before simulation");
    require(at<Vec>(vertices.data()+2*0xF0,0x28).x==velocity.x,"presentation never alters velocity");
    object=b;isCloth=false;staged=false;at<Block>(blocks.data(),0)=b;
    mode=0;++epoch;mainCount=0;
    for(int i=0;i<120;++i){++frame;hookItemCtrl(item.data());}
    require(mainCount==120&&states.empty(),"mode off restores native rate and clears histories");
    mode=1;++epoch;mainCount=0;
    for(int i=0;i<120;++i){++frame;hookItemCtrl(item.data());}
    require(mainCount==30,"Main-only active items remain paced at 30 Hz");
    auto beforePause=mainCount;deltaValue=0;
    for(int i=0;i<120;++i){++frame;hookItemCtrl(item.data());}
    require(mainCount==beforePause,"pause preserves established physical state");deltaValue=1;
    clear(b);at<Mat*>(nodes.data(),0x40)=&root;at<Block>(nodes.data(),0x38)=parent.data();
    ++frame;hookItemCtrl(item.data());
    for(auto& pose:states[b].matrices)require(pose.destination!=&root,"parent animation input never enters pose cache");
    ++frame;root.v[3]+=5;hookItemCtrl(item.data());auto currentRoot=root.v[3];clear(b);
    require(root.v[3]==currentRoot,"restoration does not rewind parent animation");
    at<Mat*>(nodes.data(),0x40)=&outputs[0];at<Block>(nodes.data(),0x38)=nullptr;
    for(int fps:{30,60,120,144,240}){
        clear(b);mainCount=0;animationValue=60.f/fps;
        for(int i=0;i<fps*2;++i){++frame;hookItemCtrl(item.data());}
        require(mainCount>=60&&mainCount<=61,"active item physics stays 30 Hz across render rates");
        require(ownerStep(b)==1,"time multiplier never leaks to body animation");
    }
    clear(b);mainCount=endCount=stageCount=0;animationValue=.5f;staged=true;
    for(int i=0;i<120;++i){++frame;std::thread worker([&]{hookItemCtrl(item.data());});worker.join();}
    require(mainCount==30&&stageCount==180,"item worker migration preserves global block clock and pose");
    clear(b);at<Block>(b,0xA8)=nodes.data();mainCount=0;
    for(int i=0;i<120;++i){++frame;hookItemCtrl(item.data());}
    require(states[b].bypass&&mainCount==120,"unsupported pose layout falls back to unscaled native physics");
    std::puts("PASS: active-item passthrough, staged/Main-only/cloth 30 Hz, display anchors, OPD/reset, pause, parent exclusion, worker migration and fallback");
}