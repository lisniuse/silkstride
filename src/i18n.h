#pragma once
#include <windows.h>
#include <array>
#include <istream>
#include <ostream>
#include <string>

enum class Language { Chinese, English };
enum class Text {
    Title, InitialTip, Hunger, Energy, Personality, Bold, Cautious, Active, Easygoing,
    Pause, Follow, Rainbow, AutoEat, HungerPanel, Slow, Normal, Fast, Size, MoveMonitor, Quit,
    Ate, Eating, Hungry, NotHungry, Skipped,
    Wake, Prepare, Explore, Observe, Approach, Flee, Shelter, Rest, Groom, Recover, Held, SeekFood, Eat,
    Count
};

class Localizer {
    struct Entry { const wchar_t* chinese; const wchar_t* english; };
    inline static constexpr std::array<Entry,static_cast<size_t>(Text::Count)> entries{{
        {L"Silkstride · 丝行",L"Silkstride"},
        {L"丝行 · 左键抓脚拖拽 · Ctrl+Alt+Q 退出",L"Silkstride · Drag any foot · Ctrl+Alt+Q to quit"},
        {L"饥饿",L"Hunger"},{L"精力",L"Energy"},{L"性格：",L"Personality: "},
        {L"大胆",L"bold"},{L"谨慎",L"cautious"},{L"活跃",L"active"},{L"悠闲",L"easygoing"},
        {L"暂停  Ctrl+Alt+S",L"Pause  Ctrl+Alt+S"},
        {L"跟随鼠标",L"Follow mouse"},{L"彩虹循环配色",L"Cycle rainbow colors"},
        {L"回收站进食（逐个永久删除文件）",L"Eat Recycle Bin files (permanently delete one at a time)"},
        {L"显示饥饿提示面板",L"Show hunger panel"},
        {L"慢速",L"Slow"},{L"正常速度",L"Normal speed"},{L"快速",L"Fast"},
        {L"切换大小",L"Change size"},{L"移到鼠标所在屏幕",L"Move to mouse's monitor"},
        {L"退出  Ctrl+Alt+Q",L"Quit  Ctrl+Alt+Q"},
        {L"吃掉了：",L"Ate: "},{L"正在吃：",L"Eating: "},
        {L"肚子饿了，想找吃的",L"Hungry, looking for food"},{L"暂时不饿",L"Not hungry yet"},
        {L"有文件无法进食，已跳过",L"Some files could not be eaten; skipped"},
        {L"醒来伸展",L"Waking up"},{L"探路准备",L"Preparing to walk"},
        {L"探索巡视",L"Exploring"},{L"停下观察",L"Observing"},
        {L"好奇试探",L"Approaching curiously"},{L"受惊逃离",L"Fleeing"},
        {L"寻找角落",L"Seeking shelter"},{L"休息恢复",L"Resting"},
        {L"整理腿脚",L"Grooming"},{L"落地站稳",L"Recovering footing"},
        {L"被抓住了",L"Being held"},{L"去回收站找吃的",L"Seeking Recycle Bin food"},
        {L"回收站进食",L"Eating in Recycle Bin"}
    }};
public:
    Language language=systemLanguage(GetUserDefaultUILanguage());
    static Language systemLanguage(LANGID id) {
        return PRIMARYLANGID(id)==LANG_CHINESE?Language::Chinese:Language::English;
    }
    static const wchar_t* get(Text key,Language language) {
        size_t index=static_cast<size_t>(key);
        if(index>=entries.size())return L"";
        return language==Language::Chinese?entries[index].chinese:entries[index].english;
    }
    const wchar_t* operator()(Text key) const {return get(key,language);}
    bool read(std::istream& input) {
        int version=0;std::string code;
        if(!(input>>version>>code)||version!=1||(code!="zh-CN"&&code!="en"))return false;
        language=code=="zh-CN"?Language::Chinese:Language::English;return true;
    }
    void write(std::ostream& output) const {output<<"1 "<<(language==Language::Chinese?"zh-CN":"en")<<"\n";}
};
