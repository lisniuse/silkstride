#pragma once
#include <windows.h>
#include <string>
#include <utility>

// Per-user logon registration. Diagnostic tests use private GUID-named keys,
// never the user's real Run key or preference marker.
class StartupRegistration {
    std::wstring runKey,preferencesKey;
    LONG markConfigured() const {
        HKEY key=nullptr;
        LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,preferencesKey.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
        if(result!=ERROR_SUCCESS)return result;
        DWORD configured=1;
        result=RegSetValueExW(key,L"StartupConfigured",0,REG_DWORD,reinterpret_cast<const BYTE*>(&configured),sizeof(configured));
        RegCloseKey(key);return result;
    }
public:
    explicit StartupRegistration(
        std::wstring run=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        std::wstring preferences=L"Software\\Silkstride")
        :runKey(std::move(run)),preferencesKey(std::move(preferences)){}
    static std::wstring executable() {
        std::wstring path(32768,L'\0');
        DWORD length=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
        if(!length||length>=path.size())return L"";
        path.resize(length);return path;
    }
    static std::wstring command(const std::wstring& path) {
        // Run commands have a documented 260-character limit. Always quote the
        // executable, including paths containing spaces or non-ASCII characters.
        if(path.empty()||path.find(L'"')!=std::wstring::npos||path.size()+2>260)return L"";
        return L"\""+path+L"\"";
    }
    bool configured() const {
        DWORD value=0,bytes=sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER,preferencesKey.c_str(),L"StartupConfigured",RRF_RT_REG_DWORD,
                            nullptr,&value,&bytes)==ERROR_SUCCESS && value==1;
    }
    bool enabled() const {
        wchar_t value[261]{};DWORD bytes=sizeof(value);
        return RegGetValueW(HKEY_CURRENT_USER,runKey.c_str(),L"Silkstride",RRF_RT_REG_SZ,
                            nullptr,value,&bytes)==ERROR_SUCCESS && value[0]!=L'\0';
    }
    LONG setEnabled(bool enable,const std::wstring& path=executable()) const {
        std::wstring launch=enable?command(path):L"";
        if(enable&&launch.empty())return ERROR_INVALID_PARAMETER;
        // Mark the first setup attempt before changing Run. Later launches must
        // not silently re-enable a choice the user disabled (including in Windows).
        LONG result=markConfigured();if(result!=ERROR_SUCCESS)return result;
        HKEY key=nullptr;
        result=RegCreateKeyExW(HKEY_CURRENT_USER,runKey.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
        if(result!=ERROR_SUCCESS)return result;
        if(enable)result=RegSetValueExW(key,L"Silkstride",0,REG_SZ,reinterpret_cast<const BYTE*>(launch.c_str()),DWORD((launch.size()+1)*sizeof(wchar_t)));
        else {result=RegDeleteValueW(key,L"Silkstride");if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;}
        RegCloseKey(key);return result;
    }
    LONG initialize(const std::wstring& path=executable()) const {
        if(configured())return ERROR_SUCCESS;
        return setEnabled(true,path);
    }
};
