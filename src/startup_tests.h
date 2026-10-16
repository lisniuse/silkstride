#pragma once

int startupTest() {
    std::ofstream out("startup-test.txt");int failures=0,checks=0;
    auto check=[&](bool ok,const char* message){++checks;if(!ok)++failures;out<<(ok?"PASS ":"FAIL ")<<message<<"\n";};
    GUID id{};if(FAILED(CoCreateGuid(&id)))return 12;
    wchar_t guid[64]{};StringFromGUID2(id,guid,64);
    std::wstring root=L"Software\\Silkstride\\Tests\\"+std::wstring(guid);
    HKEY key=nullptr;DWORD disposition=0;
    LONG created=RegCreateKeyExW(HKEY_CURRENT_USER,root.c_str(),0,nullptr,0,KEY_READ|KEY_WRITE,nullptr,&key,&disposition);
    if(key)RegCloseKey(key);
    if(created!=ERROR_SUCCESS||disposition!=REG_CREATED_NEW_KEY){out<<"FAIL: could not create an exclusive test key\n";return 12;}
    // Only this fresh GUID subtree is written and removed. No test registers an
    // actual logon task or changes the real app's first-run preference.
    std::wstring run=root+L"\\Run",preferences=root+L"\\Preferences";
    StartupRegistration startup(run,preferences);
    const std::wstring first=L"C:\\Program Files\\丝行\\Silkstride.exe";
    const std::wstring moved=L"D:\\Desktop Pets\\Silkstride.exe";
    auto storedCommand=[&] {
        wchar_t value[261]{};DWORD bytes=sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER,run.c_str(),L"Silkstride",RRF_RT_REG_SZ,nullptr,value,&bytes)==ERROR_SUCCESS?std::wstring(value):L"";
    };
    check(!startup.configured()&&!startup.enabled(),"Fresh profile has no startup choice or registration");
    check(startup.initialize(first)==ERROR_SUCCESS,"First launch enables startup");
    check(startup.configured()&&startup.enabled(),"First-run marker and registration are stored");
    check(storedCommand()==L"\""+first+L"\"","Unicode executable path with spaces is quoted exactly");
    check(startup.initialize(moved)==ERROR_SUCCESS&&storedCommand()==StartupRegistration::command(first),"Later launches do not silently rewrite the registration");
    check(startup.setEnabled(false)==ERROR_SUCCESS&&!startup.enabled(),"Tray disable removes the app's startup value");
    check(startup.configured(),"Disabling keeps the first-run marker");
    check(startup.initialize(first)==ERROR_SUCCESS&&!startup.enabled(),"Restart respects an explicit opt-out");
    check(startup.setEnabled(false)==ERROR_SUCCESS,"Disabling twice is harmless");
    check(startup.setEnabled(true,moved)==ERROR_SUCCESS&&storedCommand()==StartupRegistration::command(moved),"Re-enabling registers the chosen executable path");
    HKEY runHandle=nullptr;RegOpenKeyExW(HKEY_CURRENT_USER,run.c_str(),0,KEY_SET_VALUE,&runHandle);
    const wchar_t other[]=L"unrelated startup entry";
    if(runHandle){RegSetValueExW(runHandle,L"OtherApp",0,REG_SZ,reinterpret_cast<const BYTE*>(other),sizeof(other));RegCloseKey(runHandle);}
    check(startup.setEnabled(false)==ERROR_SUCCESS,"Disabling succeeds alongside another startup value");
    wchar_t preserved[64]{};DWORD bytes=sizeof(preserved);
    check(RegGetValueW(HKEY_CURRENT_USER,run.c_str(),L"OtherApp",RRF_RT_REG_SZ,nullptr,preserved,&bytes)==ERROR_SUCCESS
          &&std::wstring(preserved)==other,"Other applications' startup values remain untouched");
    check(startup.setEnabled(true,first)==ERROR_SUCCESS,"Can enable again after opting out");
    runHandle=nullptr;
    RegOpenKeyExW(HKEY_CURRENT_USER,run.c_str(),0,KEY_SET_VALUE,&runHandle);
    if(runHandle){RegDeleteValueW(runHandle,L"Silkstride");RegCloseKey(runHandle);}
    check(startup.initialize(first)==ERROR_SUCCESS&&!startup.enabled(),"Externally removed registration is not recreated on launch");
    check(startup.setEnabled(true,L"")!=ERROR_SUCCESS&&!startup.enabled(),"Empty executable path is rejected");
    check(startup.setEnabled(true,L"C:\\bad\"name.exe")!=ERROR_SUCCESS&&!startup.enabled(),"Embedded quotes cannot inject command arguments");
    check(startup.setEnabled(true,std::wstring(259,L'x'))!=ERROR_SUCCESS&&!startup.enabled(),"Commands exceeding the Run length limit are rejected");
    std::wstring boundary=L"C:\\"+std::wstring(255,L'x');
    check(StartupRegistration::command(boundary).size()==260,"The documented command-length boundary is accepted");
    check(!StartupRegistration::executable().empty(),"The real executable path can be resolved");
    LONG removed=RegDeleteTreeW(HKEY_CURRENT_USER,root.c_str());
    check(removed==ERROR_SUCCESS,"The owned test subtree is removed");
    out<<"Checks: "<<checks<<" Failures: "<<failures<<"\nActual Windows startup entries changed: 0\n"<<(failures?"FAIL":"PASS")<<"\n";
    return failures?12:0;
}
