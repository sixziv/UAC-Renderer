#include "tirun.h"
#include"Windows.h"
#include "WtsApi32.h"
#include <TlHelp32.h>
using namespace std;
DWORD GetProcessIdByName(const wchar_t *processName)
{
    HANDLE hSnapshot;
    if ((hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)) == INVALID_HANDLE_VALUE)
        return 0;

    DWORD pid = -1;
    PROCESSENTRY32W pe;
    ZeroMemory(&pe, sizeof(PROCESSENTRY32W));
    pe.dwSize = sizeof(PROCESSENTRY32W);
    if (Process32FirstW(hSnapshot, &pe))
    {
        while (Process32NextW(hSnapshot, &pe))
        {
            if (0==wcscmp(pe.szExeFile,processName))
            {
                pid = pe.th32ProcessID;
                break;
            }
        }
    }
    else
    {
        CloseHandle(hSnapshot);
        return 0;
    }
    if (pid == -1)
    {
        CloseHandle(hSnapshot);
        return 0;
    }
    CloseHandle(hSnapshot);
    return pid;
}

bool ImpersonateSystem()
{
    DWORD systemPid = GetProcessIdByName(L"winlogon.exe");
    if(0==systemPid)return 0;
    HANDLE hSystemProcess;
    if ((hSystemProcess = OpenProcess(PROCESS_DUP_HANDLE|PROCESS_QUERY_INFORMATION,FALSE,systemPid)) == nullptr)
    {
        return false;
    }

    HANDLE hSystemToken;
    if (!OpenProcessToken(hSystemProcess,MAXIMUM_ALLOWED,&hSystemToken))
    {
        CloseHandle(hSystemProcess);
        return false;
    }

    HANDLE hDupToken;
    SECURITY_ATTRIBUTES tokenAttributes;
    tokenAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    tokenAttributes.lpSecurityDescriptor = nullptr;
    tokenAttributes.bInheritHandle = FALSE;
    if (!DuplicateTokenEx(hSystemToken,MAXIMUM_ALLOWED,&tokenAttributes,SecurityImpersonation,TokenImpersonation,&hDupToken))
    {
        CloseHandle(hSystemToken);
        return false;
    }

    if (!ImpersonateLoggedOnUser(hDupToken))
    {
        CloseHandle(hDupToken);
        CloseHandle(hSystemToken);
        return false;
    }
    CloseHandle(hDupToken);
    CloseHandle(hSystemToken);
    return true;
}

DWORD StartTrustedInstallerService()
{
    SC_HANDLE hSCManager;
    if ((hSCManager = OpenSCManagerW(nullptr,SERVICES_ACTIVE_DATABASE,GENERIC_EXECUTE)) == nullptr)
    {
        return 0;
    }
    SC_HANDLE hService;
    if ((hService = OpenServiceW(hSCManager,L"TrustedInstaller",GENERIC_READ | GENERIC_EXECUTE)) == nullptr)
    {
        CloseServiceHandle(hSCManager);
        return 0;
    }
    SERVICE_STATUS_PROCESS statusBuffer;
    DWORD bytesNeeded;
    while (QueryServiceStatusEx(hService,SC_STATUS_PROCESS_INFO,reinterpret_cast<LPBYTE>(&statusBuffer),sizeof(SERVICE_STATUS_PROCESS),&bytesNeeded))
    {
        if (statusBuffer.dwCurrentState == SERVICE_STOPPED)
        {
            if (!StartServiceW(hService, 0, nullptr))
            {
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCManager);
                return 0;
            }
        }
        if (statusBuffer.dwCurrentState == SERVICE_START_PENDING||statusBuffer.dwCurrentState == SERVICE_STOP_PENDING)
        {
            Sleep(statusBuffer.dwWaitHint);
            continue;
        }
        if (statusBuffer.dwCurrentState == SERVICE_RUNNING)
        {
            CloseServiceHandle(hService);
            CloseServiceHandle(hSCManager);
            return statusBuffer.dwProcessId;
        }
    }
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    return 0;
}

bool CreateProcessAsTrustedInstaller(wchar_t *pName)
{
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
        return false;
    LUID luid0,luid1;
    if (!LookupPrivilegeValueW(nullptr,SE_DEBUG_NAME, &luid0))
    {
        CloseHandle(hToken);
        return false;
    }
    if (!LookupPrivilegeValueW(nullptr,SE_IMPERSONATE_NAME, &luid1))
    {
        CloseHandle(hToken);
        return false;
    }
    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 2;
    tp.Privileges[0].Luid = luid0;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    tp.Privileges[1].Luid = luid1;
    tp.Privileges[1].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), nullptr, nullptr))
    {
        CloseHandle(hToken);
        return false;
    }

    CloseHandle(hToken);
    if(false==ImpersonateSystem())return 0;

    DWORD pid = StartTrustedInstallerService();
    if(pid==0)return false;
    HANDLE hTIProcess;
    if ((hTIProcess = OpenProcess(PROCESS_DUP_HANDLE|PROCESS_QUERY_INFORMATION,FALSE,pid)) == nullptr)
    {
        return false;
    }

    HANDLE hTIToken;
    if (!OpenProcessToken(hTIProcess,MAXIMUM_ALLOWED,&hTIToken))
    {
        CloseHandle(hTIProcess);
        return false;
    }
    HANDLE hDupToken;
    SECURITY_ATTRIBUTES tokenAttributes;
    tokenAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    tokenAttributes.lpSecurityDescriptor = nullptr;
    tokenAttributes.bInheritHandle = FALSE;
    if (!DuplicateTokenEx(hTIToken,MAXIMUM_ALLOWED,&tokenAttributes,SecurityImpersonation,TokenPrimary,&hDupToken))
    {
        CloseHandle(hTIToken);
        return false;
    }
    CloseHandle(hTIToken);
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(STARTUPINFOW));
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));
    if (!CreateProcessWithTokenW(hDupToken,0,nullptr,pName,0,nullptr,nullptr,&si,&pi))
    {
        CloseHandle(hDupToken);
        return false;
    }
    CloseHandle(hDupToken);
    return true;
}
