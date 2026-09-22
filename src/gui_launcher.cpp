#include <windows.h>
#include <commdlg.h>
#include <process.h>
#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include "game_launch.h"
#include "steam_scan.h"
#include "support_folder.h"

// A window instead of a console, so the mod can be handed to someone who does not run
// PowerShell. It finds the game, checks it is the build this mod was measured against,
// starts it with the plugin loaded, and carries its own instructions so nothing extra has
// to be drawn over the game.
namespace {
enum : int { kPath=1001, kBrowse, kDetect, kLaunch, kHelp, kStatus, kGameLabel };
// Scanning and launching both touch disks that can be asleep, missing, or being scanned by
// antivirus, so neither runs on the thread that has to keep painting.
enum : UINT { kStage=WM_APP+1, kFound, kLaunched };
HWND gWindow, gPath, gBrowse, gDetect, gLaunch, gHelp, gStatus;
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
L"  F8                        Turn inspection on or off.\r\n"
L"  Hover a chest or flame    Preview the room it leads to.\r\n"
L"  Left click                Pin that preview in the game window.\r\n"
L"  Shift + left click        Open the preview in its own window.\r\n"
L"  O                         Move the preview between the game and that window.\r\n"
L"  Click a chest inside      Go one room deeper, up to eight.\r\n"
L"  Click a red or green      Look at the room outside this one.\r\n"
L"  flame inside\r\n"
L"  Backspace                 Go back; at the first room this closes the preview.\r\n"
L"  Esc                       Close the preview without pausing.\r\n"
L"\r\n"
L"Depth is counted from the room you are standing in: 1 is inside the chest, 0 is\r\n"
L"where you are, -1 is outside.\r\n"
L"\r\n"
L"What this does to your game\r\n"
L"\r\n"
L"  It never writes to the game's folder and it does not touch your normal saves.\r\n"
L"  The modded run keeps its own progress under AppData and starts with Steam\r\n"
L"  switched off, so achievements and cloud saves stay out of it. It does share the\r\n"
L"  game's own graphics and sound settings file.\r\n"
L"  Only the Steam Windows build this mod was measured against can be started.\r\n"
L"  Close the game to end the modded session; nothing is left running.\r\n"
L"\r\n"
L"If the game does not start, security software is the usual reason: the mod has to\r\n"
L"load itself into the game, which looks like what a cheat would do. Allow the two\r\n"
L"files that came with this launcher and try again.";

std::wstring pluginPath(){
    wchar_t own[MAX_PATH];GetModuleFileNameW(nullptr,own,MAX_PATH);
    return (std::filesystem::path(own).parent_path()/L"recursed_peek.dll").wstring();
}

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

struct LaunchJob {std::wstring exe,plugin;};
unsigned __stdcall launchThread(void* raw){
    std::unique_ptr<LaunchJob> job((LaunchJob*)raw);
    std::wstring error;
    const auto id=peek::launchModded(job->exe,job->plugin,error,
        [](const wchar_t* text){post(kStage,0,text);});
    post(kLaunched,id,error);
    return 0;
}

void startThread(unsigned(__stdcall* fn)(void*),void* arg){
    if(auto h=(HANDLE)_beginthreadex(nullptr,0,fn,arg,0,nullptr))CloseHandle(h);
}

void setBusy(bool busy){
    gBusy=busy;
    EnableWindow(gLaunch,!busy&&!gGame.empty()&&peek::supportedGame(gGame));
    EnableWindow(gDetect,!busy);EnableWindow(gBrowse,!busy);
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
    const int pad=scale(14),row=scale(26),gap=scale(8),button=scale(96);
    int y=pad;
    SetWindowPos(GetDlgItem(window,kGameLabel),nullptr,pad,y,r.right-2*pad,row,SWP_NOZORDER);
    y+=row;
    const int pathWidth=r.right-2*pad-2*button-2*gap;
    SetWindowPos(gPath,nullptr,pad,y,pathWidth,row,SWP_NOZORDER);
    SetWindowPos(gDetect,nullptr,pad+pathWidth+gap,y,button,row,SWP_NOZORDER);
    SetWindowPos(gBrowse,nullptr,pad+pathWidth+gap+button+gap,y,button,row,SWP_NOZORDER);
    y+=row+scale(12);
    SetWindowPos(gLaunch,nullptr,pad,y,scale(200),scale(34),SWP_NOZORDER);
    y+=scale(34)+scale(12);
    const int helpHeight=r.bottom-y-pad-row-gap;
    SetWindowPos(gHelp,nullptr,pad,y,r.right-2*pad,helpHeight>scale(80)?helpHeight:scale(80),SWP_NOZORDER);
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
    for(HWND h:{GetDlgItem(gWindow,kGameLabel),gPath,gDetect,gBrowse,gLaunch,gStatus})
        SendMessageW(h,WM_SETFONT,(WPARAM)gFont,TRUE);
    SendMessageW(gHelp,WM_SETFONT,(WPARAM)gMonoFont,TRUE);
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
        gLaunch=child(window,L"BUTTON",L"Play with Recursed++",BS_DEFPUSHBUTTON|WS_TABSTOP,kLaunch,gFont);
        gHelp=child(window,L"EDIT",kHelpText,WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,kHelp,gMonoFont);
        gStatus=child(window,L"STATIC",L"Looking for Recursed...",SS_LEFT|SS_ENDELLIPSIS,kStatus,gFont);
        EnableWindow(gLaunch,FALSE);
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
        if(w)say(L"Running. Press F8 in the game to turn inspection on.");
        else {say(L"The game did not start.");MessageBoxW(window,text.get(),L"Recursed++",MB_OK|MB_ICONWARNING);}
        return 0;}
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
        case kLaunch:
            if(!gBusy&&!gGame.empty()){
                setBusy(true);
                startThread(launchThread,new LaunchJob{gGame,pluginPath()});
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
