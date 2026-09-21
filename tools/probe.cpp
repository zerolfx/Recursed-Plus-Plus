#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cwchar>
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;DWORD pid=wcstoul(argv[1],nullptr,10);
 HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,pid);if(!p)return 3;
 wchar_t path[MAX_PATH];DWORD len=MAX_PATH;QueryFullProcessImageNameW(p,0,path,&len);
 if(!wcsstr(path,L"Recursed++\\runtime\\Recursed.exe")){CloseHandle(p);return 4;}
 HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);THREADENTRY32 e{sizeof e};
 if(Thread32First(snap,&e))do{if(e.th32OwnerProcessID!=pid)continue;
 HANDLE t=OpenThread(THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT,FALSE,e.th32ThreadID);if(!t)continue;
 if(SuspendThread(t)!=(DWORD)-1){CONTEXT c{};c.ContextFlags=CONTEXT_CONTROL|CONTEXT_INTEGER;
 if(GetThreadContext(t,&c)){DWORD words[12]{};SIZE_T bytes;ReadProcessMemory(p,(void*)c.Esp,words,sizeof words,&bytes);
 printf("TID %lu EIP %08lX ESP %08lX ECX %08lX\n",e.th32ThreadID,c.Eip,c.Esp,c.Ecx);
 for(auto w:words)printf(" %08lX",w);puts("");}
 ResumeThread(t);}CloseHandle(t);
 }while(Thread32Next(snap,&e));CloseHandle(snap);CloseHandle(p);
}
