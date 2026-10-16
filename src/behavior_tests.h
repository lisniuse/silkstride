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
    // Restored routing must increase distance from threats on every side.
    float minimumRetreat=10000;
    for(V offset:{V{100,0},V{-100,0},V{0,100},V{0,-100}}) {
        s.living=true; s.memoryEnabled=false; s.follow=false; s.scale=1;
        s.rng.seed(42);
        s.pos={2000,1500}; s.heading={1,0}; s.work={0,0,4000,3000};
        s.mind=CreatureMind{}; s.mind.reseed(42); s.resetFeet();
        s.cursorPrimed=true; s.sensedCursor=s.pos+offset; s.sensedCursorSpeed=600; s.hovered=0;
        s.sensedCursorVelocity=unit(offset)*-600;
        s.mind.init(s.senses()); s.mind.energy=.12f; s.mind.enter(State::Observe,2);
        s.advance(1.f/120);
        check(s.mind.state==State::Flee,"Nearby cursor triggers escape despite foot hover and low energy");
        check(s.escapeTurning || length(s.velocity)>1,"Startle immediately starts stepping to turn or accelerates when already facing away");
        float minBackwardAlignment=1,peakRetreat=0,maxHeadingChange=0;
        bool movedBackward=false,anyPivot=false;
        V reactionStart=s.pos,reactionHeading=unit(offset)*-1;
        for(int n=0;n<959;++n) {
            if(n==24) {s.sensedCursorSpeed=0;s.sensedCursorVelocity={};}
            s.advance(1.f/120);
            if(s.mind.state==State::Flee) {
                V retreat=s.pos-reactionStart;
                peakRetreat=std::max(peakRetreat,retreat.x*reactionHeading.x+retreat.y*reactionHeading.y);
                maxHeadingChange=std::max(maxHeadingChange,std::abs(CrawlPhysics::angle(V{1,0},s.heading)));
                anyPivot|=s.escapeTurning;
            }
            if(s.mind.state==State::Flee && length(s.velocity)>15) {
                movedBackward=true;
                V direction=unit(s.velocity);
                minBackwardAlignment=std::min(minBackwardAlignment,direction.x*s.heading.x+direction.y*s.heading.y);
            }
        }
        check(offset.x<-50 || anyPivot,"Escape uses stepping turns when the threat requires a different heading");
        check(movedBackward && minBackwardAlignment>.999f,"Escape translation follows the head without sideways sliding");
        minimumRetreat=std::min(minimumRetreat,peakRetreat);
        out<<"Away-from-threat displacement: "<<peakRetreat<<" px; heading change: "<<maxHeadingChange<<" radians\n";
        s.beginGrab(1,s.legs[1].joints[FOOT]); s.advance(1.f/120);
        check(s.grabbed==1 && s.mind.state==State::Held && length(s.legs[1].joints[FOOT]-s.dragTarget)<.01f,
              "Grabbing a foot still takes priority over escape");
        s.endGrab(true);
    }
    check(minimumRetreat>50,"Threats from all four sides produce escape away after stepping into alignment");
    mind=CreatureMind{}; mind.reseed(42); senses.body={900,700}; senses.cursor={1000,700}; senses.cursorSpeed=600;
    senses.cursorVelocity={-600,0};
    mind.init(senses); mind.update(.02f,senses);
    V committedGoal=mind.goal;
    senses.cursor={800,700}; senses.cursorVelocity={600,0};mind.update(.2f,senses);
    check(mind.state==State::Flee && length(mind.goal-committedGoal)>100,"Renewed approach from the other side replans escape away from the cursor");
    mind.update(1.9f,senses);
    check(mind.state==State::Flee && mind.goal.x>senses.body.x,"Escape goal remains on the far side of the approaching threat");
    committedGoal=mind.goal;senses.cursor={1000,600};senses.cursorVelocity={0,0};senses.cursorSpeed=0;
    mind.update(1.f,senses);
    check(length(mind.goal-committedGoal)<.01f,"Moving the cursor without a renewed approach does not steer escape");
    mind=CreatureMind{};mind.reseed(42);senses.cursor={1000,700};senses.cursorVelocity={};senses.cursorSpeed=0;
    mind.init(senses);senses.cursor={800,700};mind.update(.1f,senses);
    check(length(mind.attention-V{800,700})<.01f,"Attention uses the current cursor observation without an artificial delay");

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

    // Verify the visible torso actually pauses between exploration bouts,
    // rather than merely changing an internal desired-speed value.
    s.living=true;s.memoryEnabled=false;s.follow=false;s.hovered=-1;s.scale=1;
    s.pos={2000,1500};s.heading=s.aimHeading={1,0};s.work={0,0,10000,3000};
    s.velocity={};s.escapeTurning=false;s.angularSpeed=0;s.resetFeet();s.cursorPrimed=false;
    s.mind=CreatureMind{};s.mind.reseed(42);s.mind.init(s.senses());
    s.mind.goal={8000,1500};s.mind.enter(State::Explore,40);
    int pausedFrames=0,quickFrames=0;float peakRoamSpeed=0,roamDistance=0,peakQuickSpeed=0,peakSlowSpeed=0;
    unsigned tierMask=0;float tierPeaks[3]{};
    for(int frame=0;frame<2880;++frame) {
        V before=s.pos;s.advance(1.f/120);
        float actualSpeed=length(s.pos-before)*120;
        peakRoamSpeed=std::max(peakRoamSpeed,actualSpeed);roamDistance+=length(s.pos-before);
        if(!s.mind.inspecting) {
            tierMask|=1u<<s.mind.boutTier;
            tierPeaks[s.mind.boutTier]=std::max(tierPeaks[s.mind.boutTier],actualSpeed);
            if(s.mind.quickBout) {++quickFrames;peakQuickSpeed=std::max(peakQuickSpeed,actualSpeed);}
            else peakSlowSpeed=std::max(peakSlowSpeed,actualSpeed);
        }
        if(frame>120 && s.mind.inspecting && actualSpeed<1) ++pausedFrames;
    }
    check(pausedFrames>10 && peakRoamSpeed>35 && roamDistance>100,
          "Free exploration alternates visible walking bouts and stationary inspection pauses");
    out<<"Exploration peak speed: "<<peakRoamSpeed<<" px/s; stationary pause frames: "<<pausedFrames<<"\n";
    check(quickFrames>0 && peakQuickSpeed>peakSlowSpeed*1.3f,
          "Free exploration occasionally accelerates into a visibly faster voluntary dash");
    out<<"Ordinary bout peak: "<<peakSlowSpeed<<" px/s; quick bout peak: "<<peakQuickSpeed<<" px/s\n";
    check(tierMask==7 && tierPeaks[0]<85 && tierPeaks[1]>150 && tierPeaks[1]<205 && tierPeaks[2]>400,
          "Exploration randomly uses three distinct speed tiers including the 450 px/s preview pace");
    out<<"Tier peak speeds: "<<tierPeaks[0]<<", "<<tierPeaks[1]<<", "<<tierPeaks[2]<<" px/s\n";

    s.pos={2000,1500};s.heading=s.aimHeading={1,0};s.velocity={};
    s.escapeTurning=false;s.resetFeet();s.cursorPrimed=false;
    s.mind=CreatureMind{};s.mind.reseed(42);s.mind.init(s.senses());s.mind.enter(State::Observe,90);
    unsigned idleMask=0,idleStyles=0;int repeatedIdleLegs=0,previousIdleLeg=-1,idleCrossings=0;
    float idleHeadGap=10000,idleSideGap=10000,idleBodyDrift=0;V idleOrigin=s.pos;
    for(int frame=0;frame<7200;++frame) {
        int before=s.mind.idleLeg;s.advance(1.f/120);
        if(before<0 && s.mind.idleLeg>=0) {
            int chosen=s.mind.idleLeg;
            if(chosen==previousIdleLeg) ++repeatedIdleLegs;
            previousIdleLeg=chosen;idleMask|=1u<<chosen;idleStyles|=1u<<s.mind.idleStyle;
        }
        auto clearance=s.legClearance();
        idleHeadGap=std::min(idleHeadGap,clearance[1]);idleSideGap=std::min(idleSideGap,clearance[0]);
        idleCrossings=std::max(idleCrossings,s.exposedLegCrossings());
        idleBodyDrift=std::max(idleBodyDrift,length(s.pos-idleOrigin));
    }
    check(idleMask==255 && idleStyles==7 && repeatedIdleLegs==0,
          "Idle gestures reach all eight legs with varied styles and no immediate leg repeats");
    check(idleCrossings==0 && idleHeadGap>8.15f && idleSideGap>=0 && idleBodyDrift<.01f,
          "Random idle gestures keep the body still and legs clear of the head and each other");
    out<<"Idle leg mask: "<<idleMask<<"; style mask: "<<idleStyles<<"; crossings: "<<idleCrossings<<"\n";

    // Exercise the real controller and free-body interaction for two simulated minutes.
    s.living=true; s.memoryEnabled=false; s.follow=false; s.hovered=-1; s.scale=1;
    s.pos={900,700}; s.heading={1,0}; s.work={0,0,1800,1400};
    s.mind=CreatureMind{}; s.mind.reseed(42); s.resetFeet(); s.mind.init(s.senses());
    s.cursorPrimed=false;s.sensedCursorVelocity={};
    unsigned visited=0; float boneError=0; int nonfinite=0,outside=0; float travel=0;
    for(int frame=0;frame<14400;++frame) {
        if(frame==2400) {s.cursorPrimed=true;s.sensedCursor=s.pos+V{250,0};s.sensedCursorSpeed=0;}
        if(frame==2880) {s.sensedCursor=s.pos+V{100,0};s.sensedCursorSpeed=1400;s.sensedCursorVelocity={-1400,0};}
        if(frame==2900) {s.sensedCursorSpeed=0;s.sensedCursorVelocity={};s.cursorPrimed=false;}
        if(frame==3600) s.beginGrab(2,s.legs[2].joints[FOOT]);
        if(frame>3600 && frame<3720) s.dragTarget=s.dragTarget+V{.8f,-.2f};
        if(frame==3720) {s.lastDragTime=GetTickCount64();s.dragVelocity={300,0};s.endGrab();}
        V before=s.pos; s.advance(1.f/120); travel+=length(s.pos-before);
        visited|=1u<<int(s.mind.state);
        if(!std::isfinite(s.pos.x)||!std::isfinite(s.pos.y)) ++nonfinite;
        if(s.grabbed<0 && s.airborne<=0 && (s.pos.x<s.work.left || s.pos.x>s.work.right || s.pos.y<s.work.top || s.pos.y>s.work.bottom)) ++outside;
        for(const auto& b:s.physics.bones) {
            float distance=length(s.physics.particles[b.a].p-s.physics.particles[b.b].p);
            if(!std::isfinite(distance)) ++nonfinite;
            if(s.grabbed>=0 || s.airborne>0)
                boneError=std::max(boneError,std::abs(distance-b.rest));
        }
        if(frame==2300) s.render(L"behavior-explore.png");
        if(frame==2850) s.render(L"behavior-observe.png");
        if(frame==8000) s.render(L"behavior-rest.png");
    }
    check(nonfinite==0 && outside==0,"Two-minute integrated simulation remains finite and inside the desktop");
    check(boneError<5,"Free-body grab/throw poses preserve articulated bone lengths");
    check(travel>500 && s.mind.transitionCount>12,"Creature moves autonomously and changes behavior over time");
    unsigned required=(1u<<int(State::Explore))|(1u<<int(State::Rest))|(1u<<int(State::Groom))|(1u<<int(State::Recover))|(1u<<int(State::Flee));
    check((visited&required)==required,"Integrated lifecycle includes exploring, fleeing, recovery, rest and grooming");
    out<<"Integrated state mask: "<<visited<<"\nTransitions: "<<s.mind.transitionCount<<"\nTravel: "<<travel
       <<"\nMaximum bone error: "<<boneError<<"\nOutside frames: "<<outside<<"\nChecks: "<<checks<<" Failures: "<<failures
       <<"\n"<<(failures==0?"PASS":"FAIL")<<"\n";
    return failures==0?0:8;
}
