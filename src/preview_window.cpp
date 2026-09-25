#include "preview_window.h"
#include <algorithm>
#include <deque>
#include <imm.h>
#pragma comment(lib,"imm32.lib")
namespace peek { namespace {
static HWND popup=nullptr,owner=nullptr;
static RoomArt art;
static Snapshot snapshot;
static std::string info,title,snapshotView;
static std::deque<WindowAction> actions;
static bool escapeHeld=false,diagnostic=false;
static RECT imageRect{};
static HFONT uiFont=nullptr;
static float dpiScale=1;
static int px(int n){return (int)(n*dpiScale+.5f);}
static void setDpi(UINT dpi){dpiScale=std::max(1.f,dpi/96.f);if(uiFont)DeleteObject(uiFont);uiFont=CreateFontW(-px(18),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
static std::wstring wide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0);std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n);return w;}
static RECT fit(HWND hwnd){RECT r{};GetClientRect(hwnd,&r);int w=std::max(0L,r.right-px(32)),h=std::max(0L,r.bottom-px(144));float scale=std::min(w/20.f,h/15.f);int rw=(int)(scale*20),rh=(int)(scale*15);int x=(r.right-rw)/2,y=px(64)+(h-rh)/2;return {x,y,x+rw,y+rh};}
static void paint(HWND hwnd){PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r{};GetClientRect(hwnd,&r);HDC buffer=CreateCompatibleDC(dc);HBITMAP b=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));auto old=SelectObject(buffer,b);HBRUSH bg=CreateSolidBrush(RGB(14,18,28));FillRect(buffer,&r,bg);DeleteObject(bg);
 SetBkMode(buffer,TRANSPARENT);SetTextColor(buffer,RGB(231,236,247));auto font=SelectObject(buffer,uiFont?uiFont:GetStockObject(DEFAULT_GUI_FONT));RECT top{px(16),px(12),r.right-px(16),px(60)};auto instructions=wide(diagnostic?"Active-room render diagnostic  |  Esc: close":"Click chest, jar or cauldron: in / fire: out  |  Backspace: back  |  O: dock  |  Esc / right click: close");DrawTextW(buffer,instructions.c_str(),-1,&top,DT_LEFT|DT_WORDBREAK);
 imageRect=fit(hwnd);if(!art.pixels.empty()&&imageRect.right>imageRect.left&&imageRect.bottom>imageRect.top){BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bmi.bmiHeader.biWidth=art.width;bmi.bmiHeader.biHeight=-art.height;bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;SetStretchBltMode(buffer,COLORONCOLOR);StretchDIBits(buffer,imageRect.left,imageRect.top,imageRect.right-imageRect.left,imageRect.bottom-imageRect.top,0,0,art.width,art.height,art.pixels.data(),&bmi,DIB_RGB_COLORS,SRCCOPY);}
 RECT bottom{px(16),r.bottom-px(70),r.right-px(16),r.bottom-px(4)};auto status=wide(info);SetTextColor(buffer,RGB(170,189,211));DrawTextW(buffer,status.c_str(),-1,&bottom,DT_LEFT|DT_WORDBREAK);SelectObject(buffer,font);BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(b);DeleteDC(buffer);EndPaint(hwnd,&ps);}
static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){switch(msg){
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:paint(hwnd);return 0;
 case WM_GETMINMAXINFO:((MINMAXINFO*)l)->ptMinTrackSize={px(640),px(560)};return 0;
 case WM_SIZE:InvalidateRect(hwnd,nullptr,FALSE);return 0;
 case WM_DPICHANGED:{setDpi(HIWORD(w));auto r=(RECT*)l;SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);return 0;}
 case WM_CLOSE:actions.push_back({PreviewAction::Close});closePreviewWindow();return 0;
 case WM_KEYDOWN:if(l&(1L<<30))return 0;if(w==VK_ESCAPE){escapeHeld=true;actions.push_back({PreviewAction::Close});closePreviewWindow();}else if(w==VK_BACK)actions.push_back({PreviewAction::Back});else if(w=='O')actions.push_back({PreviewAction::Dock});else if(w=='Q')actions.push_back({PreviewAction::Undo});else if(w=='W')actions.push_back({PreviewAction::UndoSeconds});return 0;
 case WM_LBUTTONDOWN:{auto r=fit(hwnd);float s=(r.right-r.left)/20.f;if(s<=0)return 0;float x=((short)LOWORD(l)-r.left)/s,y=((short)HIWORD(l)-r.top)/s;
  for(const auto& o:snapshot.objects){
   Reach reach{};if(!opensPreview(o)||!reachOf(o.kind,reach))continue;
   const float m=reachMargin;
   if(x>=o.x-reach.half-m&&x<=o.x+reach.half+m&&y>=o.y-reach.above-m&&y<=o.y+reach.below+m){actions.push_back({PreviewAction::Select,o,snapshotView});break;}
  }return 0;}
 // The side buttons of a mouse walk the preview history, here as well as over the game.
 case WM_XBUTTONDOWN:actions.push_back({HIWORD(w)==XBUTTON1?PreviewAction::Back:PreviewAction::Forward});return TRUE;
 case WM_XBUTTONUP:return TRUE;
 // Right click closes, as it does over the game. On release, so the release has nowhere else to land.
 case WM_RBUTTONDOWN:return 0;
 case WM_RBUTTONUP:actions.push_back({PreviewAction::Close});closePreviewWindow();return 0;
 case WM_DESTROY:popup=nullptr;return 0;
 }return DefWindowProcW(hwnd,msg,w,l);}
}
bool showPreviewWindow(HWND parent){if(popup){ShowWindow(popup,SW_RESTORE);SetForegroundWindow(popup);return true;}owner=parent;actions.clear();using GetDpi=UINT(WINAPI*)(HWND);auto getDpi=(GetDpi)GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");setDpi(getDpi?getDpi(parent):96);WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"RecursedPeekRoomPreview";wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));RegisterClassW(&wc);RECT parentRect{};GetWindowRect(parent,&parentRect);RECT r{0,0,px(880),px(800)};AdjustWindowRectEx(&r,WS_OVERLAPPEDWINDOW,FALSE,WS_EX_APPWINDOW);MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(parent,MONITOR_DEFAULTTONEAREST),&monitor);int width=std::min(r.right-r.left,monitor.rcWork.right-monitor.rcWork.left),height=std::min(r.bottom-r.top,monitor.rcWork.bottom-monitor.rcWork.top);int x=monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,y=monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2;popup=CreateWindowExW(WS_EX_APPWINDOW,wc.lpszClassName,L"Recursed++ Preview",WS_OVERLAPPEDWINDOW,x,y,width,height,parent,nullptr,wc.hInstance,nullptr);if(!popup)return false;
 // This window has no text fields; IME composition must not consume O or Esc.
 ImmAssociateContext(popup,nullptr);ShowWindow(popup,SW_SHOW);SetForegroundWindow(popup);return true;}
void closePreviewWindow(){if(popup){auto h=popup;popup=nullptr;DestroyWindow(h);if(IsWindow(owner))SetForegroundWindow(owner);}art={};snapshot={};title.clear();info.clear();snapshotView.clear();}
bool previewWindowOpen(){return popup!=nullptr;}
bool reachOf(const std::string& kind,Reach& out){
 // A chest's box stands 0.4 below its position and its open lid rises to 0.89 above, and turned by
 // its random yaw the box is up to 1.3 across; a jar runs from its base 0.5 below to its rim 0.3
 // above, and its handles reach 0.63 to 0.89 either side as it is turned; a cauldron's bowl is 1.5
 // across, from its feet 0.4 below to its rim 0.6 above. A flame spans the height of the player it
 // stands for.
 if(kind=="chest")out={.6f,.9f,.4f,.43f};
 else if(kind=="jar")out={.8f,.3f,.5f,.53f};
 else if(kind=="cauldron")out={.75f,.6f,.4f,.43f};
 else if(kind=="player"||kind=="yield")out={.5f,.4f,1.f,.98f};
 else return false;
 return true;
}
bool opensPreview(const Object& o){return ((o.kind=="chest"||o.kind=="cauldron")&&!o.target.empty())||o.kind=="jar"||o.kind=="player"||o.kind=="yield";}
bool previewWindowFocused(){return popup&&GetForegroundWindow()==popup;}
bool previewEscapeHeld(){if(escapeHeld&&!(GetAsyncKeyState(VK_ESCAPE)&0x8000))escapeHeld=false;return escapeHeld;}
WindowAction pumpPreviewWindow(){if(popup){MSG msg;while(popup&&PeekMessageW(&msg,popup,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}if(actions.empty())return {};auto a=actions.front();actions.pop_front();return a;}
void updatePreviewWindow(const RoomArt& next,const Snapshot& s,const std::string& room,const std::string& depth,const std::string& status,const std::string& view){if(!popup)return;diagnostic=room=="ACTIVE ROOM - NATIVE TEST";auto newTitle="Recursed++ | "+depth+" | "+room;bool changed=false;if(newTitle!=title){title=newTitle;SetWindowTextW(popup,wide(title).c_str());changed=true;}auto nextInfo=diagnostic?status+"\n"+next.note:status;if(info!=nextInfo){info=nextInfo;changed=true;}if(art.revision!=next.revision||art.pixels.empty()){art=next;snapshot=s;snapshotView=view;changed=true;}if(changed)InvalidateRect(popup,nullptr,FALSE);}
}
