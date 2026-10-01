#ifdef PHYSICS_LOCK_OLD
#include "../../../outputs/DivaPhysicsTiming-0.1.11/source/DivaPhysicsTiming.cpp"
#else
#include "DivaPhysicsTiming.cpp"
#endif
#include <thread>
#include <chrono>
#include <memory>
#include <cstdlib>

static void require(bool ok,const char* text){if(!ok){std::printf("FAIL %s\n",text);std::exit(1);}}
struct Actor {
    std::vector<unsigned char> block,nodes,parent,item,owner,blocks;
    Mat root,output[2];
    std::atomic<int> mains{0},ends{0},stages{0};
    Actor():block(0x2500),nodes(2*0x1D0),parent(0x50),item(0x208),owner(0x950),blocks(8),root(physics::identity()) {
        auto b=block.data();at<Block>(b,0x20)=parent.data();at<Mat*>(parent.data(),0x48)=&root;
        at<Block>(b,0x50)=item.data();at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;
        at<Block>(item.data(),0xC0)=blocks.data();at<Block>(item.data(),0xC8)=blocks.data()+8;at<Block>(item.data(),0xD0)=blocks.data()+8;at<Block>(blocks.data(),0)=b;
        at<Block>(b,0xA0)=nodes.data();at<Block>(b,0xA8)=nodes.data()+nodes.size();at<Block>(b,0xB0)=nodes.data()+nodes.size();
        for(int i=0;i<2;++i){output[i]=root;at<Mat*>(nodes.data()+i*0x1D0,0x40)=&output[i];}
    }
};
static std::vector<Actor*> actors;
static std::atomic<int> running{0},maxRunning{0};
static bool overlapProof=false,delayWork=false;
static Actor& fromItem(Block item){for(auto a:actors)if(a->item.data()==item)return *a;std::abort();}
static Actor& fromBlock(Block b){return fromItem(at<Block>(b,0x50));}
static float fakeDelta(){return 1.f;}
static float fakeAnimation(){return .5f;}
static void fakeMain(Block b,bool,bool,float,float){
    auto& a=fromBlock(b);require(!cacheRegions,"native callback has no permission-cache scope");
    require(PhysicsAnimationDelta()==2.f,"each actor retains physics-only corrected time");
    require(ownerStep(b)==1.f,"actor owner step is unchanged");
    ++a.mains;for(auto& output:a.output){output=a.root;output.v[7]+=static_cast<float>(a.mains.load())*.01f;}
}
static void fakeStage(Block b,int stage,bool disabled){require(stage>=0&&stage<6&&!disabled,"native stage parameters preserved");++fromBlock(b).stages;}
static void fakeEnd(Block b){++fromBlock(b).ends;}
static void fakeReset(Block){}
static void fakeItem(Block item){
    auto& a=fromItem(item);
#ifndef PHYSICS_LOCK_OLD
    require(lockedItem==item&&lockedActor==actorIndex(item),"owning actor lock covers native item callback");
#endif
    auto live=running.fetch_add(1)+1;auto maximum=maxRunning.load();
    while(live>maximum&&!maxRunning.compare_exchange_weak(maximum,live)){}
    if(overlapProof){
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        while(maxRunning.load()<2&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } else if(delayWork)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    for(int i=0;i<6;++i)hookOsageStep(a.block.data(),i,false);
    hookOsageMain(a.block.data(),false);hookOsageEnd(a.block.data());
    --running;
}
static void pair(Actor& a,Actor& b){
    std::thread first([&]{hookItemCtrl(a.item.data());require(!fixedScope&&!withinItem&&!suspendPhysics,"first worker scopes restored");});
    std::thread second([&]{hookItemCtrl(b.item.data());require(!fixedScope&&!withinItem&&!suspendPhysics,"second worker scopes restored");});
    first.join();second.join();
}
int main(){
    gameDelta=fakeDelta;animationDelta=fakeAnimation;osageStep=fakeStage;osageMain=fakeMain;osageEnd=fakeEnd;osageReset=fakeReset;clothReset=fakeReset;itemCtrl=fakeItem;
    armed=true;mode=1;frame=100;
    Actor a;std::unique_ptr<Actor> b;
    std::vector<std::unique_ptr<Actor>> collisions;
    for(int trial=0;trial<256;++trial){
        auto candidate=std::make_unique<Actor>();
#ifndef PHYSICS_LOCK_OLD
        if(actorIndex(a.item.data())==actorIndex(candidate->item.data())){collisions.push_back(std::move(candidate));continue;}
#endif
        b=std::move(candidate);break;
    }
    require(b!=nullptr,"independent actor domains available");actors={&a,b.get()};
    overlapProof=true;pair(a,*b);overlapProof=false;
    require(maxRunning.load()==2,"independent characters execute native jobs concurrently");
    clear(a.block.data());clear(b->block.data());a.mains=0;b->mains=0;a.stages=0;b->stages=0;a.ends=0;b->ends=0;++epoch;
    for(int i=0;i<240;++i){
        ++frame;a.root.v[3]=static_cast<float>(i);b->root.v[3]=-static_cast<float>(i);
        pair(a,*b);
        require(std::abs(a.output[0].v[3]-a.root.v[3])<1e-4f&&std::abs(b->output[0].v[3]-b->root.v[3])<1e-4f,"parallel display keeps each actor's current animated root");
    }
    require(a.mains==60&&b->mains==60&&a.stages==360&&b->stages==360&&a.ends==240&&b->ends==240,"parallel worker migration preserves independent 30 Hz clocks and high-FPS references");
    clear(a.block.data());clear(b->block.data());
    at<Block>(b->item.data(),0x200)=a.owner.data();maxRunning=0;delayWork=true;
    for(int i=0;i<20;++i){++frame;pair(a,*b);}
    delayWork=false;require(maxRunning.load()==1,"items sharing one character remain serial");
    clear(a.block.data());clear(b->block.data());
    std::vector<std::unique_ptr<Actor>> many;actors.clear();
    for(int i=0;i<256;++i){many.push_back(std::make_unique<Actor>());actors.push_back(many.back().get());}
    ++frame;++epoch;std::vector<std::thread> workers;
    for(size_t worker=0;worker<8;++worker)workers.emplace_back([worker]{for(size_t i=worker;i<actors.size();i+=8)hookItemCtrl(actors[i]->item.data());});
    for(auto& worker:workers)worker.join();
    require(states.size()==256,"concurrent registry growth retains every actor state");
    for(auto a2:actors){require(a2->mains==1&&states[a2->block.data()].valid,"concurrent registry rehash leaves captured poses valid");hookOsageReset(a2->block.data());}
    require(states.empty(),"native reset safely clears all actor histories");
    std::puts("PASS: independent characters overlap, shared-owner items serialize, parallel worker migration preserves 30 Hz/anchors/scopes, concurrent registry rehash and reset remain valid");
}