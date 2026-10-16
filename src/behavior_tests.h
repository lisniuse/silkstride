#pragma once

int behaviorTest(Spider& s) {
    using State=CreatureMind::State;
    std::ofstream out("behavior-test.txt"); int failures=0,checks=0;
    auto check=[&](bool result,const char* description) {
        ++checks; if(!result) ++failures;
        out<<(result?"PASS ":"FAIL ")<<description<<"\n";
    };
    CreatureMind mind; mind.reseed(42);
    CreatureMind::Senses senses{{900,700},{1150,700},{0,0,1800,1400},0,1,true,false,false};
    mind.init(senses); mind.enter(State::Observe,.3f);
    bool curiousApproach=false;
    for(int i=0;i<360;++i) {mind.update(1.f/60,senses);curiousApproach|=mind.state==State::Approach;}
    check(curiousApproach,"Gentle nearby cursor leads from observation to a cautious approach");
    senses.cursor={1000,700}; senses.cursorSpeed=1300;
    senses.cursorVelocity={-1300,0};
    mind.update(1.f/60,senses);
    check(mind.state==State::Flee && mind.fear>.3f,"Fast nearby cursor triggers fear and escape");

    mind=CreatureMind{}; mind.reseed(42); mind.init(senses);
    senses.cursorVisible=false; senses.cursorSpeed=0;senses.cursorVelocity={};
    mind.energy=.15f; mind.enter(State::Explore,10); mind.update(.02f,senses);
    check(mind.state==State::Shelter,"Low energy creates a shelter goal");
    senses.body=mind.goal; mind.update(.02f,senses);
    check(mind.state==State::Rest,"Arriving at shelter starts rest");
    float depleted=mind.energy; bool groomed=false;
    for(int i=0;i<1800;++i) {mind.update(1.f/60,senses);groomed|=mind.state==State::Groom;}
    check(mind.energy>depleted+.4f && groomed,"Rest restores energy and includes leg grooming");

    float formerTrust=mind.trust;
    mind.grabbed({1000,700});
    check(mind.state==State::Held && mind.fear>.6f && mind.trust<formerTrust && mind.aversion>.1f,"Being grabbed changes fear, trust and interaction memory");
    mind.released(800); mind.update(.02f,senses);
    check(mind.state==State::Recover,"Release starts a recovery phase before escape");
    for(int i=0;i<100;++i) mind.update(1.f/60,senses);
    check(mind.state==State::Flee,"After regaining footing the frightened creature escapes");
    senses.cursorVisible=true; senses.cursor=senses.body+V{250,0};
    bool approached=false;
    for(int i=0;i<900;++i) {mind.update(1.f/60,senses);approached|=mind.state==State::Approach;}
    check(!approached && mind.noApproach>0,"Recent grab prevents immediate renewed approach");

    std::stringstream stored; mind.writeMemory(stored);
    CreatureMind restored; bool loaded=restored.readMemory(stored);
    check(loaded && restored.seed==mind.seed && std::abs(restored.trust-mind.trust)<.001f
          && std::abs(restored.aversion-mind.aversion)<.001f && restored.grabs==mind.grabs
          && restored.boldness==mind.boldness && restored.favoriteCorner==mind.favoriteCorner,
          "Personality and interaction memory survive a serialization round trip");
    std::stringstream broken("invalid state");
    check(!restored.readMemory(broken),"Malformed saved memory is rejected");

    // Regression for escaping while hovering over a leg, including low energy
    // and a threat behind the head (no waiting for a full turn before retreating).
    float minimumEscape=10000;
    for(V offset:{V{100,0},V{-100,0},V{0,100},V{0,-100}}) {
        s.living=true; s.memoryEnabled=false; s.follow=false; s.scale=1;
        s.pos={2000,1500}; s.heading={1,0}; s.work={0,0,4000,3000};
        s.mind=CreatureMind{}; s.mind.reseed(42); s.resetFeet();
        s.cursorPrimed=true; s.sensedCursor=s.pos+offset; s.sensedCursorSpeed=600; s.hovered=0;
        s.sensedCursorVelocity=unit(offset)*-600;
        s.mind.init(s.senses()); s.mind.energy=.12f; s.mind.enter(State::Observe,2);
        float initial=length(s.pos-s.sensedCursor);
        s.advance(1.f/120);
        check(s.mind.state==State::Flee,"Nearby cursor triggers escape despite foot hover and low energy");
        for(int n=0;n<119;++n) {if(n==24) {s.sensedCursorSpeed=0;s.sensedCursorVelocity={};}s.advance(1.f/120);}
        float gain=length(s.pos-s.sensedCursor)-initial;
        minimumEscape=std::min(minimumEscape,gain);
        out<<"One-second escape separation gain: "<<gain<<" px\n";
        s.beginGrab(1,s.legs[1].joints[4]); s.advance(1.f/120);
        check(s.grabbed==1 && s.mind.state==State::Held && length(s.legs[1].joints[4]-s.dragTarget)<.01f,
              "Grabbing a foot still takes priority over escape");
        s.endGrab(true);
    }
    check(minimumEscape>50,"Threats in all four directions produce substantial separation within one second");
    mind=CreatureMind{}; mind.reseed(42); senses.body={900,700}; senses.cursor={1000,700}; senses.cursorSpeed=600;
    senses.cursorVelocity={-600,0};
    mind.init(senses); mind.update(.02f,senses);
    senses.cursor={800,700}; senses.cursorVelocity={600,0};mind.update(.2f,senses);
    check(mind.state==State::Flee && mind.goal.x>senses.body.x,"Escape replans away from a cursor that changes sides");

    // Calm presence must not trap it in an endless fear/escape feedback loop.
    mind=CreatureMind{};mind.reseed(42);senses.cursor={950,700};senses.cursorSpeed=0;senses.cursorVelocity={};
    mind.init(senses);mind.enter(State::Observe,2);bool falseAlarm=false;
    for(int i=0;i<1800;++i) {mind.update(1.f/60,senses);falseAlarm|=mind.state==State::Flee;}
    check(!falseAlarm,"A stationary cursor beside the body never repeatedly startles it");
    mind=CreatureMind{};mind.reseed(42);mind.init(senses);mind.enter(State::Observe,2);
    senses.cursor={1000,700};senses.cursorSpeed=800;senses.cursorVelocity={800,0};mind.update(.02f,senses);
    check(mind.state!=State::Flee,"A cursor rapidly moving away is not misread as an approaching threat");
    senses.cursorVelocity={0,800};mind.update(.02f,senses);
    check(mind.state!=State::Flee,"A tangential pass does not trigger the approach alarm");
    senses.cursorVelocity={-800,0};mind.update(.02f,senses);
    check(mind.state==State::Flee,"Genuine fast approach still triggers immediate escape");
    senses.cursorVelocity={};senses.cursorSpeed=0;
    for(int i=0;i<360;++i) mind.update(1.f/60,senses);
    check(mind.state!=State::Flee,"Stopping nearby does not extend an escape indefinitely");
    mind=CreatureMind{};mind.reseed(42);mind.init(senses);mind.fear=.5f;mind.trust=.1f;
    mind.aversion=.3f;mind.noApproach=24;mind.enter(State::Observe,2);senses.cursor={1150,700};
    bool forgave=false;
    for(int i=0;i<3600;++i) {mind.update(1.f/60,senses);forgave|=mind.state==State::Approach;}
    check(mind.fear<.1f && mind.trust>.3f && forgave,"Gentle interaction restores low trust and allows approach again within a minute");

    // Exercise the real controller and Verlet body for two simulated minutes.
    s.living=true; s.memoryEnabled=false; s.follow=false; s.hovered=-1; s.scale=1;
    s.pos={900,700}; s.heading={1,0}; s.work={0,0,1800,1400};
    s.mind=CreatureMind{}; s.mind.reseed(42); s.resetFeet(); s.mind.init(s.senses());
    s.cursorPrimed=false;s.sensedCursorVelocity={};
    unsigned visited=0; float boneError=0; int nonfinite=0,outside=0; float travel=0;
    for(int frame=0;frame<14400;++frame) {
        if(frame==2400) {s.cursorPrimed=true;s.sensedCursor=s.pos+V{250,0};s.sensedCursorSpeed=0;}
        if(frame==2880) {s.sensedCursor=s.pos+V{100,0};s.sensedCursorSpeed=1400;s.sensedCursorVelocity={-1400,0};}
        if(frame==2900) {s.sensedCursorSpeed=0;s.sensedCursorVelocity={};s.cursorPrimed=false;}
        if(frame==3600) s.beginGrab(2,s.legs[2].joints[4]);
        if(frame>3600 && frame<3720) s.dragTarget=s.dragTarget+V{.8f,-.2f};
        if(frame==3720) {s.lastDragTime=GetTickCount64();s.dragVelocity={300,0};s.endGrab();}
        V before=s.pos; s.advance(1.f/120); travel+=length(s.pos-before);
        visited|=1u<<int(s.mind.state);
        if(!std::isfinite(s.pos.x)||!std::isfinite(s.pos.y)) ++nonfinite;
        if(s.grabbed<0 && s.airborne<=0 && (s.pos.x<s.work.left || s.pos.x>s.work.right || s.pos.y<s.work.top || s.pos.y>s.work.bottom)) ++outside;
        for(const auto& b:s.physics.bones) {
            float distance=length(s.physics.particles[b.a].p-s.physics.particles[b.b].p);
            if(!std::isfinite(distance)) ++nonfinite;
            boneError=std::max(boneError,std::abs(distance-b.rest));
        }
        if(frame==2300) s.render(L"behavior-explore.png");
        if(frame==2850) s.render(L"behavior-observe.png");
        if(frame==8000) s.render(L"behavior-rest.png");
    }
    check(nonfinite==0 && outside==0,"Two-minute integrated simulation remains finite and inside the desktop");
    check(boneError<5,"Behavior poses preserve articulated bone lengths");
    check(travel>500 && s.mind.transitionCount>12,"Creature moves autonomously and changes behavior over time");
    unsigned required=(1u<<int(State::Explore))|(1u<<int(State::Rest))|(1u<<int(State::Groom))|(1u<<int(State::Recover))|(1u<<int(State::Flee));
    check((visited&required)==required,"Integrated lifecycle includes exploring, fleeing, recovery, rest and grooming");
    out<<"Integrated state mask: "<<visited<<"\nTransitions: "<<s.mind.transitionCount<<"\nTravel: "<<travel
       <<"\nMaximum bone error: "<<boneError<<"\nOutside frames: "<<outside<<"\nChecks: "<<checks<<" Failures: "<<failures
       <<"\n"<<(failures==0?"PASS":"FAIL")<<"\n";
    return failures==0?0:8;
}
