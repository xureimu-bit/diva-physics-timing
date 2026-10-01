#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <atomic>
static thread_local bool fixed=false;
static thread_local float fixedReturn=2.0f;
extern "C" float PhysicsAnimationDelta(){return fixed?fixedReturn:0.5f;}
struct alignas(16) Report {uint64_t gpr[7],before,after,pad;float xmm[6][4];};
extern "C" void TestProxyRegisters(Report*);
std::atomic<bool> failed=false;
void check(){
    Report r{};TestProxyRegisters(&r);
    for(int i=0;i<7;++i) if(r.gpr[i]!=0x11111111ull*(i+1))failed=true;
    if(r.before!=r.after||r.xmm[0][0]!=(fixed?fixedReturn:0.5f))failed=true;
    for(int x=1;x<6;++x)for(int i=0;i<4;++i)if(r.xmm[x][i]!=static_cast<float>(i+1))failed=true;
}
int main(){
    fixed=false;check();fixed=true;check();fixedReturn=1.5f;check();
    std::thread a([]{for(int i=0;i<10000;++i){fixedReturn=1.5f;fixed=true;check();}});
    std::thread b([]{for(int i=0;i<10000;++i){fixed=false;check();}});
    a.join();b.join();
    if(failed){std::puts("FAIL delta proxy ABI/thread isolation");return 1;}
    std::puts("PASS: GPR/XMM1-5/flags preserved, 30/40 Hz scalar return, concurrent per-thread physics isolation");
}
