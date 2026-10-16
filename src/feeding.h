#pragma once

// No filesystem access here. A bite is a two-phase operation: finish chewing,
// request one exact item, and reduce hunger only after a successful receipt.
class FeedingCycle {
public:
    std::wstring token,name;
    HWND window=nullptr;
    V point{};
    float chewing=0;
    bool waiting=false;
    unsigned receipt=0;
    void cancel() {token.clear();name.clear();window=nullptr;chewing=0;waiting=false;}
    bool advance(float dt,bool canEat,const RecycleFoodView& food) {
        if(!canEat || food.token.empty() || !food.visible) {cancel();return false;}
        if(token!=food.token || window!=food.window) {
            if(waiting) return false;
            token=food.token;name=food.name;window=food.window;point=food.point;chewing=0;
        }
        if(std::hypot(food.point.x-point.x,food.point.y-point.y)>3) {
            if(waiting)return false;
            point=food.point;chewing=0;
        }
        if(waiting) return false;
        chewing+=dt;
        if(chewing<4.f) return false;
        waiting=true;return true;
    }
};
