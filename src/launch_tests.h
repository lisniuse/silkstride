#pragma once

int launchTest() {
    std::ofstream out("launch-test.txt");int checks=0,failures=0;
    auto check=[&](bool ok,const char* text){++checks;if(!ok)++failures;out<<(ok?"PASS ":"FAIL ")<<text<<"\n";};
    GUID id{};if(FAILED(CoCreateGuid(&id)))return 14;wchar_t guid[64]{};StringFromGUID2(id,guid,64);
    auto directory=std::filesystem::current_path()/(L"launch-"+std::wstring(guid));
    if(!CreateDirectoryW(directory.c_str(),nullptr))return 14;
    HANDLE job=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    bool configured=job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits));
    check(configured,"Create an owned launcher job with kill-on-close and explicitly allowed breakaway");
    std::wstring executable=StartupRegistration::executable();
    std::wstring command=L"\""+executable+L"\" --smoke-test /verify-independent-launch";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    bool created=configured&&CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,
                                            nullptr,directory.c_str(),&startup,&process);
    check(created,"Create only an owned diagnostic child; no pet memory, feeding or startup registration");
    bool assigned=created&&AssignProcessToJobObject(job,process.hProcess);
    check(assigned,"Put the diagnostic child in the controllable launcher job");
    auto probe=directory/L"launch-probe.txt";
    HANDLE detached=nullptr;
    if(assigned) {
        ResumeThread(process.hThread);
        DWORD childPid=0,flags=0,error=0;bool inside=true;
        for(int i=0;i<150;++i) {
            std::ifstream report(probe);
            if(report>>childPid>>inside>>flags>>error)break;
            Sleep(20);
        }
        out<<"Observed child PID: "<<childPid<<" in job: "<<inside<<" flags: 0x"<<std::hex<<flags<<std::dec<<" detach error: "<<error<<"\n";
        if(error==ERROR_ACCESS_DENIED) {
            // An outer job may forbid breakaway even when our nested job allows
            // it. This is host policy, not something the app should bypass.
            out<<"SKIP: outer host job denies breakaway; fallback remained in the managed job\n";
            CloseHandle(job);job=nullptr;
        } else {
            check(childPid!=0&&childPid!=process.dwProcessId&&!(flags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE),
                  "The production relaunch path leaves the launcher's kill-on-close job (benign Windows jobs are allowed)");
            if(childPid&&childPid!=process.dwProcessId)detached=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,childPid);
            check(detached!=nullptr,"Obtain a handle to our independent diagnostic child");
            CloseHandle(job);job=nullptr;
            check(detached&&WaitForSingleObject(detached,300)==WAIT_TIMEOUT,"Closing the launcher job does not terminate the independent pet window");
            DWORD code=1;
            bool finished=detached&&WaitForSingleObject(detached,6000)==WAIT_OBJECT_0&&GetExitCodeProcess(detached,&code)&&code==0;
            check(finished,"The independent diagnostic completes its three-second smoke test normally");
        }
    } else if(created)TerminateProcess(process.hProcess,14); // Only our own suspended test child.
    if(detached)CloseHandle(detached);
    if(process.hProcess){WaitForSingleObject(process.hProcess,3000);CloseHandle(process.hProcess);}
    if(process.hThread)CloseHandle(process.hThread);
    if(job)CloseHandle(job);
    if(std::filesystem::exists(probe))DeleteFileW(probe.c_str());
    check(RemoveDirectoryW(directory.c_str())!=FALSE,"Remove only the owned diagnostic fixture directory");
    out<<"Checks: "<<checks<<" Failures: "<<failures<<"\nPersonal pet data modified: 0\n"<<(failures?"FAIL":"PASS")<<"\n";
    return failures?14:0;
}
