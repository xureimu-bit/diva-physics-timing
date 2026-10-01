#include "DivaPhysicsTiming.cpp"
#include <cstdlib>
#include <thread>
static void require(bool ok,const char* name){if(!ok){std::printf("FAIL %s\n",name);std::exit(1);}}
static void equal(const Mat& a,const Mat& b,const char* name,float tolerance=0.002f){
    for(int i=0;i<16;++i)require(std::abs(a.v[i]-b.v[i])<tolerance,name);
}
static float deltaValue=1,animationValue=0.8f;
static float fakeDelta(){return deltaValue;}
static float fakeAnimation(){return animationValue;}
static Mat body,output,expected,previousExpected;
static Block object=nullptr;
static int rootCalls=0,updates=0;
static bool staged=true,previousValid=false;
static Vec hsc{1.2f,1.3f,1.4f};
static Mat animated(float time) {
    Mat result=physics::rotation(physics::Quat{0,0,std::sin(time*.01f),std::cos(time*.01f)});
    result.v[12]=time*.3f;result.v[13]=time*.15f;result.v[14]=-time*.05f;
    return result;
}
static void verifyRoot(Block work,const Mat* root,const Vec* scale) {
    require(!cacheRegions,"root validation cache ends before every native root callback");require(work==object+0x68&&scale==&hsc,"native work and HSC pointers forwarded");
    equal(physics::fromGame(*root),expected,"root argument samples exact physics time");
    equal(physics::fromGame(*at<Mat*>(object,0x1F30)),expected,"private cancellation root matches sampled argument");
    ++rootCalls;
}
static void fakeBegin(Block work,const Mat* root,const Vec* scale,bool children) {
    require(children==staged,"child flag forwarded, including nested Begin");
    verifyRoot(work,root,scale);
    if(previousValid)equal(physics::fromGame(at<Mat>(object,0x1F38)),previousExpected,"native previous root retained at previous tick time");
}
static void fakeApply(Block work,const Mat* root,const Vec* scale,float step,bool disable,bool ring,bool children) {
    require(step==(staged?1.0f:2.0f)&&!disable&&ring==staged&&children==staged,"all seven ApplyPhysics arguments forwarded");
    verifyRoot(work,root,scale);
    hookRootBegin(work,root,scale,children);
    equal(physics::fromGame(*root),expected,"nested Begin does not transform the root twice");
    output=*root;
}
static void fakeTarget(Block work,const Mat* root,const Vec* scale,float step,bool collide) {
    require(step==(staged?1.0f:2.0f)&&collide!=staged,"CollideTarget step and fifth bool forwarded");
    verifyRoot(work,root,scale);
}
static void fakeFinish(){
    at<Mat>(object,0x1F38)=*at<Mat*>(object,0x1F30);
    previousExpected=expected;previousValid=true;++updates;
}
static void fakeCtrl(Block work,const Mat* root,const Vec* scale,float step) {
    require(!staged&&step==2.0f,"main-only ctrl keeps corrected native delta");
    verifyRoot(work,root,scale);
    hookRootApply(work,root,scale,step,false,false,false);
    hookRootTarget(work,root,scale,step,true);
    fakeFinish();
}
static void fakeStage(Block b,int stage,bool disable) {
    require(b==object&&!disable,"stage parameters forwarded");
    if(stage==0)hookRootBegin(b+0x68,&body,&hsc,true);
    if(stage==1)hookRootApply(b+0x68,&body,&hsc,1,false,true,true);
    if(stage==5){
        hookRootTarget(b+0x68,&body,&hsc,1,false);
        fakeFinish();at<unsigned char>(b,0x59)=1;
    }
}
static void fakeMain(Block b,bool changed,bool reset,float dt,float blend) {
    require(b==object&&changed&&!reset&&dt==.5f&&blend==.25f,"native virtual-call arguments unaffected");
    if(at<unsigned char>(b,0x59)){at<unsigned char>(b,0x59)=0;return;}
    hookRootCtrl(b+0x68,&body,&hsc,PhysicsAnimationDelta());
}
static void fakeEnd(Block){}
static void fakeItem(Block item) {
    require(at<Mat*>(object,0x1F30)==&body,"private root pointer restored before next item");
    if(staged)for(int stage=0;stage<6;++stage){
        hookOsageStep(object,stage,false);
        require(at<Mat*>(object,0x1F30)==&body,"private root pointer restored after each stage");
    }
    hookOsageMain(object,true,false,.5f,.25f);hookOsageEnd(object);
    require(at<Mat*>(object,0x1F30)==&body,"private root pointer restored after Main");
    require(at<Block>(object,0x50)==item,"owner pointer unaffected");
}
int main() {
    // Independent mathematical timing oracle: constant velocity and angular speed
    // must sample a uniform two-unit grid under both fixed and uneven render steps.
    for(int fps:{30,60,75,83,120,144,240}) {
        for(bool uneven:{false,true}){
            if(fps==30&&uneven)continue; // Sub-30 frames can skip force ticks; no catch-up replay is introduced.
            physics::Clock clock;physics::RootSampler sampler;double time=0,prior=-1;
            int count=0;
            for(int i=0;i<fps*2;++i){
                float elapsed=60.f/fps*(uneven?(i%2?.75f:1.25f):1.f);
                if(i)time+=elapsed;
                bool tick=clock.advance(elapsed);Mat sample;
                bool ready=sampler.sample(animated(static_cast<float>(time)),elapsed,clock.remainder,tick,sample);
                require(ready==tick,"root sampler follows force clock exactly");
                if(tick){
                    double sampledTime=time-clock.remainder;
                    equal(sample,animated(static_cast<float>(sampledTime)),"noninteger FPS samples exact tick-time animation");
                    if(prior>=0)require(std::abs(sampledTime-prior-2)<.001,"fixed root sample interval is 33.33 ms");
                    prior=sampledTime;++count;
                }
            }
            require(count>=59&&count<=61,"30 Hz retained with uneven display frame intervals");
            Mat paused;auto before=sampler.previous;
            require(!sampler.sample(before,0,clock.remainder,false,paused),"pause produces no root update");
            equal(paused,before,"paused anchor is unchanged");
            sampler.reset();Mat current=animated(500);
            require(sampler.sample(current,.7f,.3,true,paused),"seek acquires fresh root");
            equal(paused,current,"reset does not interpolate across a seek");
        }
    }
    gameDelta=fakeDelta;animationDelta=fakeAnimation;
    rootBegin=fakeBegin;rootApply=fakeApply;rootTarget=fakeTarget;rootCtrl=fakeCtrl;
    osageMain=fakeMain;osageStep=fakeStage;osageEnd=fakeEnd;itemCtrl=fakeItem;
    std::vector<unsigned char> storage(0x2500),nodes(0x1D0),parent(0x50),item(0x208),owner(0x950),blocks(8),parentBlock(0x60);
    object=storage.data();
    at<Block>(object,0x20)=parent.data();at<Mat*>(parent.data(),0x48)=&body;
    at<Block>(object,0x50)=item.data();at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;
    at<Block>(object,0xA0)=nodes.data();at<Block>(object,0xA8)=nodes.data()+nodes.size();at<Block>(object,0xB0)=nodes.data()+nodes.size();
    at<Mat*>(nodes.data(),0x40)=&output;at<Mat*>(object,0x1F30)=&body;
    at<Block>(blocks.data(),0)=object;at<Block>(item.data(),0xC0)=blocks.data();
    at<Block>(item.data(),0xC8)=blocks.data()+8;at<Block>(item.data(),0xD0)=blocks.data()+8;
    armed=true;mode=1;resampleRoots=1;frame=100;
    for(int fps:{30,60,75,83,120,144})for(bool useStages:{false,true}) {
        staged=useStages;states.clear();++epoch;updates=rootCalls=0;previousValid=false;
        rootSkipped=0;rootSampled=0;rootArguments=0;
        animationValue=60.f/fps;deltaValue=1;double time=0;
        physics::Clock oracle;
        for(int i=0;i<fps*2;++i){
            if(i)time+=animationValue;
            ++frame;body=physics::toGame(animated(static_cast<float>(time)));
            bool tick=oracle.advance(animationValue);
            expected=animated(static_cast<float>(time-oracle.remainder));
            // Alternate workers; the only root substitution is callback-local.
            if(i%2){std::thread worker([&]{hookItemCtrl(item.data());require(!rootContext.work&&!rootContext.depth,"worker root scope is balanced");});worker.join();}
            else hookItemCtrl(item.data());
            auto& state=states[object];
            require(state.tick==tick,"adapter and independent clock agree");
            equal(physics::fromGame(body),animated(static_cast<float>(time)),"shared animated body matrix never modified");
            if(tick)equal(state.root,expected,"captured pose remembers its sampled simulation root");
            require(!rootContext.work&&!rootContext.depth,"root context does not leak across callbacks");
            require(ownerStep(object)==1,"owner time scale is never rewritten");
        }
        require(updates>=59&&updates<=61&&rootCalls>updates,"Main-only and staged native pipeline retain 30 Hz");
        require(rootSkipped==0&&rootSampled>0&&rootArguments>0,"all validated roots sampled without pointer failures");
        deltaValue=0;auto paused=output;int count=updates;
        for(int i=0;i<5;++i){++frame;hookItemCtrl(item.data());}
        require(updates==count,"pause freezes resampled native forces");
        equal(output,paused,"pause preserves displayed bones");
        deltaValue=1;
    }
    // A physical parent must remain on its own simulation timeline.
    states.clear();previousValid=false;staged=false;
    at<Block>(object,0x48)=parentBlock.data();at<int>(parentBlock.data(),0x10)=1;
    body=physics::toGame(animated(10));expected=physics::fromGame(body);
    rootSampled=0;rootDependent=0;++frame;hookItemCtrl(item.data());
    require(rootDependent==1&&rootSampled==0&&!states[object].sampleReady,"dependent osage parent never resampled");
    at<int>(parentBlock.data(),0x10)=4;states.clear();previousValid=false;++frame;hookItemCtrl(item.data());
    require(rootDependent==2&&rootSampled==0,"dependent cloth parent never resampled");
    at<Block>(object,0x48)=nullptr;
    // Argument adjustment preserves the caller's separate scale normalization.
    State state;state.sampleReady=true;state.inputRoot=animated(10);state.sampleRoot=animated(9.5f);body=physics::toGame(state.inputRoot);
    Mat scale=physics::identity();scale.v[0]=.5f;scale.v[5]=.75f;scale.v[10]=.6f;
    Mat argument=physics::toGame(physics::mul(scale,state.inputRoot));
    {
        RootScope scope(object,state);
        {RootArgument value(object+0x68,&argument);equal(physics::fromGame(*value.value),physics::mul(scale,state.sampleRoot),"scale-normalized caller root preserved");
            RootArgument nested(object+0x68,value.value);require(nested.value==value.value,"nested argument pointer passed through");}
        require(rootContext.depth==0,"argument scopes balanced");
        equal(physics::fromGame(*at<Mat*>(object,0x1F30)),state.sampleRoot,"private root uses original scale");
    }
    require(at<Mat*>(object,0x1F30)==&body,"private root pointer restored after scope exit");
    // Missing pointers fail closed without changing the shared source.
    at<Mat*>(object,0x1F30)=nullptr;{RootScope scope(object,state);require(!rootContext.resampling,"invalid private root never activates resampling; read-only input tracking may remain");}
    require(at<Mat*>(object,0x1F30)==nullptr,"invalid root pointer left intact");at<Mat*>(object,0x1F30)=&body;
    // Setting the option off forwards the root address itself.
    resampleRoots=0;states.clear();previousValid=false;expected=physics::fromGame(body);
    ++frame;hookItemCtrl(item.data());
    {RootArgument value(object+0x68,&body);require(value.value==&body,"disabled sampling is exact pointer passthrough");}
    require(!states[object].rootApplied,"disabled sampling retains existing presentation inputs");
    std::puts("PASS: exact 30 Hz animated roots at 30/60/75/83/120/144/240 FPS and uneven intervals; private-pointer restoration, nested calls, all arguments, worker migration, physical parents, pause, seek, scaling, fail-closed and passthrough");
}
