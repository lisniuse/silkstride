#pragma once

// Small position based Verlet solver. V/length/unit are supplied by main.cpp.
// A pinned particle has zero inverse mass. Constraints on the limbs therefore
// feed back into the free torso instead of being solved against a scripted body.
class CrawlPhysics {
public:
    struct Particle { V p,old; float mass; };
    struct Bone { int a,b; float rest; };
    struct Bend { int a,b,c; float rest,strength,neutral; };
    std::vector<Particle> particles;
    std::vector<Bone> bones;
    std::vector<Bend> bends;
    int add(V p,float inverseMass=1) {
        particles.push_back({p,p,inverseMass}); return int(particles.size())-1;
    }
    void bone(int a,int b) { bones.push_back({a,b,length(particles[a].p-particles[b].p)}); }
    static float angle(V u,V v) { return std::atan2(u.x*v.y-u.y*v.x,u.x*v.x+u.y*v.y); }
    void bend(int a,int b,int c,float strength) {
        float rest=angle(particles[a].p-particles[b].p,particles[c].p-particles[b].p);
        bends.push_back({a,b,c,rest,strength,rest});
    }
    void pin(int id,V p) { particles[id].p=p; particles[id].old=p; }
    void solveBone(const Bone& b) {
        auto& a=particles[b.a]; auto& c=particles[b.b]; V d=c.p-a.p;
        float distance=length(d),weight=a.mass+c.mass;
        if(distance<.0001f || weight==0) return;
        V delta=d*((distance-b.rest)/(distance*weight));
        a.p=a.p+delta*a.mass; c.p=c.p-delta*c.mass;
    }
    void solveBend(const Bend& b) {
        auto& a=particles[b.a]; auto& center=particles[b.b]; auto& c=particles[b.c];
        V u=a.p-center.p,v=c.p-center.p;
        float uu=u.x*u.x+u.y*u.y,vv=v.x*v.x+v.y*v.y;
        if(uu<.01f||vv<.01f) return;
        float error=std::remainder(angle(u,v)-b.rest,2*PI);
        V ga={u.y/uu,-u.x/uu},gc={-v.y/vv,v.x/vv},gb=(ga+gc)*-1;
        auto squared=[](V p){return p.x*p.x+p.y*p.y;};
        float denominator=a.mass*squared(ga)+center.mass*squared(gb)+c.mass*squared(gc);
        if(denominator<.000001f) return;
        float correction=-error*b.strength/denominator;
        a.p=a.p+ga*(a.mass*correction);
        center.p=center.p+gb*(center.mass*correction);
        c.p=c.p+gc*(c.mass*correction);
    }
    void step(float dt,V acceleration={}) {
        for(auto& p:particles) if(p.mass>0) {
            V current=p.p;
            p.p=p.p+(p.p-p.old)*std::exp(-dt*9)+acceleration*(dt*dt);
            p.old=current;
        }
        for(int iteration=0;iteration<120;++iteration) {
            for(const auto& b:bends) solveBend(b);
            for(const auto& b:bones) solveBone(b);
        }
        // Finish with distance projections so short anatomical sections do not
        // stretch while the angular constraints distribute a turning load.
        for(int iteration=0;iteration<24;++iteration)
            for(const auto& b:bones) solveBone(b);
    }
};
