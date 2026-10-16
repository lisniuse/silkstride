#pragma once

int loggingTest() {
    std::ofstream out("logging-test.txt");int checks=0,failures=0;
    auto check=[&](bool ok,const char* text){++checks;if(!ok)++failures;out<<(ok?"PASS ":"FAIL ")<<text<<"\n";};
    GUID id{};if(FAILED(CoCreateGuid(&id)))return 13;wchar_t guid[64]{};StringFromGUID2(id,guid,64);
    auto directory=std::filesystem::current_path()/(L"logging-"+std::wstring(guid));
    if(!CreateDirectoryW(directory.c_str(),nullptr)){out<<"FAIL: could not create an exclusive log-test directory\n";return 13;}
    auto path=directory/L"runtime.log";
    auto read=[&]{std::ifstream input(path);return std::string(std::istreambuf_iterator<char>(input),{});};
    check(RuntimeLog::open(directory.wstring()),"Open a test log outside personal app data");
    RuntimeLog::write("event=test_message value=%d",42);
    EXCEPTION_RECORD record{};record.ExceptionCode=0xC0000005;record.ExceptionAddress=reinterpret_cast<void*>(0x1234);
    EXCEPTION_POINTERS pointers{&record,nullptr};RuntimeLog::recordException(&pointers);
    RuntimeLog::finish(7);auto text=read();
    check(text.find("event=test_message value=42")!=std::string::npos,"Formatted events are flushed to disk");
    check(text.find("Z pid=")!=std::string::npos&&text.find(" tid=")!=std::string::npos,"Records carry UTC time, process ID and thread ID");
    check(text.find("code=0xC0000005")!=std::string::npos&&text.find("address=")!=std::string::npos,"Crash record includes exception code and address without terminating the test");
    check(text.find("event=process_exit code=7")!=std::string::npos,"Normal exit records its result");
    check(RuntimeLog::open(directory.wstring()),"Reopen a completed log session");RuntimeLog::finish(0);
    check(read().find("previous_session_incomplete")==std::string::npos,"Normal exit is not reported as an incomplete session");
    {std::ofstream append(path,std::ios::app);append<<"event=process_start\n";}
    check(RuntimeLog::open(directory.wstring()),"Reopen a simulated incomplete session");RuntimeLog::finish(0);
    check(read().find("previous_session_incomplete")!=std::string::npos,"Missing final exit is flagged on the next launch");
    {std::ofstream large(path,std::ios::app);large<<std::string(1024*1024,'x');}
    check(RuntimeLog::open(directory.wstring()),"Open after the rotation threshold");RuntimeLog::finish(0);
    check(std::filesystem::exists(directory/L"runtime.previous.log")&&std::filesystem::file_size(path)<4096,"Large log rotates while preserving the preceding file");
    check(DeleteFileW(path.c_str())&&DeleteFileW((directory/L"runtime.previous.log").c_str())
          &&RemoveDirectoryW(directory.c_str()),"Only owned log-test fixtures are removed after testing");
    out<<"Checks: "<<checks<<" Failures: "<<failures<<"\nPersonal runtime logs modified: 0\n"<<(failures?"FAIL":"PASS")<<"\n";
    return failures?13:0;
}
