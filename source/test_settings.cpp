#include "DivaPhysicsTiming.cpp"
#include <cstdlib>
#include <thread>
static void require(bool ok,const char* text){if(!ok){std::printf("FAIL %s\n",text);std::exit(1);}}
static float animationValue=.5f,deltaValue=1.f;
static float fakeDelta(){return deltaValue;}
static float fakeAnimation(){return animationValue;}
struct Fixture {
    std::vector<unsigned char> block,nodes,parent,item,owner,blocks;
    Mat root=physics::identity(),output=physics::identity();std::vector<Mat> roots;
    int count=0,stages=0;bool cloth;float expectedPeriod=2.f;bool original=false;
    Fixture(const char* name,bool isCloth=false):block(0x2500),nodes(isCloth?4*0xF0:0x1D0),parent(0x50),item(0x208),owner(0x950),blocks(8),roots(2),cloth(isCloth) {
        auto b=block.data();at<int>(b,0x10)=cloth?4:1;at<const char*>(b,0x18)=name;
        at<Block>(b,0x20)=parent.data();at<Mat*>(parent.data(),0x48)=&root;
        at<Block>(b,0x50)=item.data();at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;
        at<Block>(item.data(),0xC0)=blocks.data();at<Block>(item.data(),0xC8)=blocks.data()+8;at<Block>(item.data(),0xD0)=blocks.data()+8;at<Block>(blocks.data(),0)=b;
        if(!cloth){at<Block>(b,0xA0)=nodes.data();at<Block>(b,0xA8)=nodes.data()+nodes.size();at<Block>(b,0xB0)=nodes.data()+nodes.size();at<Mat*>(nodes.data(),0x40)=&output;}
        else {at<size_t>(b,0x70)=2;at<Block>(b,0x80)=nodes.data();at<Block>(b,0x88)=nodes.data()+nodes.size();at<Block>(b,0x90)=nodes.data()+nodes.size();}
    }
};
static std::vector<Fixture*> fixtures;
static Fixture& fixture(Block b){for(auto f:fixtures)if(f->block.data()==b)return *f;std::exit(2);}
static void fakeMain(Block b,bool,bool,float,float){auto& f=fixture(b);require(fixedScope!=f.original,"enabled group scopes physics delta only");require(std::abs(PhysicsAnimationDelta()-(f.original?animationValue:f.expectedPeriod))<1e-5,"callback uses its group period, not another group's rate");require(ownerStep(b)==1,"shared owner step unchanged");++f.count;f.output=f.root;}
static void fakeStage(Block b,int,bool){++fixture(b).stages;require(!fixedScope,"staged time remains native");}
static void fakeEnd(Block){}
static void fakeNoop(Block){}
static void fakeClothRoot(Block r){auto b=r-0x60;auto& f=fixture(b);auto p=at<Block>(b,0x1DA8);for(int i=0;i<2;++i)at<Mat>(p+i*0x158,0x98)=f.root;}
static void fakeCloth(Block b,bool,bool,float,float){auto& f=fixture(b);require(fixedScope!=f.original,"cloth scope follows its separate enable option");require(std::abs(PhysicsAnimationDelta()-(f.original?animationValue:f.expectedPeriod))<1e-5,"cloth has clothing-local physics delta");++f.count;fakeClothRoot(b+0x60);auto p=at<Block>(b,0x80);for(int i=0;i<4;++i)at<Vec>(p+i*0xF0,4)={0,static_cast<float>(i),0};}
static void fakeItem(Block item){auto b=at<Block>(at<Block>(item,0xC0),0);auto& f=fixture(b);if(f.cloth)hookClothMain(b,false);else{for(int i=0;i<6;++i)hookOsageStep(b,i,false);hookOsageMain(b,false);hookOsageEnd(b);}}
static void reset(){states.clear();++epoch;for(auto f:fixtures)f->count=f->stages=0;}
int main(){
    Fixture hair("c_tail_l_u_osg"),skirt("c_skirt_l_01_osg"),body("c_opai_l_osg"),cloth("custom_cloth",true);
    std::vector<unsigned char> clothRoots(2*0x158);
    auto cb=cloth.block.data();at<Block>(cb,0x1DA8)=clothRoots.data();at<Block>(cb,0x1DB0)=clothRoots.data()+clothRoots.size();at<Block>(cb,0x1DB8)=clothRoots.data()+clothRoots.size();
    for(int i=0;i<2;++i)at<Mat>(clothRoots.data()+i*0x158,0x98)=physics::identity();
    fixtures={&hair,&skirt,&body,&cloth};
    gameDelta=fakeDelta;animationDelta=fakeAnimation;itemCtrl=fakeItem;osageMain=fakeMain;osageStep=fakeStage;osageEnd=fakeEnd;osageOPD=fakeNoop;osageReset=fakeNoop;clothMain=fakeCloth;clothOPD=fakeNoop;clothReset=fakeNoop;clothRoot=fakeClothRoot;
    armed=true;mode=1;frame=100;captureRoots=0;physicsHz=30;
    using physics_settings::Group;
    require(classifyGroup(hair.block.data(),false)==Group::Hair,"tail classified as hair");
    require(classifyGroup(skirt.block.data(),false)==Group::Clothing,"osage skirt classified as clothing");
    require(classifyGroup(body.block.data(),false)==Group::Other,"body not included in skirt slider");
    require(classifyGroup(cloth.block.data(),true)==Group::Clothing,"cloth classified as clothing");
    for(const char* name:{"c_hair_f01_osg","c_fhair_osg","c_mhair_u_l_osg","c_tail_r_f2_osg"})require(physics_settings::namedGroup(name)==Group::Hair,"known character hair names recognized");
    for(const char* name:{"c_level_03_center_osg","c_belt01_osg","c_sleeve_l_osg","c_jacket_r_osg"})require(physics_settings::namedGroup(name)==Group::Clothing,"clothing names recognized");
    Fixture child("unknown_branch");at<Block>(child.block.data(),0x48)=hair.block.data();require(classifyGroup(child.block.data(),false)==Group::Hair,"unnamed physical branch inherits hair cadence");
    at<Block>(hair.block.data(),0x48)=child.block.data();require(classifyGroup(child.block.data(),false)==Group::Other,"cyclic parent graph falls back safely");at<Block>(hair.block.data(),0x48)=nullptr;
    for(auto pair:{std::pair<int,int>{40,30},{30,40},{45,25},{60,20}})for(int fps:{60,75,80,83,120,144,240}) {
        hairHz=pair.first;clothingHz=pair.second;hair.expectedPeriod=60.f/pair.first;skirt.expectedPeriod=cloth.expectedPeriod=60.f/pair.second;reset();animationValue=60.f/fps;
        for(int i=0;i<fps*2;++i){++frame;for(auto f:fixtures)hookItemCtrl(f->item.data());}
        for(auto f:fixtures){int rate=f==&hair?pair.first:f==&body?30:pair.second;require(f->count>=rate*2&&f->count<=rate*2+1,"hair, osage skirt, cloth and body retain independently selected cadence");if(!f->cloth)require(f->stages==f->count*6,"all stages share category cadence");}
        require(!fixedScope&&fixedPeriod==2.f&&!withinItem&&!suspendPhysics,"scopes restore per-thread delta and actor state");
    }
    hairHz=40;clothingHz=30;hair.expectedPeriod=1.5f;skirt.expectedPeriod=cloth.expectedPeriod=2;animationValue=.5f;hairEnabled=0;hair.original=true;reset();
    for(int i=0;i<120;++i){++frame;for(auto f:fixtures)hookItemCtrl(f->item.data());}
    require(hair.count==120&&skirt.count==30&&cloth.count==30&&body.count==30,"hair switch restores native calls without switching skirt/body off");
    hairEnabled=1;hair.original=false;clothingEnabled=0;skirt.original=cloth.original=true;reset();
    for(int i=0;i<120;++i){++frame;for(auto f:fixtures)hookItemCtrl(f->item.data());}
    require(hair.count==40&&skirt.count==120&&cloth.count==120&&body.count==30,"clothing switch restores osage and cloth independently");
    clothingEnabled=1;skirt.original=cloth.original=false;reset();
    for(int i=0;i<120;++i){++frame;for(auto f:fixtures)hookItemCtrl(f->item.data());}
    int previous=hair.count;deltaValue=0;for(int i=0;i<120;++i){++frame;for(auto f:fixtures)hookItemCtrl(f->item.data());}require(hair.count==previous,"paused established states do not advance");deltaValue=1;
    {Scope outer(1.5f);require(PhysicsAnimationDelta()==1.5f,"hair local scope");{Scope inner(3.f);require(PhysicsAnimationDelta()==3.f,"nested cloth local scope");}require(PhysicsAnimationDelta()==1.5f,"nested period restoration");}
    std::thread a([]{for(int i=0;i<1000;++i){Scope scope(1.5f);require(PhysicsAnimationDelta()==1.5f,"first worker delta isolated");}}),b([]{for(int i=0;i<1000;++i){Scope scope(3.f);require(PhysicsAnimationDelta()==3.f,"second worker delta isolated");}});a.join();b.join();
    hairInterpolation=0;clothingInterpolation=1;hairSmoothing=1;
    require(!interpolationEnabled(states[hair.block.data()])&&interpolationEnabled(states[skirt.block.data()]),"display switches separate hair and clothing");
    require(smoothingEnabled(states[hair.block.data()])&&!smoothingEnabled(states[skirt.block.data()]),"extra smoothing is hair-only");
    wchar_t directory[MAX_PATH]{},file[MAX_PATH]{};GetTempPathW(MAX_PATH,directory);require(GetTempFileNameW(directory,L"DPH",0,file)!=0,"temporary config allocated");configPath=file;
    WritePrivateProfileStringW(L"Physics",L"Mode",L"1",file);WritePrivateProfileStringW(L"Physics",L"CaptureRoots",L"1",file);WritePrivateProfileStringW(L"Physics",L"MotionTrace",L"0",file);WritePrivateProfileStringW(L"Physics",L"KeepUnknown",L"73",file);WritePrivateProfileStringW(L"Other",L"Keep",L"81",file);
    physics_settings::Controls request;request.hairHz=45;request.clothingHz=25;
    require(DivaPhysicsSetControls(&request),"valid panel request queued");require(hairHz.load()==40,"panel thread never mutates active worker settings");
    physics_settings::Controls snapshot;require(DivaPhysicsGetControls(&snapshot)&&snapshot.pending==1,"UI sees request pending");OnFrame();
    require(hairHz.load()==45&&clothingHz.load()==25,"next OnFrame saves and hot-applies category rates");
    require(DivaPhysicsGetControls(&snapshot)&&snapshot.pending==0&&snapshot.lastSaveError==0&&snapshot.revision==1,"UI sees committed active values");
    require(GetPrivateProfileIntW(L"Physics",L"KeepUnknown",0,file)==73&&GetPrivateProfileIntW(L"Other",L"Keep",0,file)==81&&captureRoots.load()==1,"persistence preserves unknown settings, other sections and diagnostics");
    auto previousRevision=snapshot.revision;request.hairHz=19;require(!DivaPhysicsSetControls(&request),"out-of-range panel rate rejected");request.hairHz=61;require(!DivaPhysicsSetControls(&request),"upper bound rejected");request.hairHz=30;request.mode=2;require(!DivaPhysicsSetControls(&request),"invalid switch rejected");request.mode=1;request.size=0;require(!DivaPhysicsSetControls(&request),"wrong ABI size rejected");require(!DivaPhysicsGetControls(nullptr)&&!DivaPhysicsSetControls(nullptr),"null API requests rejected");
    request={};configPath=std::wstring(file)+L"\\missing.ini";require(DivaPhysicsSetControls(&request),"queued request does not perform IO");OnFrame();require(DivaPhysicsGetControls(&snapshot)&&snapshot.lastSaveError!=0&&snapshot.revision==previousRevision,"failed save reported without committed revision");configPath=file;
    WritePrivateProfileStringW(L"Physics",L"HairHz",L"99",file);WritePrivateProfileStringW(L"Physics",L"ClothingHz",L"-5",file);readConfig();require(hairHz.load()==0&&clothingHz.load()==0,"invalid INI overrides inherit validated baseline");
    request={};require(DivaPhysicsSetControls(&request),"30 Hz default preset accepted");OnFrame();require(DivaPhysicsGetControls(&snapshot)&&snapshot.hairHz==30&&snapshot.clothingHz==30&&snapshot.hairSmoothing==0&&snapshot.mode==1,"default restoration hot-applies");
    DeleteFileW(file);configPath.clear();
    std::puts("PASS: independent hair/osage-skirt/cloth/body cadence at varied render FPS; group switches, parent inheritance, pause, nested/concurrent delta isolation, display controls, queued API, persistence, bad input/save failure and default restoration");
}
