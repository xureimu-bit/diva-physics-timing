#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "physics_core.hpp"
#include "physics_settings.hpp"
#include "validation.hpp"
#include "third_party/minhook/include/MinHook.h"

// Independently written adapter for the verified Mega Mix+ build.
// Layout references: korenkonder/MMPlusMods, src/MMPlusModsShared/rob/rob.hpp
// Engine behavior reference: korenkonder/ReDIVA, src/CRE/rob/ex_block.cpp.
// See THIRD_PARTY.md for reference provenance. No engine routines are copied.
using physics::Mat; using physics::Vec;
using Block = unsigned char*;
#include "motion_trace.hpp"
using VoidFn = void(__fastcall*)(Block);
using MainFn = void(__fastcall*)(Block,bool,bool,float,float);
using StepFn = void(__fastcall*)(Block,int,bool);
using DeltaFn = float(__fastcall*)();
// Verified ordinary x64 argument order at each root-consuming entry.
using RootCtrlFn = void(__fastcall*)(Block,const Mat*,const Vec*,float);
using RootBeginFn = void(__fastcall*)(Block,const Mat*,const Vec*,bool);
using RootApplyFn = void(__fastcall*)(Block,const Mat*,const Vec*,float,bool,bool,bool);
using RootTargetFn = void(__fastcall*)(Block,const Mat*,const Vec*,float,bool);
static HMODULE module;
static uintptr_t base;
static std::wstring configPath;
static FILE* logfile;
static std::mutex logMutex;
static std::atomic<uint64_t> frame{0},epoch{0};
static std::atomic<int> timeAwareInterpolation{0};
static std::atomic<int> physicsHz{30}; // legacy rate for unclassified/body blocks
static std::atomic<int> hairHz{0},clothingHz{0},hairEnabled{1},clothingEnabled{1};
static std::atomic<int> hairInterpolation{-1},hairSmoothing{-1},clothingInterpolation{-1};
static std::mutex controlsMutex;
static physics_settings::Controls pendingControls;
static bool hasPendingControls=false;
static uint32_t controlsRevision=0;
static int controlsSaveError=0;
static std::atomic<int> translationInputs{0},rigidInputs{0};
static std::atomic<uint64_t> rigidScopes{0},rigidSkipped{0};
static std::atomic<uint64_t> translationScopes{0},translationChildren{0},translationCollisions{0},translationSkipped{0};
static std::atomic<int> mode{0};
static std::atomic<int> smoothOsage{0},resampleRoots{0},captureRoots{1},interpolateOsage{1};
static std::atomic<bool> armed{false};
static MainFn osageMain,clothMain;
static StepFn osageStep;
static VoidFn osageEnd,osageOPD,osageReset,clothOPD,clothReset,clothRoot;
static VoidFn itemCtrl;
static DeltaFn gameDelta,animationDelta;
static RootCtrlFn rootCtrl;
static RootBeginFn rootBegin;
static RootApplyFn rootApply;
static RootTargetFn rootTarget;
static std::atomic<uint64_t> rootSampled{0},rootDependent{0},rootSkipped{0},rootArguments{0};
struct State;
static std::atomic<uint64_t> rootsCaptured{0},rootsChangedLate{0};
static std::atomic<float> rootLateShiftMax{0};
static std::atomic<unsigned> lateBudget{12};
struct RootContext { Block work=nullptr;Mat delta{};unsigned depth=0;State* state=nullptr;bool resampling=false,translation=false,rigid=false;Vec translationDelta{};Mat* nativePrivate=nullptr; };
static thread_local RootContext rootContext;
static thread_local bool fixedScope=false;
static thread_local float fixedPeriod=2.f;
static thread_local bool withinItem=false;
static thread_local bool suspendPhysics=false;
extern "C" void AnimationDeltaProxy();
extern "C" float PhysicsAnimationDelta() {
    if(fixedScope) return fixedPeriod;
    return animationDelta();
}
static std::atomic<uint64_t> osageCalls{0},clothCalls{0},stepCalls{0},endCalls{0},opdCalls{0},ticks{0},held{0},overrides{0},invalid{0};
static std::atomic<uint64_t> itemCalls{0},presented{0},outsideItem{0},nativeOutside{0};
static std::atomic<uint64_t> itemLockWaitUs{0};
static std::atomic<uint64_t> restoreUs{0},nativeUs{0},presentUs{0};
static std::atomic<float> lastDelta{0},lastAnimation{0},lastOsageStep{0};
static std::atomic<unsigned> nameBudget{20};
static std::atomic<unsigned> matrixBudget{4};
static int64_t timestamp() { LARGE_INTEGER v;QueryPerformanceCounter(&v);return v.QuadPart; }
static uint64_t elapsedUs(int64_t start,int64_t finish) {
    static const int64_t frequency=[](){LARGE_INTEGER v;QueryPerformanceFrequency(&v);return v.QuadPart;}();
    return static_cast<uint64_t>((finish-start)*1000000/frequency);
}

static void log(const char* format,...) {
    std::lock_guard<std::mutex> lock(logMutex);
    if(!logfile) return;
    std::fprintf(logfile,"[%llu] ",static_cast<unsigned long long>(GetTickCount64()));
    va_list args;va_start(args,format);std::vfprintf(logfile,format,args);va_end(args);
    std::fputc('\n',logfile);std::fflush(logfile);
}
static thread_local bool cacheRegions=false;
static thread_local std::vector<MEMORY_BASIC_INFORMATION> regions;
struct MemoryPhase {
    bool old;
    MemoryPhase():old(cacheRegions){regions.clear();cacheRegions=true;}
    ~MemoryPhase(){regions.clear();cacheRegions=old;}
};
static bool accessible(const void* p,size_t bytes,bool write=false) {
    if(!p||!bytes) return false;
    uintptr_t begin=reinterpret_cast<uintptr_t>(p),end=begin+bytes;
    if(end<begin) return false;
    while(begin<end) {
        MEMORY_BASIC_INFORMATION mi{};
        bool cached=false;
        if(cacheRegions)for(const auto& region:regions) {
            auto start=reinterpret_cast<uintptr_t>(region.BaseAddress);
            if(begin>=start&&begin-start<region.RegionSize){mi=region;cached=true;break;}
        }
        if(!cached) {
            if(!VirtualQuery(reinterpret_cast<void*>(begin),&mi,sizeof mi))return false;
            if(cacheRegions&&regions.size()<32)regions.push_back(mi);
        }
        if(mi.State!=MEM_COMMIT
            ||mi.Protect&(PAGE_GUARD|PAGE_NOACCESS)) return false;
        DWORD protection=mi.Protect&0xFF;
        if(write&&protection!=PAGE_READWRITE&&protection!=PAGE_WRITECOPY
            &&protection!=PAGE_EXECUTE_READWRITE&&protection!=PAGE_EXECUTE_WRITECOPY) return false;
        if(!write&&protection==PAGE_EXECUTE) return false;
        uintptr_t next=reinterpret_cast<uintptr_t>(mi.BaseAddress)+mi.RegionSize;
        if(next<=begin) return false;
        begin=next;
    }
    return true;
}
template<class T> static T& at(Block b,size_t offset) { return *reinterpret_cast<T*>(b+offset); }
struct Array { Block first{},last{},capacity{}; size_t count{}; };
static bool arrayAt(Block b,size_t offset,size_t stride,size_t max,Array& a) {
    if(!accessible(b+offset,24)) return false;
    a.first=at<Block>(b,offset);a.last=at<Block>(b,offset+8);a.capacity=at<Block>(b,offset+16);
    auto start=reinterpret_cast<uintptr_t>(a.first),end=reinterpret_cast<uintptr_t>(a.last);
    auto cap=reinterpret_cast<uintptr_t>(a.capacity);
    if(!start||end<start||cap<end||(end-start)%stride) return false;
    a.count=(end-start)/stride;
    return a.count>0&&a.count<=max&&accessible(a.first,a.count*stride,true);
}
static bool parentMatrix(Block b,Mat& m) {
    if(!accessible(b+0x20,8)) return false;
    auto parent=at<Block>(b,0x20);
    if(!accessible(parent,0x50)) return false;
    auto matrix=at<Mat*>(parent,0x48);
    if(!accessible(matrix,sizeof(Mat))) return false;
    m=physics::fromGame(*matrix);return physics::affine(m);
}
static float ownerStep(Block b) {
    auto item=at<Block>(b,0x50);
    if(!accessible(item,0x208)) return 1;
    auto owner=at<Block>(item,0x200);
    if(!accessible(owner,0x940)) return 1;
    return at<float>(owner,0x938);
}
static void traceName(Block b,const char* kind) {
    auto budget=nameBudget.load();
    if(!budget||!nameBudget.compare_exchange_strong(budget,budget-1)) return;
    auto name=at<const char*>(b,0x18);
    char text[100]{};
    if(accessible(name,sizeof text)) {
        std::memcpy(text,name,sizeof text-1);
        for(char& ch:text) if(ch&&static_cast<unsigned char>(ch)<32) ch='?';
    }
    log("BLOCK %s ptr=%p name=%s ownerStep=%.6f",kind,b,text,ownerStep(b));
    auto matrices=matrixBudget.load();
    if(matrices&&matrixBudget.compare_exchange_strong(matrices,matrices-1)) {
        Mat canonical;
        if(parentMatrix(b,canonical)) {
            auto native=physics::toGame(canonical);
            log("NATIVE_MATRIX ptr=%p columnTranslation=(%.6f,%.6f,%.6f) bottomRow=(%.6f,%.6f,%.6f,%.6f)",b,
                native.v[3],native.v[7],native.v[11],native.v[12],native.v[13],native.v[14],native.v[15]);
        }
    }
}
struct MatrixPose { Mat* destination;Mat value,previous,earlier; };
struct VertexPose { Vec pos,normal,tangent,binormal; };
struct State {
    physics_settings::Group group=physics_settings::Group::Other;
    physics::Clock clock;
    physics::PoseTimeline poseTimeline;
    float period=2.0f;
    uint64_t lastFrame=~uint64_t(0),generation=~uint64_t(0);
    bool tick=true,valid=false,cloth=false,renderHeld=false,bypass=false,interpolated=false;
    Block nodes=nullptr;size_t count=0,width=0;
    Mat root{},previousRoot{},earlierRoot{};
    Mat rootInverse{},previousRootInverse{},earlierRootInverse{};
    physics::RootSampler rootSampler;
    physics::TranslationSampler translationSampler;
    physics::RigidSampler rigidSampler;
    Mat rigidInput{},rigidOutput{};bool rigidReady=false;
    Vec translationInput{},translationOutput{};
    bool translationReady=false,translationDependent=false,translationApplied=false;
    Mat inputRoot{},sampleRoot{};
    bool sampleReady=false,rootApplied=false,simulationRootReady=false;Mat simulationRoot{};
    std::vector<MatrixPose> matrices;
    std::vector<VertexPose> vertices;
    std::vector<VertexPose> previousVertices;
    std::vector<Mat> roots,previousRoots;
};
// Native jobs are dispatched per character. Items of one character share
// item+0x200 (rob_chara_item_equip); keep that whole dependency chain serial,
// while independent characters can run in parallel. Hash collisions only
// reduce concurrency. No lock selection or topology changes during hot reload.
static std::recursive_mutex actorMutex[64];
static std::mutex stateRegistryMutex;
static std::unordered_map<Block,State> states;
static thread_local Block lockedItem=nullptr;
static thread_local size_t lockedActor=0;
static size_t actorIndex(Block item) {
    // The owner is used as an opaque key; never dereference it for lock lookup.
    if(!item||!accessible(item+0x200,sizeof(Block)))return 0;
    auto owner=reinterpret_cast<uintptr_t>(at<Block>(item,0x200));
    if(!owner)return 0;
    owner^=owner>>33;owner*=0xff51afd7ed558ccdULL;owner^=owner>>33;
    return 1+owner%63;
}
static size_t blockActorIndex(Block b) {
    // b is the native callback's verified ExBlock instance. The fast path only
    // reads its native item link, as the original callback itself does.
    if(lockedItem&&b&&at<Block>(b,0x50)==lockedItem)return lockedActor;
    if(!b||!accessible(b+0x50,sizeof(Block)))return 0;
    return actorIndex(at<Block>(b,0x50));
}
struct ItemActorLock {
    Block previousItem;size_t previousActor,index;
    std::unique_lock<std::recursive_mutex> lock;
    explicit ItemActorLock(Block item):previousItem(lockedItem),previousActor(lockedActor),index(actorIndex(item)),
        lock(actorMutex[index]) {
        lockedItem=item;lockedActor=index;
    }
    ~ItemActorLock(){lockedItem=previousItem;lockedActor=previousActor;}
};
static State& stateFor(Block b) {
    std::lock_guard<std::mutex> lock(stateRegistryMutex);return states[b];
}
static State* findState(Block b) {
    std::lock_guard<std::mutex> lock(stateRegistryMutex);
    auto it=states.find(b);return it==states.end()?nullptr:&it->second;
}
static void eraseState(Block b) {
    std::lock_guard<std::mutex> lock(stateRegistryMutex);states.erase(b);
}
// References survive unordered_map rehashes. A block's owning actor lock is
// held for its full use, including native callbacks, capture, and presentation;
// only that actor can erase its state. The registry lock never spans engine code.

static bool sameNodes(Block b,State& s) {
    Array a;
    if(!arrayAt(b,s.cloth?0x80:0xA0,s.cloth?0xF0:0x1D0,s.cloth?16384:1024,a)) return false;
    return a.first==s.nodes&&a.count==s.count;
}
static void restore(Block b,State& s) {
    if(!s.valid||!s.renderHeld||!sameNodes(b,s)) return;
    if(s.cloth) {
        for(size_t i=0;i<s.count;++i) {
            auto n=s.nodes+i*0xF0;const auto& v=s.vertices[i];
            at<Vec>(n,4)=v.pos;at<Vec>(n,0x34)=v.normal;
            at<Vec>(n,0x40)=v.tangent;at<Vec>(n,0x4C)=v.binormal;
        }
    } else if(s.interpolated) {
        // Only the whole-item entry may undo presentation matrices. This runs
        // before every dependent physics block, never midway through a chain.
        for(auto& p:s.matrices) if(accessible(p.destination,sizeof(Mat),true)) *p.destination=physics::toGame(p.value);
    }
    // Osage render matrices are outputs. Rewinding them here can rewind the
    // parent of a dependent block before its next physics update. Its internal
    // positions/velocities were never changed by attach, so no rewind is needed.
    s.renderHeld=false;s.interpolated=false;
}
static bool capture(Block b,State& s) {
    Array a;
    if(!arrayAt(b,s.cloth?0x80:0xA0,s.cloth?0xF0:0x1D0,s.cloth?16384:1024,a)) return false;
    bool history=s.valid&&a.first==s.nodes&&a.count==s.count;
        auto oldVertices=s.vertices;auto oldRoots=s.roots;auto oldRoot=s.root;auto oldPreviousRoot=s.previousRoot;
    auto oldInverse=s.rootInverse,oldPreviousInverse=s.previousRootInverse;
    s.nodes=a.first;s.count=a.count;s.renderHeld=false;
    if(s.cloth) {
        s.width=at<size_t>(b,0x70);
        Array roots;
        if(!s.width||s.width>s.count||s.count%s.width
            ||!arrayAt(b,0x1DA8,0x158,1024,roots)||roots.count!=s.width) return false;
        s.vertices.resize(s.count);s.roots.resize(s.width);
        for(size_t i=0;i<s.width;++i) {
            auto m=physics::fromGame(at<Mat>(roots.first+i*0x158,0x98));
            if(!physics::affine(m)) return false;
            s.roots[i]=m;
        }
        for(size_t i=0;i<s.count;++i) {
            Block n=a.first+i*0xF0;
            s.vertices[i]={at<Vec>(n,4),at<Vec>(n,0x34),at<Vec>(n,0x40),at<Vec>(n,0x4C)};
        }
        s.previousVertices=history?std::move(oldVertices):s.vertices;
        s.previousRoots=history?std::move(oldRoots):s.roots;
    } else {
        Mat lateRoot;
        if(!parentMatrix(b,lateRoot))return false;
        if((captureRoots.load()||s.translationApplied)&&s.simulationRootReady) {
            s.root=s.simulationRoot;++rootsCaptured;
            float distance=std::sqrt(std::pow(lateRoot.v[12]-s.root.v[12],2.f)
                +std::pow(lateRoot.v[13]-s.root.v[13],2.f)+std::pow(lateRoot.v[14]-s.root.v[14],2.f));
            float difference=0;for(int j=0;j<16;++j)difference=std::max(difference,std::abs(lateRoot.v[j]-s.root.v[j]));
            if(difference>0.0001f) {
                ++rootsChangedLate;
                auto maximum=rootLateShiftMax.load();
                while(distance>maximum&&!rootLateShiftMax.compare_exchange_weak(maximum,distance)){}
                auto budget=lateBudget.load();
                if(budget&&lateBudget.compare_exchange_strong(budget,budget-1)) {
                    const auto name=at<const char*>(b,0x18);char label[96]{};
                    if(accessible(name,sizeof label))std::memcpy(label,name,sizeof label-1);
                    log("ROOT_CHANGED_AFTER_PHYSICS block=%p name=%s translation=%.6f matrixDifference=%.6f",b,label,distance,difference);
                }
            }
        } else if(s.rootApplied)s.root=s.sampleRoot;
        else s.root=lateRoot;
        s.previousRoot=history?oldRoot:s.root;
        s.earlierRoot=history?oldPreviousRoot:s.root;
        if(!physics::inverse(s.root,s.rootInverse))return false;
        s.previousRootInverse=history?oldInverse:s.rootInverse;
        s.earlierRootInverse=history?oldPreviousInverse:s.rootInverse;
        const auto parent=at<Block>(b,0x20);
        const auto inputRoot=at<Mat*>(parent,0x48);
        size_t cursor=0,previousCount=s.matrices.size();bool matrixHistory=history;
        s.matrices.reserve((s.count+1)*2);
        auto saveMatrix=[&](Mat* destination,const Mat& value) {
            bool matching=history&&cursor<previousCount&&s.matrices[cursor].destination==destination;
            MatrixPose pose{destination,value,value,value};
            if(matching){pose.previous=s.matrices[cursor].value;pose.earlier=s.matrices[cursor].previous;}
            else matrixHistory=false;
            if(cursor<s.matrices.size())s.matrices[cursor]=pose;else s.matrices.push_back(pose);
            ++cursor;
        };
        auto save=[&](Block n)->bool {
            auto dst=at<Mat*>(n,0x40);auto bone=at<Block>(n,0x38);
            if(dst&&dst!=inputRoot) {
                if(!accessible(dst,sizeof(Mat),true)) return false;
                auto value=physics::fromGame(*dst);if(!physics::affine(value))return false;
                saveMatrix(dst,value);
            }
            if(bone) {
                if(!accessible(bone,0x50)) return false;
                auto noScale=at<Mat*>(bone,0x48);
                if(noScale&&noScale!=dst&&noScale!=inputRoot) {
                    if(!accessible(noScale,sizeof(Mat),true)) return false;
                    auto value=physics::fromGame(*noScale);if(!physics::affine(value))return false;
                    saveMatrix(noScale,value);
                }
            }
            return true;
        };
        for(size_t i=0;i<s.count;++i) if(!save(a.first+i*0x1D0)) return false;
        // The terminal effector follows the same node layout, outside the vector.
        if(!save(b+0xB8)) return false;
        matrixHistory=matrixHistory&&cursor==previousCount;
        s.matrices.resize(cursor);
        if(!matrixHistory) {
            for(auto& pose:s.matrices){pose.previous=pose.value;pose.earlier=pose.value;}
            s.previousRoot=s.earlierRoot=s.root;
            s.previousRootInverse=s.earlierRootInverse=s.rootInverse;
        }
        // Root resampling, when enabled, moves simulation to the exact grid;
        // otherwise stamp the frame where native physics actually executed.
        double stamp=s.clock.time-(s.rootApplied?s.clock.remainder:0.0);
        s.poseTimeline.capture(stamp,matrixHistory);
    }
    return true;
}
static bool interpolationEnabled(const State& s) {
    int value=s.group==physics_settings::Group::Hair?hairInterpolation.load()
        :s.group==physics_settings::Group::Clothing?clothingInterpolation.load():-1;
    return value<0?interpolateOsage.load()!=0:value!=0;
}
static bool smoothingEnabled(const State& s) {
    int value=s.group==physics_settings::Group::Hair?hairSmoothing.load():-1;
    return value<0?smoothOsage.load()!=0:value!=0;
}
static bool attach(Block b,State& s);
static bool present(Block b,State& s) {
    if(!s.valid||!sameNodes(b,s))return false;
    if(s.cloth?clothingInterpolation.load()==0:!interpolationEnabled(s)) {
        bool ok=attach(b,s);
        // This held display is still restored at the whole-item boundary.
        // Every physical parent must see native simulation matrices next time.
        s.interpolated=s.renderHeld;
        if(ok)++presented;
        return ok;
    }
    float alpha=s.clock.alpha(s.period);
    s.renderHeld=true;s.interpolated=true;
    if(s.cloth) {
        Array roots;
        if(s.previousRoots.size()!=s.width||s.previousVertices.size()!=s.count
            ||!arrayAt(b,0x1DA8,0x158,1024,roots)||roots.count!=s.width)return false;
        clothRoot(b+0x60);
        for(size_t column=0;column<s.width;++column) {
            Mat previousDelta,currentDelta;
            auto current=physics::fromGame(at<Mat>(roots.first+column*0x158,0x98));
            if(!physics::rebase(s.previousRoots[column],current,previousDelta)
                ||!physics::rebase(s.roots[column],current,currentDelta))return false;
            for(size_t i=column+s.width;i<s.count;i+=s.width) {
                auto n=s.nodes+i*0xF0;const auto& p=s.previousVertices[i];const auto& c=s.vertices[i];
                at<Vec>(n,4)=physics::lerp(physics::transform(p.pos,previousDelta),physics::transform(c.pos,currentDelta),alpha);
                at<Vec>(n,0x34)=physics::normalized(physics::lerp(physics::transform(p.normal,previousDelta,false),physics::transform(c.normal,currentDelta,false),alpha));
                at<Vec>(n,0x40)=physics::normalized(physics::lerp(physics::transform(p.tangent,previousDelta,false),physics::transform(c.tangent,currentDelta,false),alpha));
                at<Vec>(n,0x4C)=physics::normalized(physics::lerp(physics::transform(p.binormal,previousDelta,false),physics::transform(c.binormal,currentDelta,false),alpha));
            }
        }
    } else {
        Mat current,previousDelta,currentDelta;
        if(!parentMatrix(b,current))return false;
        previousDelta=physics::mul(s.previousRootInverse,current);
        currentDelta=physics::mul(s.rootInverse,current);
        if(!physics::finite(previousDelta)||!physics::finite(currentDelta))return false;
        bool smoothing=smoothingEnabled(s);
        auto blend=s.poseTimeline.sample(s.clock.time,s.period);
        bool timed=timeAwareInterpolation.load()!=0&&!smoothing;
        bool older=timed&&blend.older;
        if(timed)alpha=blend.alpha;
        Mat earlierDelta;
        if(smoothing||older){earlierDelta=physics::mul(s.earlierRootInverse,current);if(!physics::finite(earlierDelta))return false;}
        for(auto& p:s.matrices) {
            if(!accessible(p.destination,sizeof(Mat),true))return false;
            auto previousPose=physics::mul(p.previous,previousDelta),currentPose=physics::mul(p.value,currentDelta);
            auto value=smoothing?physics::smooth(physics::mul(p.earlier,earlierDelta),previousPose,currentPose,alpha)
                :older?physics::interpolate(physics::mul(p.earlier,earlierDelta),previousPose,alpha)
                :physics::interpolate(previousPose,currentPose,alpha);
            if(!physics::finite(value))return false;
            *p.destination=physics::toGame(value);
        }
    }
    ++presented;return true;
}
static bool attach(Block b,State& s) {
    if(!s.valid||!sameNodes(b,s)) return false;
    // A later validation failure must restore any earlier writes in this call.
    s.renderHeld=true;
    if(s.cloth) {
        Array roots;
        if(!arrayAt(b,0x1DA8,0x158,1024,roots)||roots.count!=s.width) return false;
        // Updates weighted body anchors without advancing force or velocity.
        clothRoot(b+0x60);
        for(size_t column=0;column<s.width;++column) {
            Mat delta;
            if(!physics::rebase(s.roots[column],physics::fromGame(at<Mat>(roots.first+column*0x158,0x98)),delta)) return false;
            for(size_t i=column+s.width;i<s.count;i+=s.width) {
                Block n=s.nodes+i*0xF0;const auto& v=s.vertices[i];
                at<Vec>(n,4)=physics::transform(v.pos,delta);
                at<Vec>(n,0x34)=physics::transform(v.normal,delta,false);
                at<Vec>(n,0x40)=physics::transform(v.tangent,delta,false);
                at<Vec>(n,0x4C)=physics::transform(v.binormal,delta,false);
            }
        }
    } else {
        Mat current,delta;
        if(!parentMatrix(b,current))return false;
        delta=physics::mul(s.rootInverse,current);if(!physics::finite(delta))return false;
        for(auto& p:s.matrices) {
            if(!accessible(p.destination,sizeof(Mat),true)) return false;
            *p.destination=physics::toGame(physics::mul(p.value,delta));
        }
    }
    return true;
}
static physics_settings::Group classifyGroup(Block b,bool cloth) {
    using physics_settings::Group;
    if(cloth)return Group::Clothing;
    Group result=Group::Other;Block seen[16]{};unsigned count=0;
    for(Block current=b;current&&count<16;) {
        if(!accessible(current+0x10,0x40))break;
        for(unsigned i=0;i<count;++i)if(seen[i]==current)return Group::Other;
        seen[count++]=current;
        auto label=at<const char*>(current,0x18);
        if(accessible(label,96)) {
            char text[97]{};std::memcpy(text,label,96);
            auto candidate=physics_settings::namedGroup(text);
            if(candidate!=Group::Other)result=candidate;
        }
        auto parent=at<Block>(current,0x48);
        if(!parent||!accessible(parent+0x10,4))break;
        int type=at<int>(parent,0x10);
        if(type==4)return Group::Clothing;
        if(type!=1)break;
        current=parent;
    }
    // Physical children use their ancestor's category so coupled chains stay
    // on one cadence, even when a branch/accessory has a different name.
    return result;
}
static int groupRate(physics_settings::Group group) {
    int rate=group==physics_settings::Group::Hair?hairHz.load()
        :group==physics_settings::Group::Clothing?clothingHz.load():0;
    return rate?rate:physicsHz.load();
}
static bool groupEnabled(physics_settings::Group group) {
    return group==physics_settings::Group::Hair?hairEnabled.load()!=0
        :group==physics_settings::Group::Clothing?clothingEnabled.load()!=0:true;
}
static State& decision(Block b,bool cloth) {
    State& s=stateFor(b);auto f=frame.load(std::memory_order_relaxed),e=epoch.load();
    if(s.generation!=e) {
        restore(b,s);s=State{};s.generation=e;s.cloth=cloth;
        s.group=classifyGroup(b,cloth);s.period=60.0f/groupRate(s.group);
        s.bypass=!groupEnabled(s.group);
    }
    if(s.lastFrame==f) return s;
    // Objects resumed after a state transition initialize immediately.
    if(s.lastFrame!=~uint64_t(0)&&f>s.lastFrame+2) { restore(b,s);s.clock.reset();s.rootSampler.reset();s.translationSampler.reset();s.rigidSampler.reset();s.valid=false; }
    s.lastFrame=f;
    if(s.bypass) {s.tick=true;return s;}
    float delta=gameDelta(),animation=animationDelta();
    // A read-only preflight phase ends before the next native callback.
    MemoryPhase preflight;float step=ownerStep(b);
    lastDelta=delta;lastAnimation=animation;lastOsageStep=step;
    bool advancing=std::isfinite(step)&&step>0;
    // Main physics uses both engine frame-speed and measured animation delta.
    // get_delta_frame alone may stay at 1 even at 120 FPS.
    float elapsed=advancing?delta*animation:0;
    s.tick=s.clock.advance(elapsed,s.period);
    if(!s.valid) s.tick=true; // Acquire a native initial pose even while paused.
    if(s.valid&&!sameNodes(b,s)) { s.valid=false;s.clock.reset();s.rootSampler.reset();s.translationSampler.reset();s.rigidSampler.reset();s.tick=true; }
    s.sampleReady=false;s.rootApplied=false;s.simulationRootReady=false;
    s.translationReady=false;s.translationApplied=false;s.rigidReady=false;
    if(!cloth&&resampleRoots.load()!=0) {
        auto parent=accessible(b+0x48,8)?at<Block>(b,0x48):nullptr;
        // A parent simulated by another secondary block is already at tick time.
        // Never resample an osage/cloth output as if it were render-time animation.
        bool dependent=parent&&accessible(parent+0x10,4)
            &&(at<int>(parent,0x10)==1||at<int>(parent,0x10)==4);
        if(dependent) {s.rootSampler.reset();if(s.tick)++rootDependent;}
        else if(parentMatrix(b,s.inputRoot)) {
            s.sampleReady=s.rootSampler.sample(s.inputRoot,elapsed,s.clock.remainder,s.tick,s.sampleRoot,s.period);
        } else {s.rootSampler.reset();if(s.tick)++rootSkipped;}
    }
    if(!cloth&&translationInputs.load()&&!resampleRoots.load()) {
        // Limit this trial to Miku's left/right tail chains; skirts and body
        // secondary blocks retain the validated baseline.
        bool tail=false;
        if(accessible(b+0x18,8)) {
            auto label=at<const char*>(b,0x18);
            if(accessible(label,16))tail=!std::strncmp(label,"c_tail_l_",9)||!std::strncmp(label,"c_tail_r_",9);
        }
        auto parent=accessible(b+0x48,8)?at<Block>(b,0x48):nullptr;
        s.translationDependent=parent&&accessible(parent+0x10,4)&&at<int>(parent,0x10)==1;
        bool coherent=true;
        if(s.translationDependent) {
            auto upstream=findState(parent);
            coherent=upstream&&upstream->lastFrame==f&&upstream->tick==s.tick
                &&std::abs(upstream->clock.time-s.clock.time)<.0001&&std::abs(upstream->clock.remainder-s.clock.remainder)<.0001;
        }
        if(tail&&coherent&&accessible(b+0x1F30,8)) {
            auto pointer=at<Mat*>(b,0x1F30);
            if(accessible(pointer,sizeof(Mat))) {
                auto current=physics::fromGame(*pointer);
                if(physics::affine(current)) {
                    s.translationInput={current.v[12],current.v[13],current.v[14]};
                    s.translationReady=s.translationSampler.sample(s.translationInput,elapsed,s.clock.remainder,s.tick,s.translationOutput,s.period);
                    if(rigidInputs.load()) {
                        s.rigidInput=current;
                        s.rigidReady=s.rigidSampler.sample(current,elapsed,s.clock.remainder,s.tick,s.rigidOutput,s.period);
                    }
                }
            }
        }
        if(tail&&!s.translationReady&&s.tick)++translationSkipped;
    }
    if(s.tick) { restore(b,s);++ticks; }
    else ++held;
    return s;
}
// Only the private RobOsage pointer changes during its own callback. Body
// animation and shared skinning inputs remain at current render time.
struct RootScope {
    RootContext old;
    Mat** slot=nullptr;
    Mat* saved=nullptr;
    alignas(16) Mat sampledPrivate{};
    RootScope(Block b,State& s,bool mainCall=false):old(rootContext) {
        rootContext=RootContext{};
        if(s.bypass)return;
        // Track actual physics input timing even with root resampling disabled.
        // A later expression/animation callback may update this parent again.
        if(captureRoots.load()){rootContext.work=b+0x68;rootContext.state=&s;}
        if(mainCall&&translationInputs.load()&&!resampleRoots.load()&&s.translationReady) {
            if(!accessible(b+0x1F30,8,true)||!accessible(b+0x318,64*0x38,true)){++translationSkipped;return;}
            auto pointer=at<Mat*>(b,0x1F30);
            if(!accessible(pointer,sizeof(Mat))){++translationSkipped;return;}
            auto original=physics::fromGame(*pointer);
            if(!physics::affine(original)){++translationSkipped;return;}
            const float mismatch=std::abs(original.v[12]-s.translationInput.x)
                +std::abs(original.v[13]-s.translationInput.y)+std::abs(original.v[14]-s.translationInput.z);
            if(mismatch>.0001f){s.translationSampler.reset();++translationSkipped;return;}
            Vec delta{s.translationOutput.x-original.v[12],s.translationOutput.y-original.v[13],s.translationOutput.z-original.v[14]};
            Mat adjusted=original,deltaMatrix=physics::identity();
            deltaMatrix.v[12]=delta.x;deltaMatrix.v[13]=delta.y;deltaMatrix.v[14]=delta.z;
            adjusted.v[12]=s.translationOutput.x;adjusted.v[13]=s.translationOutput.y;adjusted.v[14]=s.translationOutput.z;
            bool rigidTrial=rigidInputs.load()&&s.rigidReady;
            if(rigidTrial) {
                float difference=0;for(int j=0;j<16;++j)difference=std::max(difference,std::abs(original.v[j]-s.rigidInput.v[j]));
                if(difference>.0001f||!physics::rebase(original,s.rigidOutput,deltaMatrix)||!physics::rigid(deltaMatrix)) {
                    s.rigidSampler.reset();++rigidSkipped;rigidTrial=false;
                    deltaMatrix=physics::identity();deltaMatrix.v[12]=delta.x;deltaMatrix.v[13]=delta.y;deltaMatrix.v[14]=delta.z;
                } else adjusted=s.rigidOutput;
            } else if(rigidInputs.load())++rigidSkipped;
            sampledPrivate=physics::toGame(adjusted);slot=reinterpret_cast<Mat**>(b+0x1F30);saved=pointer;*slot=&sampledPrivate;
            rootContext.work=b+0x68;rootContext.state=&s;rootContext.translation=true;rootContext.translationDelta=delta;rootContext.nativePrivate=saved;
            rootContext.delta=deltaMatrix;rootContext.rigid=rigidTrial;
            // A dependent input already comes from the upstream sampled solve.
            rootContext.resampling=!s.translationDependent;
            ++translationScopes;if(s.translationDependent)++translationChildren;
            return;
        }
        if(!resampleRoots.load()||!s.sampleReady)return;
        Mat delta;
        if(!physics::rebase(s.inputRoot,s.sampleRoot,delta)
            ||!accessible(b+0x1F30,8,true)){++rootSkipped;return;}
        auto pointer=at<Mat*>(b,0x1F30);
        if(!accessible(pointer,sizeof(Mat))){++rootSkipped;return;}
        auto original=physics::fromGame(*pointer);
        if(!physics::affine(original)){++rootSkipped;return;}
        auto adjusted=physics::mul(original,delta);
        if(!physics::affine(adjusted)){++rootSkipped;return;}
        sampledPrivate=physics::toGame(adjusted);
        slot=reinterpret_cast<Mat**>(b+0x1F30);saved=pointer;*slot=&sampledPrivate;
        rootContext.work=b+0x68;rootContext.delta=delta;rootContext.resampling=true;rootContext.state=&s;
        s.rootApplied=true;++rootSampled;
    }
    ~RootScope() {
        if(slot) {
            if(*slot==&sampledPrivate)*slot=saved;
            else ++rootSkipped; // Do not overwrite a native replacement.
        }
        rootContext=old;
    }
};
struct RootArgument {
    alignas(16) Mat adjusted{};
    const Mat* value;
    bool active=false;
    unsigned char padding[7]{};
    RootArgument(Block work,const Mat* root):value(root) {
        if(work!=rootContext.work||rootContext.depth)return;
        MemoryPhase preflight; // No native routine runs during argument validation.
        if(!accessible(root,sizeof(Mat)))return;
        auto canonical=physics::fromGame(*root);
        if(!physics::affine(canonical))return;
        if(rootContext.translation&&rootContext.state) {
            if(rootContext.rigid&&!rootContext.state->translationApplied)++rigidScopes;
            rootContext.state->rootApplied=true;rootContext.state->translationApplied=true;
        }
        if((captureRoots.load()||rootContext.translation)&&rootContext.state) {
            Mat rawRoot;
            if(parentMatrix(work-0x68,rawRoot)) {
                auto usedRoot=rootContext.resampling?physics::mul(rawRoot,rootContext.delta):rawRoot;
                if(physics::affine(usedRoot)) {
                    rootContext.state->simulationRoot=usedRoot;
                    rootContext.state->simulationRootReady=true;
                    if(rootContext.translation)rootContext.state->sampleRoot=usedRoot;
                }
            }
        }
        // Also guard tracking against nested entry points when no resampling runs.
        active=true;++rootContext.depth;
        if(!rootContext.resampling)return;
        auto result=physics::mul(canonical,rootContext.delta);
        if(!physics::affine(result))return;
        adjusted=physics::toGame(result);value=&adjusted;++rootArguments;
    }
    ~RootArgument(){if(active)--rootContext.depth;}
};
static std::atomic<uint64_t> motionTraceEnd{0};
static std::atomic<int> lastMotionCookie{0};
static std::mutex motionTraceMutex;
static Block motionTraceOwner=nullptr;
static int64_t motionTraceOrigin=0;
static void startMotionTrace(uint32_t cookie,int seconds) {
    {
        std::lock_guard<std::mutex> lock(motionTraceMutex);
        motionTraceEnd=0;DivaPhysicsTraceCount=0;motionTraceOwner=nullptr;
        DivaPhysicsTraceCookie=cookie;motionTraceOrigin=timestamp();
        motionTraceEnd=GetTickCount64()+static_cast<uint64_t>(seconds)*1000;
    }
    log("MOTION_TRACE cookie=%u seconds=%d (bounded in-memory snapshots only; native solver unchanged)",cookie,seconds);
}
static void traceMotion(Block work,const Mat* argument,uint32_t phase,float step) {
    // Disabled diagnostics require one atomic load, no timer/memory queries.
    auto end=motionTraceEnd.load();
    if(!end||work!=rootContext.work)return;
    auto now=GetTickCount64();if(now>=end)return;
    Block b=work-0x68;
    MemoryPhase validation;
    if(!accessible(b+0x18,8)||!accessible(b+0x50,8))return;
    auto label=at<const char*>(b,0x18);char name[64]{};
    if(!accessible(label,sizeof name))return;std::memcpy(name,label,sizeof name-1);
    if(std::strcmp(name,"c_tail_l_osg")&&std::strcmp(name,"c_tail_l_u_osg")&&std::strcmp(name,"c_tail_l_f2_osg"))return;
    std::lock_guard<std::mutex> lock(motionTraceMutex);
    if(now>=motionTraceEnd.load())return;
    auto owner=at<Block>(b,0x50);
    if(motionTraceOwner&&motionTraceOwner!=owner)return;
    auto count=DivaPhysicsTraceCount.load();if(count>=DivaPhysicsTraceCapacity)return;
    Array nodes;
    if(!arrayAt(b,0xA0,0x1D0,1024,nodes)||!accessible(work,0x1F18)
        ||!accessible(argument,sizeof(Mat)))return;
    auto privateRoot=at<Mat*>(work,0x1EC8);
    auto parent=at<Block>(b,0x20);
    if(!accessible(privateRoot,sizeof(Mat))||!accessible(parent,0x50))return;
    auto parentRoot=at<Mat*>(parent,0x48);auto skin=at<Block>(work,0);
    if(!accessible(parentRoot,sizeof(Mat))||!accessible(skin,0x48))return;
    MotionTraceRecord record{};
    record.frame=frame.load();record.timeUs=elapsedUs(motionTraceOrigin,timestamp());
    record.block=reinterpret_cast<uintptr_t>(b);record.owner=reinterpret_cast<uintptr_t>(owner);
    record.cookie=DivaPhysicsTraceCookie.load();record.phase=phase;record.step=step;
    record.period=rootContext.state?rootContext.state->period:60.f/physicsHz.load();
    record.nodeCount=static_cast<uint32_t>(nodes.count);std::memcpy(record.name,name,sizeof name);
    record.flags=(at<unsigned char>(work,0x1F14)?1u:0u)|(at<unsigned char>(work,0x2A8)?2u:0u)
        |(at<unsigned char>(work,0x1F15)?4u:0u)|(at<unsigned char>(b,0x59)?8u:0u);
    record.localCancel=at<float>(work,0x1F10);record.skinCancel=at<float>(skin,0x44);
    record.airResistance=at<float>(skin,0x20);record.stiffness=at<float>(skin,0x40);
    record.argument=*argument;record.parent=*parentRoot;record.privateCurrent=*privateRoot;
    record.privatePrevious=at<Mat>(work,0x1ED0);
    const size_t indices[4]={0,std::min<size_t>(1,nodes.count-1),nodes.count/2,nodes.count-1};
    for(size_t i=0;i<4;++i) {
        auto n=nodes.first+indices[i]*0x1D0;
        record.nodes[i]={static_cast<uint32_t>(indices[i]),at<Vec>(n,4),at<Vec>(n,0x10),
            at<Vec>(n,0x1C),at<Vec>(n,0x28),at<float>(n,0xC8),at<float>(n,0xCC)};
    }
    // Verified native character-collision Work layout: 64 entries, stride 0x38.
    if(accessible(work+0x2B0,64*0x38))for(size_t i=0;i<64&&record.collisionCount<4;++i) {
        auto c=work+0x2B0+i*0x38;int type=at<int>(c,0);if(!type)break;
        if(type<0||type>16)break;
        record.collisions[record.collisionCount++]={static_cast<uint32_t>(i),type,at<float>(c,4),at<Vec>(c,8),at<Vec>(c,0x14)};
    }
    motionTraceOwner=owner;
    DivaPhysicsTrace[count]=record;
    // The reader only consumes this immutable committed prefix.
    DivaPhysicsTraceCount.store(count+1,std::memory_order_release);
}
// Only callback-private character scratch changes. The second array contains
// bone-bound ring scratch; the separate static stage work list is never touched.
struct CollisionTranslationScope {
    Block work;unsigned count[2]{};bool active=false,valid=false;
    struct Entry { Vec a,b,axis; } saved[2][64]{};
    explicit CollisionTranslationScope(Block value):work(value) {
        if(!rootContext.translation||work!=rootContext.work)return;
        MemoryPhase validation;
        const unsigned arrays=rootContext.rigid?2u:1u;
        const size_t offsets[2]={0x2B0,0x10B0};
        // Preflight both arrays before any write so rejection cannot half-apply.
        for(unsigned list=0;list<arrays;++list) {
            if(!accessible(work+offsets[list],64*0x38,true))return;
            for(unsigned i=0;i<64;++i) {
                auto c=work+offsets[list]+i*0x38;int type=at<int>(c,0);if(!type)break;
                if(type<1||type>5)return;
                // Rotating an axis-aligned box requires a different primitive.
                // Forward the native solve unchanged rather than changing its shape.
                if(rootContext.rigid&&type==5)return;
                saved[list][i]={at<Vec>(c,8),at<Vec>(c,0x14),at<Vec>(c,0x20)};++count[list];
            }
        }
        for(unsigned list=0;list<arrays;++list)for(unsigned i=0;i<count[list];++i) {
            auto c=work+offsets[list]+i*0x38;const auto& e=saved[list][i];int type=at<int>(c,0);
            if(rootContext.rigid) {
                at<Vec>(c,8)=physics::transform(e.a,rootContext.delta);
                if(type==2||type==4) {
                    at<Vec>(c,0x14)=physics::transform(e.b,rootContext.delta);
                    at<Vec>(c,0x20)=physics::transform(e.axis,rootContext.delta,false);
                } else if(type==3)at<Vec>(c,0x14)=physics::transform(e.b,rootContext.delta,false);
            } else {
                const auto d=rootContext.translationDelta;
                at<Vec>(c,8)={e.a.x+d.x,e.a.y+d.y,e.a.z+d.z};
                if(type==2||type==4)at<Vec>(c,0x14)={e.b.x+d.x,e.b.y+d.y,e.b.z+d.z};
            }
        }
        active=valid=true;translationCollisions+=count[0]+count[1];
    }
    ~CollisionTranslationScope() {
        if(!active)return;
        const size_t offsets[2]={0x2B0,0x10B0};
        for(unsigned list=0;list<2;++list)for(unsigned i=0;i<count[list];++i) {
            auto c=work+offsets[list]+i*0x38;const auto& e=saved[list][i];
            at<Vec>(c,8)=e.a;at<Vec>(c,0x14)=e.b;at<Vec>(c,0x20)=e.axis;
        }
    }
};
static void __fastcall hookRootCtrl(Block work,const Mat* root,const Vec* hsc,float step) {
    CollisionTranslationScope collisions(work);
    if(rootContext.translation&&work==rootContext.work&&!collisions.valid) {
        // Reject the whole correction when newly generated collider scratch
        // has an unsupported layout; forward both native roots unchanged.
        auto context=rootContext;auto sampled=at<Mat*>(work,0x1EC8);
        if(context.rigid)++rigidSkipped;
        at<Mat*>(work,0x1EC8)=context.nativePrivate;
        rootContext.translation=false;rootContext.resampling=false;rootContext.rigid=false;
        if(context.state){context.state->translationSampler.reset();context.state->rigidSampler.reset();}
        {RootArgument argument(work,root);rootCtrl(work,argument.value,hsc,step);}
        if(at<Mat*>(work,0x1EC8)==context.nativePrivate)at<Mat*>(work,0x1EC8)=sampled;
        rootContext=context;++translationSkipped;return;
    }
    RootArgument argument(work,root);rootCtrl(work,argument.value,hsc,step);
}
static void __fastcall hookRootBegin(Block work,const Mat* root,const Vec* hsc,bool children) {
    RootArgument argument(work,root);traceMotion(work,argument.value,4,0);rootBegin(work,argument.value,hsc,children);traceMotion(work,argument.value,5,0);
}
static void __fastcall hookRootApply(Block work,const Mat* root,const Vec* hsc,float step,bool disable,bool ring,bool children) {
    RootArgument argument(work,root);traceMotion(work,argument.value,1,step);rootApply(work,argument.value,hsc,step,disable,ring,children);traceMotion(work,argument.value,2,step);
}
static void __fastcall hookRootTarget(Block work,const Mat* root,const Vec* hsc,float step,bool collide) {
    RootArgument argument(work,root);rootTarget(work,argument.value,hsc,step,collide);traceMotion(work,argument.value,3,step);
}
struct Scope {
    bool old;float oldPeriod;
    explicit Scope(float period):old(fixedScope),oldPeriod(fixedPeriod) {fixedScope=true;fixedPeriod=period;++overrides;}
    ~Scope(){fixedPeriod=oldPeriod;fixedScope=old;}
};
static bool enabled() { return !suspendPhysics&&armed.load()&&mode.load()==1&&frame.load()>30; }
static void clear(Block b) {
    // Production callers already hold the block's actor lock (or its item's
    // whole-owner lock). Do not resolve/relock it for every loading callback.
    if(auto s=findState(b)) {restore(b,*s);eraseState(b);}
}
static void __fastcall hookOsageStep(Block b,int stage,bool disable) {
    std::lock_guard<std::recursive_mutex> lock(actorMutex[blockActorIndex(b)]);
    ++stepCalls;
    if(!enabled()||!withinItem) { if(enabled())++nativeOutside;clear(b);osageStep(b,stage,disable);return; }
    auto& s=decision(b,false);
    // Staged physics does not use animationDelta in the original engine.
    // Keep its native frame-speed step, but pace all stages at PhysicsHz.
    if(s.tick) {RootScope rootScope(b,s);osageStep(b,stage,disable);}
}
static void __fastcall hookOsageMain(Block b,bool arg,bool reset=false,float delta=0,float blend=0) {
    std::lock_guard<std::recursive_mutex> lock(actorMutex[blockActorIndex(b)]);
    ++osageCalls;
    if(enabled()&&!withinItem)++outsideItem;
    if(!enabled()||!withinItem) { if(enabled())++nativeOutside;clear(b);traceName(b,"osage");lastDelta=gameDelta();lastAnimation=animationDelta();lastOsageStep=ownerStep(b);osageMain(b,arg,reset,delta,blend);return; }
    auto& s=decision(b,false);
    if(s.tick) {
        if(s.bypass) osageMain(b,arg,reset,delta,blend);
        else {
            RootScope rootScope(b,s,true);Scope scope(s.period);osageMain(b,arg,reset,delta,blend);
            // Some game paths do not call CtrlEnd. Capture after Main too.
            if(!withinItem)s.valid=capture(b,s);
            if(!withinItem&&!s.valid) {++invalid;s.bypass=true;}
        }
    }
}
static void __fastcall hookOsageEnd(Block b) {
    std::lock_guard<std::recursive_mutex> lock(actorMutex[blockActorIndex(b)]);
    ++endCalls;
    auto cached=findState(b);
    if(enabled()&&withinItem&&cached&&cached->lastFrame==frame.load()) {
        auto& s=*cached;
        if(withinItem) osageEnd(b); // Normal references are display work, not force integration.
        else if(s.bypass) osageEnd(b);
        else if(s.tick) {
            osageEnd(b);s.valid=capture(b,s);
            if(!s.valid) {++invalid;s.bypass=true;}
        } else at<unsigned char>(b,0x59)=0;
    } else {if(enabled()&&!withinItem)++nativeOutside;clear(b);osageEnd(b);}
}
static void __fastcall hookClothMain(Block b,bool arg,bool reset=false,float delta=0,float blend=0) {
    std::lock_guard<std::recursive_mutex> lock(actorMutex[blockActorIndex(b)]);
    ++clothCalls;
    if(!enabled()||!withinItem) { if(enabled())++nativeOutside;clear(b);traceName(b,"cloth");lastDelta=gameDelta();lastAnimation=animationDelta();lastOsageStep=ownerStep(b);clothMain(b,arg,reset,delta,blend);return; }
    auto& s=decision(b,true);
    if(s.tick) {
        if(s.bypass) clothMain(b,arg,reset,delta,blend);
        else { Scope scope(s.period);clothMain(b,arg,reset,delta,blend);if(!withinItem){s.valid=capture(b,s);if(!s.valid){++invalid;s.bypass=true;}} }
    }
}
static void __fastcall hookOsageOPD(Block b) { std::lock_guard<std::recursive_mutex> lock(actorMutex[blockActorIndex(b)]);++opdCalls;clear(b);osageOPD(b); }
static void __fastcall hookClothOPD(Block b) { std::lock_guard<std::recursive_mutex> lock(actorMutex[blockActorIndex(b)]);++opdCalls;clear(b);clothOPD(b); }
static void __fastcall hookOsageReset(Block b) { std::lock_guard<std::recursive_mutex> lock(actorMutex[blockActorIndex(b)]);clear(b);osageReset(b); }
static void __fastcall hookClothReset(Block b) { std::lock_guard<std::recursive_mutex> lock(actorMutex[blockActorIndex(b)]);clear(b);clothReset(b); }
static void __fastcall hookItemCtrl(Block item) {
    auto beforeLock=timestamp();
    ItemActorLock lock(item);
    itemLockWaitUs+=elapsedUs(beforeLock,timestamp());
    ++itemCalls;
    auto beforeRestore=timestamp();
    Array blocks;
    bool layout;
    {
        // No engine routine runs in this restoration phase. Reuse committed
        // region checks only within the phase, never across frames or native
        // callbacks that might change an allocation.
        MemoryPhase phase;
        layout=arrayAt(item,0xC0,8,2048,blocks);
        if(layout)for(size_t i=0;i<blocks.count;++i) {
            auto b=at<Block>(blocks.first,i*8);
            if(auto s=findState(b))restore(b,*s);
        }
    }
    bool old=withinItem,oldSuspend=suspendPhysics;
    // Initial settling and baked playback remain native. Reset their histories.
    bool active=enabled()&&layout&&at<int>(item,0xB0)<=0&&!at<unsigned char>(item,0x1C8);
    if(!active&&layout)for(size_t i=0;i<blocks.count;++i)clear(at<Block>(blocks.first,i*8));
    withinItem=active;
    suspendPhysics=oldSuspend||!active;
    auto beforeNative=timestamp();
    restoreUs+=elapsedUs(beforeRestore,beforeNative);
    itemCtrl(item);
    auto afterNative=timestamp();nativeUs+=elapsedUs(beforeNative,afterNative);
    withinItem=old;suspendPhysics=oldSuspend;
    if(!active||!enabled()||!arrayAt(item,0xC0,8,2048,blocks))return;
    MemoryPhase phase;
    // Capture all native results before writing any display matrices, including
    // parent roots used by dependent blocks later in the ordered item list.
    for(size_t i=0;i<blocks.count;++i) {
        auto b=at<Block>(blocks.first,i*8);auto cached=findState(b);
        if(!cached)continue;auto& s=*cached;
        if(s.lastFrame!=frame.load()||s.bypass||!s.tick)continue;
        s.valid=capture(b,s);if(!s.valid){++invalid;s.bypass=true;}
    }
    for(size_t i=0;i<blocks.count;++i) {
        auto b=at<Block>(blocks.first,i*8);auto cached=findState(b);
        if(!cached)continue;auto& s=*cached;
        if(s.lastFrame!=frame.load()||s.bypass||!s.valid)continue;
        if(!present(b,s)){++invalid;restore(b,s);s.bypass=true;s.valid=false;}
    }
    presentUs+=elapsedUs(afterNative,timestamp());
}
static LONG CALLBACK exceptionLog(EXCEPTION_POINTERS* exception) {
    DWORD code=exception->ExceptionRecord->ExceptionCode;
    if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_ILLEGAL_INSTRUCTION
        &&code!=EXCEPTION_INT_DIVIDE_BY_ZERO&&code!=EXCEPTION_STACK_OVERFLOW) return EXCEPTION_CONTINUE_SEARCH;
    auto ip=exception->ContextRecord->Rip;
    HMODULE faultModule=nullptr;wchar_t name[1024]{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(ip),&faultModule);
    if(faultModule)GetModuleFileNameW(faultModule,name,1024);
    log("EXCEPTION code=%08lx ip=%llx module=%ls offset=%llx frame=%llu mode=%d",
        code,static_cast<unsigned long long>(ip),name,
        static_cast<unsigned long long>(ip-reinterpret_cast<uintptr_t>(faultModule)),
        static_cast<unsigned long long>(frame.load()),mode.load());
    return EXCEPTION_CONTINUE_SEARCH;
}
extern "C" __declspec(dllexport) bool DivaPhysicsGetControls(physics_settings::Controls* out) {
    if(!out||out->size!=sizeof(*out)||out->version!=1)return false;
    std::lock_guard<std::mutex> lock(controlsMutex);
    physics_settings::Controls value;
    value.mode=mode.load();value.hairHz=groupRate(physics_settings::Group::Hair);
    value.clothingHz=groupRate(physics_settings::Group::Clothing);
    value.hairEnabled=hairEnabled.load();value.clothingEnabled=clothingEnabled.load();
    value.hairInterpolation=hairInterpolation.load()<0?interpolateOsage.load():hairInterpolation.load();
    value.hairSmoothing=hairSmoothing.load()<0?smoothOsage.load():hairSmoothing.load();
    value.clothingInterpolation=clothingInterpolation.load()<0?interpolateOsage.load():clothingInterpolation.load();
    value.armed=armed.load()?1:0;value.pending=hasPendingControls?1:0;
    value.lastSaveError=controlsSaveError;value.revision=controlsRevision;
    *out=value;return true;
}
extern "C" __declspec(dllexport) bool DivaPhysicsSetControls(const physics_settings::Controls* in) {
    if(!in||!physics_settings::valid(*in)||!armed.load())return false;
    // ReShade can render on another thread. Queue settings only; OnFrame
    // persists and applies them at the same boundary used for INI reloads.
    std::lock_guard<std::mutex> lock(controlsMutex);
    pendingControls=*in;hasPendingControls=true;controlsSaveError=0;return true;
}
static bool flushControlSettings() {
    std::lock_guard<std::mutex> lock(controlsMutex);
    if(!hasPendingControls)return false;
    auto settings=pendingControls;hasPendingControls=false;
    if(configPath.empty()||GetFileAttributesW(configPath.c_str())==INVALID_FILE_ATTRIBUTES) {
        controlsSaveError=ERROR_FILE_NOT_FOUND;return false;
    }
    struct Setting {const wchar_t* key;int value;};
    Setting desired[]={{L"Mode",settings.mode},{L"HairHz",settings.hairHz},{L"ClothingHz",settings.clothingHz},
        {L"HairEnabled",settings.hairEnabled},{L"ClothingEnabled",settings.clothingEnabled},
        {L"HairInterpolation",settings.hairInterpolation},{L"HairSmoothing",settings.hairSmoothing},
        {L"ClothingInterpolation",settings.clothingInterpolation}};
    wchar_t section[32768]{};
    DWORD count=GetPrivateProfileSectionW(L"Physics",section,32768,configPath.c_str());
    if(count>=32766){controlsSaveError=ERROR_INSUFFICIENT_BUFFER;return false;}
    std::wstring updated;
    for(const wchar_t* record=section;*record;record+=std::wcslen(record)+1) {
        std::wstring line(record);auto equals=line.find(L'=');bool controlled=false;
        if(equals!=std::wstring::npos) {
            auto key=line.substr(0,equals);
            for(const auto& setting:desired)if(!_wcsicmp(key.c_str(),setting.key)){controlled=true;break;}
        }
        if(!controlled){updated+=line;updated.push_back(L'\0');}
    }
    for(const auto& setting:desired){updated+=setting.key;updated+=L'=';updated+=std::to_wstring(setting.value);updated.push_back(L'\0');}
    updated.push_back(L'\0');
    if(!WritePrivateProfileSectionW(L"Physics",updated.c_str(),configPath.c_str())) {
        controlsSaveError=static_cast<int>(GetLastError());if(!controlsSaveError)controlsSaveError=ERROR_WRITE_FAULT;return false;
    }
    for(const auto& setting:desired)if(GetPrivateProfileIntW(L"Physics",setting.key,-1,configPath.c_str())!=static_cast<UINT>(setting.value)) {
        controlsSaveError=ERROR_WRITE_FAULT;return false;
    }
    ++controlsRevision;controlsSaveError=0;log("CONTROLS_SAVED revision=%u",controlsRevision);return true;
}
static void readConfig() {
    std::lock_guard<std::mutex> settingsLock(controlsMutex);
    int rigidTrial=GetPrivateProfileIntW(L"Physics",L"RigidInputs",0,configPath.c_str())==1?1:0;
    if(rigidInputs.exchange(rigidTrial)!=rigidTrial) {
        ++epoch;log("RIGID_INPUTS=%d (Miku tail Main-only root rotation/translation and private body colliders share the fixed physics grid)",rigidTrial);
    }
    int translating=GetPrivateProfileIntW(L"Physics",L"TranslationInputs",0,configPath.c_str())==1?1:0;
    if(translationInputs.exchange(translating)!=translating) {
        ++epoch;log("TRANSLATION_INPUTS=%d (Miku tail Main-only fixed-grid translation; private ancestor root and character collider centers coordinated)",translating);
    }
    int cookie=GetPrivateProfileIntW(L"Physics",L"MotionTrace",0,configPath.c_str());
    if(cookie<0)cookie=0;
    int oldCookie=lastMotionCookie.exchange(cookie);
    if(!cookie)motionTraceEnd=0;
    else if(cookie!=oldCookie) {
        int seconds=GetPrivateProfileIntW(L"Physics",L"MotionTraceSeconds",30,configPath.c_str());
        if(seconds<5||seconds>30)seconds=30;
        startMotionTrace(static_cast<uint32_t>(cookie),seconds);
    }
    int rate=GetPrivateProfileIntW(L"Physics",L"PhysicsHz",30,configPath.c_str());
    if(rate!=30&&rate!=40)rate=30;
    if(physicsHz.exchange(rate)!=rate) {
        ++epoch;log("PHYSICS_HZ=%d (render/body rate unchanged; pose history restarts at new physics period)",rate);
    }
    bool groupChanged=false;
    int nextHair=physics_settings::validOverride(GetPrivateProfileIntW(L"Physics",L"HairHz",0,configPath.c_str()));
    int nextClothing=physics_settings::validOverride(GetPrivateProfileIntW(L"Physics",L"ClothingHz",0,configPath.c_str()));
    groupChanged|=hairHz.exchange(nextHair)!=nextHair;
    groupChanged|=clothingHz.exchange(nextClothing)!=nextClothing;
    int hairOn=GetPrivateProfileIntW(L"Physics",L"HairEnabled",1,configPath.c_str())==1?1:0;
    int clothingOn=GetPrivateProfileIntW(L"Physics",L"ClothingEnabled",1,configPath.c_str())==1?1:0;
    groupChanged|=hairEnabled.exchange(hairOn)!=hairOn;
    groupChanged|=clothingEnabled.exchange(clothingOn)!=clothingOn;
    if(groupChanged){++epoch;log("GROUPS hairHz=%d clothingHz=%d hairEnabled=%d clothingEnabled=%d (other/body uses legacy PhysicsHz)",groupRate(physics_settings::Group::Hair),groupRate(physics_settings::Group::Clothing),hairOn,clothingOn);}
    auto option=[&](const wchar_t* key){int value=GetPrivateProfileIntW(L"Physics",key,-1,configPath.c_str());return value==0||value==1?value:-1;};
    hairInterpolation=option(L"HairInterpolation");hairSmoothing=option(L"HairSmoothing");clothingInterpolation=option(L"ClothingInterpolation");
    int timed=GetPrivateProfileIntW(L"Physics",L"TimeAwareInterpolation",0,configPath.c_str())==1?1:0;
    if(timeAwareInterpolation.exchange(timed)!=timed)
        log("TIME_AWARE_INTERPOLATION=%d (osage poses bracket actual capture time with one configured physics tick of display delay)",timed);
    int next=GetPrivateProfileIntW(L"Physics",L"Mode",0,configPath.c_str());
    if(next!=1) next=0;
    if(mode.exchange(next)!=next) { ++epoch;log("MODE=%d (0=original diagnostic, 1=paced secondary character physics)",next); }
    int smoothing=GetPrivateProfileIntW(L"Physics",L"Smoothing",0,configPath.c_str())==1?1:0;
    if(smoothOsage.exchange(smoothing)!=smoothing)log("SMOOTHING=%d (0=two-pose interpolation, 1=three-pose osage display smoothing)",smoothing);
    int interpolating=GetPrivateProfileIntW(L"Physics",L"InterpolateOsage",1,configPath.c_str())==1?1:0;
    if(interpolateOsage.exchange(interpolating)!=interpolating)
        log("INTERPOLATE_OSAGE=%d (0=hold native physical pose, 1=interpolated display; physics uses configured rate)",interpolating);
    int capturing=GetPrivateProfileIntW(L"Physics",L"CaptureRoots",1,configPath.c_str())==1?1:0;
    if(captureRoots.exchange(capturing)!=capturing) {
        ++epoch;log("CAPTURE_ROOTS=%d (0=late parent snapshot, 1=actual physics callback input)",capturing);
    }
    int resampling=GetPrivateProfileIntW(L"Physics",L"ResampleRoots",0,configPath.c_str())==1?1:0;
    if(resampleRoots.exchange(resampling)!=resampling) {
        ++epoch;log("RESAMPLE_ROOTS=%d (0=render-time native roots, 1=fixed-tick animation root samples)",resampling);
    }
}
extern "C" __declspec(dllexport) void OnFrame() {
    ++frame;
    static ULONGLONG configTime=0,statsTime=0;
    auto now=GetTickCount64();
    if(flushControlSettings()||now-configTime>=500) { configTime=now;readConfig(); }
    if(now-statsTime>=2000&&armed.load()) {
        if(rigidInputs.load())log("RIGID_STATS scopes=%llu skipped=%llu",
            static_cast<unsigned long long>(rigidScopes.exchange(0)),static_cast<unsigned long long>(rigidSkipped.exchange(0)));
        if(translationInputs.load())log("TRANSLATION_STATS scopes=%llu children=%llu colliders=%llu skipped=%llu",
            static_cast<unsigned long long>(translationScopes.exchange(0)),static_cast<unsigned long long>(translationChildren.exchange(0)),
            static_cast<unsigned long long>(translationCollisions.exchange(0)),static_cast<unsigned long long>(translationSkipped.exchange(0)));
        statsTime=now;
        log("STATS frame=%llu mode=%d smoothing=%d osage=%llu cloth=%llu stages=%llu opd=%llu ticks=%llu held=%llu stepScales=%llu invalid=%llu rawDelta=%.6f animation=%.6f ownerStep=%.6f endCalls=%llu items=%llu presented=%llu outsideItem=%llu restoreUs=%llu nativeUs=%llu presentUs=%llu rootResample=%d rootScopes=%llu rootArgs=%llu rootDependent=%llu rootSkipped=%llu captureRoots=%d rootsCaptured=%llu rootsChangedLate=%llu rootLateShiftMax=%.6f interpolateOsage=%d nativeOutside=%llu itemLockWaitUs=%llu timeAwareInterpolation=%d physicsHz=%d",
            static_cast<unsigned long long>(frame.load()),mode.load(),
            smoothOsage.load(),
            static_cast<unsigned long long>(osageCalls.exchange(0)),static_cast<unsigned long long>(clothCalls.exchange(0)),
            static_cast<unsigned long long>(stepCalls.exchange(0)),static_cast<unsigned long long>(opdCalls.exchange(0)),
            static_cast<unsigned long long>(ticks.exchange(0)),static_cast<unsigned long long>(held.exchange(0)),
            static_cast<unsigned long long>(overrides.exchange(0)),static_cast<unsigned long long>(invalid.exchange(0)),
            static_cast<double>(lastDelta.load()),static_cast<double>(lastAnimation.load()),static_cast<double>(lastOsageStep.load()),
            static_cast<unsigned long long>(endCalls.exchange(0)),
            static_cast<unsigned long long>(itemCalls.exchange(0)),static_cast<unsigned long long>(presented.exchange(0)),
            static_cast<unsigned long long>(outsideItem.exchange(0)),
            static_cast<unsigned long long>(restoreUs.exchange(0)),static_cast<unsigned long long>(nativeUs.exchange(0)),
            static_cast<unsigned long long>(presentUs.exchange(0)),resampleRoots.load(),
            static_cast<unsigned long long>(rootSampled.exchange(0)),static_cast<unsigned long long>(rootArguments.exchange(0)),
            static_cast<unsigned long long>(rootDependent.exchange(0)),static_cast<unsigned long long>(rootSkipped.exchange(0)),
            captureRoots.load(),static_cast<unsigned long long>(rootsCaptured.exchange(0)),
            static_cast<unsigned long long>(rootsChangedLate.exchange(0)),static_cast<double>(rootLateShiftMax.exchange(0)),interpolateOsage.load(),nativeOutside.exchange(0),itemLockWaitUs.exchange(0),timeAwareInterpolation.load(),physicsHz.load());
    }
}
extern "C" __declspec(dllexport) void Init() {
    if(armed.load()) return;
    wchar_t path[32768]{};GetModuleFileNameW(module,path,32768);
    std::wstring folder(path);folder.resize(folder.find_last_of(L"\\/")+1);
    configPath=folder+L"physics.ini";
    logfile=_wfsopen((folder+L"DivaPhysicsTiming.log").c_str(),L"w",_SH_DENYNO);
    log("Diva Physics Timing 0.1.18 adjustable hair/clothing cadence and ReShade controls; 30 Hz baseline retained");
    AddVectoredExceptionHandler(1,exceptionLog);
    base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE||nt->Signature!=IMAGE_NT_SIGNATURE) { log("REFUSED: invalid PE");return; }
    for(auto& t:targets) {
        if(t.rva+16>nt->OptionalHeader.SizeOfImage||std::memcmp(reinterpret_cast<void*>(base+t.rva),t.bytes,16)) {
            log("REFUSED: code signature mismatch at %s RVA=%llx",t.name,static_cast<unsigned long long>(t.rva));return;
        }
    }
    const uintptr_t osageTable=0xCB6700,clothTable=0xCB6698;
    if(*reinterpret_cast<uintptr_t*>(base+osageTable+4*8)!=base+0x5948E0
        ||*reinterpret_cast<uintptr_t*>(base+clothTable+4*8)!=base+0x5971E0) {
        log("REFUSED: physics vtable mismatch");return;
    }
    animationDelta=reinterpret_cast<DeltaFn>(base+0x2B73B0);
    gameDelta=reinterpret_cast<DeltaFn>(base+0x2B7370);
    clothRoot=reinterpret_cast<VoidFn>(base+0x30BDA0);
    auto status=MH_Initialize();
    if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED) { log("REFUSED: MinHook initialize %d",status);return; }
    struct Hook {uintptr_t rva;void* replacement;void** original;};
    Hook hooks[]={
        {0x590560,reinterpret_cast<void*>(hookItemCtrl),reinterpret_cast<void**>(&itemCtrl)},
        {0x5946E0,reinterpret_cast<void*>(hookOsageStep),reinterpret_cast<void**>(&osageStep)},
        {0x5948E0,reinterpret_cast<void*>(hookOsageMain),reinterpret_cast<void**>(&osageMain)},
        {0x594B50,reinterpret_cast<void*>(hookOsageEnd),reinterpret_cast<void**>(&osageEnd)},
        {0x594AF0,reinterpret_cast<void*>(hookOsageOPD),reinterpret_cast<void**>(&osageOPD)},
        {0x594140,reinterpret_cast<void*>(hookOsageReset),reinterpret_cast<void**>(&osageReset)},
        {0x5971E0,reinterpret_cast<void*>(hookClothMain),reinterpret_cast<void**>(&clothMain)},
        {0x597280,reinterpret_cast<void*>(hookClothOPD),reinterpret_cast<void**>(&clothOPD)},
        {0x5972C0,reinterpret_cast<void*>(hookClothReset),reinterpret_cast<void**>(&clothReset)},
        {0x2B73B0,reinterpret_cast<void*>(AnimationDeltaProxy),reinterpret_cast<void**>(&animationDelta)},
        {0x47DBE0,reinterpret_cast<void*>(hookRootCtrl),reinterpret_cast<void**>(&rootCtrl)},
        {0x47EE50,reinterpret_cast<void*>(hookRootBegin),reinterpret_cast<void**>(&rootBegin)},
        {0x47DC80,reinterpret_cast<void*>(hookRootApply),reinterpret_cast<void**>(&rootApply)},
        {0x47F770,reinterpret_cast<void*>(hookRootTarget),reinterpret_cast<void**>(&rootTarget)},
    };
    for(auto& h:hooks) {
        status=MH_CreateHook(reinterpret_cast<void*>(base+h.rva),h.replacement,h.original);
        if(status!=MH_OK) { log("REFUSED: create hook RVA=%llx status=%d",static_cast<unsigned long long>(h.rva),status);MH_Uninitialize();return; }
    }
    readConfig();
    status=MH_EnableHook(MH_ALL_HOOKS);
    if(status!=MH_OK) { log("REFUSED: enable hooks %d",status);MH_Uninitialize();return; }
    armed=true;log("ARMED: 14 validated hooks; Mode=%d; late presentation isolated from simulation; no shared owner-step writes",mode.load());
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {module=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
