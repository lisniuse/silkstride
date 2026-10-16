#pragma once

int i18nTest(Spider& spider) {
    std::ofstream out("i18n-test.txt");int failures=0,checks=0;
    auto check=[&](bool ok,const char* message){++checks;if(!ok)++failures;out<<(ok?"PASS ":"FAIL ")<<message<<"\n";};
    for(Language language:{Language::Chinese,Language::English}) {
        for(int i=0;i<static_cast<int>(Text::Count);++i)
            check(wcslen(Localizer::get(static_cast<Text>(i),language))>0,"Catalog entry is nonempty");
        for(int i=0;i<=static_cast<int>(CreatureMind::State::Eat);++i)
            check(wcslen(CreatureMind::label(static_cast<CreatureMind::State>(i),language))>0,"Behavior state has a translated label");
        Localizer original;original.language=language;std::stringstream file;original.write(file);
        Localizer restored;restored.language=language==Language::Chinese?Language::English:Language::Chinese;
        check(restored.read(file)&&restored.language==language,"Language preference survives serialization");
    }
    check(Localizer::systemLanguage(MAKELANGID(LANG_CHINESE,SUBLANG_CHINESE_SIMPLIFIED))==Language::Chinese,"Chinese system UI selects Chinese");
    check(Localizer::systemLanguage(MAKELANGID(LANG_ENGLISH,SUBLANG_ENGLISH_US))==Language::English,"English system UI selects English");
    check(Localizer::systemLanguage(MAKELANGID(LANG_JAPANESE,SUBLANG_DEFAULT))==Language::English,"Unsupported system UI falls back to English");
    for(const char* invalid:{"","1 fr","2 en","1"}) {
        Localizer ui;ui.language=Language::Chinese;std::stringstream input(invalid);
        check(!ui.read(input)&&ui.language==Language::Chinese,"Malformed or unsupported preference leaves current language intact");
    }
    check(!spider.showHungerPanel,"Hunger panel defaults to hidden");
    spider.living=true;spider.showHungerPanel=true;spider.mind.hunger=85;
    spider.ui.language=Language::English;spider.render(L"i18n-en.png");
    spider.ui.language=Language::Chinese;spider.render(L"i18n-zh-CN.png");
    out<<"Checks: "<<checks<<" Failures: "<<failures<<"\n"<<(failures?"FAIL":"PASS")<<"\n";
    return failures?11:0;
}
