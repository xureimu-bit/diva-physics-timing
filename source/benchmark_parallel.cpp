#ifdef PHYSICS_LOCK_OLD
#include "../../../outputs/DivaPhysicsTiming-0.1.11/source/DivaPhysicsTiming.cpp"
#else
#include "DivaPhysicsTiming.cpp"
#endif
#include <cstdlib>
#include <thread>
#include <condition_variable>
#include <array>
static float fakeDelta(){return 1.f;}
static float fakeAnimation(){return .5f;}
static Vec hsc{1,1,1};
static void fakeBegin(Block,const Mat*,const Vec*,bool){}
static void fakeApply(Block work,const Mat* root,const Vec* scale,float,bool,bool,bool){hookRootBegin(work,root,scale,true);}
static void fakeTarget(Block,const Mat*,const Vec*,float,bool){}
static void fakeCtrl(Block work,const Mat* root,const Vec* scale,float){hookRootBegin(work,root,scale,true);}
static void fakeStage(Block b,int stage,bool){auto root=at<Mat*>(at<Block>(b,0x20),0x48);if(stage==0)hookRootBegin(b+0x68,root,&hsc,true);if(stage==1)hookRootApply(b+0x68,root,&hsc,1,false,true,true);if(stage==5)hookRootTarget(b+0x68,root,&hsc,1,false);}
static void fakeEnd(Block){}
static void fakeMain(Block b,bool,bool,float,float){
 auto root=at<Mat*>(at<Block>(b,0x20),0x48);hookRootCtrl(b+0x68,root,&hsc,PhysicsAnimationDelta());
 auto n=at<Block>(b,0xA0),end=at<Block>(b,0xA8);
 for(;n<end;n+=0x1D0){*at<Mat*>(n,0x40)=*root;auto bone=at<Block>(n,0x38);*at<Mat*>(bone,0x48)=*root;}
}
static void fakeItem(Block item){
 auto first=at<Block>(item,0xC0),last=at<Block>(item,0xC8);
 for(int stage=0;stage<6;++stage)for(auto b=first;b<last;b+=8)hookOsageStep(at<Block>(b,0),stage,false);
 for(auto b=first;b<last;b+=8)hookOsageMain(at<Block>(b,0),false);
 for(auto b=first;b<last;b+=8)hookOsageEnd(at<Block>(b,0));
}
int main(int argc,char** argv){
 size_t count=argc>1?std::strtoul(argv[1],nullptr,10):278;
 if(count<2||count>2048||count%2)return 2;
 gameDelta=fakeDelta;animationDelta=fakeAnimation;osageMain=fakeMain;osageStep=fakeStage;osageEnd=fakeEnd;itemCtrl=fakeItem;
 rootCtrl=fakeCtrl;rootBegin=fakeBegin;rootApply=fakeApply;rootTarget=fakeTarget;
 struct F {std::vector<unsigned char> b,n,p,bone;std::vector<Mat> output;};
 constexpr size_t nodes=8;
 std::vector<F> fixture(count);
 std::array<std::vector<unsigned char>,2> items,owners,lists;
 std::array<Mat,2> roots{physics::identity(),physics::identity()};
 for(int actor=0;actor<2;++actor){items[actor].resize(0x208);owners[actor].resize(0x950);lists[actor].resize(count/2*8);at<Block>(items[actor].data(),0x200)=owners[actor].data();}
#ifndef PHYSICS_LOCK_OLD
 std::vector<std::vector<unsigned char>> collisions;
 while(actorIndex(items[0].data())==actorIndex(items[1].data())){
     collisions.push_back(std::move(owners[1]));owners[1]=std::vector<unsigned char>(0x950);at<Block>(items[1].data(),0x200)=owners[1].data();
 }
#endif
 for(size_t i=0;i<count;++i){auto actor=i/(count/2);auto& item=items[actor];auto& list=lists[actor];auto& root=roots[actor];auto& f=fixture[i];f.b.resize(0x2500);f.n.resize(nodes*0x1D0);f.p.resize(0x50);f.bone.resize(nodes*0x50);f.output.resize(nodes*2,root);
  auto b=f.b.data();at<Block>(b,0x20)=f.p.data();at<Mat*>(f.p.data(),0x48)=&root;at<Mat*>(b,0x1F30)=&root;at<Block>(b,0x50)=item.data();
  at<Block>(b,0xA0)=f.n.data();at<Block>(b,0xA8)=f.n.data()+f.n.size();at<Block>(b,0xB0)=f.n.data()+f.n.size();
  for(size_t n=0;n<nodes;++n){auto node=f.n.data()+n*0x1D0,bone=f.bone.data()+n*0x50;at<Block>(node,0x38)=bone;at<Mat*>(node,0x40)=&f.output[n*2];at<Mat*>(bone,0x48)=&f.output[n*2+1];}
  at<Block>(list.data(),(i%(count/2))*8)=b;
 }
 for(int actor=0;actor<2;++actor){auto& item=items[actor];auto& owner=owners[actor];auto& list=lists[actor];
 at<float>(owner.data(),0x938)=1;at<Block>(item.data(),0xC0)=list.data();at<Block>(item.data(),0xC8)=list.data()+list.size();at<Block>(item.data(),0xD0)=list.data()+list.size();}
 armed=true;mode=1;captureRoots=1;frame=100;
 std::mutex barrier;std::condition_variable begin,done;int round=0,finished=0;bool stop=false;
 std::array<std::thread,2> workers;
 for(int actor=0;actor<2;++actor)workers[actor]=std::thread([&,actor]{
     int seen=0;
     for(;;){std::unique_lock<std::mutex> lock(barrier);begin.wait(lock,[&]{return stop||round!=seen;});if(stop)return;seen=round;lock.unlock();
         hookItemCtrl(items[actor].data());lock.lock();++finished;done.notify_one();}
 });
 auto advance=[&](int i){std::unique_lock<std::mutex> lock(barrier);++frame;roots[0].v[3]=i*.01f;roots[1].v[3]=-i*.01f;finished=0;++round;begin.notify_all();done.wait(lock,[&]{return finished==2;});};
 for(int i=0;i<20;++i)advance(i);
 restoreUs=0;nativeUs=0;presentUs=0;invalid=0;itemLockWaitUs=0;
 auto start=timestamp();for(int i=0;i<240;++i)advance(i);auto total=elapsedUs(start,timestamp());
 {std::lock_guard<std::mutex> lock(barrier);stop=true;begin.notify_all();}
 for(auto& worker:workers)worker.join();
 std::printf("ACTOR BENCH 2 characters x %zu blocks x 8 nodes, captureRoots=1: wall %.3f ms/frame, aggregate restore %.3f native %.3f present %.3f lockWait %.3f, invalid=%llu\n",count/2,total/240000.,restoreUs.load()/240000.,nativeUs.load()/240000.,presentUs.load()/240000.,itemLockWaitUs.load()/240000.,static_cast<unsigned long long>(invalid.load()));
 return invalid.load()?1:0;
}