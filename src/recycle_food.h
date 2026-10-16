#pragma once
#include <exdisp.h>
#include <shlguid.h>
#include <servprov.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>
#include <unordered_set>
#include <shlwapi.h>
#include <UIAutomation.h>

using Microsoft::WRL::ComPtr;

struct RecycleFoodView {
    HWND window=nullptr;
    RECT area{};
    RECT itemArea{};
    V point{};
    std::wstring token,name;
    bool visible=false;
    unsigned receipt=0;
    bool consumed=false;
    std::wstring receiptName;
    bool filesSkipped=false;
};

// Only the Shell's actual Recycle Bin namespace is enumerated. No arbitrary
// filesystem paths, recursive deletion, empty-bin operation or file execution.
class RecycleShell {
    PIDLIST_ABSOLUTE root=nullptr;
public:
    RecycleShell() { SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder,0,nullptr,&root); }
    ~RecycleShell() { CoTaskMemFree(root); }
    struct Item { ComPtr<IShellItem> shell; std::wstring token,name; };
    static std::wstring identity(PCIDLIST_ABSOLUTE pidl) {
        const BYTE* data=reinterpret_cast<const BYTE*>(pidl);
        UINT bytes=ILGetSize(pidl); std::wstring value; value.reserve(bytes*2);
        for(UINT i=0;i<bytes;++i) { value+=L"0123456789abcdef"[data[i]>>4];value+=L"0123456789abcdef"[data[i]&15]; }
        return value;
    }
    bool isDirectFile(IShellItem* item) const {
        if(!root||!item) return false;
        SFGAOF attrs=0;
        if(FAILED(item->GetAttributes(SFGAO_FOLDER,&attrs)) || (attrs&SFGAO_FOLDER)) return false;
        PIDLIST_ABSOLUTE pidl=nullptr;
        if(FAILED(SHGetIDListFromObject(item,&pidl))) return false;
        bool result=ILIsParent(root,pidl,TRUE)!=FALSE;
        CoTaskMemFree(pidl);
        if(result) return true;
        // Shell items from another volume can canonicalize to a filesystem PIDL
        // ($RECYCLE.BIN/$R...) instead of retaining the virtual-bin parent. Verify
        // identity against actual direct children of the bin, never a path prefix.
        ComPtr<IShellFolder> desktop,bin;ComPtr<IEnumIDList> children;
        if(FAILED(SHGetDesktopFolder(&desktop)) || FAILED(desktop->BindToObject(root,nullptr,IID_PPV_ARGS(&bin)))
           || FAILED(bin->EnumObjects(nullptr,SHCONTF_NONFOLDERS,&children)) || !children) return false;
        PITEMID_CHILD child=nullptr;
        while(children->Next(1,&child,nullptr)==S_OK) {
            PIDLIST_ABSOLUTE full=ILCombine(root,child);CoTaskMemFree(child);child=nullptr;
            ComPtr<IShellItem> candidate;
            HRESULT hr=full?SHCreateItemFromIDList(full,IID_PPV_ARGS(&candidate)):E_FAIL;CoTaskMemFree(full);
            int order=1;
            if(SUCCEEDED(hr) && SUCCEEDED(item->Compare(candidate.Get(),SICHINT_CANONICAL,&order)) && order==0) return true;
        }
        return false;
    }
    RecycleFoodView visibleView(ComPtr<IFolderView>* activeFolder=nullptr,HWND* contentWindow=nullptr,HWND wantedWindow=nullptr) {
        RecycleFoodView result;
        if(!root) return result;
        ComPtr<IShellWindows> windows;
        if(FAILED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_ALL,IID_PPV_ARGS(&windows)))) return result;
        long count=0; windows->get_Count(&count);
        for(long index=0;index<count;++index) {
            VARIANT number{};number.vt=VT_I4;number.lVal=index;
            ComPtr<IDispatch> dispatch;
            if(FAILED(windows->Item(number,&dispatch)) || !dispatch) continue;
            ComPtr<IServiceProvider> services;ComPtr<IShellBrowser> browser;ComPtr<IShellView> view;
            ComPtr<IFolderView> folderView;ComPtr<IPersistFolder2> folder;
            if(FAILED(dispatch.As(&services)) || FAILED(services->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser)))
               || FAILED(browser->QueryActiveShellView(&view)) || FAILED(view.As(&folderView))
               || FAILED(folderView->GetFolder(IID_PPV_ARGS(&folder)))) continue;
            PIDLIST_ABSOLUTE location=nullptr;
            if(FAILED(folder->GetCurFolder(&location))) continue;
            bool correct=ILIsEqual(location,root)!=FALSE;CoTaskMemFree(location);
            if(!correct) continue;
            HWND top=nullptr,content=nullptr;
            browser->GetWindow(&top);view->GetWindow(&content);
            if(wantedWindow && top!=wantedWindow) continue;
            if(!top||!content||!IsWindowVisible(top)||!IsWindowVisible(content)||IsIconic(top)) continue;
            RECT area{};GetClientRect(content,&area);
            MapWindowPoints(content,nullptr,reinterpret_cast<POINT*>(&area),2);
            if(area.right-area.left<100 || area.bottom-area.top<100) continue;
            // A covered, minimized or inactive tab is not something it can see.
            // Pick an exposed point in the actual folder view, never a title match.
            for(float fy:{.5f,.3f,.7f}) for(float fx:{.5f,.3f,.7f}) {
                POINT p{LONG(area.left+(area.right-area.left)*fx),LONG(area.top+(area.bottom-area.top)*fy)};
                HWND hit=WindowFromPoint(p);
                if(hit && GetAncestor(hit,GA_ROOT)==GetAncestor(top,GA_ROOT)) {
                    result.window=top;result.area=area;result.point={float(p.x),float(p.y)};result.visible=true;
                    if(activeFolder)*activeFolder=folderView;if(contentWindow)*contentWindow=content;return result;
                }
            }
        }
        return result;
    }
    Item findFile(const std::wstring& exactToken=L"",const std::unordered_set<std::wstring>* skipped=nullptr,const std::wstring& requiredNamePrefix=L"",std::ostream* trace=nullptr,IShellItem* equivalent=nullptr) {
        Item result;
        if(!root) return result;
        ComPtr<IShellFolder> desktop,bin;ComPtr<IEnumIDList> items;
        if(FAILED(SHGetDesktopFolder(&desktop)) || FAILED(desktop->BindToObject(root,nullptr,IID_PPV_ARGS(&bin)))
           || FAILED(bin->EnumObjects(nullptr,SHCONTF_NONFOLDERS,&items)) || !items) return result;
        PITEMID_CHILD child=nullptr;unsigned seen=0;
        while(items->Next(1,&child,nullptr)==S_OK) {
            ++seen;
            std::wstring originalName;STRRET display{};PWSTR original=nullptr;
            if(SUCCEEDED(bin->GetDisplayNameOf(child,SHGDN_NORMAL,&display)) && SUCCEEDED(StrRetToStrW(&display,child,&original))) {
                originalName=original;CoTaskMemFree(original);
            }
            PIDLIST_ABSOLUTE full=ILCombine(root,child);CoTaskMemFree(child);child=nullptr;
            if(!full) continue;
            std::wstring token=identity(full);
            if(skipped && skipped->count(token)) {CoTaskMemFree(full);continue;}
            if(!exactToken.empty() && token!=exactToken) {CoTaskMemFree(full);continue;}
            ComPtr<IShellItem> item;HRESULT hr=SHCreateItemFromIDList(full,IID_PPV_ARGS(&item));CoTaskMemFree(full);
            SFGAOF attrs=0;
            // Membership is established by the direct-child enumeration above.
            HRESULT attributes=SUCCEEDED(hr)?item->GetAttributes(SFGAO_FOLDER,&attrs):hr;
            if(trace && originalName.find(L"NeonSpiderCanary-")!=std::wstring::npos)
                *trace<<"Owned disposable item found; attributes="<<unsigned(attrs)<<"\n";
            if(FAILED(hr)||FAILED(attributes)||(attrs&SFGAO_FOLDER)) continue;
            if(equivalent) {int order=1;if(FAILED(item->Compare(equivalent,SICHINT_CANONICAL,&order))||order!=0)continue;}
            PWSTR name=nullptr;
            if(FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY,&name))) continue;
            // The bin's SHGDN_NORMAL label can include the original full path.
            std::wstring label=originalName.empty()?name:PathFindFileNameW(originalName.c_str());CoTaskMemFree(name);
            if(!requiredNamePrefix.empty() && label.rfind(requiredNamePrefix,0)!=0) continue;
            result.shell=item;result.token=std::move(token);result.name=std::move(label);return result;
        }
        if(trace) *trace<<"Direct non-folder items enumerated: "<<seen<<"\n";
        return result;
    }
    static bool sameLabel(const std::wstring& file,const std::wstring& label) {
        if(_wcsicmp(file.c_str(),label.c_str())==0)return true;
        size_t dot=file.find_last_of(L'.');
        return dot!=std::wstring::npos && _wcsicmp(file.substr(0,dot).c_str(),label.c_str())==0;
    }
    static POINT textAnchor(const RECT& rectangle,const std::wstring& label) {
        LONG width=std::max(2L,rectangle.right-rectangle.left-4);
        HDC dc=GetDC(nullptr);LOGFONTW description{};
        description.lfHeight=-std::max(9L,rectangle.bottom-rectangle.top-4);wcscpy_s(description.lfFaceName,L"Segoe UI");
        HFONT font=CreateFontIndirectW(&description);HGDIOBJ old=font?SelectObject(dc,font):nullptr;
        SIZE extent{};
        if(GetTextExtentPoint32W(dc,label.c_str(),int(label.size()),&extent))width=std::min(width,std::max(2L,extent.cx));
        if(old)SelectObject(dc,old);if(font)DeleteObject(font);ReleaseDC(nullptr,dc);
        return {rectangle.left+std::max(2L,std::min(45L,width/2)),(rectangle.top+rectangle.bottom)/2};
    }
    RecycleFoodView visibleFile(const std::unordered_set<std::wstring>* skipped=nullptr,
                                const std::wstring& preferred=L"",HWND wantedWindow=nullptr,bool strict=false,std::ostream* trace=nullptr) {
        ComPtr<IFolderView> view;HWND content=nullptr;
        auto result=visibleView(&view,&content,wantedWindow);
        if(!result.visible)return result;
        ComPtr<IUIAutomation> automation;ComPtr<IUIAutomationElement> rootElement;
        ComPtr<IUIAutomationCondition> listCondition,dataCondition,rowCondition;
        ComPtr<IUIAutomationElementArray> rows;
        if(FAILED(CoCreateInstance(__uuidof(CUIAutomation),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation)))
           || FAILED(automation->ElementFromHandle(content,&rootElement))) return result;
        VARIANT type{};type.vt=VT_I4;type.lVal=UIA_ListItemControlTypeId;
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&listCondition);
        type.lVal=UIA_DataItemControlTypeId;
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId,type,&dataCondition);
        if(!listCondition||!dataCondition||FAILED(automation->CreateOrCondition(listCondition.Get(),dataCondition.Get(),&rowCondition))
           ||FAILED(rootElement->FindAll(TreeScope_Descendants,rowCondition.Get(),&rows)))return result;
        struct Row {std::wstring label;RECT rectangle;ComPtr<IUIAutomationElement> element;};
        std::vector<Row> visibleRows;int rowCount=0;rows->get_Length(&rowCount);
        for(int i=0;i<rowCount;++i) {
            ComPtr<IUIAutomationElement> row;BOOL offscreen=TRUE;RECT rectangle{};BSTR name=nullptr;
            if(FAILED(rows->GetElement(i,&row))||FAILED(row->get_CurrentIsOffscreen(&offscreen))||offscreen
               ||FAILED(row->get_CurrentBoundingRectangle(&rectangle))||FAILED(row->get_CurrentName(&name)))continue;
            std::wstring label=name?name:L"";SysFreeString(name);
            RECT intersection{};
            if(label.empty() || !IntersectRect(&intersection,&rectangle,&result.area) || rectangle.bottom-rectangle.top<3)continue;
            visibleRows.push_back({std::move(label),rectangle,row});
        }
        ComPtr<IShellFolder> parent;if(FAILED(view->GetFolder(IID_PPV_ARGS(&parent))))return result;
        if(trace) {
            *trace<<"Visible UI rows: "<<visibleRows.size()<<"\n";
            for(const auto& row:visibleRows)if(row.label.find(L"NeonSpiderCanary-")!=std::wstring::npos)
                *trace<<"Owned canary row visible at "<<row.rectangle.left<<","<<row.rectangle.top<<"\n";
        }
        struct Candidate {ComPtr<IShellItem> shell;std::wstring label;POINT position{};bool positioned=false;};
        std::vector<Candidate> candidates;int count=0;view->ItemCount(SVGIO_ALLVIEW,&count);
        for(int i=0;i<count;++i) {
            PITEMID_CHILD child=nullptr;if(FAILED(view->Item(i,&child))||!child)continue;
            Candidate candidate;candidate.positioned=SUCCEEDED(view->GetItemPosition(child,&candidate.position));
            if(candidate.positioned)ClientToScreen(content,&candidate.position);
            STRRET text{};PWSTR label=nullptr;
            if(SUCCEEDED(parent->GetDisplayNameOf(child,SHGDN_NORMAL,&text))&&SUCCEEDED(StrRetToStrW(&text,child,&label))) {
                candidate.label=PathFindFileNameW(label);CoTaskMemFree(label);
            }
            PIDLIST_ABSOLUTE full=ILCombine(root,child);CoTaskMemFree(child);
            HRESULT hr=full?SHCreateItemFromIDList(full,IID_PPV_ARGS(&candidate.shell)):E_FAIL;CoTaskMemFree(full);
            SFGAOF attrs=0;
            if(FAILED(hr)||FAILED(candidate.shell->GetAttributes(SFGAO_FOLDER,&attrs))||(attrs&SFGAO_FOLDER)||candidate.label.empty())continue;
            candidates.push_back(std::move(candidate));
        }
        // Use the displayed order. Names only locate a row; the exact Shell
        // identity still selects the file. Ambiguous duplicate names are skipped
        // unless the Shell's item position disambiguates their screen rows.
        for(int pass=0;pass<(preferred.empty()?1:2);++pass) for(const auto& candidate:candidates) {
            for(const auto& row:visibleRows) {
                if(!sameLabel(candidate.label,row.label))continue;
                int duplicates=0;
                for(const auto& other:candidates)if(sameLabel(other.label,row.label))++duplicates;
                if(duplicates>1 && (!candidate.positioned || candidate.position.y<row.rectangle.top-3 || candidate.position.y>row.rectangle.bottom+3))continue;
                auto canonical=findFile(L"",nullptr,L"",nullptr,candidate.shell.Get());
                if(trace && candidate.label.find(L"NeonSpiderCanary-")!=std::wstring::npos)
                    *trace<<"Owned row canonical match="<<bool(canonical.shell)<<" requested identity="<<(canonical.token==preferred)<<"\n";
                if(!canonical.shell || (skipped&&skipped->count(canonical.token)))continue;
                if(!preferred.empty() && ((pass==0 && canonical.token!=preferred)||(pass==1 && strict)))continue;
                RECT labelRect=row.rectangle;
                VARIANT labelName{};labelName.vt=VT_BSTR;labelName.bstrVal=SysAllocString(row.label.c_str());
                ComPtr<IUIAutomationCondition> nameCondition;ComPtr<IUIAutomationElement> nameElement;
                automation->CreatePropertyCondition(UIA_NamePropertyId,labelName,&nameCondition);VariantClear(&labelName);
                if(nameCondition && SUCCEEDED(row.element->FindFirst(TreeScope_Descendants,nameCondition.Get(),&nameElement)) && nameElement) {
                    RECT textRect{};BOOL hidden=TRUE;
                    if(SUCCEEDED(nameElement->get_CurrentBoundingRectangle(&textRect)) && SUCCEEDED(nameElement->get_CurrentIsOffscreen(&hidden))
                       &&!hidden&&textRect.right>textRect.left&&textRect.bottom>textRect.top)labelRect=textRect;
                }
                if(labelRect.left==row.rectangle.left && labelRect.right==row.rectangle.right) {
                    // Details view exposes filename cells as Edit elements named
                    // after the column ("Name"/"名称"), with the filename in Value.
                    ComPtr<IUIAutomationCondition> any;ComPtr<IUIAutomationElementArray> cells;
                    automation->CreateTrueCondition(&any);RECT firstCell{};bool found=false;
                    if(any && SUCCEEDED(row.element->FindAll(TreeScope_Descendants,any.Get(),&cells))) {
                        int total=0;cells->get_Length(&total);
                        for(int j=0;j<total;++j) {
                            ComPtr<IUIAutomationElement> cell;RECT r{};CONTROLTYPEID kind=0;
                            if(FAILED(cells->GetElement(j,&cell))||FAILED(cell->get_CurrentBoundingRectangle(&r))
                               ||FAILED(cell->get_CurrentControlType(&kind))||r.right<=r.left||r.bottom<=r.top
                               ||(kind!=UIA_EditControlTypeId&&kind!=UIA_TextControlTypeId))continue;
                            if(!found||r.left<firstCell.left){firstCell=r;found=true;}
                            ComPtr<IUIAutomationValuePattern> value;BSTR text=nullptr;
                            if(SUCCEEDED(cell->GetCurrentPatternAs(UIA_ValuePatternId,IID_PPV_ARGS(&value)))
                               &&value&&SUCCEEDED(value->get_CurrentValue(&text))) {
                                bool matches=text&&sameLabel(candidate.label,text);SysFreeString(text);
                                if(matches){firstCell=r;found=true;break;}
                            }
                        }
                    }
                    if(found)labelRect=firstCell;
                }
                if(trace) {
                    ComPtr<IUIAutomationCondition> any;ComPtr<IUIAutomationElementArray> children;
                    automation->CreateTrueCondition(&any);
                    if(any && SUCCEEDED(row.element->FindAll(TreeScope_Descendants,any.Get(),&children))) {
                        int total=0;children->get_Length(&total);*trace<<"Chosen row descendant count: "<<total<<"\n";
                        for(int j=0;j<std::min(total,10);++j) {
                            ComPtr<IUIAutomationElement> child;children->GetElement(j,&child);BSTR name=nullptr;RECT r{};CONTROLTYPEID kind=0;
                            child->get_CurrentName(&name);child->get_CurrentBoundingRectangle(&r);child->get_CurrentControlType(&kind);
                            std::wstring value=name?name:L"";SysFreeString(name);
                            int bytes=WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),nullptr,0,nullptr,nullptr);
                            std::string utf8(bytes,' ');WideCharToMultiByte(CP_UTF8,0,value.c_str(),int(value.size()),utf8.data(),bytes,nullptr,nullptr);
                            *trace<<"Child type "<<kind<<" name="<<utf8<<" rect="<<r.left<<","<<r.top<<","<<r.right<<","<<r.bottom<<"\n";
                        }
                    }
                }
                POINT point=textAnchor(labelRect,row.label);
                if(point.x<result.area.left||point.x>=result.area.right||point.y<result.area.top||point.y>=result.area.bottom)continue;
                HWND hit=WindowFromPoint(point);
                if(!hit||GetAncestor(hit,GA_ROOT)!=GetAncestor(result.window,GA_ROOT))continue;
                result.token=canonical.token;result.name=canonical.name;result.itemArea=labelRect;
                result.point={float(point.x),float(point.y)};return result;
            }
        }
        return result;
    }
    bool revealForCanary(const std::wstring& token,HWND window) {
        // Read-only selection/scroll helper used exclusively by the explicit
        // disposable-file integration test, never by autonomous feeding.
        auto target=findFile(token);if(!target.shell)return false;
        ComPtr<IFolderView> view;auto info=visibleView(&view,nullptr,window);
        if(!info.visible||!view)return false;
        int count=0;view->ItemCount(SVGIO_ALLVIEW,&count);
        for(int i=0;i<count;++i) {
            PITEMID_CHILD child=nullptr;if(FAILED(view->Item(i,&child))||!child)continue;
            PIDLIST_ABSOLUTE full=ILCombine(root,child);ComPtr<IShellItem> candidate;
            HRESULT hr=full?SHCreateItemFromIDList(full,IID_PPV_ARGS(&candidate)):E_FAIL;CoTaskMemFree(full);
            int order=1;
            bool matches=SUCCEEDED(hr)&&SUCCEEDED(candidate->Compare(target.shell.Get(),SICHINT_CANONICAL,&order))&&order==0;
            HRESULT selected=matches?view->SelectItem(i,SVSI_SELECT|SVSI_FOCUSED|SVSI_DESELECTOTHERS|SVSI_ENSUREVISIBLE):E_FAIL;
            CoTaskMemFree(child);if(matches)return SUCCEEDED(selected);
        }
        return false;
    }
    class DeleteReceipt final:public IFileOperationProgressSink {
        std::atomic<ULONG> references{1};
        std::function<bool()> permitted;
    public:
        bool deleted=false;
        HRESULT outcome=E_PENDING;
        explicit DeleteReceipt(std::function<bool()> guard):permitted(std::move(guard)){}
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** result) override {
            if(!result) return E_POINTER;*result=nullptr;
            if(iid==IID_IUnknown||iid==IID_IFileOperationProgressSink) {*result=static_cast<IFileOperationProgressSink*>(this);AddRef();return S_OK;}
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override {return ++references;}
        ULONG STDMETHODCALLTYPE Release() override {ULONG n=--references;if(!n)delete this;return n;}
        HRESULT STDMETHODCALLTYPE StartOperations() override {return S_OK;}
        HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override {return S_OK;}
        HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD,IShellItem*,LPCWSTR) override {return E_ABORT;}
        HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override {return S_OK;}
        HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR) override {return E_ABORT;}
        HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override {return S_OK;}
        HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR) override {return E_ABORT;}
        HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override {return S_OK;}
        HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD,IShellItem*) override {return permitted()?S_OK:E_ABORT;}
        HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD,IShellItem*,HRESULT result,IShellItem* recycled) override {
            outcome=result;deleted=SUCCEEDED(result) && recycled==nullptr;return S_OK;
        }
        HRESULT STDMETHODCALLTYPE PreNewItem(DWORD,IShellItem*,LPCWSTR) override {return E_ABORT;}
        HRESULT STDMETHODCALLTYPE PostNewItem(DWORD,IShellItem*,LPCWSTR,LPCWSTR,DWORD,HRESULT,IShellItem*) override {return S_OK;}
        HRESULT STDMETHODCALLTYPE UpdateProgress(UINT,UINT) override {return S_OK;}
        HRESULT STDMETHODCALLTYPE ResetTimer() override {return S_OK;}
        HRESULT STDMETHODCALLTYPE PauseTimer() override {return S_OK;}
        HRESULT STDMETHODCALLTYPE ResumeTimer() override {return S_OK;}
    };
    bool consumeOne(const std::wstring& token,HWND window,const std::function<bool()>& permit,HRESULT* outcome=nullptr) {
        auto guard=[&]() {auto view=visibleView();return permit() && view.visible && view.window==window;};
        if(token.empty()||!guard()) return false;
        Item candidate=findFile(token);
        if(!candidate.shell || !isDirectFile(candidate.shell.Get())) return false;
        auto guardFile=[&]() {return guard() && bool(findFile(token).shell) && isDirectFile(candidate.shell.Get());};
        ComPtr<IFileOperation> operation;
        if(FAILED(CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&operation)))) return false;
        // Explicitly omit ALLOWUNDO/RECYCLEONDELETE: an eaten recycled item is
        // permanently removed. The Shell handles its Recycle Bin metadata too.
        DWORD flags=FOF_SILENT|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOF_NO_CONNECTED_ELEMENTS|FOF_NORECURSION|FOFX_EARLYFAILURE;
        if(FAILED(operation->SetOperationFlags(flags))) return false;
        operation->SetOwnerWindow(window);
        ComPtr<DeleteReceipt> receipt;receipt.Attach(new DeleteReceipt(guardFile));
        if(FAILED(operation->DeleteItem(candidate.shell.Get(),receipt.Get())) || !guardFile()) return false;
        operation->PerformOperations();
        if(outcome) *outcome=receipt->outcome;
        // PostDeleteItem reports the actual per-file outcome, including whether
        // it was permanently removed. Queuing/overall HRESULTs are insufficient.
        return receipt->deleted && !findFile(token).shell;
    }
};

// Shell calls live on their own STA thread so Explorer/disk latency never blocks
// the animation or the low-level mouse hook. At most one named item is pending.
class RecycleFeeder {
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    RecycleFoodView snapshot;
    std::wstring pending,pendingName;
    std::unordered_set<std::wstring> blocked;
    HWND pendingWindow=nullptr;
    unsigned pendingEpoch=0;
    std::atomic_uint epoch{0};
    std::atomic<float> bodyX{0},bodyY{0},bodyScale{1};
    std::atomic_bool stopping{false},permitted{false},enabled{true};
    void run() {
        HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
        if(FAILED(com)) return;
        {
            RecycleShell shell;
            while(!stopping) {
                std::wstring request,name;HWND expected=nullptr;unsigned generation=0;
                {std::lock_guard<std::mutex> lock(mutex);request.swap(pending);name.swap(pendingName);expected=pendingWindow;generation=pendingEpoch;}
                if(!request.empty()) {
                    bool eaten=enabled && shell.consumeOne(request,expected,[&](){
                        if(stopping || !enabled || !permitted || epoch!=generation)return false;
                        auto target=shell.visibleFile(nullptr,request,expected,true);
                        return target.token==request && std::hypot(bodyX.load()-target.point.x,bodyY.load()-target.point.y)<=22*bodyScale.load();
                    });
                    std::lock_guard<std::mutex> lock(mutex);
                    snapshot.receipt++;snapshot.consumed=eaten;snapshot.receiptName=name;
                    if(!eaten && permitted && epoch==generation) blocked.insert(request);
                    permitted=false;
                }
                RecycleFoodView fresh;
                if(enabled) {
                    std::wstring preferred;{std::lock_guard<std::mutex> lock(mutex);preferred=snapshot.token;}
                    fresh=shell.visibleFile(&blocked,preferred);
                    fresh.filesSkipped=fresh.visible && fresh.token.empty()&&!blocked.empty();
                }
                if(!fresh.visible) blocked.clear();
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    fresh.receipt=snapshot.receipt;fresh.consumed=snapshot.consumed;fresh.receiptName=snapshot.receiptName;snapshot=std::move(fresh);
                    wake.wait_for(lock,std::chrono::milliseconds(500),[&](){return stopping || !pending.empty();});
                }
                MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
            }
        }
        CoUninitialize();
    }
public:
    void start() {if(!worker.joinable()) worker=std::thread([this](){run();});}
    void setBody(V body,float scale) {bodyX=body.x;bodyY=body.y;bodyScale=scale;}
    RecycleFoodView read() {std::lock_guard<std::mutex> lock(mutex);return snapshot;}
    void allow(bool value) {if(!value) {if(permitted.exchange(false))++epoch;} else permitted=true;}
    void setEnabled(bool value) {enabled=value;if(!value)allow(false);wake.notify_one();}
    bool submit(const std::wstring& token,HWND window,const std::wstring& name) {
        std::lock_guard<std::mutex> lock(mutex);
        if(token.empty()||!pending.empty()||!enabled||!permitted) return false;
        pending=token;pendingName=name;pendingWindow=window;pendingEpoch=epoch;wake.notify_one();return true;
    }
    void stop() {allow(false);enabled=false;stopping=true;wake.notify_one();if(worker.joinable())worker.join();}
    ~RecycleFeeder() {stop();}
};
