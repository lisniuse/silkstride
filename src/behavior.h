#pragma once
#include "i18n.h"

// Perception, needs and memory are independent of rendering and input hooks.
// Navigation proposes contacts; the Verlet body still moves only through its feet.
class CreatureMind {
public:
    enum class State { Wake, Prepare, Explore, Observe, Approach, Flee, Shelter, Rest, Groom, Recover, Held, SeekFood, Eat };
    struct Senses {
        V body,cursor;
        RECT bounds;
        float cursorSpeed=0,scale=1;
        bool cursorVisible=true,follow=false,hovering=false;
        bool foodVisible=false;
        V foodPoint{};
        RECT foodArea{};
        V cursorVelocity{};
    };
    State state=State::Wake,pending=State::Explore;
    std::mt19937 random;
    unsigned seed=0;
    float energy=.8f,fear=0,trust=.6f,aversion=0;
    float hunger=20;
    bool autoEat=true,mealWanted=false;
    float boldness=.5f,curiosity=.7f,activity=.6f;
    float age=0,time=0,deadline=1,attentionTime=0,calmTime=0;
    float noApproach=0,threatAge=1000,stance=1,speedFactor=0,look=0,startleCooldown=0,safeMouseTime=0;
    V goal{},attention{},threat{},favorite{};
    int favoriteCorner=0,visits[9]{},grabs=0,transitionCount=0;
    bool moving=false,initialized=false;

    CreatureMind() { reseed(std::random_device{}()); }
    bool readMemory(std::istream& file) {
        int version=0,count=0; unsigned savedSeed=0;
        float savedTrust=0,savedEnergy=0,savedFear=0,savedAversion=0;
        if(!(file>>version>>savedSeed>>savedTrust>>savedEnergy>>savedFear>>savedAversion>>count) || (version!=1 && version!=2)) return false;
        if(!std::isfinite(savedTrust)||!std::isfinite(savedEnergy)||!std::isfinite(savedFear)||!std::isfinite(savedAversion)) return false;
        float savedHunger=20;int enabled=1;
        if(version==2 && (!(file>>savedHunger>>enabled)||!std::isfinite(savedHunger))) return false;
        reseed(savedSeed); trust=std::clamp(savedTrust,.05f,.95f); energy=std::clamp(savedEnergy,.1f,1.f);
        fear=std::clamp(savedFear,0.f,1.f); aversion=std::clamp(savedAversion,0.f,1.f);
        grabs=std::clamp(count,0,100000);
        hunger=std::clamp(savedHunger,0.f,100.f);autoEat=enabled!=0;mealWanted=hunger>=80;
        if(fear>.3f) noApproach=12+aversion*25;
        for(int& visit:visits) { int value=0; if(file>>value) visit=std::clamp(value,0,10000); }
        return true;
    }
    void writeMemory(std::ostream& file) const {
        file<<"2 "<<seed<<" "<<trust<<" "<<energy<<" "<<fear<<" "<<aversion<<" "<<grabs<<" "<<hunger<<" "<<autoEat<<"\n";
        for(int count:visits) file<<count<<" ";
        file<<"\n";
    }
    float range(float a,float b) { return std::uniform_real_distribution<float>(a,b)(random); }
    void reseed(unsigned value) {
        seed=value; random.seed(seed);
        boldness=range(.3f,.8f); curiosity=range(.45f,.95f); activity=range(.4f,.85f);
        favoriteCorner=int(random()%4);
    }
    static const wchar_t* label(State s,Language language=Language::Chinese) {
        return Localizer::get(static_cast<Text>(static_cast<int>(Text::Wake)+static_cast<int>(s)),language);
    }
    V clampGoal(V point,const Senses& s) const {
        float mx=std::min(150*s.scale,(s.bounds.right-s.bounds.left)*.25f);
        float my=std::min(150*s.scale,(s.bounds.bottom-s.bounds.top)*.25f);
        return {std::clamp(point.x,s.bounds.left+mx,s.bounds.right-mx),
                std::clamp(point.y,s.bounds.top+my,s.bounds.bottom-my)};
    }
    V corner(int index,const Senses& s) const {
        return clampGoal({float(index%2?s.bounds.right:s.bounds.left),float(index/2?s.bounds.bottom:s.bounds.top)},s);
    }
    void enter(State next,float seconds) {
        if(state!=next) ++transitionCount;
        state=next; age=0; deadline=seconds; attentionTime=0;
    }
    void init(const Senses& s) {
        favorite=corner(favoriteCorner,s); goal=s.body; attention=s.body+V{80,0};
        initialized=true; enter(State::Wake,range(.7f,1.3f));
    }
    void prepare(State next,float seconds=.6f) { pending=next; enter(State::Prepare,seconds); }
    void explore(const Senses& s) {
        // Prefer little-visited cells, with a local step instead of screen-wide teleports.
        float best=-100000; V chosen=s.body;
        for(int i=0;i<12;++i) {
            float angle=range(-PI,PI),radius=range(170,460)*s.scale;
            V candidate=clampGoal(s.body+V{std::cos(angle),std::sin(angle)}*radius,s);
            int col=std::clamp(int((candidate.x-s.bounds.left)*3/(s.bounds.right-s.bounds.left)),0,2);
            int row=std::clamp(int((candidate.y-s.bounds.top)*3/(s.bounds.bottom-s.bounds.top)),0,2);
            float score=range(0,1)-float(visits[row*3+col])*.35f+length(candidate-s.body)*.001f;
            if(fear>.2f) score+=length(candidate-s.cursor)*.002f;
            if(score>best) { best=score; chosen=candidate; }
        }
        goal=chosen; prepare(State::Explore,range(.4f,.8f));
    }
    void seekShelter(const Senses& s) {
        favorite=corner(favoriteCorner,s);
        int safest=favoriteCorner;
        if(fear>.4f) {
            float best=-1;
            for(int i=0;i<4;++i) {
                V c=corner(i,s); float score=length(c-s.cursor)-length(c-s.body)*.25f;
                if(score>best) { best=score; safest=i; }
            }
        }
        goal=corner(safest,s); enter(State::Shelter,14);
    }
    void escapeGoal(const Senses& s) {
        threat=s.cursor; threatAge=0;
        V away=unit(s.body-threat);
        goal=clampGoal(s.body+away*(350+fear*280)*s.scale,s);
        if(length(goal-s.body)<100*s.scale) {
            float best=-1;
            for(int i=0;i<4;++i) {
                V c=corner(i,s); float score=length(c-threat);
                if(score>best) {best=score;goal=c;}
            }
        }
    }
    void flee(const Senses& s) {
        noApproach=std::max(noApproach,8.f+aversion*25);
        escapeGoal(s); enter(State::Flee,range(2.2f,4.2f));
        // Startle uses an immediate burst instead of the slow exploration ramp.
        speedFactor=std::max(speedFactor,1.4f);
        startleCooldown=2.f;
    }
    void grabbed(V cursor) {
        ++grabs; threat=cursor; threatAge=0;
        fear=std::min(1.f,fear+.65f); aversion=std::min(1.f,aversion+.16f);
        safeMouseTime=0;
        trust=std::max(.05f,trust-.1f); noApproach=18+aversion*35;
        enter(State::Held,100000); moving=false;
    }
    void released(float throwSpeed) {
        fear=std::min(1.f,fear+throwSpeed/6000);
        energy=std::max(.1f,energy-.04f); enter(State::Recover,1.2f);
    }
    void ateFile() {
        hunger=std::max(0.f,hunger-15);energy=std::min(1.f,energy+.04f);
        if(hunger<=5) {mealWanted=false;enter(State::Observe,1.5f);}
    }
    void update(float dt,const Senses& s) {
        if(!initialized) init(s);
        age+=dt; time+=dt; threatAge+=dt; noApproach=std::max(0.f,noApproach-dt);
        startleCooldown=std::max(0.f,startleCooldown-dt);
        float distance=length(s.cursor-s.body)/s.scale;
        bool visible=s.cursorVisible && s.cursor.x>=s.bounds.left && s.cursor.x<=s.bounds.right
                     && s.cursor.y>=s.bounds.top && s.cursor.y<=s.bounds.bottom;
        bool resting=state==State::Rest || state==State::Groom;
        energy=std::clamp(energy+dt*(resting?.025f:state==State::Held?-.009f:moving?-.0075f:-.001f),.05f,1.f);
        hunger=std::min(100.f,hunger+dt*(resting?.025f:.05f));
        if(hunger>=80) mealWanted=true;
        if(hunger<=5) mealWanted=false;
        V towardBody=unit(s.body-s.cursor);
        float closing=(s.cursorVelocity.x*towardBody.x+s.cursorVelocity.y*towardBody.y)/s.scale;
        float rawSpeed=length(s.cursorVelocity)/s.scale;
        bool safe=!visible || (closing<60 && (s.cursorSpeed<180 || distance>360));
        safeMouseTime=safe?safeMouseTime+dt:0;
        if(state!=State::Held) {
            float recovery=(resting?.022f:.012f)+(safeMouseTime>2?.012f:0.f);
            fear=std::max(0.f,fear-dt*recovery);
            if(safeMouseTime>3 && trust<.6f) trust=std::min(.6f,trust+dt*.0015f);
        }
        aversion=std::max(0.f,aversion-dt*.00065f);
        bool gentle=visible && distance>60 && distance<400 && s.cursorSpeed<160 && closing<80;
        calmTime=gentle?calmTime+dt:std::max(0.f,calmTime-dt*2);
        if(gentle && calmTime>2 && fear<.45f && state!=State::Held) trust=std::min(.95f,trust+dt*.005f);
        if(state==State::Held) { moving=false; return; }

        // Only the mouse's own motion counts: walking toward a still cursor,
        // a fast departure or a nearby stationary cursor cannot startle it.
        bool sudden=visible && distance<300 && rawSpeed>420 && closing>140;
        bool intrusion=visible && distance<90 && closing>200;
        if((sudden||intrusion) && (startleCooldown<=0 || (distance<60 && closing>350))
           && state!=State::Recover && state!=State::Flee) {
            fear=std::min(1.f,fear+.4f+(1-boldness)*.2f); flee(s);
        }
        bool foodReady=autoEat && mealWanted && s.foodVisible;
        bool feeding=state==State::SeekFood || state==State::Eat;
        if(foodReady && state!=State::Flee && state!=State::Recover) {
            if(!feeding) enter(State::SeekFood,30);
        } else if(feeding && !foodReady) {enter(State::Observe,1.2f);}
        if(!foodReady && state!=State::Flee && state!=State::Shelter && !resting && state!=State::Recover && energy<.26f) seekShelter(s);

        switch(state) {
            case State::Wake: if(age>deadline) explore(s); break;
            case State::Prepare:
                if(age>deadline) enter(pending,pending==State::Approach?8.f:range(7,13));
                break;
            case State::Explore:
                if(gentle && calmTime>1-curiosity*.5f && distance<240+curiosity*140 && age>1.4f) enter(State::Observe,range(1.1f,2));
                else if(length(goal-s.body)<40*s.scale || age>deadline) {
                    int c=std::clamp(int((s.body.x-s.bounds.left)*3/(s.bounds.right-s.bounds.left)),0,2);
                    int r=std::clamp(int((s.body.y-s.bounds.top)*3/(s.bounds.bottom-s.bounds.top)),0,2);
                    ++visits[r*3+c]; enter(State::Observe,range(.7f,1.7f));
                }
                break;
            case State::Observe:
                if(age>deadline) {
                    if(gentle && noApproach<=0 && fear<.15f+curiosity*.18f && trust>.3f && distance>160 && calmTime>1.4f-curiosity*.5f) {
                        goal=clampGoal(s.cursor,s); prepare(State::Approach,.7f);
                    } else if(energy<.4f || fear>.4f) seekShelter(s);
                    else explore(s);
                }
                break;
            case State::Approach: {
                float spacing=145+(1-boldness)*45+aversion*65;
                goal=clampGoal(s.cursor+unit(s.body-s.cursor)*(spacing*s.scale),s);
                if(!visible || age>deadline || distance<spacing+18) {
                    noApproach=std::max(noApproach,6.f); enter(State::Observe,range(1.5f,3));
                }
                break;
            }
            case State::Flee:
                if(visible && distance<420 && threatAge>.15f
                   && closing>80 && (length(s.cursor-threat)>35*s.scale || length(goal-s.body)<65*s.scale)) escapeGoal(s);
                if(age>deadline) {
                    if(visible && distance<300 && closing>90) {deadline=age+1.f;escapeGoal(s);}
                    else seekShelter(s);
                } else if(age>1.3f && length(goal-s.body)<55*s.scale && distance>340) seekShelter(s);
                break;
            case State::Shelter:
                if(length(goal-s.body)<55*s.scale || age>deadline) enter(State::Rest,range(12,23));
                break;
            case State::Rest:
                if(age>deadline && energy>.6f && fear<.4f) explore(s);
                else if(age>5 && energy>.4f) enter(State::Groom,range(2.5f,4));
                break;
            case State::Groom:
                if(age>deadline) {
                    if(energy>.72f && fear<.25f) explore(s);
                    else enter(State::Rest,range(7,12));
                }
                break;
            case State::Recover:
                if(age>deadline) { if(fear>.3f) flee(s); else enter(State::Observe,1.2f); }
                break;
            case State::Held: break;
            case State::SeekFood: {
                goal=s.foodPoint;
                bool inside=s.body.x>s.foodArea.left+15 && s.body.x<s.foodArea.right-15
                         && s.body.y>s.foodArea.top+15 && s.body.y<s.foodArea.bottom-15;
                if(inside && length(goal-s.body)<14*s.scale) enter(State::Eat,100000);
                break;
            }
            case State::Eat:
                goal=s.foodPoint;
                if(length(s.foodPoint-s.body)>18*s.scale || s.body.x<s.foodArea.left || s.body.x>s.foodArea.right || s.body.y<s.foodArea.top || s.body.y>s.foodArea.bottom)
                    enter(State::SeekFood,30);
                break;
        }
        if(s.follow && !foodReady && noApproach<=0 && fear<.3f && state!=State::Recover && !resting) {
            if(state!=State::Approach && state!=State::Prepare) { goal=clampGoal(s.cursor,s); prepare(State::Approach,.5f); }
        }
        if(state==State::SeekFood || state==State::Eat) {
            float inset=30*s.scale;
            goal={std::clamp(goal.x,s.bounds.left+inset,s.bounds.right-inset),std::clamp(goal.y,s.bounds.top+inset,s.bounds.bottom-inset)};
        } else goal=clampGoal(goal,s);
        attention=(visible && distance<500 && state!=State::Flee && state!=State::Shelter)?s.cursor:goal;
        if(state==State::Rest || state==State::Groom) attention=s.body+V{std::cos(time*.35f),std::sin(time*.35f)}*(100*s.scale);
        moving=state==State::Explore || state==State::Approach || state==State::Flee || state==State::Shelter || state==State::SeekFood;
        float desiredSpeed=state==State::Flee?2.6f:state==State::SeekFood?1.2f:state==State::Approach?.4f:state==State::Shelter?.85f:.65f+activity*.5f;
        if(!moving) desiredSpeed=0;
        else if(state!=State::Flee) desiredSpeed*=std::clamp(age/.7f,.2f,1.f)*(.65f+energy*.35f);
        if(state!=State::Flee && state!=State::SeekFood && hunger>65) desiredSpeed*=std::max(.3f,1-(hunger-65)*.02f);
        speedFactor+=(desiredSpeed-speedFactor)*(1-std::exp(-dt*5));
        float desiredStance=state==State::Rest?.84f:state==State::Groom?.9f:state==State::Flee?1.08f:state==State::Recover?.88f:1.f;
        stance+=(desiredStance-stance)*(1-std::exp(-dt*2));
        look=visible?std::atan2(attention.y-s.body.y,attention.x-s.body.x):0;
    }
    V toeMotion(int leg,V heading) const {
        float envelope=0,x=0,y=0;
        int side=leg<4?-1:1;
        if((state==State::Observe || state==State::Prepare || state==State::Wake) && leg==(int(time/3)%2?4:0)) {
            float phase=std::clamp(age/std::max(.1f,deadline),0.f,1.f);
            envelope=std::sin(phase*PI); x=12*envelope; y=-side*9*envelope;
        } else if(state==State::Groom && leg==(int(time/4)%2?4:0)) {
            float edge=std::min(1.f,std::min(age,deadline-age)*3);
            envelope=std::max(0.f,edge); x=std::sin(age*7)*7*envelope; y=-side*(15+9*std::sin(age*5))*envelope;
        } else if(state==State::Eat && (leg==0 || leg==4)) {
            envelope=std::min(1.f,age*2);x=(8+std::sin(age*12)*4)*envelope;y=-side*(12+std::sin(age*12)*5)*envelope;
        }
        return {heading.x*x-heading.y*y,heading.y*x+heading.x*y-envelope*5};
    }
};
