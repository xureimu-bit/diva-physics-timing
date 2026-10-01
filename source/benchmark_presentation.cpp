#include "DivaPhysicsTiming.cpp"
static float fakeDelta(){return 1;}
static float fakeAnimation(){return 0.5f;}
static void fakeStage(Block,int,bool){}
static void fakeEnd(Block){}
static void fakeMain(Block b,bool,bool,float,float){
    auto root=at<Mat*>(at<Block>(b,0x20),0x48);
    auto n=at<Block>(b,0xA0),end=at<Block>(b,0xA8);
    for(;n<end;n+=0x1D0){*at<Mat*>(n,0x40)=*root;auto bone=at<Block>(n,0x38);*at<Mat*>(bone,0x48)=*root;}
}
static void fakeItem(Block item){
    auto blocks=at<Block>(item,0xC0),end=at<Block>(item,0xC8);
    for(int stage=0;stage<6;++stage)for(auto b=blocks;b<end;b+=8)hookOsageStep(at<Block>(b,0),stage,false);
    for(auto b=blocks;b<end;b+=8)hookOsageMain(at<Block>(b,0),false);
    for(auto b=blocks;b<end;b+=8)hookOsageEnd(at<Block>(b,0));
}
int main(){
    gameDelta=fakeDelta;animationDelta=fakeAnimation;osageMain=fakeMain;osageStep=fakeStage;osageEnd=fakeEnd;itemCtrl=fakeItem;
    constexpr size_t count=109,nodeCount=8;
    struct Fixture {std::vector<unsigned char> block,node,parent,bone;std::vector<Mat> output;};
    std::vector<Fixture> fixtures(count);std::vector<unsigned char> item(0x208),owner(0x950),blocks(count*8);
    Mat root=physics::identity();
    for(size_t i=0;i<count;++i){
        auto& f=fixtures[i];f.block.resize(0x2500);f.node.resize(nodeCount*0x1D0);f.parent.resize(0x50);f.bone.resize(nodeCount*0x50);f.output.resize(nodeCount*2,physics::identity());
        auto b=f.block.data();at<Block>(b,0x20)=f.parent.data();at<Mat*>(f.parent.data(),0x48)=&root;at<Block>(b,0x50)=item.data();
        at<Block>(b,0xA0)=f.node.data();at<Block>(b,0xA8)=f.node.data()+f.node.size();at<Block>(b,0xB0)=f.node.data()+f.node.size();
        for(size_t n=0;n<nodeCount;++n){auto node=f.node.data()+n*0x1D0, bone=f.bone.data()+n*0x50;at<Block>(node,0x38)=bone;at<Mat*>(node,0x40)=&f.output[n*2];at<Mat*>(bone,0x48)=&f.output[n*2+1];}
        at<Block>(blocks.data(),i*8)=b;
    }
    at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;
    at<Block>(item.data(),0xC0)=blocks.data();at<Block>(item.data(),0xC8)=blocks.data()+blocks.size();at<Block>(item.data(),0xD0)=blocks.data()+blocks.size();
    armed=true;mode=1;
    for(int smoothing:{0,1}) {
    frame=100;states.clear();smoothOsage=smoothing;
    restoreUs=0;nativeUs=0;presentUs=0;invalid=0;
    for(auto& f:fixtures)for(auto& matrix:f.output)matrix=physics::identity();
    auto start=timestamp();
    for(int i=0;i<240;++i){++frame;root.v[3]=static_cast<float>(i)*0.01f;hookItemCtrl(item.data());}
    std::printf("BENCH smoothing=%d: 109 blocks x 8 nodes, %.3f ms/render frame; restore=%.3f native=%.3f presentation=%.3f ms/frame; invalid=%llu\n",
        smoothing,elapsedUs(start,timestamp())/240000.0,restoreUs.load()/240000.0,nativeUs.load()/240000.0,presentUs.load()/240000.0,static_cast<unsigned long long>(invalid.load()));
    }
}
