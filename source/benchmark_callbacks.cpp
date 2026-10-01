#ifdef PHYSICS_PERF_OLD
#include "../../../outputs/DivaPhysicsTiming-0.1.10/source/DivaPhysicsTiming.cpp"
#else
#include "DivaPhysicsTiming.cpp"
#endif
#include <cstdlib>
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
 if(count<1||count>1024)return 2;
 gameDelta=fakeDelta;animationDelta=fakeAnimation;osageMain=fakeMain;osageStep=fakeStage;osageEnd=fakeEnd;itemCtrl=fakeItem;
 rootCtrl=fakeCtrl;rootBegin=fakeBegin;rootApply=fakeApply;rootTarget=fakeTarget;
 struct F {std::vector<unsigned char> b,n,p,bone;std::vector<Mat> output;};
 constexpr size_t nodes=8;
 std::vector<F> fixture(count);std::vector<unsigned char> item(0x208),owner(0x950),list(count*8);Mat root=physics::identity();
 for(size_t i=0;i<count;++i){auto& f=fixture[i];f.b.resize(0x2500);f.n.resize(nodes*0x1D0);f.p.resize(0x50);f.bone.resize(nodes*0x50);f.output.resize(nodes*2,root);
  auto b=f.b.data();at<Block>(b,0x20)=f.p.data();at<Mat*>(f.p.data(),0x48)=&root;at<Mat*>(b,0x1F30)=&root;at<Block>(b,0x50)=item.data();
  at<Block>(b,0xA0)=f.n.data();at<Block>(b,0xA8)=f.n.data()+f.n.size();at<Block>(b,0xB0)=f.n.data()+f.n.size();
  for(size_t n=0;n<nodes;++n){auto node=f.n.data()+n*0x1D0,bone=f.bone.data()+n*0x50;at<Block>(node,0x38)=bone;at<Mat*>(node,0x40)=&f.output[n*2];at<Mat*>(bone,0x48)=&f.output[n*2+1];}
  at<Block>(list.data(),i*8)=b;
 }
 at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;at<Block>(item.data(),0xC0)=list.data();at<Block>(item.data(),0xC8)=list.data()+list.size();at<Block>(item.data(),0xD0)=list.data()+list.size();
 armed=true;mode=1;captureRoots=1;frame=100;
 for(int i=0;i<20;++i){++frame;hookItemCtrl(item.data());}
 restoreUs=0;nativeUs=0;presentUs=0;invalid=0;
 auto start=timestamp();for(int i=0;i<240;++i){++frame;root.v[3]=i*.01f;hookItemCtrl(item.data());}
 std::printf("CALLBACK BENCH %zu blocks x 8 nodes, captureRoots=1: total %.3f ms/frame, restore %.3f native %.3f present %.3f, invalid=%llu\n",count,elapsedUs(start,timestamp())/240000.,restoreUs.load()/240000.,nativeUs.load()/240000.,presentUs.load()/240000.,static_cast<unsigned long long>(invalid.load()));
 return invalid.load()?1:0;
}