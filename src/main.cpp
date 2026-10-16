#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <string>
#include <chrono>
#include <vector>
#include <fstream>
#include <filesystem>
#include <sstream>

using namespace Gdiplus;
constexpr float PI = 3.14159265359f;
constexpr int CANVAS = 480;
constexpr int LEG_SEGMENTS = 6; // Coxa/trochanter share a root; five distal anatomical segments.
constexpr int LEG_JOINTS = LEG_SEGMENTS + 1;
constexpr int FOOT = LEG_SEGMENTS;
constexpr int LEG_BENDS = LEG_SEGMENTS + 1;
constexpr UINT TRAY = WM_APP + 1;
struct V {
    float x=0, y=0;
    V operator+(V b) const { return {x+b.x,y+b.y}; }
    V operator-(V b) const { return {x-b.x,y-b.y}; }
    V operator*(float n) const { return {x*n,y*n}; }
};
float length(V p) { return std::hypot(p.x,p.y); }
V unit(V p) { float l=length(p); return l>0.001f?p*(1/l):V{1,0}; }
V mix(V a,V b,float t) { return a*(1-t)+b*t; }
#include "physics.h"
#include "i18n.h"
#include "startup.h"
#include "runtime_log.h"
#include "process_launch.h"
#include "behavior.h"
#include "recycle_food.h"
#include "feeding.h"
struct Leg {
    V foot, from, goal;
    float phase=1, duration=.25f, lift=0;
    std::array<V,LEG_JOINTS> joints{}, previous{};
    std::array<float,LEG_SEGMENTS> bones{};
    std::array<int,LEG_JOINTS> ids{};
};
class Spider {
public:
    HWND hwnd=nullptr;
    RECT work{};
    V pos{}, target{}, heading{1,0}, velocity{};
    std::array<Leg,8> legs{};
    CrawlPhysics physics;
    CreatureMind mind;
    Localizer ui;
    StartupRegistration startup;
    RecycleFeeder feeder;
    RecycleFoodView food;
    FeedingCycle feeding;
    bool feedingLive=false;
    unsigned foodReceipt=0;
    std::wstring lastFood;
    float mealToast=0;
    std::array<V,8> toeOffsets{};
    bool living=true,memoryEnabled=false,cursorPrimed=false,memoryDirty=false;
    V sensedCursor{},previousCursor{},sensedCursorVelocity{};
    float sensedCursorSpeed=0,memoryTimer=0,trayTimer=0;
    std::wstring memoryPath;
    std::wstring uiSettingsPath;
    V aimHeading{1,0};
    int legCursor=0;
    int grabbed=-1,hovered=-1;
    bool escapeTurning=false;
    float airborne=0,grabTime=0;
    V dragTarget{},dragVelocity{},lastDragPoint{};
    ULONGLONG lastDragTime=0;
    HHOOK mouseHook=nullptr;
    bool swallowMouseUp=false;
    bool inputTest=false;
    int inputStage=0,inputFailures=0;
    int hookDowns=0,hookUps=0,hookGrabs=0;
    V lastDown{};
    POINT savedCursor{};
    HWND testBackdrop=nullptr;
    std::ofstream inputReport;
    std::mt19937 rng{std::random_device{}()};
    float scale=1, speed=75, rest=0, elapsed=0;
    float angularSpeed=0, stepCooldown=0, stride=0, bodyYaw=0;
    float accumulator=0;
    float cursorSampleAge=0;
    V bodyOffset{}, bodySpring{};
    bool paused=false, follow=false, rainbow=false, showHungerPanel=false;
    HDC dc=nullptr;
    HBITMAP dib=nullptr;
    HGDIOBJ old=nullptr;
    void* pixels=nullptr;
    NOTIFYICONDATAW tray{};
    HICON icon=nullptr;
    std::chrono::steady_clock::time_point last=std::chrono::steady_clock::now();

    float random(float a,float b) { return std::uniform_real_distribution<float>(a,b)(rng); }
    CreatureMind::Senses senses() const {
        CreatureMind::Senses result{pos,sensedCursor,work,sensedCursorSpeed,scale,cursorPrimed,follow,hovered>=0};
        result.foodVisible=mind.autoEat && food.visible && !food.token.empty()
            && food.point.x>=work.left && food.point.x<=work.right && food.point.y>=work.top && food.point.y<=work.bottom;
        result.foodPoint=food.point;result.foodArea=food.area;result.cursorVelocity=sensedCursorVelocity;result.facing=heading;result.turning=escapeTurning;return result;
    }
    void stopEating() {feeder.allow(false);feeding.cancel();}
    void updateFeeding(float dt) {
        if(!feedingLive) return;
        feeder.setBody(pos,scale);
        food=feeder.read();
        if(food.receipt!=foodReceipt) {
            foodReceipt=food.receipt;
            if(food.consumed) {
                mind.ateFile();memoryDirty=true;lastFood=food.receiptName;mealToast=2;
            }
            feeding.cancel();
        }
        mealToast=std::max(0.f,mealToast-dt);
        bool canEat=living && !paused && grabbed<0 && airborne<=0 && mind.autoEat
            && mind.state==CreatureMind::State::Eat && mind.mealWanted && mind.hunger>5 && food.visible
            && length(pos-food.point)<=18*scale
            && pos.x>food.area.left && pos.x<food.area.right && pos.y>food.area.top && pos.y<food.area.bottom;
        if(feeding.waiting && (feeding.token!=food.token || feeding.window!=food.window || length(food.point-feeding.point)>3)) {feeder.allow(false);feeding.cancel();}
        feeder.allow(canEat);
        if(feeding.advance(dt,canEat,food)) {
            if(!feeder.submit(feeding.token,feeding.window,feeding.name)) feeding.cancel();
        }
    }
    void loadMemory() {
        wchar_t folder[MAX_PATH]{};
        if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,folder))) return;
        std::wstring directory=std::wstring(folder)+L"\\NeonSpider";
        CreateDirectoryW(directory.c_str(),nullptr);
        memoryPath=directory+L"\\creature.state"; memoryEnabled=true;
        uiSettingsPath=directory+L"\\ui.settings";
        std::ifstream settings{std::filesystem::path(uiSettingsPath)};ui.read(settings);
        std::ifstream file{std::filesystem::path(memoryPath)};
        mind.readMemory(file);
    }
    void saveUiSettings() {
        if(uiSettingsPath.empty())return;
        std::wstring temporary=uiSettingsPath+L".tmp";
        {std::ofstream file{std::filesystem::path(temporary)};if(!file)return;ui.write(file);if(!file)return;}
        MoveFileExW(temporary.c_str(),uiSettingsPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
    void reportStartupError(LONG error) {
        RuntimeLog::write("event=startup_registration status=%ld",error);
        if(error==ERROR_SUCCESS)return;
        std::wstring message=std::wstring(ui(Text::StartupError))+std::to_wstring(error);
        MessageBoxW(hwnd,message.c_str(),ui(Text::Title),MB_OK|MB_ICONERROR);
    }
    void saveMemory() {
        if(!memoryEnabled) return;
        std::wstring temporary=memoryPath+L".tmp";
        {
            std::ofstream file{std::filesystem::path(temporary)};
            if(!file) return;
            mind.writeMemory(file);
        }
        MoveFileExW(temporary.c_str(),memoryPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }
    void injectMouse(DWORD flags) {
        INPUT event{}; event.type=INPUT_MOUSE; event.mi.dwFlags=flags;
        if(SendInput(1,&event,sizeof(event))!=1) { ++inputFailures; inputReport<<"SendInput failed\n"; }
    }
    void injectMove(V point,DWORD button=0) {
        int left=GetSystemMetrics(SM_XVIRTUALSCREEN),top=GetSystemMetrics(SM_YVIRTUALSCREEN);
        int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
        INPUT event{}; event.type=INPUT_MOUSE;
        event.mi.dx=LONG((point.x-left+.5f)*65536/width);
        event.mi.dy=LONG((point.y-top+.5f)*65536/height);
        event.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|button;
        if(SendInput(1,&event,sizeof(event))!=1) ++inputFailures;
    }
    void inputTestTick() {
        if(inputStage==0) {
            V foot=legs[0].joints[FOOT];
            inputReport<<"Toe: "<<foot.x<<","<<foot.y<<"\n";
            BOOL moved=SetCursorPos(int(std::round(foot.x)),int(std::round(foot.y)));
            inputReport<<"SetCursorPos: "<<moved<<"\n";
        } else if(inputStage==1) {
            POINT cursor; GetCursorPos(&cursor);
            inputReport<<"Cursor before down: "<<cursor.x<<","<<cursor.y<<" hit: "<<footAt({float(cursor.x),float(cursor.y)})<<"\n";
            injectMove(legs[0].joints[FOOT],MOUSEEVENTF_LEFTDOWN);
        }
        else if(inputStage==2 || inputStage==3 || inputStage==4) {
            bool held=grabbed>=0;
            inputReport<<"Held after "<<(inputStage-1)*150<<" ms: "<<held<<"\n";
            if(!held) ++inputFailures;
            if(held && inputStage>=3) {
                // Sample through the normal frame path before inspecting it. A
                // live cursor can move between timer callbacks; compare against
                // the position consumed by this frame rather than a newer event.
                tick();
                float error=length(legs[grabbed].joints[FOOT]-sensedCursor);
                inputReport<<"Toe-to-sampled-cursor distance: "<<error<<" px\n";
                if(error>2) ++inputFailures;
            }
            POINT p; GetCursorPos(&p); SetCursorPos(p.x+25,p.y-8);
        } else if(inputStage==5) injectMouse(MOUSEEVENTF_LEFTUP);
        else if(inputStage==6) {
            inputReport<<"Released: "<<(grabbed<0)<<"\n";
            if(grabbed>=0) ++inputFailures;
            inputReport<<"Hook downs/ups/grabs: "<<hookDowns<<"/"<<hookUps<<"/"<<hookGrabs
                       <<" down position: "<<lastDown.x<<","<<lastDown.y<<"\n";
            inputReport<<(inputFailures==0?"PASS":"FAIL")<<"\n"; inputReport.close();
            SetCursorPos(savedCursor.x,savedCursor.y);
            if(testBackdrop) DestroyWindow(testBackdrop);
            DestroyWindow(hwnd);
        }
        ++inputStage;
    }
    int footAt(V point) const {
        int nearest=-1; float distance=std::max(12.f,12*scale);
        for(int i=0;i<8;++i) {
            float d=length(point-legs[i].joints[FOOT]);
            if(d<distance) { nearest=i; distance=d; }
        }
        return nearest;
    }
    void beginGrab(int leg,V cursor) {
        if(leg<0||leg>=8||grabbed>=0) return;
        stopEating();
        grabbed=leg; hovered=leg; airborne=0; grabTime=0;
        if(living) { mind.grabbed(cursor); memoryDirty=true; }
        dragTarget=cursor; lastDragPoint=cursor; dragVelocity={}; lastDragTime=GetTickCount64();
        for(int i=0;i<8;++i) {
            legs[i].phase=1; legs[i].lift=0;
            physics.particles[legs[i].ids[FOOT]].mass=i==grabbed?0.f:1.f;
        }
        if(hwnd) RegisterHotKey(hwnd,3,MOD_NOREPEAT,VK_ESCAPE);
    }
    void dragTo(V point) {
        if(length(point-lastDragPoint)<.1f) { dragTarget=point; return; }
        ULONGLONG now=GetTickCount64();
        float dt=float(now-lastDragTime)*.001f;
        if(dt>.001f) {
            V measured=(point-lastDragPoint)*(1/dt);
            if(length(measured)>1800) measured=unit(measured)*1800;
            dragVelocity=mix(dragVelocity,measured,.4f);
        }
        lastDragTime=now; lastDragPoint=point; dragTarget=point;
    }
    void endGrab(bool cancel=false) {
        if(grabbed<0) return;
        // A stationary hold should not reuse an old fast mouse movement as a throw.
        if(GetTickCount64()-lastDragTime>100 || cancel) dragVelocity={};
        if(living) mind.released(length(dragVelocity));
        for(auto& p:physics.particles) {
            if(p.mass==0) p.mass=1;
            V residual=p.p-p.old;
            p.old=p.p-residual*.35f-dragVelocity*(.65f/120);
        }
        grabbed=-1; hovered=-1; airborne=cancel?.12f:.28f+std::min(.15f,length(dragVelocity)/10000);
        if(hwnd) UnregisterHotKey(hwnd,3);
    }
    void land() {
        POINT point{LONG(pos.x),LONG(pos.y)};
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&mi); work=mi.rcWork;
        float mx=std::min(140*scale,(work.right-work.left)*.25f);
        float my=std::min(140*scale,(work.bottom-work.top)*.25f);
        V center{std::clamp(pos.x,work.left+mx,work.right-mx),std::clamp(pos.y,work.top+my,work.bottom-my)};
        V shift=center-pos;
        for(auto& p:physics.particles) { p.p=p.p+shift; p.old=p.old+shift; }
        pos=center; aimHeading=heading;
        for(auto& l:legs) {
            l.foot=physics.particles[l.ids[FOOT]].p;
            l.from=l.goal=l.foot; l.phase=1; l.lift=0;
            physics.particles[l.ids[FOOT]].mass=0;
            for(int j=0;j<LEG_JOINTS;++j) l.joints[j]=physics.particles[l.ids[j]].p;
        }
        for(auto& b:physics.bends) b.rest=b.neutral;
        velocity={}; angularSpeed=0; stepCooldown=.12f; rest=.25f; chooseTarget();
        if(living) { mind.enter(CreatureMind::State::Recover,1.1f); memoryDirty=true; }
    }
    void interact(float dt) {
        bool held=grabbed>=0;
        grabTime+=dt;
        if(held) {
            // The held toe is the sole fixed particle; the other seven legs can flail.
            physics.pin(legs[grabbed].ids[FOOT],dragTarget);
            for(size_t n=0;n<physics.bends.size();++n) {
                auto& b=physics.bends[n]; int leg=int(n)/LEG_BENDS;
                float amplitude=leg==grabbed?.07f:(n%LEG_BENDS==0?.3f:.48f);
                // First recoil, then bursts of kicking with pauses during a long hold.
                float recoil=grabTime<.3f?.3f:1.f;
                float burst=recoil*(.2f+.8f*std::pow(.5f+.5f*std::sin(grabTime*4.1f),2.f));
                b.rest=b.neutral+amplitude*burst*std::sin(grabTime*(10.f+leg*.73f)+leg*2.7f+n%LEG_BENDS);
            }
            for(int i=0;i<8;++i) if(i!=grabbed) {
                auto& p=physics.particles[legs[i].ids[FOOT]];
                float burst=.25f+.75f*std::pow(.5f+.5f*std::sin(grabTime*4.1f),2.f);
                p.p=p.p+V{std::sin(grabTime*17+i*1.8f),std::cos(grabTime*13+i*2.2f)}*(1900*burst*scale*dt*dt);
            }
        } else {
            airborne-=dt;
            for(auto& b:physics.bends) b.rest+=(b.neutral-b.rest)*(1-std::exp(-dt*8));
        }
        physics.step(dt,held?V{0,300*scale}:V{});
        constrainAnatomy(false);
        pos=physics.particles[0].p; heading=unit(physics.particles[1].p-pos);
        for(auto& l:legs) for(int j=0;j<LEG_JOINTS;++j) l.joints[j]=physics.particles[l.ids[j]].p;
        if(!held && airborne<=0) { airborne=0; land(); }
    }
    V local(float x,float y) const { return {heading.x*x-heading.y*y,heading.y*x+heading.x*y}; }
    V bodyLocal(float x,float y) const {
        return local(x*std::cos(bodyYaw)-y*std::sin(bodyYaw),x*std::sin(bodyYaw)+y*std::cos(bodyYaw));
    }
    std::array<V,LEG_JOINTS> neutralChain(int i) const {
        int row=i%4,side=i<4?-1:1;
        float forward=row<2?1.f:-1.f;
        // Robinson & Robinson (1973), Table 1: adult female N. pilipes
        // (then N. maculata), bilateral means normalized to leg I.
        static constexpr float ratios[]={1.f,.8254f,.4796f,.832f};
        // Harvey et al. (2007): female leg I, femur through tarsus (mm).
        // The combined root and projected resting angles are artistic choices;
        // posture never changes these physical lengths.
        static constexpr float segments[]={4.f,21.10f,3.98f,18.03f,16.38f,4.65f};
        static constexpr float directions[]={25.f,60.f,115.f,165.f};
        static constexpr float roots[]={70.f,85.f,105.f,120.f};
        // Bent main sections provide useful contraction/extension reserve;
        // distal sections continue smoothly rather than folding into hooks.
        static constexpr float bends[]={0.f,.55f,.20f,-.65f,-.55f,-.50f};
        std::array<V,LEG_JOINTS> chain{};
        for(int j=0;j<LEG_SEGMENTS;++j) {
            float angle=side*(directions[row]*PI/180+forward*bends[j]);
            if(j==0) angle=side*roots[row]*PI/180;
            float extent=segments[j]*(160.f/68.14f)*ratios[row]*scale;
            chain[j+1]=chain[j]+V{std::cos(angle),std::sin(angle)}*extent;
        }
        return chain;
    }
    std::array<V,LEG_JOINTS> restingChain(int i) const {
        auto chain=neutralChain(i);
        for(auto& p:chain) p=pos+local(p.x,p.y);
        return chain;
    }
    V home(int i) const {
        V neutral=neutralChain(i)[FOOT];
        float stance=living?mind.stance:1.f;
        float x=neutral.x,y=neutral.y*stance;
        return pos+V{aimHeading.x*x-aimHeading.y*y,aimHeading.y*x+aimHeading.x*y};
    }
    void resetFeet() {
        grabbed=-1; airborne=0; hovered=-1;
        escapeTurning=false;
        velocity={}; angularSpeed=0;
        toeOffsets={};
        if(hwnd) UnregisterHotKey(hwnd,3);
        aimHeading=heading;
        for(int i=0;i<8;++i) legs[i]={home(i),home(i),home(i),1};
        physics=CrawlPhysics{};
        physics.add(pos,.3f);
        physics.add(pos+local(12*scale,0),.65f);
        physics.add(pos+local(-19*scale,0),.35f);
        physics.bone(0,1); physics.bone(0,2); physics.bone(1,2);
        for(int i=0;i<8;++i) {
            auto& l=legs[i]; l.joints=restingChain(i); l.previous=l.joints;
            l.foot=l.from=l.goal=l.joints[FOOT];
            l.joints[0]=pos; l.ids[0]=0;
            for(int j=1;j<LEG_JOINTS;++j) l.ids[j]=physics.add(l.joints[j],j==FOOT?0.f:1.f);
            for(int j=0;j<LEG_SEGMENTS;++j) {
                physics.bone(l.ids[j],l.ids[j+1]);
                l.bones[j]=length(l.joints[j+1]-l.joints[j]);
            }
            // Each anatomical section transmits contact forces to the torso.
            physics.bend(1,0,l.ids[1],.075f);
            physics.bend(l.ids[0],l.ids[1],l.ids[2],.06f);
            physics.bend(l.ids[1],l.ids[2],l.ids[3],.10f);
            physics.bend(l.ids[2],l.ids[3],l.ids[4],.10f);
            physics.bend(l.ids[3],l.ids[4],l.ids[5],.08f);
            physics.bend(l.ids[4],l.ids[5],l.ids[6],.08f);
            // A very short patella must not become a hinge that allows the
            // femur and tibia to buckle into a hairpin under planted contacts.
            physics.bend(l.ids[1],l.ids[2],l.ids[4],.25f);
        }
        stepCooldown=0; legCursor=0;
    }
    void monitorAt(POINT pt) {
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint(pt,MONITOR_DEFAULTTONEAREST),&mi);
        work=mi.rcWork;
        pos={(work.left+work.right)*.5f,(work.top+work.bottom)*.5f};
        chooseTarget(); resetFeet();
        if(living) mind.init(senses());
    }
    void chooseTarget() {
        float margin=150*scale;
        float mx=std::min(margin,(work.right-work.left)*.25f);
        float my=std::min(margin,(work.bottom-work.top)*.25f);
        target={random(work.left+mx,work.right-mx),random(work.top+my,work.bottom-my)};
    }
    void initSurface() {
        dc=CreateCompatibleDC(nullptr);
        BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth=CANVAS; bi.bmiHeader.biHeight=-CANVAS;
        bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
        dib=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&pixels,nullptr,0);
        old=SelectObject(dc,dib);
    }
    void tick() {
        auto now=std::chrono::steady_clock::now();
        float dt=std::min(.05f,std::chrono::duration<float>(now-last).count()); last=now;
        POINT cursor; GetCursorPos(&cursor);
        V currentCursor{float(cursor.x),float(cursor.y)};
        cursorSampleAge+=dt;
        if(!cursorPrimed) {
            previousCursor=currentCursor;cursorPrimed=true;cursorSampleAge=0;
        } else if(cursorSampleAge>=1.f/120) {
            sensedCursorVelocity=(currentCursor-previousCursor)*(1/cursorSampleAge);
            float measured=length(sensedCursorVelocity);
            sensedCursorSpeed+=(measured-sensedCursorSpeed)*(1-std::exp(-cursorSampleAge*12));
            previousCursor=currentCursor;cursorSampleAge=0;
        }
        sensedCursor=currentCursor;
        if(feedingLive) food=feeder.read();
        trayTimer+=dt;
        if(living && tray.hWnd && trayTimer>1) {
            trayTimer=0;
            std::wstring text=std::wstring(ui(Text::Title))+L" · "+CreatureMind::label(mind.state,ui.language)+L" · "+ui(Text::Hunger)+L" "+std::to_wstring(int(mind.hunger))+L"% · "+ui(Text::Energy)+L" "+std::to_wstring(int(mind.energy*100))+L"%";
            wcsncpy_s(tray.szTip,text.c_str(),_TRUNCATE);
            NOTIFYICONDATAW update=tray; update.uFlags=NIF_TIP; Shell_NotifyIconW(NIM_MODIFY,&update);
        }
        if(grabbed>=0) dragTo({float(cursor.x),float(cursor.y)});
        // The hook consumes the down event, so GetAsyncKeyState cannot be used
        // to infer release. Only the matching hook up event (or Esc) ends a grab.
        hovered=grabbed>=0?grabbed:footAt({float(cursor.x),float(cursor.y)});
        if(paused && grabbed<0 && airborne<=0) {updateFeeding(0);stopEating();render();return;}
        // Fixed simulation steps keep the Verlet inertia consistent across refresh rates.
        accumulator+=dt;
        while(accumulator>=1.f/120) { advance(1.f/120); accumulator-=1.f/120; }
        updateFeeding(dt);
        render();
    }
    void advance(float dt) {
        elapsed+=dt;
        if(living) {
            bool eatingBefore=mind.state==CreatureMind::State::Eat;
            // Turning does not consume the time reserved for running away.
            if(escapeTurning && mind.state==CreatureMind::State::Flee) mind.deadline+=dt;
            mind.update(dt,senses());
            if(!eatingBefore && mind.state==CreatureMind::State::Eat) {
                for(auto& l:legs) {l.foot=l.from=l.goal=l.joints[FOOT];l.phase=1;l.lift=0;}
                for(auto& p:physics.particles) p.old=p.p;
                velocity={};toeOffsets={};
            }
            memoryTimer+=dt;
            if(memoryTimer>20 || (memoryDirty && memoryTimer>2)) { memoryTimer=0; memoryDirty=false; saveMemory(); }
        }
        if(grabbed>=0 || airborne>0) { interact(dt); return; }
        bool walking=hovered<0; // Let a nearby foot stay available for grabbing.
        float effectiveSpeed=speed;
        if(living) {
            target=mind.goal;
            walking=mind.moving && (hovered<0 || mind.state==CreatureMind::State::Flee);
            effectiveSpeed=speed*mind.speedFactor;
        } else if(follow) {
            POINT p; GetCursorPos(&p);
            target={std::clamp(float(p.x),work.left+140*scale,work.right-140*scale),
                    std::clamp(float(p.y),work.top+140*scale,work.bottom-140*scale)};
            if(length(target-pos)<90*scale) walking=false;
        } else if(rest>0) { rest-=dt; walking=false; if(rest<=0) chooseTarget(); }
        else if(length(target-pos)<18) { rest=random(.6f,2.4f); walking=false; }
        float angle=std::atan2(aimHeading.y,aimHeading.x);
        float wanted=std::atan2(target.y-pos.y,target.x-pos.x);
        // Turn in place before translating along the body axis.
        float diff=std::remainder(wanted-angle,2*PI);
        bool fleeing=living && mind.state==CreatureMind::State::Flee;
        bool seekingFood=living && mind.state==CreatureMind::State::SeekFood;
        float escapeError=std::remainder(wanted-std::atan2(heading.y,heading.x),2*PI);
        float turnThreshold=seekingFood?(escapeTurning?.06f:.12f):(escapeTurning?.12f:.35f);
        bool pivoting=walking && std::abs(escapeError)>turnThreshold;
        if(pivoting && fleeing) mind.deadline=std::max(mind.deadline,mind.age+.8f);
        if(pivoting && !escapeTurning) {
            // Keep existing contacts and finish any stride already in flight.
            velocity={};stepCooldown=0;angularSpeed=0;
        }
        escapeTurning=pivoting;
        if(pivoting) {
            // Rotate only while feet are actively repositioning. Grounded
            // contacts remain in world space; never rotate the entire pose.
            float support=0;
            for(const auto& l:legs) if(l.phase<1) support+=std::sin(PI*l.phase);
            float limit=fleeing?7.6f:seekingFood?2.6f:1.3f;
            float desiredTurn=std::clamp(escapeError*(fleeing?10.f:seekingFood?5.f:3.f),-limit,limit)*std::min(1.f,support);
            angularSpeed+=(desiredTurn-angularSpeed)*(1-std::exp(-dt*(fleeing?24.f:12.f)));
            float rotation=angularSpeed*dt;
            // A planted foot near the head's centerline needs a new contact
            // before further yaw can safely carry its root around the torso.
            for(int i=0;i<8;++i) {
                const auto& l=legs[i];
                V arm=l.foot-pos;
                float sideAngle=std::atan2(heading.x*arm.y-heading.y*arm.x,heading.x*arm.x+heading.y*arm.y);
                float side=i<4?-1.f:1.f;
                float margin=std::asin(std::min(.95f,16*scale/std::max(1.f,length(arm))));
                float signedAngle=side*sideAngle;
                float lower=std::min(0.f,signedAngle-PI+margin),upper=std::max(0.f,signedAngle-margin);
                rotation=side*std::clamp(side*rotation,lower,upper);
            }
            float c=std::cos(rotation),sn=std::sin(rotation);
            heading={heading.x*c-heading.y*sn,heading.x*sn+heading.y*c};
            aimHeading=heading;if(pivoting) velocity={};
        }
        float turnLimit=fleeing?3.8f:1.65f;
        float turn=walking && !fleeing?std::clamp(diff*3.f,-turnLimit,turnLimit):0;
        if(!pivoting && !fleeing) {
            angularSpeed+=(turn-angularSpeed)*(1-std::exp(-dt*5));
            angle+=angularSpeed*dt;
            aimHeading={std::cos(angle),std::sin(angle)};
        }
        float arrival=std::clamp(length(target-pos)/100.f,.18f,1.f);
        float alignment=std::clamp(std::cos(diff),.12f,1.f);
        float pace=1;
        V travelDirection=heading;
        float travelFactor=fleeing || (living && mind.state==CreatureMind::State::Explore)?1.f:seekingFood?std::clamp(length(target-pos)/90.f,.04f,1.f):arrival*alignment;
        V desired=walking && !pivoting?travelDirection*(effectiveSpeed*travelFactor*pace):V{};
        bool inspecting=living && mind.state==CreatureMind::State::Explore && mind.inspecting;
        bool quickRoam=living && mind.state==CreatureMind::State::Explore && mind.quickBout && !inspecting;
        velocity=mix(velocity,desired,1-std::exp(-dt*(fleeing?14.f:inspecting||quickRoam?12.f:4.f)));
        if(inspecting && length(velocity)<.8f) velocity={};
        if(fleeing || seekingFood) velocity=pivoting?V{}:heading*length(velocity);
        // Locomotion owns a smooth torso trajectory. Feet articulate around
        // it; their constraint corrections cannot shake the body anymore.
        V next=pos+velocity*dt;
        float bodyInset=35*scale;
        next.x=std::clamp(next.x,work.left+bodyInset,work.right-bodyInset);
        next.y=std::clamp(next.y,work.top+bodyInset,work.bottom-bodyInset);
        pos=next;
        stepCooldown-=dt;
        int swinging=0;
        for(int i=0;i<8;++i) {
            auto& l=legs[i];
            if(l.phase<1) {
                l.phase=std::min(1.f,l.phase+dt/l.duration);
                float t=l.phase*l.phase*l.phase*(10+l.phase*(-15+6*l.phase));
                l.foot=mix(l.from,l.goal,t);
                float lift=std::max(0.f,std::sin(PI*l.phase));l.lift=lift*lift;
                if(l.phase<1) ++swinging;
                else l.lift=0;
            }
        }
        // Reference crawl order: (legIndex * 3) % 8, with its interleaved
        // left/right indices converted to this renderer's two groups of four.
        int candidate=-1;
        bool urgentStep=false;
        if(walking && swinging<4) {
            float urgency=1;
            V predicted=pos+velocity*.12f;
            for(int i=0;i<8;++i) if(legs[i].phase>=1) {
                float span=0;for(float b:legs[i].bones) span+=b;
                float distance=length(legs[i].foot-predicted);
                float score=std::max(distance/(span*.91f),span*.48f/std::max(1.f,distance));
                if(score>urgency) {urgency=score;candidate=i;urgentStep=true;}
            }
            // Translate supports in the same interleaved cycle; prioritizing
            // short legs repeatedly would leave the long ones behind.
            if(candidate<0 && stepCooldown<=0) {
                int interleaved=((legCursor++)*3)%8;
                candidate=interleaved/2+(interleaved%2)*4;
                if(legs[candidate].phase<1) candidate=-1;
            }
            if(pivoting && stepCooldown<=0) {
                float furthest=0;candidate=-1;
                for(int i=0;i<8;++i) if(legs[i].phase>=1) {
                    auto neutral=neutralChain(i)[FOOT];
                    float lead=fleeing?.7f:seekingFood?.38f:.28f;
                    float rotation=std::atan2(heading.y,heading.x)+std::clamp(escapeError,-lead,lead);
                    V turningContact=pos+V{neutral.x*std::cos(rotation)-neutral.y*std::sin(rotation),
                                    neutral.x*std::sin(rotation)+neutral.y*std::cos(rotation)};
                    V attainable=contactInSector(i,turningContact,true);
                    float distance=length(attainable-legs[i].foot);
                    float score=distance/std::max(1.f,length(neutral));
                    if(distance>2*scale && score>furthest) {furthest=score;candidate=i;}
                }
            }
        }
        // A newly resting or recently dropped creature places its feet one by one.
        // This changes actual contacts through the same solver as normal walking.
        if(living && !walking && hovered<0 && swinging<2 && stepCooldown<=0
           && ((mind.state==CreatureMind::State::Rest && mind.age<2.5f)
               || mind.state==CreatureMind::State::Recover)) {
            float stretch=19*scale;
            for(int i=0;i<8;++i) if(legs[i].phase>=1) {
                float distance=length(legs[i].foot-home(i));
                if(distance>stretch) {stretch=distance;candidate=i;}
            }
        }
        if(candidate>=0) {
            auto& l=legs[candidate];
            l.duration=std::clamp(.26f-effectiveSpeed*.00065f,.12f,.25f);
            if(candidate%4==2) l.duration*=.85f;
            if(fleeing || seekingFood) l.duration=.12f;
            if(urgentStep) l.duration=fleeing?.065f:.09f;
            if(pivoting) l.duration=fleeing?.055f:seekingFood?.13f:.19f;
            else if(quickRoam) l.duration=.065f;
            l.from=living?l.joints[FOOT]:l.foot;
            // Desktop contacts are continuous: rounding and fresh random
            // offsets on every step made contacts jump between lattice cells.
            bool precise=seekingFood && length(target-pos)<80*scale;
            // Rebuild supports relative to the torso each cycle; accumulating
            // translations would eventually bunch every foot ahead of it.
            V neutral=neutralChain(candidate)[FOOT];
            V support=pos+local(neutral.x,neutral.y);
            V desiredContact=walking?support+velocity*.34f:home(candidate);
            if(pivoting) {
                float lead=fleeing?.7f:seekingFood?.38f:.28f;
                float rotation=std::atan2(heading.y,heading.x)+std::clamp(escapeError,-lead,lead);
                desiredContact=pos+V{neutral.x*std::cos(rotation)-neutral.y*std::sin(rotation),
                                    neutral.x*std::sin(rotation)+neutral.y*std::cos(rotation)};
            }
            l.goal=desiredContact;
            l.goal=contactInSector(candidate,l.goal,true);
            if(living) {
                float inset=20*scale;
                l.goal.x=std::clamp(l.goal.x,work.left+inset,work.right-inset);
                l.goal.y=std::clamp(l.goal.y,work.top+inset,work.bottom-inset);
            }
            // A long, nearly extended leg must land relative to where the body
            // can be when the step finishes. Clamping against the current torso
            // would prevent its front feet from ever stepping ahead.
            V landingCenter=pos+(walking?velocity*(l.duration*.8f):V{});
            V reach=l.goal-landingCenter;
            float maxReach=0; for(float bone:l.bones) maxReach+=bone;
            if(length(reach)>maxReach*.98f) l.goal=landingCenter+unit(reach)*(maxReach*.98f);
            // Walking uses foreshortened leg projections, allowing a broad
            // contact workspace without bending the distal sections backward.
            else if(walking && length(reach)<maxReach*.45f)
                l.goal=landingCenter+unit(reach)*(maxReach*.45f);
            float minimumStep=(precise?.2f:pivoting?2.f:fleeing||seekingFood?4.f:8.f)*scale;
            if(length(l.goal-l.from)>=minimumStep) {
                l.phase=0;l.lift=0;
                stepCooldown=walking?std::clamp(.085f*75.f/std::max(15.f,effectiveSpeed),.045f,.18f):.16f;
                if(fleeing) stepCooldown=.045f;
                else if(seekingFood) stepCooldown=.055f;
                if(pivoting) stepCooldown=fleeing?.0175f:seekingFood?.045f:.065f;
            } else {l.goal=l.foot;stepCooldown=.025f;}
        }
        solveLegs(dt);
        if(!fleeing && !seekingFood) velocity=heading*length(velocity);
    }
    void rotatePose(float radians) {
        float c=std::cos(radians),s=std::sin(radians);
        auto vector=[&](V p){return V{c*p.x-s*p.y,s*p.x+c*p.y};};
        auto point=[&](V p){return pos+vector(p-pos);};
        for(auto& p:physics.particles) {p.p=point(p.p);p.old=point(p.old);}
        for(auto& leg:legs) {
            leg.foot=point(leg.foot);leg.from=point(leg.from);leg.goal=point(leg.goal);
            for(auto& p:leg.joints) p=point(p);
            for(auto& p:leg.previous) p=point(p);
        }
        for(auto& offset:toeOffsets) offset=vector(offset);
        heading=vector(heading);
    }
    V contactInSector(int leg,V point,bool contact=false) const {
        V center=physics.particles[0].p;
        V ray=physics.particles[legs[leg].ids[FOOT]].p-center;
        float lower=-PI,upper=PI;
        for(int i=0;i<8;++i) if(i!=leg) {
            V other=physics.particles[legs[i].ids[FOOT]].p-center;
            float gap=CrawlPhysics::angle(ray,other);
            if(gap>0) upper=std::min(upper,gap*.45f);
            else lower=std::max(lower,gap*.45f);
        }
        V offset=point-center;
        float angle=std::atan2(ray.y,ray.x)+std::clamp(CrawlPhysics::angle(ray,offset),lower,upper);
        float radius=length(offset);
        V result=center+V{std::cos(angle),std::sin(angle)}*radius;
        V axis=unit(physics.particles[1].p-center),side{-axis.y,axis.x};
        float sign=leg<4?-1.f:1.f;
        V offsetResult=result-center;
        float lateral=(offsetResult.x*side.x+offsetResult.y*side.y)*sign;
        if(contact && lateral<10*scale) result=result+side*(sign*(10*scale-lateral));
        return result;
    }
    void constrainAnatomy(bool lanes) {
        for(int pass=0;pass<80;++pass) {
            for(const auto& b:physics.bones) physics.solveBone(b);
            for(const auto& leg:legs) for(int j=4;j<FOOT;++j) {
                int a=leg.ids[j-1],b=leg.ids[j],c=leg.ids[j+1];
                V before=physics.particles[b].p-physics.particles[a].p;
                V after=physics.particles[c].p-physics.particles[b].p;
                float deflection=CrawlPhysics::angle(before,after);
                float limit=(j==FOOT-1?35.f:45.f)*PI/180;
                if(std::abs(deflection)>limit) {
                    float bendTarget=std::remainder(PI+std::clamp(deflection,-limit,limit),2*PI);
                    physics.solveBend({a,b,c,bendTarget,.8f,bendTarget});
                }
            }
            V center=physics.particles[0].p;
            V axis=unit(physics.particles[1].p-center),across{-axis.y,axis.x};
            for(int i=0;i<8;++i) {
                float sign=i<4?-1.f:1.f;
                V normal=across*sign;
                for(int j=1;j<=FOOT;++j) {
                    auto& p=physics.particles[legs[i].ids[j]];
                    auto& torso=physics.particles[0];
                    auto& head=physics.particles[1];
                    float torsoLength=std::max(scale,length(head.p-torso.p));
                    V forward=unit(head.p-torso.p),side{-forward.y,forward.x};
                    V offset=p.p-torso.p;
                    float raw=offset.x*side.x+offset.y*side.y;
                    float lateral=raw*sign;
                    static constexpr float rootMargin[]={8.f,5.f,2.f,3.f};
                    float minimum=(j==1?rootMargin[i%4]:8.5f)*scale;
                    V gp=side*sign;
                    V gh=(V{offset.y,-offset.x}-forward*raw)*(sign/torsoLength);
                    V gc=(gp+gh)*-1;
                    auto squared=[](V v){return v.x*v.x+v.y*v.y;};
                    float weight=p.mass*squared(gp)+torso.mass*squared(gc)+head.mass*squared(gh);
                    if(lateral<minimum && weight>0) {
                        float correction=(minimum-lateral)/weight;
                        p.p=p.p+gp*(correction*p.mass);
                        torso.p=torso.p+gc*(correction*torso.mass);
                        head.p=head.p+gh*(correction*head.mass);
                    }
                }
                if(lanes) for(int j=2;j<FOOT;++j) {
                    auto& p=physics.particles[legs[i].ids[j]];
                    V projected=contactInSector(i,p.p);
                    V shift=projected-p.p;p.p=projected;p.old=p.old+shift;
                }
                // Protect the entire segment, not just its endpoints: a long
                // femur could otherwise cut straight across the head circle.
                for(int j=0;j<FOOT;++j) {
                    auto& a=physics.particles[legs[i].ids[j]];
                    auto& b=physics.particles[legs[i].ids[j+1]];
                    auto& headParticle=physics.particles[1];
                    V head=headParticle.p,segment=b.p-a.p,from=head-a.p;
                    float squared=segment.x*segment.x+segment.y*segment.y;
                    float t=squared>.001f?std::clamp((from.x*segment.x+from.y*segment.y)/squared,0.f,1.f):0;
                    V offset=mix(a.p,b.p,t)-head;
                    float distance=length(offset),r=8.5f*scale;
                    float weight=a.mass*(1-t)*(1-t)+b.mass*t*t+headParticle.mass;
                    if(distance<r && weight>.001f) {
                        V outward=distance>.001f?offset*(1/distance):normal;
                        V push=outward*std::min((r-distance)/weight,2*scale);
                        a.p=a.p+push*(a.mass*(1-t));b.p=b.p+push*(b.mass*t);
                        headParticle.p=headParticle.p-push*headParticle.mass;
                    }
                }
            }
        }
    }
    int exposedLegCrossings() const {
        auto cross=[](V a,V b){return a.x*b.y-a.y*b.x;};
        int count=0;
        for(int a=0;a<8;++a) for(int b=a+1;b<8;++b)
            for(int j=1;j<LEG_SEGMENTS;++j) for(int k=1;k<LEG_SEGMENTS;++k) {
                V start=legs[a].joints[j],other=legs[b].joints[k];
                V u=legs[a].joints[j+1]-start,v=legs[b].joints[k+1]-other;
                float denominator=cross(u,v);
                if(std::abs(denominator)<.001f) continue;
                float t=cross(other-start,v)/denominator,s=cross(other-start,u)/denominator;
                if(t>.001f && t<.999f && s>.001f && s<.999f
                   && length(start+u*t-pos)>26*scale) ++count;
            }
        return count;
    }
    std::array<float,2> legClearance() const {
        std::array<float,2> result{10000,10000};
        V across{-heading.y,heading.x},head=physics.particles[1].p;
        for(int i=0;i<8;++i) for(int j=1;j<=FOOT;++j) {
            V offset=legs[i].joints[j]-pos;
            result[0]=std::min(result[0],(offset.x*across.x+offset.y*across.y)*(i<4?-1:1)/scale);
            V start=legs[i].joints[j-1],segment=legs[i].joints[j]-start,from=head-start;
            float square=segment.x*segment.x+segment.y*segment.y;
            float t=square>.001f?std::clamp((from.x*segment.x+from.y*segment.y)/square,0.f,1.f):0;
            result[1]=std::min(result[1],length(start+segment*t-head)/scale);
        }
        return result;
    }
    float distalFold() const {
        float result=0;
        for(const auto& leg:legs) for(int j=4;j<FOOT;++j)
            result=std::max(result,std::abs(CrawlPhysics::angle(leg.joints[j]-leg.joints[j-1],leg.joints[j+1]-leg.joints[j]))*180/PI);
        return result;
    }
    void solveLegs(float dt) {
        physics.pin(0,pos);
        physics.pin(1,pos+local(12*scale,0));
        physics.pin(2,pos+local(-19*scale,0));
        // Walking uses a variable-length planar projection of the raised legs.
        // Nominal bones remain unchanged for the free drag/throw simulation.
        for(int i=0;i<8;++i) {
            auto& l=legs[i];
            V toe=l.foot;
            if(living) {
                V desiredOffset=l.phase>=1 && hovered!=i?mind.toeMotion(i,heading)*scale:V{};
                toeOffsets[i]=mix(toeOffsets[i],desiredOffset,1-std::exp(-dt*14));
                if(l.phase>=1) toe=contactInSector(i,toe+toeOffsets[i],true);
            }
            auto neutral=neutralChain(i);
            V root=pos+local(neutral[1].x,neutral[1].y),reach=toe-root;
            V direction=unit(reach),across{-direction.y,direction.x};
            float sign=(i<4?-1.f:1.f)*(i%4<2?1.f:-1.f);
            float span=0;for(int j=1;j<LEG_SEGMENTS;++j) span+=l.bones[j];
            float along=0,bow=std::min(8*scale,length(reach)*.08f);
            l.joints[0]=pos;l.joints[1]=root;
            for(int j=2;j<=FOOT;++j) {
                along+=l.bones[j-1];float t=along/span;
                l.joints[j]=root+reach*t+across*(sign*bow*std::sin(PI*t));
            }
            l.joints[FOOT]=toe;
            for(int j=0;j<LEG_JOINTS;++j) physics.pin(l.ids[j],l.joints[j]);
        }
        for(int pass=0;pass<8;++pass) for(int i=0;i<8;++i) {
            V side=local(0,i<4?-1.f:1.f);
            for(int j=2;j<FOOT;++j) {
                auto& p=physics.particles[legs[i].ids[j]];
                V projected=contactInSector(i,p.p);
                float y=(projected.x-pos.x)*side.x+(projected.y-pos.y)*side.y;
                if(y<9*scale) projected=projected+side*(9*scale-y);
                physics.pin(legs[i].ids[j],projected);
            }
            for(int j=1;j<FOOT;++j) {
                auto& a=physics.particles[legs[i].ids[j]];
                auto& b=physics.particles[legs[i].ids[j+1]];
                V segment=b.p-a.p,from=physics.particles[1].p-a.p;
                float square=segment.x*segment.x+segment.y*segment.y;
                float t=square>.001f?std::clamp((from.x*segment.x+from.y*segment.y)/square,0.f,1.f):0;
                V offset=mix(a.p,b.p,t)-physics.particles[1].p;
                float distance=length(offset),wa=j==1?0.f:1.f,wb=j+1==FOOT?0.f:1.f;
                float weight=wa*(1-t)*(1-t)+wb*t*t;
                if(distance<8.5f*scale && weight>.001f) {
                    V correction=unit(offset)*((8.5f*scale-distance)/weight);
                    a.p=a.p+correction*(wa*(1-t));b.p=b.p+correction*(wb*t);
                }
            }
        }
        for(auto& l:legs) for(int j=0;j<LEG_JOINTS;++j) l.joints[j]=physics.particles[l.ids[j]].p;
    }
    Color pigment(int segment=0) const {
        if(!rainbow) {
            static const Color legColors[]={Color(255,149,112,66),Color(255,143,111,69),
                                             Color(255,125,94,59),Color(255,95,71,45)};
            return legColors[std::clamp(segment,0,3)];
        }
        float hue=std::fmod(elapsed*.055f,1.f);
        float h=hue*6, c=.78f, x=c*(1-std::abs(std::fmod(h,2.f)-1));
        float r=0,g=0,b=0;
        if(h<1) {r=c;g=x;} else if(h<2) {r=x;g=c;} else if(h<3) {g=c;b=x;}
        else if(h<4) {g=x;b=c;} else if(h<5) {r=x;b=c;} else {r=c;b=x;}
        float shade=1-segment*.13f;
        return Color(255,BYTE((r+.18f)*255*shade),BYTE((g+.18f)*255*shade),BYTE((b+.18f)*255*shade));
    }
    Color bodyPigment(int part) const {
        static const Color shellColors[]={Color(255,170,132,78),Color(255,110,78,46),Color(255,78,57,38)};
        Color base=rainbow?pigment(part==0?0:1):shellColors[std::clamp(part,0,2)];
        // Hunger changes the body, not the legs or the locomotion speed.
        float fullness=mind.fullness();
        static const Color fedColors[]={Color(255,36,29,24),Color(255,26,23,20),Color(255,18,17,16)};
        Color fed=fedColors[std::clamp(part,0,2)];
        auto darken=[&](BYTE value,BYTE dark) {return BYTE(value*(1-fullness)+dark*fullness);};
        base=Color(base.GetA(),darken(base.GetR(),fed.GetR()),darken(base.GetG(),fed.GetG()),darken(base.GetB(),fed.GetB()));
        float hunger=std::clamp((mind.hunger-80.f)/20.f,0.f,1.f);
        hunger=hunger*hunger*(3-2*hunger);
        BYTE gray=BYTE(base.GetR()*.30f+base.GetG()*.59f+base.GetB()*.11f);
        float fade=hunger*.8f;
        auto channel=[&](BYTE value) {return BYTE(value*(1-fade)+(gray*.75f+62)*fade);};
        return Color(base.GetA(),channel(base.GetR()),channel(base.GetG()),channel(base.GetB()));
    }
    void line(Graphics& g,V a,V b,Color c,float w) {
        Pen pen(c,w*scale); pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen,a.x,a.y,b.x,b.y);
    }
    void dot(Graphics& g,V p,float r,Color c) {
        SolidBrush b(c); r*=scale; g.FillEllipse(&b,p.x-r,p.y-r,r*2,r*2);
    }
    void draw(Graphics& g,V origin) {
        auto screen=[&](V p) { return p-pos+origin; };
        V center=origin+bodyOffset;
        for(int i=0;i<8;++i) {
            auto visualJoint=[&](int j) {
                V p=screen(legs[i].joints[j]);
                p.y-=3*scale*legs[i].lift*std::max(0,j-2)/float(FOOT-2);
                return p;
            };
            static const float widths[]={3.8f,3.f,2.8f,2.2f,1.5f,.95f};
            static const int shades[]={0,1,1,2,2,3};
            for(int j=0;j<LEG_SEGMENTS;++j) {
                V a=visualJoint(j),b=visualJoint(j+1);
                // A fine shadow keeps the colored silhouette visible on light windows.
                line(g,a+V{.7f,1.2f},b+V{.7f,1.2f},Color(90,8,13,19),widths[j]+1);
                line(g,a,b,pigment(shades[j]),widths[j]);
                if(!rainbow&&(j==1||j==3||j==4))for(float start:{.40f,.72f})
                    line(g,mix(a,b,start),mix(a,b,start+.09f),Color(245,64,49,34),widths[j]*1.04f);
                if(j>0) dot(g,a,widths[j]*.58f,pigment(shades[j]));
            }
            V toe=visualJoint(FOOT);
            dot(g,toe,1.35f,pigment(3));
            if(i==hovered || i==grabbed) {
                Pen ring(Color(i==grabbed?230:150,235,245,255),1.2f);
                float r=(i==grabbed?8.f:10.f)*scale;
                g.DrawEllipse(&ring,toe.x-r,toe.y-r,r*2,r*2);
            }
        }
        V abdomen=screen(physics.particles[2].p),head=screen(physics.particles[1].p);
        constexpr float bodySize=1.25f;
        float bodyScale=scale*bodySize;
        line(g,abdomen,head,bodyPigment(1),5*bodySize);
        float breath=living && !mind.moving?1+.025f*std::sin(elapsed*2.2f):1.f;
        V abdomenAxis=unit(head-abdomen),abdomenAcross{-abdomenAxis.y,abdomenAxis.x};
        for(bool shadow:{true,false}) {
            auto point=[&](float x,float y) {
                V p=abdomen+(abdomenAxis*x+abdomenAcross*y)*(bodyScale*breath);
                if(shadow) p=p+V{1,1};
                return PointF(p.x,p.y);
            };
            GraphicsPath shell;
            shell.AddBezier(point(10,0),point(11,5),point(8,8),point(3,9));
            shell.AddBezier(point(3,9),point(-6,11),point(-17,6),point(-22,0));
            shell.AddBezier(point(-22,0),point(-17,-6),point(-6,-11),point(3,-9));
            shell.AddBezier(point(3,-9),point(8,-8),point(11,-5),point(10,0));
            shell.CloseFigure();
            SolidBrush fill(shadow?Color(100,7,10,17):bodyPigment(0));
            g.FillPath(&fill,&shell);
        }
        if(!rainbow) {
            V axis=unit(head-abdomen),across{-axis.y,axis.x};
            float marking=1-mind.fullness()*.85f;
            line(g,abdomen-axis*(5*bodyScale),abdomen+axis*(5*bodyScale),Color(BYTE(230*marking),196,157,101),2.4f*bodySize);
            for(float x:{-3.f,0.f,3.f})for(int side:{-1,1})
                line(g,abdomen+axis*(x*bodyScale),abdomen+axis*((x-2)*bodyScale)+across*(side*4*bodyScale),Color(BYTE(225*marking),93,65,34),1.1f*bodySize);
        }
        dot(g,center,5.5f*bodySize,bodyPigment(1));dot(g,head,5*bodySize,bodyPigment(2));
        if(living) for(int side:{-1,1}) dot(g,head+local(3*bodyScale,side*2*bodyScale),.9f*bodySize,rainbow?Color(220,20,30,40):Color(245,18,14,10));
        float gloss=mind.fullness();
        V highlight=abdomen+bodyLocal(2*bodyScale,-3*bodyScale);
        GraphicsPath shinePath;
        shinePath.AddEllipse(highlight.x-3*bodyScale,highlight.y-2*bodyScale,6*bodyScale,4*bodyScale);
        PathGradientBrush shineBrush(&shinePath);
        shineBrush.SetCenterColor(Color(BYTE(45+100*gloss),245,248,255));
        Color shineEdge(0,245,248,255);int shineEdges=1;
        shineBrush.SetSurroundColors(&shineEdge,&shineEdges);
        g.FillPath(&shineBrush,&shinePath);
        if(gloss>0) {
            V shine=abdomen+bodyLocal(-4*bodyScale,-4*bodyScale);
            line(g,shine-bodyLocal(3*bodyScale,0),shine+bodyLocal(3*bodyScale,0),Color(BYTE(150*gloss),230,238,248),1.2f*bodySize);
            dot(g,head+bodyLocal(0,-2*bodyScale),1.3f*bodySize,Color(BYTE(100*gloss),235,242,255));
        }
        if(living && showHungerPanel) {
            RectF panel(origin.x-115,origin.y+38,230,48);
            SolidBrush background(Color(220,17,23,32)),text(Color(255,240,242,246));
            g.FillRectangle(&background,panel);
            Font font(ui.language==Language::Chinese?L"Microsoft YaHei UI":L"Segoe UI",10,FontStyleRegular,UnitPixel);
            std::wstring line=mealToast>0?std::wstring(ui(Text::Ate))+lastFood:mind.state==CreatureMind::State::Eat?std::wstring(ui(Text::Eating))+feeding.name:ui(mind.hunger>=65?Text::Hungry:Text::NotHungry);
            if(mealToast<=0 && food.filesSkipped) line=ui(Text::Skipped);
            StringFormat format;format.SetTrimming(StringTrimmingEllipsisCharacter);format.SetFormatFlags(StringFormatFlagsNoWrap);
            RectF label(panel.X+8,panel.Y+6,panel.Width-16,17);g.DrawString(line.c_str(),-1,&font,label,&format,&text);
            std::wstring hunger=std::wstring(ui(Text::Hunger))+L" "+std::to_wstring(int(mind.hunger))+L"%";
            g.DrawString(hunger.c_str(),-1,&font,PointF(panel.X+8,panel.Y+26),&text);
            SolidBrush track(Color(255,58,65,76)),fill(Color(255,249,168,66));
            g.FillRectangle(&track,panel.X+82,panel.Y+31,137.f,4.f);
            g.FillRectangle(&fill,panel.X+82,panel.Y+31,137.f*mind.hunger/100,4.f);
        }
        if(living && mind.state==CreatureMind::State::Eat) {
            float chew=std::sin(elapsed*14)*2;
            for(int side:{-1,1}) line(g,head+local(5*bodyScale,side*3*bodyScale),head+local((12+chew)*scale,side*6*scale),pigment(),1.5f);
            V morsel=head+local(16*scale,0);SolidBrush paper(Color(255,226,232,243));
            g.FillRectangle(&paper,morsel.x-3,morsel.y-4,6.f,8.f);
        }
    }
    void render(const wchar_t* file=nullptr) {
        Bitmap bitmap(CANVAS,CANVAS,CANVAS*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
        {
            Graphics g(&bitmap); g.Clear(Color(0,0,0,0));
            g.SetSmoothingMode(SmoothingModeAntiAlias); g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            // Cancel integer window positioning so planted feet don't jitter a pixel.
            draw(g,{CANVAS*.5f+pos.x-std::round(pos.x),CANVAS*.5f+pos.y-std::round(pos.y)});
        }
        if(file) {
            UINT count=0,bytes=0; GetImageEncodersSize(&count,&bytes);
            std::vector<BYTE> data(bytes); auto enc=reinterpret_cast<ImageCodecInfo*>(data.data());
            GetImageEncoders(count,bytes,enc);
            for(UINT i=0;i<count;++i) if(wcscmp(enc[i].MimeType,L"image/png")==0) bitmap.Save(file,&enc[i].Clsid,nullptr);
        } else {
            POINT dest{LONG(std::lround(pos.x-CANVAS*.5f)),LONG(std::lround(pos.y-CANVAS*.5f))},src{};
            ::SIZE size{CANVAS,CANVAS}; BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            UpdateLayeredWindow(hwnd,nullptr,&dest,&size,dc,&src,0,&blend,ULW_ALPHA);
        }
    }
    void addTray() {
        if(icon) { DestroyIcon(icon); icon=nullptr; }
        // Generate a recognizable cyan/pink tray icon without external assets.
        HDC idc=CreateCompatibleDC(nullptr);
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth=32; info.bmiHeader.biHeight=-32; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
        void* bits=nullptr; HBITMAP color=CreateDIBSection(idc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
        {
            Bitmap b(32,32,128,PixelFormat32bppPARGB,static_cast<BYTE*>(bits)); Graphics g(&b);
            g.Clear(Color(0,0,0,0)); g.SetSmoothingMode(SmoothingModeAntiAlias);
            Pen p(Color(255,100,235,255),1.6f); SolidBrush pink(Color(255,255,100,160));
            for(int i=0;i<4;++i) for(int s:{-1,1}) {
                float y=5.f+i*7; g.DrawLine(&p,16.f,16.f,16.f+s*8,y);
                g.DrawLine(&p,16.f+s*8,y,16.f+s*14,y+(i<2?-3.f:3.f));
            }
            g.FillEllipse(&pink,13,11,6,11);
        }
        HBITMAP mask=CreateBitmap(32,32,1,1,nullptr);
        ICONINFO ii{TRUE,0,0,mask,color}; icon=CreateIconIndirect(&ii);
        DeleteObject(mask); DeleteObject(color); DeleteDC(idc);
        tray.cbSize=sizeof(tray); tray.hWnd=hwnd; tray.uID=1;
        tray.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; tray.uCallbackMessage=TRAY; tray.hIcon=icon;
        wcsncpy_s(tray.szTip,ui(Text::InitialTip),_TRUNCATE);
        Shell_NotifyIconW(NIM_ADD,&tray);
    }
    void menu() {
        HMENU m=CreatePopupMenu();
        AppendMenuW(m,MF_STRING|MF_DISABLED,0,ui(Text::Title));
        if(living) {
            std::wstring status=std::wstring(CreatureMind::label(mind.state,ui.language))+L" · "+ui(Text::Energy)+L" "+std::to_wstring(int(mind.energy*100))+L"% · "+ui(Text::Hunger)+L" "+std::to_wstring(int(mind.hunger))+L"%";
            AppendMenuW(m,MF_STRING|MF_DISABLED,0,status.c_str());
            std::wstring personality=std::wstring(ui(Text::Personality))+ui(mind.boldness>.55f?Text::Bold:Text::Cautious)+L", "+ui(mind.activity>.6f?Text::Active:Text::Easygoing);
            AppendMenuW(m,MF_STRING|MF_DISABLED,0,personality.c_str());
        }
        AppendMenuW(m,MF_SEPARATOR,0,nullptr);
        AppendMenuW(m,MF_STRING|(paused?MF_CHECKED:0),1,ui(Text::Pause));
        AppendMenuW(m,MF_STRING|(follow?MF_CHECKED:0),2,ui(Text::Follow));
        AppendMenuW(m,MF_STRING|(rainbow?MF_CHECKED:0),9,ui(Text::Rainbow));
        AppendMenuW(m,MF_STRING|(mind.autoEat?MF_CHECKED:0),10,ui(Text::AutoEat));
        AppendMenuW(m,MF_STRING|(showHungerPanel?MF_CHECKED:0),11,ui(Text::HungerPanel));
        AppendMenuW(m,MF_STRING|(startup.enabled()?MF_CHECKED:0),14,ui(Text::Startup));
        HMENU languages=CreatePopupMenu();
        AppendMenuW(languages,MF_STRING|(ui.language==Language::Chinese?MF_CHECKED:0),12,L"简体中文");
        AppendMenuW(languages,MF_STRING|(ui.language==Language::English?MF_CHECKED:0),13,L"English");
        AppendMenuW(m,MF_POPUP,reinterpret_cast<UINT_PTR>(languages),L"Language / 语言");
        AppendMenuW(m,MF_STRING,3,ui(Text::Slow)); AppendMenuW(m,MF_STRING,4,ui(Text::Normal)); AppendMenuW(m,MF_STRING,5,ui(Text::Fast));
        AppendMenuW(m,MF_STRING,6,ui(Text::Size));
        AppendMenuW(m,MF_STRING,7,ui(Text::MoveMonitor));
        AppendMenuW(m,MF_SEPARATOR,0,nullptr); AppendMenuW(m,MF_STRING,8,ui(Text::Quit));
        POINT p; GetCursorPos(&p); SetForegroundWindow(hwnd);
        int cmd=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,nullptr);
        if(cmd)RuntimeLog::write("event=menu_command id=%d",cmd);
        DestroyMenu(m); PostMessageW(hwnd,WM_NULL,0,0);
        switch(cmd) {
            case 1: paused=!paused;if(paused)stopEating();break; case 2: follow=!follow; chooseTarget(); break;
            case 3: speed=38; break; case 4: speed=75; break; case 5: speed=135; break;
            case 6: scale=scale<1?1.f:(scale<1.2f?1.3f:.7f); resetFeet(); break;
            case 7: GetCursorPos(&p); monitorAt(p); break;
            case 8: RuntimeLog::write("event=exit_request reason=tray_menu");DestroyWindow(hwnd); break;
            case 9: rainbow=!rainbow; break;
            case 10: mind.autoEat=!mind.autoEat;feeder.setEnabled(mind.autoEat);stopEating();memoryDirty=true;break;
            case 11: showHungerPanel=!showHungerPanel;break;
            case 12: case 13: ui.language=cmd==12?Language::Chinese:Language::English;saveUiSettings();trayTimer=1;render();break;
            case 14: reportStartupError(startup.setEnabled(!startup.enabled()));break;
        }
    }
    ~Spider() {
        if(mouseHook) UnhookWindowsHookEx(mouseHook);
        feeder.stop();
        if(feedingLive) {
            auto finalReceipt=feeder.read();
            if(finalReceipt.receipt!=foodReceipt && finalReceipt.consumed) mind.ateFile();
        }
        saveMemory();
        if(tray.hWnd) Shell_NotifyIconW(NIM_DELETE,&tray);
        if(icon) DestroyIcon(icon);
        if(old) SelectObject(dc,old);
        if(dib) DeleteObject(dib); if(dc) DeleteDC(dc);
    }
};
Spider* app=nullptr;
LRESULT CALLBACK MouseHook(int code,WPARAM message,LPARAM data) {
    if(code==HC_ACTION && app) {
        const auto& event=*reinterpret_cast<MSLLHOOKSTRUCT*>(data);
        V point{float(event.pt.x),float(event.pt.y)};
        if(message==WM_LBUTTONDOWN) {
            ++app->hookDowns; app->lastDown=point;
            int leg=app->footAt(point);
            if(leg>=0) {
                app->beginGrab(leg,point); app->swallowMouseUp=true;
                ++app->hookGrabs;
                return 1;
            }
        } else if(message==WM_MOUSEMOVE && app->grabbed>=0) app->dragTo(point);
        else if(message==WM_LBUTTONUP && app->swallowMouseUp) {
            ++app->hookUps;
            if(app->grabbed>=0) { app->dragTo(point); app->endGrab(); }
            app->swallowMouseUp=false; return 1;
        }
    }
    return CallNextHookEx(nullptr,code,message,data);
}
int motionTest(Spider& s) {
    s.living=false;
    s.rng.seed(42); s.follow=false; s.pos={900,700}; s.heading={1,0};
    s.work={0,0,1800,1400}; s.target={1600,700}; s.resetFeet();
    // Check the intended pair hierarchy and fixed anatomy across size/stance
    // changes, rather than allowing a shorter foot radius to fold long bones.
    int anatomyFailures=0;
    for(float size:{.7f,1.f,1.3f}) for(float stance:{.7f,1.3f}) {
        s.scale=size;s.living=true;s.mind.stance=stance;s.resetFeet();
        float baseline=0;for(float b:s.legs[0].bones) baseline+=b;
        const float expected[]={1.f,.83f,.48f,.83f};
        for(int i=0;i<8;++i) {
            float total=0;for(float b:s.legs[i].bones) total+=b;
            if(std::abs(total/baseline-expected[i%4])>.01f) ++anatomyFailures;
        }
        if(std::abs(baseline-160*size)>.01f) ++anatomyFailures;
    }
    s.scale=1;s.living=false;s.resetFeet();
    // Locomotion keeps the torso stable even when a contact is adjusted.
    // Drag/throw retain the free-body solver in the interaction regression.
    for(int n=0;n<240;++n) s.solveLegs(1.f/120);
    float standingFold=0;
    for(const auto& leg:s.legs)
        standingFold=std::max(standingFold,std::abs(CrawlPhysics::angle(leg.joints[2]-leg.joints[1],leg.joints[4]-leg.joints[3]))*180/PI);
    V fixedStart=s.pos;
    for(int n=0;n<240;++n) s.solveLegs(1.f/120);
    float fixedBodyDrift=length(s.pos-fixedStart);
    V pullStart=s.pos;
    for(int n=0;n<120;++n) {
        if(n<60) s.legs[0].foot.x+=.5f;
        s.solveLegs(1.f/120);
    }
    float contactPull=length(s.pos-pullStart);
    s.pos={900,700}; s.heading={1,0}; s.resetFeet();
    int steps=0, planted=0, maxSwing=0,maxCrossings=0; float drift=0, maxReach=0,minExtension=100,maxExtension=0;
    float bodyTravel=0,maxGroundedFold=0,groundedFoldSum=0,maxTorsoError=0;int groundedFoldSamples=0;
    V previousTravel{};float steadyTime=0,accelerationSquared=0,stepDistanceSum=0;
    int accelerationSamples=0,microSteps=0;
    float minSide=10000,minHead=10000,maxDistalFold=0;
    for(int frame=0;frame<3600;++frame) {
        if(frame==900) s.target={200,250};
        if(frame==1800) { s.speed=135; s.target={1500,1100}; }
        if(frame==2700) { s.speed=38; s.target={300,1100}; }
        auto before=s.legs;
        V beforeBody=s.pos;
        s.advance(1.f/120);
        maxTorsoError=std::max(maxTorsoError,length(s.pos-beforeBody-s.velocity*(1.f/120)));
        maxCrossings=std::max(maxCrossings,s.exposedLegCrossings());
        maxDistalFold=std::max(maxDistalFold,s.distalFold());
        auto clearance=s.legClearance();minSide=std::min(minSide,clearance[0]);minHead=std::min(minHead,clearance[1]);
        bodyTravel+=length(s.pos-beforeBody);
        V travel=s.pos-beforeBody;
        steadyTime=!s.escapeTurning && length(s.velocity)>20?steadyTime+1.f/120:0;
        if(steadyTime>.4f) {
            float acceleration=length(travel-previousTravel)*120*120;
            accelerationSquared+=acceleration*acceleration;++accelerationSamples;
        }
        previousTravel=travel;
        int swinging=0,starts=0;
        for(int i=0;i<8;++i) {
            const auto& l=s.legs[i];
            if(l.phase<1) ++swinging;
            if(before[i].phase>=1 && l.phase<1) {
                ++steps;++starts;float distance=length(l.goal-l.from)/s.scale;
                stepDistanceSum+=distance;if(distance<8) ++microSteps;
            }
            if(before[i].phase>=1 && l.phase>=1) {
                drift=std::max(drift,length(l.foot-before[i].foot)); ++planted;
                float fold=std::abs(CrawlPhysics::angle(l.joints[2]-l.joints[1],l.joints[4]-l.joints[3]))*180/PI;
                if(fold>maxGroundedFold) {maxGroundedFold=fold;s.render(L"maximum-fold.png");}
                groundedFoldSum+=fold;++groundedFoldSamples;
            }
            maxReach=std::max(maxReach,length(l.foot-(s.pos+s.local((23-i%4*15)*s.scale,(i<4?-8.f:8.f)*s.scale))));
            if(!std::isfinite(l.foot.x)||!std::isfinite(l.foot.y)||starts>1) return 3;
            for(int j=0;j<LEG_SEGMENTS;++j) {
                float bone=length(l.joints[j+1]-l.joints[j]);
                if(!std::isfinite(bone)) return 3;
            }
            float span=0;for(int j=1;j<LEG_SEGMENTS;++j) span+=l.bones[j];
            float extension=length(l.joints[FOOT]-l.joints[1])/span;
            minExtension=std::min(minExtension,extension);maxExtension=std::max(maxExtension,extension);
        }
        maxSwing=std::max(maxSwing,swinging);
        if(frame==220 || frame==234 || frame==248) {
            const wchar_t* name=frame==220?L"stride-1.png":frame==234?L"stride-2.png":L"stride-3.png";
            s.render(name);
        }
    }
    int turnFailures=0;float turnContactDrift=0,maxTurnError=0;
    for(float turnAngle:{PI*.5f,-PI*.5f,PI}) {
        s.pos={900,700};s.heading=s.aimHeading={1,0};s.resetFeet();
        s.rest=0;s.escapeTurning=false;s.angularSpeed=0;s.velocity={};
        s.target=s.pos+V{std::cos(turnAngle),std::sin(turnAngle)}*500;
        int turnSteps=0;
        for(int frame=0;frame<960;++frame) {
            auto before=s.legs;s.advance(1.f/120);
            for(int i=0;i<8;++i) {
                if(before[i].phase>=1 && s.legs[i].phase<1) ++turnSteps;
                if(before[i].phase>=1 && s.legs[i].phase>=1)
                    turnContactDrift=std::max(turnContactDrift,length(before[i].foot-s.legs[i].foot));
            }
        }
        float error=std::abs(CrawlPhysics::angle(s.heading,unit(s.target-s.pos)));
        maxTurnError=std::max(maxTurnError,error);
        if(error>.35f || turnSteps<8) ++turnFailures;
    }
    std::ofstream report("motion-test.txt");
    float rmsAcceleration=std::sqrt(accelerationSquared/std::max(1,accelerationSamples));
    report<<"30 seconds at 120 Hz, normal/fast/slow speeds, sharp turns\n"
          <<"Anatomy failures across three sizes and two stances: "<<anatomyFailures<<"\n"
          <<"Steps: "<<steps<<"\nMax simultaneous lifted legs: "<<maxSwing
          <<"\nSteps shorter than 8px: "<<microSteps
          <<"\nMean step distance: "<<stepDistanceSum/std::max(1,steps)
          <<"\nSteady-walk RMS acceleration: "<<rmsAcceleration
          <<"\nMaximum exposed leg intersections: "<<maxCrossings
          <<"\nMinimum signed side clearance (scale normalized): "<<minSide
          <<"\nMinimum leg-to-head distance (scale normalized): "<<minHead
          <<"\nMaximum distal joint deflection (degrees): "<<maxDistalFold
          <<"\nPlanted foot checks: "<<planted<<"\nPlanted foot drift: "<<drift
          <<"\nMaximum hip-foot distance: "<<maxReach
          <<"\nMinimum projected leg extension: "<<minExtension
          <<"\nMaximum projected leg extension: "<<maxExtension
          <<"\nMaximum standing main-joint deflection (degrees): "<<standingFold
          <<"\nMaximum grounded main-joint deflection (degrees): "<<maxGroundedFold
          <<"\nMean grounded main-joint deflection (degrees): "<<groundedFoldSum/std::max(1,groundedFoldSamples)
          <<"\nBody drift with all contacts fixed: "<<fixedBodyDrift
          <<"\nBody displacement from moving one foot 30px: "<<contactPull
          <<"\nMaximum torso trajectory error: "<<maxTorsoError
          <<"\nStepping turn failures (left/right/reverse): "<<turnFailures
          <<"\nPlanted contact drift during turns: "<<turnContactDrift
          <<"\nMaximum completed turn error (radians): "<<maxTurnError
          <<"\nSmooth commanded body travel: "<<bodyTravel<<"\n";
    bool ok=rmsAcceleration<2500 && microSteps<steps*.03f && anatomyFailures==0 && maxCrossings==0 && minSide>=0 && minHead>8.15f && maxDistalFold<50 && steps>60 && maxSwing<=4 && planted>1000 && drift==0 && maxReach<180 && minExtension>.25f && maxExtension<1.15f && maxExtension-minExtension>.25f
        && turnFailures==0 && turnContactDrift==0 && fixedBodyDrift<.1f && contactPull<.01f && maxTorsoError<.01f && bodyTravel>200 && standingFold<90 && maxGroundedFold<145;
    report<<(ok?"PASS":"FAIL")<<"\n";
    return ok?0:4;
}
int interactionTest(Spider& s) {
    s.living=false;
    s.rng.seed(123);
    int cases=0,failures=0; float maxPinError=0,maxBoneError=0,minBodyMotion=100000,minFlail=100000,minBodyResponse=100000;
    float minSide=10000,minHead=10000;
    for(float scale:{.7f,1.f,1.3f}) for(int leg=0;leg<8;++leg) {
        s.scale=scale; s.pos={900,700}; s.heading={1,0}; s.resetFeet();
        s.elapsed=0; s.rest=0; s.follow=false;
        // Exercise pickup from the walking projection rather than only the
        // nominal reset pose: switching to free physics must remain stable.
        s.solveLegs(1.f/120);
        if(s.footAt(s.legs[leg].joints[FOOT])!=leg || s.footAt(s.pos+V{220,220})!=-1) ++failures;
        V start=s.pos,initialFree=s.legs[(leg+1)%8].joints[FOOT]-s.pos;
        V anchor=s.legs[leg].joints[FOOT]; s.beginGrab(leg,anchor);
        float flail=0;
        for(int frame=0;frame<180;++frame) {
            float t=frame/120.f;
            s.dragTarget=anchor+V{t*220,std::sin(t*5)*45-t*85};
            s.advance(1.f/120);
            auto clearance=s.legClearance();minSide=std::min(minSide,clearance[0]);minHead=std::min(minHead,clearance[1]);
            if(scale==1.f && leg==0 && frame==110) s.render(L"drag-preview.png");
            maxPinError=std::max(maxPinError,length(s.legs[leg].joints[FOOT]-s.dragTarget));
            flail=std::max(flail,length((s.legs[(leg+1)%8].joints[FOOT]-s.pos)-initialFree));
            for(const auto& b:s.physics.bones) {
                float actual=length(s.physics.particles[b.a].p-s.physics.particles[b.b].p);
                if(!std::isfinite(actual)) ++failures;
                maxBoneError=std::max(maxBoneError,std::abs(actual-b.rest));
            }
        }
        minBodyMotion=std::min(minBodyMotion,length(s.pos-start)); minFlail=std::min(minFlail,flail);
        // Longer natural legs absorb more drag by unfolding. Require a clear
        // body response relative to the actual pointer excursion, not a fixed
        // displacement tuned to the former short, heavily folded silhouette.
        minBodyResponse=std::min(minBodyResponse,length(s.pos-start)/std::max(1.f,length(s.dragTarget-anchor)));
        s.dragVelocity={700,-150}; s.lastDragTime=GetTickCount64(); s.endGrab();
        for(int n=0;n<180;++n) s.advance(1.f/120);
        if(s.grabbed!=-1 || s.airborne>0 || !std::isfinite(s.pos.x)||!std::isfinite(s.pos.y)) ++failures;
        for(const auto& l:s.legs) if(s.physics.particles[l.ids[FOOT]].mass!=0) ++failures;
        // Holding the mouse still before release must not replay a stale throw.
        s.beginGrab(leg,s.legs[leg].joints[FOOT]); s.dragVelocity={1500,0};
        s.lastDragTime=GetTickCount64()-200; s.endGrab();
        if(length(s.dragVelocity)>.01f) ++failures;
        for(int n=0;n<90;++n) s.advance(1.f/120);
        ++cases;
    }
    bool ok=failures==0 && minSide>=0 && minHead>8.15f && maxPinError<.01f && maxBoneError<5 && minBodyResponse>.2f && minFlail>10;
    std::ofstream report("interaction-test.txt");
    report<<"All 8 legs at 3 sizes, drag/flail/throw/land/stationary release\nCases: "<<cases
          <<"\nFailures: "<<failures<<"\nMaximum held toe error: "<<maxPinError
          <<"\nMinimum signed side clearance (scale normalized): "<<minSide
          <<"\nMinimum leg-to-head distance (scale normalized): "<<minHead
          <<"\nMaximum bone length error while dragging: "<<maxBoneError
          <<"\nMinimum body displacement: "<<minBodyMotion
          <<"\nMinimum body-to-pointer displacement ratio: "<<minBodyResponse
          <<"\nMinimum free leg motion relative to torso: "<<minFlail<<"\n"<<(ok?"PASS":"FAIL")<<"\n";
    return ok?0:6;
}
#include "behavior_tests.h"
#include "feeding_tests.h"
#include "i18n_tests.h"
#include "startup_tests.h"
#include "logging_tests.h"
#include "launch_tests.h"
LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    static UINT taskbar=RegisterWindowMessageW(L"TaskbarCreated");
    if(m==taskbar && app) {RuntimeLog::write("event=taskbar_recreated");app->addTray(); return 0; }
    switch(m) {
        case WM_TIMER: if(w==2) DestroyWindow(h); else if(w==3) app->inputTestTick(); else if(w==1) {
                RuntimeLog::write("event=heartbeat state=%ls paused=%d hunger=%.1f energy=%.2f x=%.0f y=%.0f gdi=%lu",
                    CreatureMind::label(app->mind.state,Language::English),app->paused,app->mind.hunger,app->mind.energy,
                    app->pos.x,app->pos.y,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS));
        } return 0;
        case WM_HOTKEY: RuntimeLog::write("event=hotkey id=%llu",static_cast<unsigned long long>(w));
                        if(w==1) {app->paused=!app->paused;if(app->paused)app->stopEating();}
                        if(w==2) {RuntimeLog::write("event=exit_request reason=hotkey_ctrl_alt_q");DestroyWindow(h);}
                        if(w==3) app->endGrab(true); return 0;
        case TRAY: if(l==WM_RBUTTONUP || l==WM_CONTEXTMENU) app->menu();
                   if(l==WM_LBUTTONDBLCLK) {app->paused=!app->paused;if(app->paused)app->stopEating();} return 0;
        case WM_DISPLAYCHANGE: { POINT p; GetCursorPos(&p); app->monitorAt(p); return 0; }
        case WM_CLOSE: RuntimeLog::write("event=exit_request reason=window_close");break;
        case WM_QUERYENDSESSION: RuntimeLog::write("event=session_end_requested flags=0x%llX",static_cast<unsigned long long>(l));return TRUE;
        case WM_ENDSESSION: RuntimeLog::write("event=session_end confirmed=%d",w!=0);break;
        case WM_DESTROY: RuntimeLog::write("event=window_destroyed");app->stopEating();KillTimer(h,1); KillTimer(h,2); KillTimer(h,3); UnregisterHotKey(h,1); UnregisterHotKey(h,2); PostQuitMessage(0); return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    }
    return DefWindowProcW(h,m,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int) {
    bool diagnostic=wcsstr(command,L"--")!=nullptr;
    bool launchProbe=wcsstr(command,L"/verify-independent-launch")!=nullptr;
    auto launch=ProcessLaunch::detachIfNeeded(command,!diagnostic||launchProbe);
    if(launch.relaunched)return 0;
    // The isolated lifetime probe runs alongside the test harness, without
    // joining the normal pet's instance lock or enabling any personal features.
    HANDLE mutex=launchProbe?nullptr:CreateMutexW(nullptr,TRUE,L"Local\\NeonSpider.DesktopPet");
    DWORD mutexError=GetLastError();
    if(!launchProbe&&mutexError==ERROR_ALREADY_EXISTS) { if(mutex) CloseHandle(mutex); return 0; }
    if(!diagnostic)RuntimeLog::start();
    auto job=ProcessLaunch::currentJob();
    RuntimeLog::write("event=process_job inside=%d flags=0x%08lX query_error=%lu detach_error=%lu",job.inside,job.flags,job.error,launch.error);
    if(auto origin=wcsstr(command,L"/independent-from="))RuntimeLog::write("event=independent_launch parent_pid=%lu",wcstoul(origin+18,nullptr,10));
    if(launchProbe){std::ofstream probe("launch-probe.txt");probe<<GetCurrentProcessId()<<" "<<job.inside<<" "<<job.flags<<" "<<launch.error<<"\n";}
    if(!mutex&&!launchProbe)RuntimeLog::write("event=mutex_failed error=%lu",mutexError);
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    ULONG_PTR token; GdiplusStartupInput startup;
    if(GdiplusStartup(&token,&startup,nullptr)!=Ok) {RuntimeLog::write("event=gdiplus_failed");RuntimeLog::finish(1);if(SUCCEEDED(com))CoUninitialize();CloseHandle(mutex);return 1;}
    int result=0;
    {
        Spider spider; app=&spider;
        spider.living=wcsstr(command,L"--")==nullptr;
        if(spider.living) spider.loadMemory();
        POINT p; GetCursorPos(&p); spider.monitorAt(p); spider.initSurface();
        if(spider.living) {spider.saveMemory();spider.feeder.setEnabled(spider.mind.autoEat);spider.feeder.start();spider.feedingLive=true;}
        if(wcsstr(command,L"--launch-test")) {result=launchTest();}
        else if(wcsstr(command,L"--logging-test")) {result=loggingTest();}
        else if(wcsstr(command,L"--startup-test")) {result=startupTest();}
        else if(wcsstr(command,L"--i18n-test")) {result=i18nTest(spider);}
        else if(wcsstr(command,L"--feeding-canary-test")) {result=feedingCanaryTest();}
        else if(wcsstr(command,L"--feeding-test")) {result=feedingTest(spider);}
        else if(wcsstr(command,L"--recycle-probe")) {result=recycleProbe();}
        else if(wcsstr(command,L"--behavior-test")) { result=behaviorTest(spider); }
        else if(wcsstr(command,L"--interaction-test")) { result=interactionTest(spider); }
        else if(wcsstr(command,L"--motion-test")) { result=motionTest(spider); }
        else if(wcsstr(command,L"--preview")) {
            spider.rainbow=wcsstr(command,L"--preview-rainbow")!=nullptr;
            spider.render(spider.rainbow?L"preview-rainbow.png":L"preview.png");
        }
        else {
            WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=instance; wc.lpszClassName=L"NeonSpiderOverlay";
            RegisterClassW(&wc);
            spider.hwnd=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
                wc.lpszClassName,L"Silkstride",WS_POPUP,0,0,CANVAS,CANVAS,nullptr,nullptr,instance,nullptr);
            if(!spider.hwnd || !spider.dc || !spider.dib) {RuntimeLog::write("event=window_init_failed error=%lu",GetLastError());result=2;}
            else {
                // Observe screen coordinates without activating the overlay. Swallow
                // only a foot's left-button down/up pair; every other click passes on.
                spider.mouseHook=SetWindowsHookExW(WH_MOUSE_LL,MouseHook,instance,0);
                if(!spider.mouseHook) {RuntimeLog::write("event=mouse_hook_failed error=%lu",GetLastError());result=5;}
                spider.addTray(); spider.render(); ShowWindow(spider.hwnd,SW_SHOWNOACTIVATE);
                if(spider.living&&!spider.inputTest)spider.reportStartupError(spider.startup.initialize());
                if(!RegisterHotKey(spider.hwnd,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'S'))RuntimeLog::write("event=hotkey_registration_failed key=S error=%lu",GetLastError());
                if(!RegisterHotKey(spider.hwnd,2,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'Q'))RuntimeLog::write("event=hotkey_registration_failed key=Q error=%lu",GetLastError());
                SetTimer(spider.hwnd,1,60000,nullptr); // Runtime heartbeat only.
                if(wcsstr(command,L"--smoke-test")) SetTimer(spider.hwnd,2,3000,nullptr);
                if(wcsstr(command,L"--input-test")) {
                    spider.living=true; spider.mind.init(spider.senses());
                    spider.inputTest=true; spider.paused=true;
                    GetCursorPos(&spider.savedCursor);
                    spider.inputReport.open("input-test.txt");
                    // An inert backdrop catches any missed test clicks, protecting
                    // the user's apps while real Windows input traverses the hook.
                    spider.testBackdrop=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
                        L"STATIC",L"Mouse interaction check",WS_POPUP|SS_BLACKRECT,
                        int(spider.pos.x-CANVAS/2),int(spider.pos.y-CANVAS/2),CANVAS,CANVAS,
                        nullptr,nullptr,instance,nullptr);
                    ShowWindow(spider.testBackdrop,SW_SHOWNOACTIVATE);
                    SetWindowPos(spider.hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                    SetTimer(spider.hwnd,3,150,nullptr);
                }
                RuntimeLog::write("event=window_ready");
                MSG msg{};bool running=true;unsigned long long renderedFrames=0;
                auto renderStart=std::chrono::steady_clock::now();
                spider.last=renderStart;
                while(running) {
                    // Keep input responsive even under a busy message queue,
                    // then render immediately without a timer, sleep, or cap.
                    for(int count=0;count<64 && PeekMessageW(&msg,nullptr,0,0,PM_REMOVE);++count) {
                        if(msg.message==WM_QUIT) {running=false;break;}
                        TranslateMessage(&msg);DispatchMessageW(&msg);
                    }
                    if(!running) break;
                    spider.tick();++renderedFrames;
                    if(spider.paused && spider.grabbed<0 && spider.airborne<=0)
                        MsgWaitForMultipleObjectsEx(0,nullptr,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
                }
                if(wcsstr(command,L"--smoke-test") && !launchProbe) {
                    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-renderStart).count();
                    std::ofstream report("render-smoke.txt");
                    report<<"Frames: "<<renderedFrames<<"\nWall seconds: "<<seconds
                          <<"\nAverage render FPS: "<<renderedFrames/std::max(.001,seconds)
                          <<"\nSimulation seconds: "<<spider.elapsed<<"\n";
                }
                if(spider.inputTest && spider.inputFailures) result=7;
            }
        }
        app=nullptr;
    }
    GdiplusShutdown(token);if(SUCCEEDED(com))CoUninitialize();if(mutex)CloseHandle(mutex);RuntimeLog::finish(result);return result;
}
