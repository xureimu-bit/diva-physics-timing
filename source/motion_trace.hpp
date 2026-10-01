#pragma once
#include <atomic>
#include <cstdint>
// Packed read-only diagnostic format. Native matrices remain column-vector.
// A committed prefix never changes until a new trace cookie starts.
#pragma pack(push,1)
struct MotionTraceNode {
    uint32_t index;physics::Vec position,oldPosition,displacement,velocity;
    float hit,friction;
};
struct MotionTraceCollision { uint32_t index;int32_t type;float radius;physics::Vec a,b; };
struct MotionTraceRecord {
    uint64_t frame,timeUs,block,owner;
    uint32_t cookie,phase;float period,step;
    uint32_t nodeCount,flags;char name[64];
    float localCancel,skinCancel,airResistance,stiffness;
    physics::Mat argument,parent,privateCurrent,privatePrevious;
    MotionTraceNode nodes[4];uint32_t collisionCount;
    MotionTraceCollision collisions[4];
};
#pragma pack(pop)
static_assert(sizeof(MotionTraceRecord)==780,"motion trace packed layout");
extern "C" {
__declspec(dllexport) uint32_t DivaPhysicsTraceMagic=0x31505444;
__declspec(dllexport) uint32_t DivaPhysicsTraceStride=sizeof(MotionTraceRecord);
__declspec(dllexport) uint32_t DivaPhysicsTraceCapacity=16384;
__declspec(dllexport) std::atomic<uint32_t> DivaPhysicsTraceCount{0};
__declspec(dllexport) std::atomic<uint32_t> DivaPhysicsTraceCookie{0};
__declspec(dllexport) MotionTraceRecord DivaPhysicsTrace[16384]{};
}
static_assert(sizeof(std::atomic<uint32_t>)==4,"external atomic layout");
