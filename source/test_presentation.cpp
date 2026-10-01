#include "DivaPhysicsTiming.cpp"
#include <cstdlib>
#include <thread>
static void require(bool ok,const char* name){if(!ok){std::printf("FAIL %s\n",name);std::exit(1);}}
static float deltaValue=1,animationValue=0.5f;
static float fakeDelta(){return deltaValue;}
static float fakeAnimation(){return animationValue;}
static Mat physical[2],output[2];
static int updates[2]{},currentIteration=0;
static Block object[2]{};
static void fakeMain(Block b,bool changed,bool reset,float dt,float blend){
    require(changed&&!reset&&dt==0.5f&&blend==0.25f,"all native virtual-call parameters forwarded");
    int index=b==object[0]?0:1;
    if(updates[index])for(int i=0;i<16;++i)
        require(std::abs(output[index].v[i]-physical[index].v[i])<0.0001f,"display interpolation never leaks into next physics call");
    auto parent=at<Block>(b,0x20);auto root=at<Mat*>(parent,0x48);
    float angle=static_cast<float>(++updates[index])*0.04f;
    Mat local=physics::rotation(physics::Quat{0,0,std::sin(angle*0.5f),std::cos(angle*0.5f)});
    local.v[12]=index?1.0f:0;
    output[index]=physics::toGame(physics::mul(local,physics::fromGame(*root)));physical[index]=output[index];
}
static void fakeStage(Block,int,bool){}
static void fakeEnd(Block){}
static void fakeReset(Block){}
static void fakeItem(Block item){
    auto blocks=at<Block>(item,0xC0);
    for(int stage=0;stage<6;++stage)for(int i=0;i<2;++i)hookOsageStep(at<Block>(blocks,i*8),stage,false);
    for(int i=0;i<2;++i)hookOsageMain(at<Block>(blocks,i*8),true,false,0.5f,0.25f);
    for(int i=0;i<2;++i)hookOsageEnd(at<Block>(blocks,i*8));
}
int main(){
    auto page=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    require(page!=nullptr,"memory validation fixture allocated");
    {MemoryPhase phase;require(accessible(page,64,true),"writable page validated and cached");}
    DWORD oldProtection=0;require(VirtualProtect(page,4096,PAGE_READONLY,&oldProtection)!=0,"fixture made read-only between phases");
    {MemoryPhase phase;require(accessible(page,64)&&!accessible(page,64,true),"cached permissions never survive a native phase boundary");}
    VirtualFree(page,0,MEM_RELEASE);
    {MemoryPhase phase;require(!accessible(page,64),"released memory rejected in next phase");}
    gameDelta=fakeDelta;animationDelta=fakeAnimation;osageMain=fakeMain;osageStep=fakeStage;
    osageEnd=fakeEnd;osageReset=fakeReset;itemCtrl=fakeItem;
    std::vector<unsigned char> item(0x208),owner(0x950),blocks(16),storage[2],nodes[2],parents[2];
    Mat root=physics::identity();
    for(int i=0;i<2;++i){
        storage[i].resize(0x2500);nodes[i].resize(0x1D0);parents[i].resize(0x50);
        auto b=storage[i].data();object[i]=b;
        at<Block>(b,0x20)=parents[i].data();at<Mat*>(parents[i].data(),0x48)=i?&output[0]:&root;
        at<Block>(b,0x50)=item.data();at<Block>(b,0xA0)=nodes[i].data();
        at<Block>(b,0xA8)=nodes[i].data()+nodes[i].size();at<Block>(b,0xB0)=nodes[i].data()+nodes[i].size();
        at<Mat*>(nodes[i].data(),0x40)=&output[i];at<Block>(blocks.data(),i*8)=b;
        output[i]=physics::identity();
    }
    at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;
    at<Block>(item.data(),0xC0)=blocks.data();at<Block>(item.data(),0xC8)=blocks.data()+16;
    at<Block>(item.data(),0xD0)=blocks.data()+16;
    armed=true;
    for(int smoothing:{0,1}) {
    smoothOsage=smoothing;mode=1;frame=100;++epoch;states.clear();
    deltaValue=1;root=physics::identity();
    for(int i=0;i<2;++i){updates[i]=0;output[i]=physics::identity();}
    float lastAngle=0;int renderChanges=0;
    for(currentIteration=0;currentIteration<120;++currentIteration){
        ++frame;root.v[3]=static_cast<float>(currentIteration)*0.1f;
        // Different workers own the whole item on alternating frames.
        std::thread worker([&]{hookItemCtrl(item.data());});worker.join();
        float angle=std::atan2(output[0].v[4],output[0].v[0]);
        if(currentIteration>4){
            require(angle>=lastAngle-0.0001f&&angle-lastAngle<0.0101f,"display deform advances continuously across physics tick boundaries");
            if(angle>lastAngle+0.005f)++renderChanges;
        }
        lastAngle=angle;
        require(std::abs(output[0].v[3]-root.v[3])<0.0001f,"body anchor follows render time without physics delay");
        auto& child=states[object[1]];
        require(child.valid,"dependent child pose captured before parent presentation");
        require(std::abs(output[1].v[3]-output[0].v[3])<1.1f,"child remains attached to presented parent");
        float length=std::sqrt(output[0].v[0]*output[0].v[0]+output[0].v[4]*output[0].v[4]);
        require(std::abs(length-1)<0.0001f,"bone rotations do not shrink during interpolation");
    }
    require(updates[0]==30&&updates[1]==30,"physics rate stays 30 Hz for each coupled block");
    require(renderChanges>100,"presentation uses high frame rate rather than repeated 30 Hz poses");
    deltaValue=0;auto paused=output[0];auto beforePause=updates[0];
    for(int i=0;i<10;++i){++frame;hookItemCtrl(item.data());}
    require(updates[0]==beforePause,"pause does not advance forces");
    for(int i=0;i<16;++i)require(std::abs(output[0].v[i]-paused.v[i])<0.0001f,"pause retains identical displayed pose");
    deltaValue=1;mode=0;++epoch;++frame;hookItemCtrl(item.data());
    require(states.empty(),"disabling clears all interpolated histories");
    require(!withinItem&&!suspendPhysics&&!fixedScope&&!cacheRegions,"thread-local scopes do not leak");
    }
    // Isolate presentation without changing forces. A held pose must keep its
    // native rotations and restore all parent matrices before the next update.
    interpolateOsage=0;smoothOsage=0;mode=1;++epoch;states.clear();
    deltaValue=1;animationValue=.5f;root=physics::identity();
    for(int i=0;i<2;++i){updates[i]=0;output[i]=physics::identity();}
    float heldAngle=0;int lastUpdates=0;
    for(int i=0;i<80;++i) {
        ++frame;root.v[3]=i*.15f;hookItemCtrl(item.data());
        float angle=std::atan2(output[0].v[4],output[0].v[0]);
        require(std::abs(angle-updates[0]*.04f)<.0001f,"disabled interpolation displays latest native rotation without blending");
        if(updates[0]==lastUpdates)require(std::abs(angle-heldAngle)<.0001f,"held physics pose does not acquire intermediate rotations");
        require(std::abs(output[0].v[3]-root.v[3])<.0001f,"held pose follows high-FPS body anchor");
        require(states[object[0]].interpolated&&states[object[1]].interpolated,"held parent matrices are marked for simulation restoration");
        lastUpdates=updates[0];heldAngle=angle;
    }
    require(updates[0]==20&&updates[1]==20,"held display keeps identical 30 Hz simulation");
    auto pausePose=output[0];auto count=updates[0];deltaValue=0;
    for(int i=0;i<8;++i){++frame;hookItemCtrl(item.data());}
    require(updates[0]==count,"hold mode pause does not advance forces");
    for(int i=0;i<16;++i)require(std::abs(output[0].v[i]-pausePose.v[i])<.0001f,"hold mode pause retains exact pose");
    deltaValue=1;interpolateOsage=1;++frame;hookItemCtrl(item.data());
    require(states[object[0]].valid&&states[object[1]].valid,"hot interpolation re-enable preserves native histories");
    std::puts("PASS: interpolated, smoothed and held display preserve native simulation inputs, 30 Hz pacing, high-FPS anchors, parent restoration, pause and hot switching");
}
