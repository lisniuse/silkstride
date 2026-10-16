#pragma once
#include <windows.h>
#include <string>
#include "startup.h"

class ProcessLaunch {
public:
    struct Job {bool inside=false;DWORD flags=0,error=0;};
    struct Attempt {bool relaunched=false;DWORD error=0;};
    static Job currentJob() {
        Job result;BOOL inside=FALSE;
        if(!IsProcessInJob(GetCurrentProcess(),nullptr,&inside)){result.error=GetLastError();return result;}
        result.inside=inside!=FALSE;
        if(result.inside) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            if(QueryInformationJobObject(nullptr,JobObjectExtendedLimitInformation,&info,sizeof(info),nullptr))
                result.flags=info.BasicLimitInformation.LimitFlags;
            else result.error=GetLastError();
        }
        return result;
    }
    static Attempt detachIfNeeded(const wchar_t* arguments,bool enabled) {
        Attempt result;auto job=currentJob();
        // Only use the host's explicitly allowed breakaway mechanism. Ordinary
        // desktop launches and synchronous diagnostic processes are unchanged.
        if(!enabled||wcsstr(arguments,L"/independent-from=")||!job.inside
           ||!(job.flags&JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE)||!(job.flags&JOB_OBJECT_LIMIT_BREAKAWAY_OK))return result;
        std::wstring executable=StartupRegistration::executable();
        if(executable.empty()){result.error=ERROR_FILE_NOT_FOUND;return result;}
        std::wstring command=L"\""+executable+L"\" "+arguments+L" /independent-from="+std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_BREAKAWAY_FROM_JOB,
                           nullptr,nullptr,&startup,&process)){result.error=GetLastError();return result;}
        CloseHandle(process.hThread);CloseHandle(process.hProcess);result.relaunched=true;return result;
    }
};
