#pragma once
#include <windows.h>
#include <shlobj.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

class RuntimeLog {
    inline static HANDLE file=INVALID_HANDLE_VALUE;
    static bool previousIncomplete(const std::wstring& path) {
        HANDLE previous=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
        if(previous==INVALID_HANDLE_VALUE)return false;
        LARGE_INTEGER size{};bool incomplete=false;
        if(GetFileSizeEx(previous,&size)&&size.QuadPart>0) {
            LARGE_INTEGER offset{};offset.QuadPart=size.QuadPart>2048?size.QuadPart-2048:0;
            SetFilePointerEx(previous,offset,nullptr,FILE_BEGIN);
            char tail[2049]{};DWORD bytes=0;
            if(ReadFile(previous,tail,2048,&bytes,nullptr)&&bytes) {
                std::string text(tail,bytes);auto last=text.find_last_not_of("\r\n");
                if(last!=std::string::npos) {
                    text.resize(last+1);auto line=text.find_last_of('\n');
                    incomplete=text.substr(line==std::string::npos?0:line+1).find("event=process_exit ")==std::string::npos;
                }
            }
        }
        CloseHandle(previous);return incomplete;
    }
public:
    static bool open(const std::wstring& directory) {
        if(file!=INVALID_HANDLE_VALUE)return false;
        CreateDirectoryW(directory.c_str(),nullptr);
        std::wstring path=directory+L"\\runtime.log";
        bool incomplete=previousIncomplete(path);
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if(GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data)
           &&(data.nFileSizeHigh||data.nFileSizeLow>=1024*1024))
            MoveFileExW(path.c_str(),(directory+L"\\runtime.previous.log").c_str(),MOVEFILE_REPLACE_EXISTING);
        file=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        write("event=process_start");
        if(incomplete)write("event=previous_session_incomplete note=possible_crash_or_external_termination");
        return true;
    }
    static void write(const char* format,...) noexcept {
        if(file==INVALID_HANDLE_VALUE)return;
        char message[1200]{};va_list args;va_start(args,format);
        _vsnprintf_s(message,sizeof(message),_TRUNCATE,format,args);va_end(args);
        SYSTEMTIME now{};GetSystemTime(&now);char line[1536]{};
        int length=_snprintf_s(line,sizeof(line),_TRUNCATE,
            "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ pid=%lu tid=%lu %s\r\n",
            now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,
            GetCurrentProcessId(),GetCurrentThreadId(),message);
        if(length>0){DWORD bytes=0;WriteFile(file,line,DWORD(length),&bytes,nullptr);FlushFileBuffers(file);}
    }
    static void recordException(EXCEPTION_POINTERS* exception) noexcept {
        if(exception&&exception->ExceptionRecord)
            write("event=unhandled_exception code=0x%08lX address=%p",exception->ExceptionRecord->ExceptionCode,exception->ExceptionRecord->ExceptionAddress);
        else write("event=unhandled_exception details=unavailable");
    }
    static LONG WINAPI onException(EXCEPTION_POINTERS* exception) noexcept {
        recordException(exception);return EXCEPTION_CONTINUE_SEARCH;
    }
    static void start() {
        wchar_t folder[MAX_PATH]{};
        if(SUCCEEDED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,0,folder))
           &&open(std::wstring(folder)+L"\\NeonSpider")) {
            write("event=version value=0.3.0");
            SetUnhandledExceptionFilter(onException);
            std::set_terminate([]()noexcept{write("event=cpp_terminate");std::abort();});
        }
    }
    static void finish(int code) noexcept {
        if(file==INVALID_HANDLE_VALUE)return;
        write("event=process_exit code=%d",code);CloseHandle(file);file=INVALID_HANDLE_VALUE;
    }
};
