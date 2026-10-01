#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace physics {
// The engine delta is measured in 60 Hz frame units: 2 units = 1/30 second.
// Schedule only character physics; never replace the global game delta.
struct Clock {
    double remainder = 0;
    double time = 0;
    bool initialized = false;
    bool advance(float delta,double period=2.0) {
        if (!std::isfinite(delta) || delta <= 0 || !std::isfinite(period) || period <= 0) return false;
        if (!initialized) { initialized = true; remainder = 0; return true; }
        // Avoid accumulating a whole song seek / alt-tab gap as catch-up work.
        const double elapsed=std::min<double>(delta, 4.0);
        time += elapsed;remainder += elapsed;
        if (remainder + 1e-5 < period) return false;
        remainder = std::fmod(std::max(remainder,period), period);
        return true;
    }
    void reset() { remainder = 0; time = 0; initialized = false; }
    float alpha(double period=2.0) const { return static_cast<float>(std::clamp(remainder / period, 0.0, 1.0)); }
};
// Native ticks execute on the render frame crossing the 30 Hz boundary.
// Their actual timestamps are uneven. Blend poses at a fixed delayed display
// time, using the older pair when a short tick would otherwise move time ahead.
struct PoseBlend { bool older=false;float alpha=0; };
struct PoseTimeline {
    double earlier=0,previous=0,current=0;bool initialized=false;
    void reset(){*this=PoseTimeline{};}
    void capture(double stamp,bool history) {
        if(!history||!initialized||!std::isfinite(stamp)||stamp<=current) {
            earlier=previous=current=stamp;initialized=true;return;
        }
        earlier=previous;previous=current;current=stamp;
    }
    PoseBlend sample(double now,double delay=2.0) const {
        if(!initialized||!std::isfinite(now))return {};
        const double target=now-delay;
        bool older=target<previous&&previous>earlier;
        double a=older?earlier:previous,b=older?previous:current;
        float alpha=b>a?static_cast<float>(std::clamp((target-a)/(b-a),0.0,1.0)):0;
        return {older,alpha};
    }
};
struct Mat { float v[16]{}; };
struct Vec { float x=0,y=0,z=0; };
inline Vec lerp(Vec a,Vec b,float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t}; }
inline Vec normalized(Vec a) {
    float length=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
    if(length>1e-8f) return {a.x/length,a.y/length,a.z/length};
    return a;
}
inline Mat identity() { Mat m; for (int i=0;i<4;++i) m.v[i*4+i]=1; return m; }
inline Mat transpose(const Mat& source) {
    Mat result;
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)result.v[r*4+c]=source.v[c*4+r];
    return result;
}
// This MM+ executable stores column-vector matrices, translation at 3/7/11.
// All math below uses a canonical row-vector representation, at 12/13/14.
// Convert at every game-memory boundary; do not apply row math to native bytes.
inline Mat fromGame(const Mat& source){return transpose(source);}
inline Mat toGame(const Mat& source){return transpose(source);}
inline bool finite(const Mat& m) { for(float f:m.v) if(!std::isfinite(f)) return false; return true; }
inline bool affine(const Mat& m) {
    return finite(m)&&std::abs(m.v[3])<0.001f&&std::abs(m.v[7])<0.001f
        &&std::abs(m.v[11])<0.001f&&std::abs(m.v[15]-1)<0.001f;
}
inline Mat mul(const Mat& a,const Mat& b) {
    Mat m;
    for(int r=0;r<4;++r) for(int c=0;c<4;++c)
        for(int k=0;k<4;++k) m.v[r*4+c]+=a.v[r*4+k]*b.v[k*4+c];
    return m;
}
inline bool inverse(const Mat& m,Mat& result) {
    if(!finite(m)) return false;
    double a[4][8]{};
    for(int r=0;r<4;++r) { for(int c=0;c<4;++c) a[r][c]=m.v[r*4+c]; a[r][r+4]=1; }
    for(int c=0;c<4;++c) {
        int best=c; for(int r=c+1;r<4;++r) if(std::abs(a[r][c])>std::abs(a[best][c])) best=r;
        if(std::abs(a[best][c])<1e-9) return false;
        for(int j=0;j<8;++j) std::swap(a[best][j],a[c][j]);
        double d=a[c][c]; for(double& f:a[c]) f/=d;
        for(int r=0;r<4;++r) if(r!=c) { d=a[r][c]; for(int j=0;j<8;++j) a[r][j]-=d*a[c][j]; }
    }
    for(int r=0;r<4;++r) for(int c=0;c<4;++c) result.v[r*4+c]=static_cast<float>(a[r][c+4]);
    return finite(result);
}
// Canonical matrices use row vectors and translation in the last row.
inline Vec transform(Vec p,const Mat& m,bool point=true) {
    return {p.x*m.v[0]+p.y*m.v[4]+p.z*m.v[8]+(point?m.v[12]:0),
            p.x*m.v[1]+p.y*m.v[5]+p.z*m.v[9]+(point?m.v[13]:0),
            p.x*m.v[2]+p.y*m.v[6]+p.z*m.v[10]+(point?m.v[14]:0)};
}
inline bool rebase(const Mat& oldRoot,const Mat& newRoot,Mat& delta) {
    Mat inv;
    if(!inverse(oldRoot,inv)||!finite(newRoot)) return false;
    delta=mul(inv,newRoot);
    return finite(delta);
}
struct Quat { float x=0,y=0,z=0,w=1; };
inline Quat normalized(Quat q) {
    float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    return {q.x/n,q.y/n,q.z/n,q.w/n};
}
inline Quat rotation(const Mat& rowMatrix) {
    // Quaternion extraction uses the transpose of the game's row-vector matrix.
    float m[3][3];
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)m[r][c]=rowMatrix.v[c*4+r];
    Quat q;
    float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0) {
        float s=std::sqrt(trace+1)*2;
        q={ (m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s,s*0.25f };
    } else {
        int i=m[1][1]>m[0][0]?1:0;if(m[2][2]>m[i][i])i=2;
        int j=(i+1)%3,k=(i+2)%3;
        float s=std::sqrt(std::max(0.0f,1+m[i][i]-m[j][j]-m[k][k]))*2;
        float v[3]{};v[i]=s*0.25f;v[j]=(m[i][j]+m[j][i])/s;v[k]=(m[i][k]+m[k][i])/s;
        q={v[0],v[1],v[2],(m[k][j]-m[j][k])/s};
    }
    return normalized(q);
}
inline Quat slerp(Quat a,Quat b,float t) {
    float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0){b={-b.x,-b.y,-b.z,-b.w};dot=-dot;}
    float u=1-t,v=t;
    if(dot<0.9995f) {
        float angle=std::acos(std::clamp(dot,-1.0f,1.0f)),sinAngle=std::sin(angle);
        u=std::sin((1-t)*angle)/sinAngle;v=std::sin(t*angle)/sinAngle;
    }
    return normalized({u*a.x+v*b.x,u*a.y+v*b.y,u*a.z+v*b.z,u*a.w+v*b.w});
}
inline Mat rotation(Quat q) {
    Mat m=identity();
    m.v[0]=1-2*(q.y*q.y+q.z*q.z);m.v[1]=2*(q.x*q.y+q.z*q.w);m.v[2]=2*(q.x*q.z-q.y*q.w);
    m.v[4]=2*(q.x*q.y-q.z*q.w);m.v[5]=1-2*(q.x*q.x+q.z*q.z);m.v[6]=2*(q.y*q.z+q.x*q.w);
    m.v[8]=2*(q.x*q.z+q.y*q.w);m.v[9]=2*(q.y*q.z-q.x*q.w);m.v[10]=1-2*(q.x*q.x+q.y*q.y);
    return m;
}
inline bool decompose(const Mat& m,Mat& r,float scale[3]) {
    if(!affine(m))return false;
    r=identity();
    for(int row=0;row<3;++row){
        float n=0;for(int c=0;c<3;++c)n+=m.v[row*4+c]*m.v[row*4+c];
        scale[row]=std::sqrt(n);if(scale[row]<1e-7f)return false;
        for(int c=0;c<3;++c)r.v[row*4+c]=m.v[row*4+c]/scale[row];
    }
    for(int a=0;a<3;++a)for(int b=a+1;b<3;++b){
        float dot=0;for(int c=0;c<3;++c)dot+=r.v[a*4+c]*r.v[b*4+c];
        if(std::abs(dot)>0.001f)return false; // Preserve shear with affine fallback.
    }
    float determinant=r.v[0]*(r.v[5]*r.v[10]-r.v[6]*r.v[9])
        -r.v[1]*(r.v[4]*r.v[10]-r.v[6]*r.v[8])+r.v[2]*(r.v[4]*r.v[9]-r.v[5]*r.v[8]);
    if(determinant<0){scale[0]=-scale[0];for(int c=0;c<3;++c)r.v[c]=-r.v[c];}
    return true;
}
inline Mat interpolate(const Mat& a,const Mat& b,float t) {
    t=std::clamp(t,0.0f,1.0f);
    if(t==0)return a;if(t==1)return b;
    Mat ra,rb,m;float sa[3],sb[3];
    if(decompose(a,ra,sa)&&decompose(b,rb,sb)) {
        m=rotation(slerp(rotation(ra),rotation(rb),t));
        for(int r=0;r<3;++r)for(int c=0;c<3;++c)m.v[r*4+c]*=sa[r]+(sb[r]-sa[r])*t;
        for(int c=12;c<15;++c)m.v[c]=a.v[c]+(b.v[c]-a.v[c])*t;
    } else for(int i=0;i<16;++i)m.v[i]=a.v[i]+(b.v[i]-a.v[i])*t;
    return m;
}

// Sample the animated root at the last exact 30 Hz tick, instead of at the
// render frame that happened to cross it. Only animated roots use this sampler.
// Translation-only input sampling leaves render-time rotation and scale intact.
// A large discontinuity reacquires the current input instead of blending a seek.
struct TranslationSampler {
    Vec previous{};bool initialized=false;
    void reset(){initialized=false;}
    bool sample(Vec current,float elapsed,double remainder,bool tick,Vec& result,double period) {
        if(!std::isfinite(current.x)||!std::isfinite(current.y)||!std::isfinite(current.z)){reset();return false;}
        result=current;
        float dx=current.x-previous.x,dy=current.y-previous.y,dz=current.z-previous.z;
        bool usable=initialized&&std::isfinite(elapsed)&&elapsed>0&&elapsed<period
            &&std::isfinite(remainder)&&remainder>=0&&remainder<period&&dx*dx+dy*dy+dz*dz<.25f;
        if(usable&&tick)result=lerp(previous,current,std::clamp(1.f-static_cast<float>(remainder)/elapsed,0.f,1.f));
        previous=current;initialized=true;return tick;
    }
};
// A proper rigid transform is required before rotating collision scratch.
inline bool rigid(const Mat& m,float tolerance=.002f) {
    if(!affine(m))return false;
    for(int r=0;r<3;++r) {
        float norm=0;for(int c=0;c<3;++c)norm+=m.v[r*4+c]*m.v[r*4+c];
        if(std::abs(norm-1)>tolerance)return false;
        for(int q=r+1;q<3;++q) {
            float dot=0;for(int c=0;c<3;++c)dot+=m.v[r*4+c]*m.v[q*4+c];
            if(std::abs(dot)>tolerance)return false;
        }
    }
    float determinant=m.v[0]*(m.v[5]*m.v[10]-m.v[6]*m.v[9])
        -m.v[1]*(m.v[4]*m.v[10]-m.v[6]*m.v[8])+m.v[2]*(m.v[4]*m.v[9]-m.v[5]*m.v[8]);
    return std::abs(determinant-1)<tolerance*2;
}
struct RigidSampler {
    Mat previous{};bool initialized=false;
    void reset(){initialized=false;}
    bool sample(const Mat& current,float elapsed,double remainder,bool tick,Mat& result,double period) {
        if(!rigid(current)){reset();return false;}
        result=current;
        float dx=current.v[12]-previous.v[12],dy=current.v[13]-previous.v[13],dz=current.v[14]-previous.v[14];
        bool usable=initialized&&std::isfinite(elapsed)&&elapsed>0&&elapsed<period
            &&std::isfinite(remainder)&&remainder>=0&&remainder<period&&dx*dx+dy*dy+dz*dz<.25f;
        if(usable) {
            auto a=rotation(previous),b=rotation(current);
            float dot=std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
            usable=dot>.70710678f; // Reacquire a rotation jump of 90 degrees or more.
        }
        if(usable&&tick)result=interpolate(previous,current,std::clamp(1.f-static_cast<float>(remainder)/elapsed,0.f,1.f));
        previous=current;initialized=true;return tick&&rigid(result);
    }
};
struct RootSampler {
    Mat previous{};
    bool initialized=false;
    void reset(){initialized=false;}
    bool sample(const Mat& current,float elapsed,double remainder,bool tick,Mat& result,double period=2.0) {
        if(!affine(current)){reset();return false;}
        result=current;
        bool usable=initialized&&std::isfinite(elapsed)&&elapsed>0&&elapsed<period
            &&std::isfinite(remainder)&&remainder>=0&&remainder<period;
        if(usable&&tick) {
            float fraction=std::clamp(1.0f-static_cast<float>(remainder)/elapsed,0.0f,1.0f);
            result=interpolate(previous,current,fraction);
        }
        previous=current;initialized=true;
        return tick&&affine(result);
    }
};

// A convex quadratic B-spline over three simulated poses. The position and
// quaternion curves meet with continuous first derivatives at tick boundaries.
// It adds half a physics tick of display delay relative to two-pose interpolation.
inline Mat smooth(const Mat& earlier,const Mat& previous,const Mat& current,float t) {
    t=std::clamp(t,0.0f,1.0f);
    const float wa=0.5f*(1-t)*(1-t),wb=0.5f+t-t*t,wc=0.5f*t*t;
    Mat ra,rb,rc,m;float sa[3],sb[3],sc[3];
    if(decompose(earlier,ra,sa)&&decompose(previous,rb,sb)&&decompose(current,rc,sc)) {
        auto a=rotation(ra),b=rotation(rb),c=rotation(rc);
        // Keep the three quaternions in a common hemisphere before blending.
        if(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w<0)a={-a.x,-a.y,-a.z,-a.w};
        if(c.x*b.x+c.y*b.y+c.z*b.z+c.w*b.w<0)c={-c.x,-c.y,-c.z,-c.w};
        m=rotation(normalized({wa*a.x+wb*b.x+wc*c.x,wa*a.y+wb*b.y+wc*c.y,
            wa*a.z+wb*b.z+wc*c.z,wa*a.w+wb*b.w+wc*c.w}));
        for(int r=0;r<3;++r)for(int col=0;col<3;++col)m.v[r*4+col]*=wa*sa[r]+wb*sb[r]+wc*sc[r];
        for(int col=12;col<15;++col)m.v[col]=wa*earlier.v[col]+wb*previous.v[col]+wc*current.v[col];
    } else for(int i=0;i<16;++i)m.v[i]=wa*earlier.v[i]+wb*previous.v[i]+wc*current.v[i];
    return m;
}
}
