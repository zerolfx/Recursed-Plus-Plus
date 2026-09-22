#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <process.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include "embedded_plugin.h"
#include "game_launch.h"
#include "save_store.h"
#include "steam_scan.h"
#include "support_folder.h"

// A window instead of a console, so the mod can be handed to someone who does not run
// PowerShell. It finds the game, checks it is the build this mod was measured against,
// starts it with the plugin loaded, and carries its own instructions so nothing extra has
// to be drawn over the game.
namespace {
enum : int { kPath=1001, kBrowse, kDetect, kLaunch, kImport, kFolder, kHelp, kStatus, kGameLabel, kTabs, kIsolated, kAdvancedHelp, kNotices };
// Scanning, launching, and copying saves all touch disks that can be asleep, missing, or being
// scanned by antivirus, so none of them runs on the thread that has to keep painting.
enum : UINT { kStage=WM_APP+1, kFound, kLaunched, kSteamSaves, kImported };
HWND gWindow, gPath, gBrowse, gDetect, gLaunch, gImport, gFolder, gHelp, gStatus;
// Playing is the whole point; keeping progress somewhere else is a decision almost nobody has
// to make, so it lives on a second page instead of in front of every player.
HWND gTabs, gIsolated, gAdvancedHelp, gNotices;
int gTab=0;
HFONT gFont, gMonoFont;
std::wstring gGame;
int gDpi=96;
bool gBusy=false;

int scale(int n){return MulDiv(n,gDpi,96);}
void say(const wchar_t* text){SetWindowTextW(gStatus,text);}
// Hands a heap copy to the window thread; the handler owns it from there.
void post(UINT message,WPARAM w,const std::wstring& text){
    PostMessageW(gWindow,message,w,(LPARAM)_wcsdup(text.c_str()));
}

const wchar_t* kHelpText=
L"Controls\r\n"
L"\r\n"
L"  Hover a chest or flame    Preview the room it leads to.\r\n"
L"  Left click                Pin that preview in the game window.\r\n"
L"  Shift + left click        Open the preview in its own window.\r\n"
L"  O                         Move the preview between the game and that window.\r\n"
L"  Click a chest inside      Go one room deeper, up to eight.\r\n"
L"  Click a red or green      Look at the room outside this one.\r\n"
L"  flame inside\r\n"
L"  Mouse back and forward    Walk the rooms you have looked at, both ways.\r\n"
L"  Backspace                 Go back; at the first room this closes the preview.\r\n"
L"  Esc                       Close the preview without pausing the game.\r\n"
L"\r\n"
L"Depth is counted from the room you are standing in: 1 is inside the chest, 0 is\r\n"
L"where you are, -1 is outside.\r\n"
L"\r\n"
L"What this does to your game\r\n"
L"\r\n"
L"  It never writes to the game's folder. You play the progress you already have,\r\n"
L"  through Steam, with achievements and cloud saves as they always were.\r\n"
L"  Only the Steam Windows build this mod was measured against can be started.\r\n"
L"  Close the game to end the modded session; nothing is left running.\r\n"
L"\r\n"
L"If the game does not start, security software is the usual reason: the mod has to\r\n"
L"load itself into the game, which looks like what a cheat would do. Allow the two\r\n"
L"files that came with this launcher and try again.";

// The second page. Everything here changes where progress goes, which is a decision a player
// only makes on purpose.
const wchar_t* kAdvancedText=
L"Steam\r\n"
L"\r\n"
L"  This build plays through Steam like the game always has: your own progress,\r\n"
L"  your achievements, your cloud saves. When Steam is not running it keeps that\r\n"
L"  session's progress in the save folder below instead, so nothing is lost.\r\n"
L"\r\n"
L"  Play isolated keeps the game away from Steam entirely. Progress then lives in\r\n"
L"  the save folder alone, achievements are not unlocked, and the progress you\r\n"
L"  normally play is never written to.\r\n"
L"\r\n"
L"The save folder\r\n"
L"\r\n"
L"  Import from Steam copies what you have already done in Steam into that\r\n"
L"  folder, which is what an isolated run starts from. Whatever it replaces\r\n"
L"  is kept in a dated folder beside it. Close the game first, or the running\r\n"
L"  game writes its own progress back over the copy.\r\n"
L"  Save folder opens where all of this is kept.";

// Always the copy carried inside this executable, never one that happens to sit beside it: the
// download is a single file, and a file of that name in a Downloads folder is not ours to load
// into a game. Developers iterate through the console launcher, which does load a loose build.
std::wstring pluginPath(std::wstring& error){return peek::embeddedPlugin(error);}

// Shows what was chosen and whether it can actually be started, so the button never lies
// about what will happen.
void setGame(const std::wstring& exe,const wchar_t* note){
    gGame=exe;
    SetWindowTextW(gPath,exe.c_str());
    const bool ok=!exe.empty()&&peek::supportedGame(exe);
    EnableWindow(gLaunch,ok&&!gBusy);
    if(exe.empty())say(L"No copy of Recursed found. Choose Recursed.exe yourself.");
    else if(!ok)say(L"That is not the build this mod was measured against, so it cannot be started.");
    else say(note?note:L"Ready.");
}

unsigned __stdcall scanThread(void*){
    // A library folder on a sleeping external drive blocks, and an empty card reader would
    // otherwise pop the system-modal "There is no disk in the drive" box.
    SetThreadErrorMode(SEM_FAILCRITICALERRORS,nullptr);
    auto found=peek::findRecursed();
    std::wstring best;
    for(const auto& exe:found)if(peek::supportedGame(exe)){best=exe;break;}
    const bool exact=!best.empty();
    if(!exact&&!found.empty())best=found.front();
    post(kFound,exact?1:0,best);
    return 0;
}

struct LaunchJob {std::wstring exe;peek::SteamUse steam;};
unsigned __stdcall launchThread(void* raw){
    std::unique_ptr<LaunchJob> job((LaunchJob*)raw);
    std::wstring error;
    post(kStage,0,L"Unpacking the mod...");
    const auto plugin=pluginPath(error);
    if(plugin.empty()){post(kLaunched,0,error);return 0;}
    const auto id=peek::launchModded(job->exe,plugin,job->steam,false,error,
        [](const wchar_t* text){post(kStage,0,text);});
    post(kLaunched,id,error);
    return 0;
}

void startThread(unsigned(__stdcall* fn)(void*),void* arg){
    if(auto h=(HANDLE)_beginthreadex(nullptr,0,fn,arg,0,nullptr))CloseHandle(h);
}

// A running game holds its progress in memory and writes the whole file back when it next
// saves, so anything copied in underneath it would simply disappear again.
bool gameRunning(){
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W entry{sizeof entry};bool running=false;
    for(BOOL more=Process32FirstW(snapshot,&entry);more&&!running;more=Process32NextW(snapshot,&entry))
        running=_wcsicmp(entry.szExeFile,L"Recursed.exe")==0;
    CloseHandle(snapshot);
    return running;
}

// The modded run keeps its own progress, so the first thing most players want is what they
// already did in Steam. Both sides store one file per save slot under the same names, so
// bringing it over is a copy rather than a conversion.
unsigned __stdcall steamSaveThread(void*){
    SetThreadErrorMode(SEM_FAILCRITICALERRORS,nullptr);
    PostMessageW(gWindow,kSteamSaves,0,(LPARAM)new std::vector<peek::SteamSave>(peek::findSteamSaves()));
    return 0;
}

unsigned __stdcall importThread(void* raw){
    std::unique_ptr<peek::SteamSave> save((peek::SteamSave*)raw);
    std::wstring error;
    // The game can be started while the question is still on screen, and a copy made under a
    // running game is a copy it overwrites.
    if(gameRunning())post(kImported,0,L"Recursed started while this was being confirmed, and it would write its own progress back over the copy. Close the game and import again.");
    else {
        const int copied=peek::importSaves(save->folder,save->files,peek::saveFolder(),error);
        post(kImported,(WPARAM)copied,error);
    }
    return 0;
}

// Which account a save belongs to means little; when it was last played identifies it.
std::wstring playedOn(unsigned long long written){
    FILETIME stored{(DWORD)written,(DWORD)(written>>32)},local{};SYSTEMTIME date{};
    if(!written||!FileTimeToLocalFileTime(&stored,&local)||!FileTimeToSystemTime(&local,&date))return L"an unknown date";
    wchar_t text[32]{};swprintf_s(text,L"%04u-%02u-%02u",date.wYear,date.wMonth,date.wDay);
    return text;
}

void setBusy(bool busy){
    gBusy=busy;
    EnableWindow(gLaunch,!busy&&!gGame.empty()&&peek::supportedGame(gGame));
    EnableWindow(gDetect,!busy);EnableWindow(gBrowse,!busy);
    EnableWindow(gImport,!busy);EnableWindow(gFolder,!busy);EnableWindow(gIsolated,!busy);
}

bool isolated(){return SendMessageW(gIsolated,BM_GETCHECK,0,0)==BST_CHECKED;}

// One page at a time. The controls of the other page keep their state; they are only hidden.
void showTab(int tab){
    gTab=tab;
    for(HWND h:{gLaunch,gHelp})ShowWindow(h,tab==0?SW_SHOW:SW_HIDE);
    for(HWND h:{gIsolated,gImport,gFolder,gAdvancedHelp})ShowWindow(h,tab==1?SW_SHOW:SW_HIDE);
    ShowWindow(gNotices,tab==2?SW_SHOW:SW_HIDE);
}

void browse(){
    wchar_t buffer[MAX_PATH]{};
    if(!gGame.empty())wcsncpy_s(buffer,gGame.c_str(),_TRUNCATE);
    OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=gWindow;
    dialog.lpstrFilter=L"Recursed.exe\0Recursed.exe\0Programs\0*.exe\0";
    dialog.lpstrFile=buffer;dialog.nMaxFile=MAX_PATH;
    dialog.lpstrTitle=L"Where is Recursed.exe?";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog))setGame(buffer,nullptr);
}

HWND child(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,int id,HFONT font){
    HWND h=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,parent,(HMENU)(INT_PTR)id,nullptr,nullptr);
    SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
    return h;
}

void layout(HWND window){
    RECT r;GetClientRect(window,&r);
    const int pad=scale(14),row=scale(26),gap=scale(8),button=scale(96),tall=scale(34);
    int y=pad;
    SetWindowPos(GetDlgItem(window,kGameLabel),nullptr,pad,y,r.right-2*pad,row,SWP_NOZORDER);
    y+=row;
    const int pathWidth=r.right-2*pad-2*button-2*gap;
    SetWindowPos(gPath,nullptr,pad,y,pathWidth,row,SWP_NOZORDER);
    SetWindowPos(gDetect,nullptr,pad+pathWidth+gap,y,button,row,SWP_NOZORDER);
    SetWindowPos(gBrowse,nullptr,pad+pathWidth+gap+button+gap,y,button,row,SWP_NOZORDER);
    y+=row+scale(12);
    RECT tabs{pad,y,r.right-pad,r.bottom-pad-row-gap};
    SetWindowPos(gTabs,nullptr,tabs.left,tabs.top,tabs.right-tabs.left,tabs.bottom-tabs.top,SWP_NOZORDER);
    RECT page=tabs;SendMessageW(gTabs,TCM_ADJUSTRECT,FALSE,(LPARAM)&page);
    const int left=page.left+gap,width=page.right-page.left-2*gap;
    int py=page.top+gap;
    SetWindowPos(gLaunch,nullptr,left,py,scale(200),tall,SWP_NOZORDER);
    SetWindowPos(gIsolated,nullptr,left,py+scale(6),width,row,SWP_NOZORDER);
    int ay=py+row+scale(12);
    SetWindowPos(gImport,nullptr,left,ay,scale(160),tall,SWP_NOZORDER);
    SetWindowPos(gFolder,nullptr,left+scale(160)+gap,ay,scale(120),tall,SWP_NOZORDER);
    const int playHelp=page.bottom-(py+tall+gap)-gap;
    SetWindowPos(gHelp,nullptr,left,py+tall+gap,width,playHelp>scale(80)?playHelp:scale(80),SWP_NOZORDER);
    const int advancedHelp=page.bottom-(ay+tall+gap)-gap;
    SetWindowPos(gAdvancedHelp,nullptr,left,ay+tall+gap,width,advancedHelp>scale(80)?advancedHelp:scale(80),SWP_NOZORDER);
    const int notices=page.bottom-page.top-2*gap;
    SetWindowPos(gNotices,nullptr,left,page.top+gap,width,notices>scale(80)?notices:scale(80),SWP_NOZORDER);
    SetWindowPos(gStatus,nullptr,pad,r.bottom-pad-row,r.right-2*pad,row,SWP_NOZORDER);
}

void makeFonts(){
    if(gFont)DeleteObject(gFont);
    if(gMonoFont)DeleteObject(gMonoFont);
    NONCLIENTMETRICSW metrics{sizeof metrics};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof metrics,&metrics,0);
    metrics.lfMessageFont.lfHeight=-scale(12);
    gFont=CreateFontIndirectW(&metrics.lfMessageFont);
    LOGFONTW mono=metrics.lfMessageFont;wcscpy_s(mono.lfFaceName,L"Consolas");
    gMonoFont=CreateFontIndirectW(&mono);
}

void applyFonts(){
    for(HWND h:{GetDlgItem(gWindow,kGameLabel),gPath,gDetect,gBrowse,gLaunch,gImport,gFolder,gIsolated,gTabs,gStatus})
        SendMessageW(h,WM_SETFONT,(WPARAM)gFont,TRUE);
    for(HWND h:{gHelp,gAdvancedHelp,gNotices})SendMessageW(h,WM_SETFONT,(WPARAM)gMonoFont,TRUE);
}

LRESULT CALLBACK proc(HWND window,UINT message,WPARAM w,LPARAM l){
    switch(message){
    case WM_CREATE:
        gWindow=window;
        makeFonts();
        child(window,L"STATIC",L"Game",SS_LEFT,kGameLabel,gFont);
        gPath=child(window,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|ES_READONLY,kPath,gFont);
        gDetect=child(window,L"BUTTON",L"Find it",BS_PUSHBUTTON|WS_TABSTOP,kDetect,gFont);
        gBrowse=child(window,L"BUTTON",L"Choose...",BS_PUSHBUTTON|WS_TABSTOP,kBrowse,gFont);
        gTabs=child(window,WC_TABCONTROLW,L"",WS_CLIPSIBLINGS|WS_TABSTOP,kTabs,gFont);
        {TCITEMW item{};item.mask=TCIF_TEXT;
         item.pszText=(LPWSTR)L"Play";SendMessageW(gTabs,TCM_INSERTITEMW,0,(LPARAM)&item);
         item.pszText=(LPWSTR)L"Advanced";SendMessageW(gTabs,TCM_INSERTITEMW,1,(LPARAM)&item);
         item.pszText=(LPWSTR)L"Notices";SendMessageW(gTabs,TCM_INSERTITEMW,2,(LPARAM)&item);}
        gLaunch=child(window,L"BUTTON",L"Play with Recursed++",BS_DEFPUSHBUTTON|WS_TABSTOP,kLaunch,gFont);
        gHelp=child(window,L"EDIT",kHelpText,WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,kHelp,gMonoFont);
        gIsolated=child(window,L"BUTTON",L"Play isolated: keep progress here and leave Steam alone",BS_AUTOCHECKBOX|WS_TABSTOP,kIsolated,gFont);
        gImport=child(window,L"BUTTON",L"Import from Steam",BS_PUSHBUTTON|WS_TABSTOP,kImport,gFont);
        gFolder=child(window,L"BUTTON",L"Save folder",BS_PUSHBUTTON|WS_TABSTOP,kFolder,gFont);
        gAdvancedHelp=child(window,L"EDIT",kAdvancedText,WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,kAdvancedHelp,gMonoFont);
        {const auto notices=peek::embeddedNotices();
         gNotices=child(window,L"EDIT",notices.empty()?L"The third-party notices are missing from this build.":notices.c_str(),
                        WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,kNotices,gMonoFont);}
        gStatus=child(window,L"STATIC",L"Looking for Recursed...",SS_LEFT|SS_ENDELLIPSIS,kStatus,gFont);
        EnableWindow(gLaunch,FALSE);
        showTab(0);
        layout(window);
        startThread(scanThread,nullptr);
        return 0;
    case kStage:{
        std::unique_ptr<wchar_t,decltype(&free)> text((wchar_t*)l,free);
        say(text.get());
        return 0;}
    case kFound:{
        std::unique_ptr<wchar_t,decltype(&free)> text((wchar_t*)l,free);
        setGame(text.get(),w?L"Found your Steam copy.":nullptr);
        return 0;}
    case kLaunched:{
        std::unique_ptr<wchar_t,decltype(&free)> text((wchar_t*)l,free);
        setBusy(false);
        if(w)say(L"Running. Hover a chest in the game to look inside it.");
        else {say(L"The game did not start.");MessageBoxW(window,text.get(),L"Recursed++",MB_OK|MB_ICONWARNING);}
        return 0;}
    case kSteamSaves:{
        std::unique_ptr<std::vector<peek::SteamSave>> saves((std::vector<peek::SteamSave>*)l);
        setBusy(false);
        if(saves->empty()){
            say(L"No Steam progress found.");
            MessageBoxW(window,L"No Recursed progress was found in a Steam account on this computer.\n\nSteam keeps it beside the Steam client, under userdata. If yours is on another computer, copy its save files into the save folder this window opens.",L"Recursed++",MB_OK|MB_ICONINFORMATION);
            return 0;
        }
        const auto& save=saves->front();
        std::wstring question=L"Copy the Recursed progress of Steam account "+save.account+L", last played "+playedOn(save.written)+L", into this build?";
        if(saves->size()>1)question+=L"\n\nThis computer has "+std::to_wstring(saves->size())+L" Steam accounts with progress. This is the one played most recently.";
        question+=L"\n\nThe progress this build has now is replaced, and a copy of it is kept in the save folder.";
        if(MessageBoxW(window,question.c_str(),L"Recursed++",MB_YESNO|MB_ICONQUESTION)!=IDYES){say(L"Nothing was copied.");return 0;}
        setBusy(true);say(L"Copying the Steam progress...");
        startThread(importThread,new peek::SteamSave(save));
        return 0;}
    case kImported:{
        std::unique_ptr<wchar_t,decltype(&free)> text((wchar_t*)l,free);
        setBusy(false);
        if(w)say((L"Imported "+std::to_wstring((int)w)+L" save files from Steam.").c_str());
        else {say(L"Nothing was copied.");MessageBoxW(window,text.get(),L"Recursed++",MB_OK|MB_ICONWARNING);}
        return 0;}
    case WM_NOTIFY:
        if(((LPNMHDR)l)->idFrom==kTabs&&((LPNMHDR)l)->code==TCN_SELCHANGE)showTab((int)SendMessageW(gTabs,TCM_GETCURSEL,0,0));
        return 0;
    case WM_SIZE: layout(window); return 0;
    case WM_DPICHANGED:{
        gDpi=HIWORD(w);
        makeFonts();applyFonts();
        auto* suggested=(RECT*)l;
        SetWindowPos(window,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
        layout(window);
        return 0;}
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w,TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_COMMAND:
        switch(LOWORD(w)){
        case kDetect: if(!gBusy){setBusy(true);say(L"Looking for Recursed...");startThread(scanThread,nullptr);setBusy(false);} return 0;
        case kBrowse: if(!gBusy)browse(); return 0;
        case kImport:
            if(gBusy)return 0;
            if(gameRunning()){
                MessageBoxW(window,L"Close Recursed first.\n\nA running game writes its whole progress back the next time it saves, so anything copied in now would be lost again.",L"Recursed++",MB_OK|MB_ICONWARNING);
                return 0;
            }
            setBusy(true);say(L"Looking for Steam progress...");
            startThread(steamSaveThread,nullptr);
            return 0;
        case kFolder:{
            if(gBusy)return 0;
            const auto folder=peek::saveFolder();
            if(folder.empty()){say(L"This build has nowhere to keep progress.");return 0;}
            // Anything at or below 32 is a failure code, and a window that simply never opens
            // leaves the player with nothing to act on, so the path is put where it can be read.
            if((INT_PTR)ShellExecuteW(window,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)
                MessageBoxW(window,(L"This build keeps your progress here:\n\n"+folder+L"\n\nOpening that folder failed, so copy the path into Explorer yourself.").c_str(),L"Recursed++",MB_OK|MB_ICONINFORMATION);
            say(folder.c_str());
            return 0;}
        case kLaunch:
            if(!gBusy&&!gGame.empty()){
                setBusy(true);
                startThread(launchThread,new LaunchJob{gGame,isolated()?peek::SteamUse::Isolated:peek::SteamUse::Steam});
            }
            return 0;
        }
        return 0;
    case WM_DESTROY:
        if(gFont)DeleteObject(gFont);
        if(gMonoFont)DeleteObject(gMonoFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
}

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    // Opening the save folder goes through the shell, which needs an apartment on this thread.
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX common{sizeof common,ICC_TAB_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSEXW cls{sizeof cls};
    cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"RecursedPlusPlusLauncher";
    // This project does not define UNICODE, so the stock resource ids come through as narrow
    // pointers; they are integer-encoded either way.
    cls.hCursor=LoadCursorW(nullptr,(LPCWSTR)IDC_ARROW);
    cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    cls.hIcon=cls.hIconSm=LoadIconW(nullptr,(LPCWSTR)IDI_APPLICATION);
    if(!RegisterClassExW(&cls))return 1;
    if(auto* user=GetModuleHandleW(L"user32.dll")){
        using ForSystem=UINT(WINAPI*)();
        if(auto dpi=(ForSystem)GetProcAddress(user,"GetDpiForSystem"))gDpi=dpi();
    }
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"Recursed++",
        WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,scale(620),scale(560),
        nullptr,nullptr,instance,nullptr);
    if(!window)return 1;
    ShowWindow(window,show);UpdateWindow(window);
    MSG message;
    while(GetMessageW(&message,nullptr,0,0)>0){
        if(IsDialogMessageW(window,&message))continue;
        TranslateMessage(&message);DispatchMessageW(&message);
    }
    return 0;
}
