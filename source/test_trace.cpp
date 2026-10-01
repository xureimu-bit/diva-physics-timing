#include "DivaPhysicsTiming.cpp"
#include <cstdlib>
#include <thread>
static void require(bool ok,const char* label){if(!ok){std::printf("FAIL %s\n",label);std::exit(1);}}
int main(){
    std::vector<unsigned char> block(0x2500),nodes(5*0x1D0),parent(0x50),skin(0x80),item(0x208),otherItem(0x208);
    char name[64]="c_tail_l_osg";auto b=block.data(),work=b+0x68;
    Mat argument=physics::identity(),privateRoot=physics::identity();argument.v[3]=1;privateRoot.v[3]=2;
    at<const char*>(b,0x18)=name;at<Block>(b,0x20)=parent.data();at<Mat*>(parent.data(),0x48)=&argument;
    at<Block>(b,0x50)=item.data();at<Block>(work,0)=skin.data();
    at<Block>(b,0xA0)=nodes.data();at<Block>(b,0xA8)=nodes.data()+nodes.size();at<Block>(b,0xB0)=nodes.data()+nodes.size();
    at<Mat*>(work,0x1EC8)=&privateRoot;at<Mat>(work,0x1ED0)=physics::identity();
    at<float>(skin.data(),0x44)=.3f;at<float>(skin.data(),0x20)=.7f;at<float>(skin.data(),0x40)=.8f;
    for(int i=0;i<5;++i){auto n=nodes.data()+i*0x1D0;at<Vec>(n,4)={static_cast<float>(i),2,3};at<Vec>(n,0x10)={4,5,6};at<Vec>(n,0x1C)={7,8,9};at<Vec>(n,0x28)={10,11,12};at<float>(n,0xC8)=2;at<float>(n,0xCC)=.5f;}
    at<int>(work,0x2B0)=1;at<float>(work,0x2B4)=.2f;at<Vec>(work,0x2B8)={1,2,3};
    auto oldBlock=block,oldNodes=nodes;auto oldArgument=argument,oldPrivate=privateRoot;
    State state;rootContext.work=work;rootContext.state=&state;frame=100;
    motionTraceEnd=0;traceMotion(work,&argument,1,2);require(DivaPhysicsTraceCount==0,"disabled tracing emits no record");
    startMotionTrace(1,30);traceMotion(work,&argument,1,2);traceMotion(work,&argument,2,2);traceMotion(work,&argument,3,2);
    require(DivaPhysicsTraceCount==3&&DivaPhysicsTraceCookie==1,"records committed under one cookie");
    const auto& r=DivaPhysicsTrace[0];require(r.frame==100&&r.period==2&&r.step==2&&r.nodeCount==5,"frame and native time metadata captured");
    require(r.argument.v[3]==1&&r.privateCurrent.v[3]==2&&r.privatePrevious.v[3]==0,"separate root inputs preserved in native layout");
    require(r.nodes[2].index==2&&r.nodes[3].index==4&&r.nodes[3].position.x==4&&r.nodes[1].displacement.x==7,"root, bend and tip node position/velocity captured");
    require(r.collisionCount==1&&r.collisions[0].type==1&&r.collisions[0].a.y==2,"native character collider snapshot captured");
    require(block==oldBlock&&nodes==oldNodes&&std::memcmp(&argument,&oldArgument,sizeof(Mat))==0&&std::memcmp(&privateRoot,&oldPrivate,sizeof(Mat))==0,"diagnostic never changes native block, nodes, root inputs or collider bytes");
    at<Block>(b,0x50)=otherItem.data();traceMotion(work,&argument,1,2);require(DivaPhysicsTraceCount==3,"other actor/item excluded from selected trace");at<Block>(b,0x50)=item.data();
    strcpy_s(name,sizeof name,"unrelated_hair");traceMotion(work,&argument,1,2);require(DivaPhysicsTraceCount==3,"unselected block ignored");strcpy_s(name,sizeof name,"c_tail_l_osg");
    DivaPhysicsTraceCount=DivaPhysicsTraceCapacity;traceMotion(work,&argument,1,2);require(DivaPhysicsTraceCount==DivaPhysicsTraceCapacity,"buffer capacity cannot overrun");
    startMotionTrace(2,30);require(DivaPhysicsTraceCount==0&&DivaPhysicsTraceCookie==2,"new token clears prior committed prefix");
    auto worker=[&]{rootContext.work=work;rootContext.state=&state;for(int i=0;i<100;++i)traceMotion(work,&argument,4,0);};
    std::thread a(worker),c(worker);a.join();c.join();require(DivaPhysicsTraceCount==200,"migrating workers serialize diagnostic commits");
    for(unsigned i=0;i<200;++i)require(DivaPhysicsTrace[i].cookie==2&&DivaPhysicsTrace[i].phase==4,"committed records remain complete and from current cookie");
    motionTraceEnd=GetTickCount64()-1;traceMotion(work,&argument,1,2);require(DivaPhysicsTraceCount==200,"expired diagnostic stops without changing solver");
    motionTraceEnd=0;rootContext={};traceMotion(nullptr,nullptr,1,2);require(DivaPhysicsTraceCount==200,"disabled path never dereferences callback pointers");
    std::puts("PASS: bounded native-phase snapshots preserve every native byte; immutable committed prefix, selected owner, capacity, cookie reset, worker migration, disabled and expiry paths");
}

