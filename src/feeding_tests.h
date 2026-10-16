#pragma once

int feedingTest(Spider& spider) {
    using State=CreatureMind::State;
    std::ofstream out("feeding-test.txt");int checks=0,failures=0;
    auto check=[&](bool ok,const char* text){++checks;if(!ok)++failures;out<<(ok?"PASS ":"FAIL ")<<text<<"\n";};
    CreatureMind mind;mind.reseed(42);
    CreatureMind::Senses s{{900,700},{0,0},{0,0,1800,1400},0,1,false,false,false};
    mind.init(s);mind.hunger=95;mind.update(.02f,s);
    check(mind.mealWanted && mind.state!=State::Eat && mind.state!=State::SeekFood,"Hunger alone cannot start eating without a visible bin with food");
    CreatureMind healthy,hungry;healthy.reseed(42);hungry.reseed(42);
    healthy.init(s);hungry.init(s);healthy.enter(State::Explore,10);hungry.enter(State::Explore,10);
    healthy.age=hungry.age=1;healthy.goal=hungry.goal={1300,700};hungry.hunger=95;
    healthy.update(.1f,s);hungry.update(.1f,s);
    check(std::abs(hungry.speedFactor-healthy.speedFactor)<.0001f && hungry.boutTier==healthy.boutTier,
          "Hunger does not change movement speed or random exploration tier");
    CreatureMind digestion;digestion.hunger=0;
    digestion.advanceHunger(3600);
    check(digestion.hunger<25,"A full meal remains satiating for at least one active hour");
    digestion.advanceHunger(3600);
    check(digestion.hunger>=25 && digestion.hunger<80,"Satiety ends within two hours without becoming severely hungry");
    digestion.hunger=25;digestion.hungerRemainder=0;digestion.advanceHunger(3*3600);
    check(digestion.hunger<80,"Ordinary hunger lasts at least three active hours");
    digestion.advanceHunger(3600);
    check(digestion.hunger>=80,"Severe hunger begins within four hours after satiety ends");
    digestion.hunger=0;digestion.hungerRemainder=0;
    for(int frame=0;frame<3600*120;++frame) digestion.advanceHunger(1.f/120);
    check(std::abs(digestion.hunger-25.f*3600/5400)<.001f,"120 Hz hunger accumulation preserves hour-scale timing");
    spider.rainbow=false;spider.mind.hunger=0;Color fullColor=spider.bodyPigment(0);
    spider.render(L"satiety-full.png");
    spider.mind.hunger=50;
    Color fedColor=spider.bodyPigment(0),legColor=spider.pigment(0);
    spider.render(L"satiety-ordinary.png");
    spider.mind.hunger=100;Color hungryColor=spider.bodyPigment(0);
    spider.render(L"satiety-hungry.png");
    check(fullColor.GetR()<fedColor.GetR()*.4f && fullColor.GetG()<fedColor.GetG()*.4f,
          "A full meal produces a distinctly darker body than ordinary hunger");
    check(hungryColor.GetR()-hungryColor.GetB()<fedColor.GetR()-fedColor.GetB()
          && spider.pigment(0).GetValue()==legColor.GetValue(),
          "Hunger fades the body color without changing the legs");
    s.foodVisible=true;s.foodPoint={900,700};s.foodArea={600,400,1400,1000};
    mind.update(.02f,s);
    check(mind.state==State::Eat,"Very hungry creature enters the open bin and starts eating only after arriving");
    FeedingCycle bite;RecycleFoodView food;
    food.visible=true;food.window=reinterpret_cast<HWND>(1);food.area=s.foodArea;food.point=s.foodPoint;food.token=L"file-0";food.name=L"模拟文件.txt";
    float before=mind.hunger;
    check(!bite.advance(3.9f,true,food) && mind.hunger==before,"Chewing does not delete early or reduce hunger before confirmation");
    check(bite.advance(.2f,true,food) && bite.waiting,"One completed animation queues exactly one item");
    check(!bite.advance(100,true,food),"Waiting for deletion cannot queue repeated bites");
    // A failed backend operation produces no receipt and therefore no nutrition.
    bite.cancel();check(mind.hunger==before,"A failed file operation cannot reduce hunger");
    bite.advance(3.9f,true,food);food.visible=false;
    check(!bite.advance(1,true,food) && bite.chewing==0 && !bite.waiting,"Closing the window cancels a partial bite");
    food.visible=true;bite.advance(3.9f,true,food);
    check(!bite.advance(1,false,food) && bite.chewing==0,"Pausing or grabbing cancels the bite without requesting deletion");
    check(!bite.advance(1,true,food),"Resuming starts a fresh chewing animation");
    bite.cancel();bite.advance(3.9f,true,food);food.point.x+=40;
    check(!bite.advance(.2f,true,food) && bite.chewing<1,"A moved or scrolled filename restarts chewing instead of deleting at its old position");
    food.point=s.foodPoint;
    mind.enter(State::Eat,100000);s.foodPoint.y+=60;mind.update(.02f,s);
    check(mind.state==State::SeekFood,"A filename moving away sends the creature back to that file before eating");
    s.foodPoint.y-=60;s.body=s.foodPoint+V{0,40};mind.enter(State::SeekFood,30);mind.update(.02f,s);
    check(mind.state==State::SeekFood,"Being inside the window but forty pixels from the filename is not close enough to eat");
    s.body=s.foodPoint;
    RECT shortName{100,100,350,120};auto shortPoint=RecycleShell::textAnchor(shortName,L"a");
    check(shortPoint.x>=100 && shortPoint.x<115,"Short filenames anchor on their text instead of the blank part of a wide column");

    // Exercise actual articulated movement and a full four-second stationary
    // chew, with a fake visible-file anchor and no Shell worker/deletion calls.
    for(float size:{.7f,1.f,1.3f})for(V point:{V{1100,700},V{600,500},V{350,180}}) {
        spider.living=true;spider.memoryEnabled=false;spider.feedingLive=false;spider.scale=size;
        spider.pos={900,700};spider.heading={1,0};spider.work={0,0,1800,1400};spider.rng.seed(42);
        spider.mind=CreatureMind{};spider.mind.reseed(42);spider.mind.hunger=95;spider.resetFeet();
        spider.cursorPrimed=false;spider.sensedCursorVelocity={};spider.hovered=-1;
        RecycleFoodView anchor;anchor.visible=true;anchor.token=L"mock-visible-row";anchor.name=L"mock.txt";
        anchor.window=reinterpret_cast<HWND>(1);anchor.point=point;anchor.area={200,100,1600,1200};
        spider.food=anchor;spider.mind.init(spider.senses());FeedingCycle chewing;bool ready=false;float distance=10000,maxChew=0;
        int eatEntries=0;bool wasEating=false;
        for(int frame=0;frame<3000 && !ready;++frame) {
            spider.advance(1.f/120);distance=length(spider.pos-point);
            bool eating=spider.mind.state==State::Eat;
            if(eating&&!wasEating)++eatEntries;wasEating=eating;
            ready=chewing.advance(1.f/120,spider.mind.state==State::Eat && distance<=18*size,anchor);
            maxChew=std::max(maxChew,chewing.chewing);
        }
        out<<"Arrival scale="<<size<<" target="<<point.x<<","<<point.y<<" distance="<<distance
           <<" eat entries="<<eatEntries<<" longest chew="<<maxChew<<"\n";
        check(ready && distance<=18*size,"Physical body reaches the filename and stays close for a complete chew");
    }
    spider.food={};spider.scale=1;

    // End-to-end logic with an in-memory bin: never touches the user's files.
    bite.cancel();mind.hunger=95;mind.mealWanted=true;mind.enter(State::Eat,100000);
    int filesEaten=0,remaining=10;std::unordered_set<std::wstring> eaten;
    for(int frame=0;frame<3600;++frame) {
        food.token=L"file-"+std::to_wstring(filesEaten);food.name=food.token;
        s.foodVisible=remaining>0;mind.update(1.f/60,s);
        bool ready=mind.state==State::Eat && mind.mealWanted && s.foodVisible;
        if(bite.advance(1.f/60,ready,food)) {
            check(eaten.insert(bite.token).second,"Each completed bite targets one distinct mock file");
            --remaining;++filesEaten;mind.ateFile();bite.cancel();
        }
    }
    check(filesEaten==7 && remaining==3 && mind.hunger<5 && !mind.mealWanted,"Creature eats one file per bite and stops when full, leaving other files alone");
    mind.hunger=90;mind.mealWanted=true;mind.autoEat=false;mind.enter(State::Observe,10);mind.update(.02f,s);
    check(mind.state!=State::Eat && mind.state!=State::SeekFood,"Disabling automatic feeding prevents feeding behavior");
    mind.autoEat=true;mind.enter(State::Eat,100000);s.foodVisible=false;mind.update(.02f,s);
    check(mind.state!=State::Eat,"An empty, hidden or closed bin interrupts eating");

    std::stringstream legacy("1 123 .6 .8 .2 .1 5\n0 1 2 3 4 5 6 7 8\n");CreatureMind loaded;
    check(loaded.readMemory(legacy) && loaded.hunger==20 && loaded.autoEat && loaded.grabs==5,"Existing version-1 personality memory migrates without losing prior data");
    loaded.hunger=91;loaded.autoEat=false;std::stringstream current;loaded.writeMemory(current);CreatureMind restored;
    check(restored.readMemory(current) && restored.hunger==91 && !restored.autoEat,"Hunger and feeding switch persist across restart");

    // Production destructive entry points are tested with denial guards only.
    RecycleShell shell;wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    ComPtr<IShellItem> ordinary;SHCreateItemFromParsingName(executable,nullptr,IID_PPV_ARGS(&ordinary));
    check(ordinary && !shell.isDirectFile(ordinary.Get()),"Production scope gate rejects ordinary files outside the Recycle Bin");
    ComPtr<IShellItem> bin;SHCreateItemInKnownFolder(FOLDERID_RecycleBinFolder,0,nullptr,IID_PPV_ARGS(&bin));
    check(bin && !shell.isDirectFile(bin.Get()),"Production scope gate rejects the bin itself and folders");
    check(!shell.consumeOne(L"invalid-token",nullptr,[]{return false;}),"Denied authorization cannot reach a deletion operation");
    ComPtr<RecycleShell::DeleteReceipt> receipt;receipt.Attach(new RecycleShell::DeleteReceipt([]{return false;}));
    check(receipt->PreDeleteItem(0,ordinary.Get())==E_ABORT,"Per-file callback aborts a revoked feeding authorization");
    receipt->PostDeleteItem(0,ordinary.Get(),E_ACCESSDENIED,nullptr);
    check(!receipt->deleted,"Failure callbacks cannot be mistaken for eating");
    receipt->PostDeleteItem(0,ordinary.Get(),S_OK,ordinary.Get());
    check(!receipt->deleted,"Recycling instead of permanent removal cannot be counted as eating");
    receipt->PostDeleteItem(0,ordinary.Get(),S_OK,nullptr);
    check(receipt->deleted,"Only a successful permanent-delete callback counts as a consumed file");
    receipt->PostDeleteItem(0,ordinary.Get(),HRESULT(0x270008),nullptr);
    check(receipt->deleted,"Shell success statuses other than S_OK are recognized, with item absence verified by the backend");

    spider.living=true;spider.memoryEnabled=false;spider.mind=CreatureMind{};spider.mind.hunger=95;
    spider.mind.mealWanted=true;spider.mind.enter(State::Eat,100000);spider.feeding.name=L"示例文件.txt";spider.render(L"feeding-preview.png");
    out<<"Checks: "<<checks<<" Failures: "<<failures<<"\nReal files deleted during tests: 0\n"<<(failures==0?"PASS":"FAIL")<<"\n";
    return failures?9:0;
}

int recycleProbe() {
    RecycleShell shell;auto view=shell.visibleView();
    auto file=view.visible?shell.findFile():RecycleShell::Item{};
    std::ofstream trace("recycle-anchor-details.txt");auto anchor=shell.visibleFile(nullptr,L"",nullptr,false,&trace);
    std::ofstream out("recycle-probe.txt");
    out<<"Visible genuine Recycle Bin view: "<<view.visible<<"\nEligible direct file available: "<<bool(file.shell)
       <<"\nVisible exact file anchor: "<<!anchor.token.empty()<<"\nAnchor point: "<<anchor.point.x<<","<<anchor.point.y
       <<"\nFilename rectangle: "<<anchor.itemArea.left<<","<<anchor.itemArea.top<<","<<anchor.itemArea.right<<","<<anchor.itemArea.bottom
       <<"\nNo deletion requested or performed.\n";
    return 0;
}

int feedingCanaryTest() {
    std::ofstream out("feeding-canary-test.txt");
    RecycleShell shell;auto view=shell.visibleView();
    if(!view.visible) {out<<"FAIL: no exposed Recycle Bin view; no test file created.\n";return 10;}
    GUID id{};if(FAILED(CoCreateGuid(&id)))return 10;
    wchar_t guid[64]{},module[32768]{};StringFromGUID2(id,guid,64);GetModuleFileNameW(nullptr,module,32768);
    std::wstring prefix=L"NeonSpiderCanary-"+std::wstring(guid);
    // CREATE_NEW proves ownership. The only destructive target later is the
    // unique Shell item carrying this fresh GUID, never a pre-existing bin item.
    std::filesystem::path path=std::filesystem::path(module).parent_path()/(prefix+L".txt");
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) {out<<"FAIL: could not create disposable test file.\n";return 10;}
    const char content[]="Disposable NeonSpider feeding integration test.\r\n";
    DWORD written=0;WriteFile(file,content,DWORD(sizeof(content)-1),&written,nullptr);CloseHandle(file);
    ComPtr<IShellItem> original;ComPtr<IFileOperation> recycle;
    HRESULT hr=SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&original));
    if(SUCCEEDED(hr))hr=CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&recycle));
    if(SUCCEEDED(hr))hr=recycle->SetOperationFlags(FOF_SILENT|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOFX_EARLYFAILURE|FOFX_RECYCLEONDELETE);
    if(SUCCEEDED(hr))hr=recycle->DeleteItem(original.Get(),nullptr);
    if(SUCCEEDED(hr))hr=recycle->PerformOperations();
    if(FAILED(hr)) {
        // This is our just-created direct file, not a recursive or computed tree.
        DeleteFileW(path.c_str());out<<"FAIL: could not recycle disposable test file.\n";return 10;
    }
    RecycleShell::Item canary;
    std::ofstream diagnostic("recycle-diagnostic.txt");
    for(int n=0;n<20 && !canary.shell;++n) {
        canary=shell.findFile(L"",nullptr,prefix,&diagnostic);
        if(!canary.shell) {MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(100);}
    }
    if(!canary.shell) {out<<"FAIL: disposable file was not found in the bin; no existing item deleted.\n";return 10;}
    RecycleFoodView anchor;
    for(int n=0;n<20 && anchor.token.empty();++n) {
        shell.revealForCanary(canary.token,view.window);
        anchor=shell.visibleFile(nullptr,canary.token,view.window,true,&diagnostic);
        if(anchor.token.empty()){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(100);}
    }
    if(anchor.token.empty()){out<<"FAIL: own canary filename not visible; no deletion attempted.\n";return 10;}
    // Use the real asynchronous worker's position guard, but only submit the
    // unique disposable canary. No ordinary autonomous feeding is enabled.
    RecycleFeeder feeder;feeder.start();feeder.setBody(anchor.point,1);feeder.allow(true);
    bool submitted=feeder.submit(canary.token,view.window,canary.name);RecycleFoodView response;
    for(int n=0;n<120 && response.receipt==0;++n) {
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        Sleep(100);response=feeder.read();
    }
    feeder.stop();
    bool consumed=submitted && response.receipt>0 && response.consumed;
    bool absent=!shell.findFile(canary.token).shell;
    out<<"Own disposable file recycled: 1\nExact canary permanently consumed: "<<consumed
       <<"\nExact canary absent afterwards: "<<absent<<"\nVerified filename anchor: "<<anchor.point.x<<","<<anchor.point.y
       <<"\nPre-existing user files requested for deletion: 0\n"<<(consumed&&absent?"PASS":"FAIL")<<"\n";
    return consumed&&absent?0:10;
}
