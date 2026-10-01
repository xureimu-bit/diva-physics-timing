#include "DivaPhysicsTiming.cpp"
#include <cstdlib>
static void require(bool ok,const char* name){if(!ok){std::printf("FAIL %s\n",name);std::exit(1);}}
static float fakeDelta(){return 1;}
static float fakeAnimation(){return .5f;}
static Mat body,output,usedRoot,afterRoot;
static Block object;
static int updates=0;static bool staged=false;
static void fakeCtrl(Block work,const Mat* root,const Vec*,float){
    require(work==object+0x68,"correct root work");
    usedRoot=physics::fromGame(*root);
    Mat local=physics::identity();local.v[12]=1;
    output=physics::toGame(physics::mul(local,usedRoot));++updates;
}
static void fakeMain(Block b,bool,bool,float,float){
    if(at<unsigned char>(b,0x59)){at<unsigned char>(b,0x59)=0;return;}
    static Vec hsc{1,1,1};hookRootCtrl(b+0x68,&body,&hsc,PhysicsAnimationDelta());
}
static void fakeEnd(Block){}
static void fakeTarget(Block work,const Mat* root,const Vec* hsc,float step,bool){fakeCtrl(work,root,hsc,step);}
static void fakeStage(Block b,int stage,bool) {
    if(stage==5) {
        static Vec hsc{1,1,1};hookRootTarget(b+0x68,&body,&hsc,1,false);at<unsigned char>(b,0x59)=1;
    }
}
static void fakeItem(Block){
    if(staged)for(int stage=0;stage<6;++stage)hookOsageStep(object,stage,false);
    hookOsageMain(object,false);
    // An animation/expression block later in the native item updates the anchor.
    // Physics results remain in the earlier root's coordinate system.
    body=physics::toGame(afterRoot);
    hookOsageEnd(object);
}
int main(){
    gameDelta=fakeDelta;animationDelta=fakeAnimation;osageMain=fakeMain;osageEnd=fakeEnd;osageStep=fakeStage;rootCtrl=fakeCtrl;rootTarget=fakeTarget;itemCtrl=fakeItem;
    std::vector<unsigned char> storage(0x2500),nodes(0x1D0),parent(0x50),item(0x208),owner(0x950),blocks(8);
    object=storage.data();at<Block>(object,0x20)=parent.data();at<Mat*>(parent.data(),0x48)=&body;
    at<Block>(object,0x50)=item.data();at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;
    at<Block>(object,0xA0)=nodes.data();at<Block>(object,0xA8)=nodes.data()+nodes.size();at<Block>(object,0xB0)=nodes.data()+nodes.size();
    at<Mat*>(nodes.data(),0x40)=&output;at<Mat*>(object,0x1F30)=&body;
    at<Block>(blocks.data(),0)=object;at<Block>(item.data(),0xC0)=blocks.data();at<Block>(item.data(),0xC8)=blocks.data()+8;at<Block>(item.data(),0xD0)=blocks.data()+8;
    armed=true;mode=1;resampleRoots=0;frame=100;
    for(bool path:{false,true}) {
    staged=path;states.clear();++epoch;updates=0;
    for(int i=0;i<120;++i){
        body=physics::toGame(physics::identity());body.v[3]=i*.02f;
        afterRoot=physics::fromGame(body);afterRoot.v[12]+=.1f*std::sin(i*.15f);
        ++frame;hookItemCtrl(item.data());auto& state=states[object];
        if(state.tick)for(int j=0;j<16;++j)
            require(std::abs(state.root.v[j]-usedRoot.v[j])<.00001f,"pose stores the root actually used by physics, before later animation updates");
        require(std::abs(output.v[3]-body.v[3]-1)<.0001f,"late anchor movement cannot introduce artificial local deformation");
        require(at<Mat*>(object,0x1F30)==&body,"tracking never substitutes the private pointer when resampling is off");
    }
    require(updates==30,"correct capture does not change physics frequency");
    }
    captureRoots=0;++epoch;++frame;body=physics::identity();afterRoot=physics::identity();afterRoot.v[12]=.2f;
    hookItemCtrl(item.data());
    for(int j=0;j<16;++j)require(std::abs(states[object].root.v[j]-afterRoot.v[j])<.00001f,"disabling callback capture reproduces legacy late-root metadata");
    require(!rootContext.work&&!rootContext.depth,"capture contexts are restored");
    std::puts("PASS: later animation/expression root updates cannot corrupt pose coordinates or introduce movement-dependent display jitter");
}
