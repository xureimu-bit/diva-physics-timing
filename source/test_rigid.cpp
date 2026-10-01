#ifdef PHYSICS_RIGID_OLD
#include "../before-v17/DivaPhysicsTiming.cpp"
#else
#include "DivaPhysicsTiming.cpp"
#endif
#include <cstdlib>
#include <thread>
static void require(bool value,const char* why){if(!value){std::printf("FAIL %s\n",why);std::exit(1);}}
static void equal(const Mat& a,const Mat& b,const char* why){for(int j=0;j<16;++j)require(std::abs(a.v[j]-b.v[j])<.0003f,why);}
static void equalVec(Vec a,Vec b,const char* why){require(std::abs(a.x-b.x)<.0003f&&std::abs(a.y-b.y)<.0003f&&std::abs(a.z-b.z)<.0003f,why);}
static float dt=1,ad=.75f;static float fakeDelta(){return dt;}static float fakeAnimation(){return ad;}
static Block objects[3]{};static Mat body,output[3],nativeOutput[3],renderBody,tickBody,prior[3];static Vec hsc{1,1,1};static int updates[3]{};static bool rigidTrial=true;
static Vec localA{.1f,.2f,.3f},localB{.4f,.5f,.6f},localAxis{.577350269f,.577350269f,.577350269f};
static int indexOf(Block w){for(int i=0;i<3;++i)if(w==objects[i]+0x68)return i;require(false,"known work");return 0;}
static Mat pose(double t){float half=static_cast<float>(t*.013);Mat r=physics::rotation(physics::Quat{0,std::sin(half)*.6f,std::sin(half)*.8f,std::cos(half)});r.v[12]=static_cast<float>(t*.007);r.v[13]=static_cast<float>(t*.002);r.v[14]=static_cast<float>(t*-.003);return r;}
static Mat expectedPrivate(int i){Mat r=renderBody;if(i<2){r.v[12]=tickBody.v[12];r.v[13]=tickBody.v[13];r.v[14]=tickBody.v[14];if(rigidTrial)r=tickBody;}return r;}
static Mat expectedRoot(int i){return i==1?physics::fromGame(nativeOutput[0]):expectedPrivate(i);}
static void verify(Block w,const Mat* root){
 int i=indexOf(w);equal(physics::fromGame(*root),expectedRoot(i),"root rotation and translation consume one uniform 30 Hz animation time");
 auto privateRoot=expectedPrivate(i);equal(physics::fromGame(*at<Mat*>(w,0x1EC8)),privateRoot,"dependent private ancestor uses the same sampled orientation without shifting child outputs twice");
 if(updates[i])equal(physics::fromGame(at<Mat>(w,0x1ED0)),prior[i],"previous root matches the previous native solve");
 require(at<Vec>(at<Block>(objects[i],0xA0),4).x==123,"internal physical positions are not overwritten");
 Mat collisionRoot=expectedPrivate(i);
 for(int j=0;j<4;++j){auto c=w+0x2B0+j*0x38;equalVec(at<Vec>(c,8),physics::transform(localA,collisionRoot),"collider anchor shares the root time");
  if(j==1||j==3){equalVec(at<Vec>(c,0x14),physics::transform(localB,collisionRoot),"capsule/ellipse second endpoint shares the root time");equalVec(at<Vec>(c,0x20),physics::transform(localAxis,rigidTrial&&i<2?tickBody:renderBody,false),"capsule/ellipse relative axis rotates without translation");}
  else if(j==2)equalVec(at<Vec>(c,0x14),physics::transform(localAxis,rigidTrial&&i<2?tickBody:renderBody,false),"plane normal rotates as a vector");
  else equalVec(at<Vec>(c,0x14),{91,92,93},"unused ball fields retain native bytes");
  require(at<float>(c,4)==.03f&&at<float>(c,0x2C)==.7f&&at<float>(c,0x30)==.49f&&at<float>(c,0x34)==.9f,"radii, lengths and friction remain native");
 }
 Mat ringRoot=rigidTrial&&i<2?tickBody:renderBody;equalVec(at<Vec>(w+0x10B0,8),physics::transform(localA,ringRoot),"bone-bound ring scratch follows rigid time; stage-world list remains separate");
 equal(physics::fromGame(body),renderBody,"shared body and skinning input stays at render time");
}
static void fakeBegin(Block w,const Mat* r,const Vec*,bool){verify(w,r);}
static void fakeApply(Block w,const Mat* r,const Vec* scale,float step,bool,bool,bool){require(step==2,"native strength remains 30 Hz");verify(w,r);hookRootBegin(w,r,scale,false);verify(w,r);}
static void fakeTarget(Block w,const Mat* r,const Vec*,float,bool){verify(w,r);}
static void fakeCtrl(Block w,const Mat* r,const Vec* scale,float step){int i=indexOf(w);hookRootApply(w,r,scale,step,false,false,false);hookRootTarget(w,r,scale,step,true);auto offset=physics::identity();offset.v[12]=.1f;output[i]=physics::toGame(physics::mul(offset,physics::fromGame(*r)));nativeOutput[i]=output[i];at<Mat>(w,0x1ED0)=*at<Mat*>(w,0x1EC8);prior[i]=physics::fromGame(at<Mat>(w,0x1ED0));++updates[i];}
static void fakeMain(Block b,bool,bool,float,float){int i=indexOf(b+0x68);if(updates[i])equal(output[i],nativeOutput[i],"presentation restores native input before a new solve");auto w=b+0x68;
 for(int j=0;j<4;++j){auto c=w+0x2B0+j*0x38;at<int>(c,0)=j+1;at<float>(c,4)=.03f;at<Vec>(c,8)=physics::transform(localA,renderBody);at<Vec>(c,0x14)=j==1||j==3?physics::transform(localB,renderBody):j==2?physics::transform(localAxis,renderBody,false):Vec{91,92,93};at<Vec>(c,0x20)=physics::transform(localAxis,renderBody,false);at<float>(c,0x2C)=.7f;at<float>(c,0x30)=.49f;at<float>(c,0x34)=.9f;}
 at<int>(w+0x2B0+4*0x38,0)=0;at<int>(w+0x10B0,0)=1;at<Vec>(w+0x10B0,8)=physics::transform(localA,renderBody);at<int>(w+0x10B0+0x38,0)=0;
 unsigned char charBytes[64*0x38],ringBytes[64*0x38];std::memcpy(charBytes,w+0x2B0,sizeof charBytes);std::memcpy(ringBytes,w+0x10B0,sizeof ringBytes);
 hookRootCtrl(w,at<Mat*>(at<Block>(b,0x20),0x48),&hsc,PhysicsAnimationDelta());
 require(!std::memcmp(charBytes,w+0x2B0,sizeof charBytes)&&!std::memcmp(ringBytes,w+0x10B0,sizeof ringBytes),"both scratch arrays restore byte-for-byte after nested callbacks");
}
static void fakeStep(Block,int,bool){}static void fakeEnd(Block){}
static void fakeItem(Block){for(int stage=0;stage<6;++stage)for(auto b:objects)hookOsageStep(b,stage,false);for(auto b:objects){hookOsageMain(b,false);require(at<Mat*>(b,0x1F30)==&body,"private pointer restores after Main");}for(auto b:objects)hookOsageEnd(b);}
#ifndef PHYSICS_RIGID_OLD
static void fallback(Block w,const Mat* r,const Vec*,float){equal(*r,body,"unsupported collider forwards original root");require(at<Mat*>(w,0x1EC8)==&body,"unsupported collider forwards original private ancestor");}
#endif
int main(){
 gameDelta=fakeDelta;animationDelta=fakeAnimation;osageMain=fakeMain;osageStep=fakeStep;osageEnd=fakeEnd;itemCtrl=fakeItem;rootCtrl=fakeCtrl;rootBegin=fakeBegin;rootApply=fakeApply;rootTarget=fakeTarget;
 std::vector<unsigned char> item(0x208),owner(0x950),blocks(24),storage[3],nodes[3],parents[3];char labels[3][64]={"c_tail_l_osg","c_tail_l_u_osg","c_skirt_osg"};
 for(int i=0;i<3;++i){storage[i].resize(0x2500);nodes[i].resize(0x1D0);parents[i].resize(0x50);auto b=storage[i].data();objects[i]=b;at<int>(b,0x10)=1;at<const char*>(b,0x18)=labels[i];at<Block>(b,0x20)=parents[i].data();at<Mat*>(parents[i].data(),0x48)=i==1?&output[0]:&body;at<Block>(b,0x48)=i==1?objects[0]:nullptr;at<Block>(b,0x50)=item.data();at<Mat*>(b,0x1F30)=&body;at<Block>(b,0xA0)=nodes[i].data();at<Block>(b,0xA8)=nodes[i].data()+nodes[i].size();at<Block>(b,0xB0)=nodes[i].data()+nodes[i].size();at<Mat*>(nodes[i].data(),0x40)=&output[i];at<Vec>(nodes[i].data(),4)={123,456,789};at<Block>(blocks.data(),i*8)=b;}
 at<Block>(item.data(),0x200)=owner.data();at<float>(owner.data(),0x938)=1;at<Block>(item.data(),0xC0)=blocks.data();at<Block>(item.data(),0xC8)=blocks.data()+24;at<Block>(item.data(),0xD0)=blocks.data()+24;
 armed=true;mode=1;frame=100;translationInputs=1;captureRoots=1;resampleRoots=0;smoothOsage=0;interpolateOsage=1;timeAwareInterpolation=1;
 for(bool trial:{true,false})for(int fps:{60,75,80,83,120,144,240})for(bool uneven:{false,true}){
  rigidTrial=trial;
#ifndef PHYSICS_RIGID_OLD
  rigidInputs=trial;
#endif
  states.clear();++epoch;std::memset(updates,0,sizeof updates);double time=0;dt=1;physics::Clock oracle;for(int i=0;i<3;++i)output[i]=nativeOutput[i]=physics::identity();
  for(int frameIndex=0;frameIndex<fps*2;++frameIndex){ad=60.f/fps*(uneven?(frameIndex%3==0?1.3f:frameIndex%3==1?.7f:1.f):1.f);if(frameIndex)time+=ad;renderBody=pose(time);body=physics::toGame(renderBody);oracle.advance(ad);tickBody=pose(time-oracle.remainder);++frame;if(frameIndex%2){std::thread worker([&]{hookItemCtrl(item.data());});worker.join();}else hookItemCtrl(item.data());}
  for(int i=0;i<3;++i)require(updates[i]>=59&&updates[i]<=61,"tails and unchanged skirt retain 30 Hz");int before=updates[0];dt=0;for(int i=0;i<5;++i){++frame;hookItemCtrl(item.data());}require(updates[0]==before,"pause adds no physics updates");
 }
#ifndef PHYSICS_RIGID_OLD
 rigidInputs=1;State rejected;rejected.translationReady=rejected.rigidReady=true;rejected.rigidInput=physics::fromGame(body);rejected.rigidOutput=physics::identity();rejected.translationInput={rejected.rigidInput.v[12],rejected.rigidInput.v[13],rejected.rigidInput.v[14]};rejected.translationOutput=rejected.translationInput;auto w=objects[0]+0x68;at<int>(w+0x10B0,0)=5;unsigned char cb[64*0x38],rb[64*0x38];std::memcpy(cb,w+0x2B0,sizeof cb);std::memcpy(rb,w+0x10B0,sizeof rb);rootCtrl=fallback;
 {RootScope scope(objects[0],rejected,true);hookRootCtrl(w,&body,&hsc,2);require(!rejected.rootApplied,"rejected AABB trial does not stamp a sampled pose");}
 require(at<Mat*>(w,0x1EC8)==&body&&!std::memcmp(cb,w+0x2B0,sizeof cb)&&!std::memcmp(rb,w+0x10B0,sizeof rb),"unsupported ring shape rejects all writes and restores pointer");
 physics::RigidSampler sampler;Mat result;sampler.sample(pose(0),.75f,0,true,result,2);auto jump=pose(0);jump.v[12]=3;sampler.sample(jump,.75f,.5,true,result,2);equal(result,jump,"translation jump reacquired directly");sampler.reset();auto turn=physics::rotation(physics::Quat{0,0,1,0});sampler.sample(physics::identity(),.75f,0,true,result,2);sampler.sample(turn,.75f,.5,true,result,2);equal(result,turn,"large rotation jump not blended across scenes");Mat shear=physics::identity();shear.v[1]=.2f;require(!sampler.sample(shear,.75f,.5,true,result,2)&&!sampler.initialized,"shear is rejected before changing collision geometry");
#endif
 std::puts("PASS: uniform-grid root orientation/position at 60/75/80/83/120/144/240 FPS and uneven steps; child outputs not resampled twice; private body and bone-bound ring collider points/normals/axes agree; radius/length/friction and skirt unchanged; byte-exact restoration, fallback, pause, scene jumps, workers");
}
