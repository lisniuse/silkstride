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
    std::array<V,5> joints{}, previous{};
    std::array<float,4> bones{};
    std::array<int,5> ids{};
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
    V bodyOffset{}, bodySpring{};
    bool paused=false, follow=false, rainbow=true, showHungerPanel=false;
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
        result.foodPoint=food.point;result.foodArea=food.area;result.cursorVelocity=sensedCursorVelocity;return result;
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
            V foot=legs[0].joints[4];
            inputReport<<"Toe: "<<foot.x<<","<<foot.y<<"\n";
            BOOL moved=SetCursorPos(int(std::round(foot.x)),int(std::round(foot.y)));
            inputReport<<"SetCursorPos: "<<moved<<"\n";
        } else if(inputStage==1) {
            POINT cursor; GetCursorPos(&cursor);
            inputReport<<"Cursor before down: "<<cursor.x<<","<<cursor.y<<" hit: "<<footAt({float(cursor.x),float(cursor.y)})<<"\n";
            injectMove(legs[0].joints[4],MOUSEEVENTF_LEFTDOWN);
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
                float error=length(legs[grabbed].joints[4]-sensedCursor);
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
            float d=length(point-legs[i].joints[4]);
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
            physics.particles[legs[i].ids[4]].mass=i==grabbed?0.f:1.f;
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
            l.foot=physics.particles[l.ids[4]].p;
            l.from=l.goal=l.foot; l.phase=1; l.lift=0;
            physics.particles[l.ids[4]].mass=0;
            for(int j=0;j<5;++j) l.joints[j]=physics.particles[l.ids[j]].p;
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
            physics.pin(legs[grabbed].ids[4],dragTarget);
            for(size_t n=0;n<physics.bends.size();++n) {
                auto& b=physics.bends[n]; int leg=int(n)/4;
                float amplitude=leg==grabbed?.07f:(n%4==0?.3f:.48f);
                // First recoil, then bursts of kicking with pauses during a long hold.
                float recoil=grabTime<.3f?.3f:1.f;
                float burst=recoil*(.2f+.8f*std::pow(.5f+.5f*std::sin(grabTime*4.1f),2.f));
                b.rest=b.neutral+amplitude*burst*std::sin(grabTime*(10.f+leg*.73f)+leg*2.7f+n%4);
            }
            for(int i=0;i<8;++i) if(i!=grabbed) {
                auto& p=physics.particles[legs[i].ids[4]];
                float burst=.25f+.75f*std::pow(.5f+.5f*std::sin(grabTime*4.1f),2.f);
                p.p=p.p+V{std::sin(grabTime*17+i*1.8f),std::cos(grabTime*13+i*2.2f)}*(1900*burst*scale*dt*dt);
            }
        } else {
            airborne-=dt;
            for(auto& b:physics.bends) b.rest+=(b.neutral-b.rest)*(1-std::exp(-dt*8));
        }
        physics.step(dt,held?V{0,300*scale}:V{});
        pos=physics.particles[0].p; heading=unit(physics.particles[1].p-pos);
        for(auto& l:legs) for(int j=0;j<5;++j) l.joints[j]=physics.particles[l.ids[j]].p;
        if(!held && airborne<=0) { airborne=0; land(); }
    }
    V local(float x,float y) const { return {heading.x*x-heading.y*y,heading.y*x+heading.x*y}; }
    V bodyLocal(float x,float y) const {
        return local(x*std::cos(bodyYaw)-y*std::sin(bodyYaw),x*std::sin(bodyYaw)+y*std::cos(bodyYaw));
    }
    V hip(int i) const { return pos+bodyOffset+bodyLocal((13-i%4*8)*scale,(i<4?-5.f:5.f)*scale); }
    std::array<V,5> restingChain(int i) const {
        int row=i%4,side=i<4?-1:1;
        float forward=row<2?1.f:-1.f;
        V h=hip(i), f=home(i);
        return {h,h+bodyLocal(forward*3*scale,side*9*scale),
                pos+bodyOffset+local((row==0?78.f:row==1?60.f:row==2?-60.f:-78.f)*scale,
                                     (row==0||row==3?45.f:66.f)*side*scale),
                f+local(forward*10*scale,side*18*scale),f};
    }
    V home(int i) const {
        int side=i<4?-1:1, row=i%4;
        static const float xs[]={89,34,-36,-92};
        static const float ys[]={79,116,116,80};
        float stance=living?mind.stance:1.f;
        float x=xs[row]*scale,y=ys[row]*side*scale*stance;
        return pos+V{aimHeading.x*x-aimHeading.y*y,aimHeading.y*x+aimHeading.x*y};
    }
    void resetFeet() {
        grabbed=-1; airborne=0; hovered=-1;
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
            l.joints[0]=pos; l.ids[0]=0;
            for(int j=1;j<5;++j) l.ids[j]=physics.add(l.joints[j],j==4?0.f:1.f);
            for(int j=0;j<4;++j) {
                physics.bone(l.ids[j],l.ids[j+1]);
                l.bones[j]=length(l.joints[j+1]-l.joints[j]);
            }
            // Angular constraints couple the entire four-segment leg to the torso.
            physics.bend(1,0,l.ids[1],.075f);
            physics.bend(l.ids[0],l.ids[1],l.ids[2],.045f);
            physics.bend(l.ids[1],l.ids[2],l.ids[3],.012f);
            physics.bend(l.ids[2],l.ids[3],l.ids[4],.025f);
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
        sensedCursorVelocity={};
        if(cursorPrimed && dt>.001f) {
            sensedCursorVelocity=(currentCursor-previousCursor)*(1/dt);
            float measured=length(sensedCursorVelocity);
            sensedCursorSpeed+=(measured-sensedCursorSpeed)*(1-std::exp(-dt*12));
        }
        previousCursor=sensedCursor=currentCursor; cursorPrimed=true;
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
            mind.update(dt,senses());
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
        // Steering chooses future contacts only. It never translates the torso.
        float diff=std::remainder(wanted-angle,2*PI);
        bool fleeing=living && mind.state==CreatureMind::State::Flee;
        float turnLimit=fleeing?3.8f:1.65f;
        float turn=walking?std::clamp(diff*(fleeing?5.f:3.f),-turnLimit,turnLimit):0;
        angularSpeed+=(turn-angularSpeed)*(1-std::exp(-dt*5));
        angle+=angularSpeed*dt;
        aimHeading={std::cos(angle),std::sin(angle)};
        float arrival=std::clamp(length(target-pos)/100.f,.18f,1.f);
        float alignment=std::clamp(std::cos(diff),.12f,1.f);
        float pace=1+.09f*std::sin(elapsed*1.7f)+.04f*std::sin(elapsed*3.1f);
        bool seekingFood=living && mind.state==CreatureMind::State::SeekFood;
        V travelDirection=fleeing||seekingFood?unit(target-pos):aimHeading;
        float travelFactor=fleeing?1.f:seekingFood?std::clamp(length(target-pos)/90.f,.04f,1.f):arrival*alignment;
        V desired=walking?travelDirection*(effectiveSpeed*travelFactor*pace):V{};
        velocity=mix(velocity,desired,1-std::exp(-dt*(fleeing?14.f:4.f)));
        stepCooldown-=dt;
        int swinging=0;
        for(int i=0;i<8;++i) {
            auto& l=legs[i];
            if(l.phase<1) {
                l.phase=std::min(1.f,l.phase+dt/l.duration);
                float t=l.phase*l.phase*(3-2*l.phase);
                l.foot=mix(l.from,l.goal,t);
                l.lift=std::pow(std::max(0.f,std::sin(PI*l.phase)),.8f);
                if(l.phase<1) ++swinging;
                else l.lift=0;
            }
        }
        // Reference crawl order: (legIndex * 3) % 8, with its interleaved
        // left/right indices converted to this renderer's two groups of four.
        int candidate=-1;
        if(walking && swinging<3 && stepCooldown<=0) {
            int interleaved=((legCursor++)*3)%8;
            candidate=interleaved/2+(interleaved%2)*4;
            if(legs[candidate].phase<1) candidate=-1;
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
            l.duration=std::clamp(.29f-effectiveSpeed*.0008f,.13f,.28f)+random(-.015f,.015f);
            l.from=living?l.joints[4]:l.foot;
            // The web in the reference supplies discrete anchor particles. Use an
            // invisible desktop contact lattice, biased toward the roaming target.
            V desiredContact=home(candidate)+(walking?velocity*(fleeing||seekingFood?.34f:.58f):V{});
            bool precise=seekingFood && length(target-pos)<80*scale;
            float scatter=precise?2.f:10.f;
            desiredContact=desiredContact+V{random(-scatter,scatter),random(-scatter,scatter)}*scale;
            float grid=(precise?2.f:10.f)*scale;
            l.goal={std::round(desiredContact.x/grid)*grid,std::round(desiredContact.y/grid)*grid};
            if(living) {
                float inset=20*scale;
                l.goal.x=std::clamp(l.goal.x,work.left+inset,work.right-inset);
                l.goal.y=std::clamp(l.goal.y,work.top+inset,work.bottom-inset);
            }
            V reach=l.goal-pos;
            float maxReach=0; for(float bone:l.bones) maxReach+=bone;
            if(length(reach)>maxReach*.94f) l.goal=pos+unit(reach)*(maxReach*.94f);
            l.phase=0; l.lift=0;
            stepCooldown=walking?random(.085f,.145f)*75.f/std::max(15.f,effectiveSpeed):.16f;
        }
        solveLegs(dt);
    }
    void solveLegs(float dt) {
        // Only feet are pinned. All torso and leg joints remain free particles,
        // so changing a foot contact pulls the body through the constraint graph.
        for(int i=0;i<8;++i) {
            auto& l=legs[i];
            int side=i<4?-1:1;
            V toe=l.foot+local(0,-side*5*l.lift*scale)+V{0,-11*l.lift*scale};
            if(living) {
                V desiredOffset=l.phase>=1 && hovered!=i?mind.toeMotion(i,heading)*scale:V{};
                toeOffsets[i]=mix(toeOffsets[i],desiredOffset,1-std::exp(-dt*14));
                if(l.phase>=1) toe=toe+toeOffsets[i];
            }
            physics.pin(l.ids[4],toe);
        }
        if(living) {
            float look=std::remainder(std::atan2(mind.attention.y-pos.y,mind.attention.x-pos.x)-std::atan2(heading.y,heading.x),2*PI);
            look=std::clamp(look,-.7f,.7f);
            for(size_t n=0;n<physics.bends.size();++n) {
                auto& b=physics.bends[n];
                float breath=.006f*std::sin(elapsed*2.2f+n/4.f);
                float desired=b.neutral+breath-(n%4==0 && !mind.moving?look*.12f:0.f);
                b.rest+=(desired-b.rest)*.05f;
            }
        }
        physics.step(dt);
        pos=physics.particles[0].p;
        heading=unit(physics.particles[1].p-pos);
        for(auto& l:legs) for(int j=0;j<5;++j) l.joints[j]=physics.particles[l.ids[j]].p;
    }
    Color pigment(int segment=0) const {
        float hue=rainbow?std::fmod(elapsed*.055f,1.f):.51f;
        float h=hue*6, c=.78f, x=c*(1-std::abs(std::fmod(h,2.f)-1));
        float r=0,g=0,b=0;
        if(h<1) {r=c;g=x;} else if(h<2) {r=x;g=c;} else if(h<3) {g=c;b=x;}
        else if(h<4) {g=x;b=c;} else if(h<5) {r=x;b=c;} else {r=c;b=x;}
        float shade=1-segment*.13f;
        return Color(255,BYTE((r+.18f)*255*shade),BYTE((g+.18f)*255*shade),BYTE((b+.18f)*255*shade));
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
            static const float widths[]={3.8f,3.f,2.f,1.15f};
            for(int j=0;j<4;++j) {
                V a=screen(legs[i].joints[j]),b=screen(legs[i].joints[j+1]);
                // A fine shadow keeps the colored silhouette visible on light windows.
                line(g,a+V{.7f,1.2f},b+V{.7f,1.2f},Color(90,8,13,19),widths[j]+1);
                line(g,a,b,pigment(j),widths[j]);
                if(j>0) dot(g,a,widths[j]*.58f,pigment(j));
            }
            V toe=screen(legs[i].joints[4]);
            dot(g,toe,2.6f,pigment(2));
            if(i==hovered || i==grabbed) {
                Pen ring(Color(i==grabbed?230:150,235,245,255),1.2f);
                float r=(i==grabbed?8.f:10.f)*scale;
                g.DrawEllipse(&ring,toe.x-r,toe.y-r,r*2,r*2);
            }
        }
        V abdomen=screen(physics.particles[2].p),head=screen(physics.particles[1].p);
        // Three round body masses match the reference's small organic silhouette.
        line(g,abdomen,head,pigment(1),5);
        dot(g,abdomen+V{1,1},10.3f,Color(100,7,10,17));
        float breath=living?1+.025f*std::sin(elapsed*2.2f):1.f;
        dot(g,abdomen,10*breath,pigment()); dot(g,center,5.5f,pigment()); dot(g,head,5,pigment());
        if(living) for(int side:{-1,1}) dot(g,head+local(3*scale,side*2*scale),.9f,Color(220,20,30,40));
        dot(g,abdomen+bodyLocal(2*scale,-3*scale),2.8f,Color(45,255,255,255));
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
            for(int side:{-1,1}) line(g,head+local(5*scale,side*3*scale),head+local((12+chew)*scale,side*6*scale),pigment(),1.5f);
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
    // Causality regression: stationary contacts must not follow a scripted path;
    // changing one contact must propagate to the free torso via constraints.
    for(int n=0;n<240;++n) s.solveLegs(1.f/120);
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
    int steps=0, planted=0, maxSwing=0; float drift=0, maxReach=0, maxBoneError=0;
    float bodyTravel=0;
    for(int frame=0;frame<3600;++frame) {
        if(frame==900) s.target={200,250};
        if(frame==1800) { s.speed=135; s.target={1500,1100}; }
        if(frame==2700) { s.speed=38; s.target={300,1100}; }
        auto before=s.legs;
        V beforeBody=s.pos;
        s.advance(1.f/120);
        bodyTravel+=length(s.pos-beforeBody);
        int swinging=0,starts=0;
        for(int i=0;i<8;++i) {
            const auto& l=s.legs[i];
            if(l.phase<1) ++swinging;
            if(before[i].phase>=1 && l.phase<1) { ++steps; ++starts; }
            if(before[i].phase>=1 && l.phase>=1) {
                drift=std::max(drift,length(l.foot-before[i].foot)); ++planted;
            }
            maxReach=std::max(maxReach,length(l.foot-(s.pos+s.local((23-i%4*15)*s.scale,(i<4?-8.f:8.f)*s.scale))));
            if(!std::isfinite(l.foot.x)||!std::isfinite(l.foot.y)||starts>1) return 3;
            for(int j=0;j<4;++j) {
                float bone=length(l.joints[j+1]-l.joints[j]);
                if(!std::isfinite(bone)) return 3;
                maxBoneError=std::max(maxBoneError,std::abs(bone-l.bones[j]));
            }
        }
        maxSwing=std::max(maxSwing,swinging);
        if(frame==220 || frame==234 || frame==248) {
            const wchar_t* name=frame==220?L"stride-1.png":frame==234?L"stride-2.png":L"stride-3.png";
            s.render(name);
        }
    }
    std::ofstream report("motion-test.txt");
    report<<"30 seconds at 120 Hz, normal/fast/slow speeds, sharp turns\n"
          <<"Steps: "<<steps<<"\nMax simultaneous lifted legs: "<<maxSwing
          <<"\nPlanted foot checks: "<<planted<<"\nPlanted foot drift: "<<drift
          <<"\nMaximum hip-foot distance: "<<maxReach
          <<"\nMaximum four-segment bone error: "<<maxBoneError
          <<"\nBody drift with all contacts fixed: "<<fixedBodyDrift
          <<"\nBody displacement from moving one foot 30px: "<<contactPull
          <<"\nBody travel driven by foot contacts: "<<bodyTravel<<"\n";
    bool ok=steps>60 && maxSwing<=3 && planted>1000 && drift==0 && maxReach<180 && maxBoneError<2
        && fixedBodyDrift<.1f && contactPull>.5f && bodyTravel>200;
    report<<(ok?"PASS":"FAIL")<<"\n";
    return ok?0:4;
}
int interactionTest(Spider& s) {
    s.living=false;
    s.rng.seed(123);
    int cases=0,failures=0; float maxPinError=0,maxBoneError=0,minBodyMotion=100000,minFlail=100000;
    for(float scale:{.7f,1.f,1.3f}) for(int leg=0;leg<8;++leg) {
        s.scale=scale; s.pos={900,700}; s.heading={1,0}; s.resetFeet();
        s.elapsed=0; s.rest=0; s.follow=false;
        if(s.footAt(s.legs[leg].joints[4])!=leg || s.footAt(s.pos+V{220,220})!=-1) ++failures;
        V start=s.pos,initialFree=s.legs[(leg+1)%8].joints[4]-s.pos;
        V anchor=s.legs[leg].joints[4]; s.beginGrab(leg,anchor);
        float flail=0;
        for(int frame=0;frame<180;++frame) {
            float t=frame/120.f;
            s.dragTarget=anchor+V{t*220,std::sin(t*5)*45-t*85};
            s.advance(1.f/120);
            if(scale==1.f && leg==0 && frame==110) s.render(L"drag-preview.png");
            maxPinError=std::max(maxPinError,length(s.legs[leg].joints[4]-s.dragTarget));
            flail=std::max(flail,length((s.legs[(leg+1)%8].joints[4]-s.pos)-initialFree));
            for(const auto& b:s.physics.bones) {
                float actual=length(s.physics.particles[b.a].p-s.physics.particles[b.b].p);
                if(!std::isfinite(actual)) ++failures;
                maxBoneError=std::max(maxBoneError,std::abs(actual-b.rest));
            }
        }
        minBodyMotion=std::min(minBodyMotion,length(s.pos-start)); minFlail=std::min(minFlail,flail);
        s.dragVelocity={700,-150}; s.lastDragTime=GetTickCount64(); s.endGrab();
        for(int n=0;n<180;++n) s.advance(1.f/120);
        if(s.grabbed!=-1 || s.airborne>0 || !std::isfinite(s.pos.x)||!std::isfinite(s.pos.y)) ++failures;
        for(const auto& l:s.legs) if(s.physics.particles[l.ids[4]].mass!=0) ++failures;
        // Holding the mouse still before release must not replay a stale throw.
        s.beginGrab(leg,s.legs[leg].joints[4]); s.dragVelocity={1500,0};
        s.lastDragTime=GetTickCount64()-200; s.endGrab();
        if(length(s.dragVelocity)>.01f) ++failures;
        for(int n=0;n<90;++n) s.advance(1.f/120);
        ++cases;
    }
    bool ok=failures==0 && maxPinError<.01f && maxBoneError<5 && minBodyMotion>100 && minFlail>10;
    std::ofstream report("interaction-test.txt");
    report<<"All 8 legs at 3 sizes, drag/flail/throw/land/stationary release\nCases: "<<cases
          <<"\nFailures: "<<failures<<"\nMaximum held toe error: "<<maxPinError
          <<"\nMaximum bone length error while dragging: "<<maxBoneError
          <<"\nMinimum body displacement: "<<minBodyMotion
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
        case WM_TIMER: if(w==2) DestroyWindow(h); else if(w==3) app->inputTestTick(); else {
            app->tick();static ULONGLONG lastHeartbeat=GetTickCount64();
            if(GetTickCount64()-lastHeartbeat>=60000) {
                lastHeartbeat=GetTickCount64();
                RuntimeLog::write("event=heartbeat state=%ls paused=%d hunger=%.1f energy=%.2f x=%.0f y=%.0f gdi=%lu",
                    CreatureMind::label(app->mind.state,Language::English),app->paused,app->mind.hunger,app->mind.energy,
                    app->pos.x,app->pos.y,GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS));
            }
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
        else if(wcsstr(command,L"--preview")) { spider.render(L"preview.png"); }
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
                SetTimer(spider.hwnd,1,16,nullptr);
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
                MSG msg;BOOL messageResult;
                while((messageResult=GetMessageW(&msg,nullptr,0,0))>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                if(messageResult==-1) {RuntimeLog::write("event=message_loop_failed error=%lu",GetLastError());result=4;}
                if(spider.inputTest && spider.inputFailures) result=7;
            }
        }
        app=nullptr;
    }
    GdiplusShutdown(token);if(SUCCEEDED(com))CoUninitialize();if(mutex)CloseHandle(mutex);RuntimeLog::finish(result);return result;
}
