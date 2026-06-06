#include "phnt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "ntdll.lib")


static HANDLE  HPipeHandle = INVALID_HANDLE_VALUE;
static wchar_t ServicePath[MAX_PATH] = { 0 };

const wchar_t RealServicePath[] = L"C:\\Program Files (x86)\\Common Files\\BattlEye\\BEService.exe";
const wchar_t TargetServicePath[] = L"C:\\Program Files (x86)\\Common Files\\BattlEye\\BEService_arksa.exe";
const wchar_t CommonBattlEyeDir[] = L"C:\\Program Files (x86)\\Common Files\\BattlEye";
const wchar_t TargetGamePath[] = L"C:\\Users\\dev\\Desktop\\BattleJack\\x64\\Release\\Testapp.exe";
const wchar_t EmulatedGameID[] = L"arksa";


static BOOL InstallService(void)
{
    SERVICE_STATUS_PROCESS ssp;
    SC_HANDLE hscm = NULL;
    SC_HANDLE hservice = NULL;
    HANDLE hfile = INVALID_HANDLE_VALUE;
    DWORD attributes = 0;
    DWORD bytesneeded = 0;
    BOOL result = FALSE;

    memset(&ssp, 0, sizeof(ssp));

    printf("Checking for existing BattlEye Service...\n");

    hscm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hscm == NULL)
    {
        printf("Failed to open SCM (%lu)\n", GetLastError());
        goto cleanup;
    }

    hservice = OpenServiceA(hscm, "BEService", SERVICE_ALL_ACCESS);
    if (hservice != NULL)
    {
        if (QueryServiceStatusEx(hservice, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesneeded))
        {
            if (ssp.dwCurrentState == SERVICE_RUNNING)
            {
                printf("BattlEye Service is already running.\n");
                result = TRUE;
                goto cleanup;
            }
            if (!StartServiceA(hservice, 0, NULL))
            {
                printf("Failed to start existing BattlEye Service (%lu)\n", GetLastError());
                goto cleanup;
            }
            while (ssp.dwCurrentState != SERVICE_RUNNING)
            {
                Sleep(100);
                if (!QueryServiceStatusEx(hservice, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesneeded))
                {
                    printf("Failed to query existing BattlEye Service (%lu)\n", GetLastError());
                    goto cleanup;
                }
            }
            printf("BattlEye Service started successfully.\n");
            result = TRUE;
            goto cleanup;
        }
        CloseServiceHandle(hservice);
        hservice = NULL;
    }

    printf("Installing BattlEye Service...\n");
    wprintf(L"Service Path: %s\n", RealServicePath);

    printf("Preparing to copy BattlEye Service...\n");

    attributes = GetFileAttributesW(CommonBattlEyeDir);
    if (!(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)))
    {
        if (!CreateDirectoryW(CommonBattlEyeDir, NULL))
            goto cleanup;
    }

    hfile = CreateFileW(RealServicePath, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hfile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hfile);
        hfile = INVALID_HANDLE_VALUE;
        if (!DeleteFileW(RealServicePath))
            goto cleanup;
    }

    if (ServicePath[0] == 0)
        wcsncpy_s(ServicePath, MAX_PATH, RealServicePath, _TRUNCATE);

    printf("Copying %ws\n", TargetServicePath);
    if (!CopyFileW(TargetServicePath, RealServicePath, FALSE))
        goto cleanup;
    printf("BattlEye Service copied to: %ls\n", RealServicePath);

    hservice = CreateServiceW(hscm, L"BEService", L"BattlEye Service",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
        RealServicePath,
        NULL, NULL, NULL, NULL, NULL);

    if (hservice == NULL)
        goto cleanup;

    if (!StartServiceA(hservice, 0, NULL))
        goto cleanup;

    while (ssp.dwCurrentState != SERVICE_RUNNING)
    {
        Sleep(100);
        if (!QueryServiceStatusEx(hservice, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesneeded))
        {
            goto cleanup;
        }
    }

    result = TRUE;

cleanup:
    if (hfile != INVALID_HANDLE_VALUE)
        CloseHandle(hfile);
    if (hservice != NULL)
        CloseServiceHandle(hservice);
    if (hscm != NULL)
        CloseServiceHandle(hscm);
    return result;
}

static BOOL UninstallService(void)
{
    SC_HANDLE hscm = NULL;
    SC_HANDLE hservice = NULL;
    DWORD bytesneeded = 0;
    SERVICE_STATUS_PROCESS ssp;
    SERVICE_STATUS stopcondition;
    BOOL result = FALSE;

    memset(&ssp, 0, sizeof(ssp));
    memset(&stopcondition, 0, sizeof(stopcondition));

    hscm = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hscm == NULL)
        return FALSE;

    hservice = OpenServiceA(hscm, "BEService", SERVICE_ALL_ACCESS);
    if (hservice == NULL)
    {
        result = TRUE; // nothing to uninstall
        goto cleanup;
    }

    if (!QueryServiceStatusEx(hservice, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesneeded))
    {
        goto cleanup;
    }

    if (ssp.dwCurrentState == SERVICE_RUNNING)
    {
        if (!ControlService(hservice, SERVICE_CONTROL_STOP, &stopcondition))
            goto cleanup;
        while (ssp.dwCurrentState != SERVICE_STOPPED)
        {
            Sleep(120);
            if (!QueryServiceStatusEx(hservice, SC_STATUS_PROCESS_INFO, (LPBYTE)&ssp, sizeof(ssp), &bytesneeded))
            {
                goto cleanup;
            }
        }
    }

    if (!DeleteService(hservice))
        goto cleanup;

    // Poll until SCM finishes unregistering.
    CloseServiceHandle(hservice);
    hservice = OpenServiceA(hscm, "BEService", SERVICE_ALL_ACCESS);
    while (hservice != NULL)
    {
        CloseServiceHandle(hservice);
        Sleep(120);
        hservice = OpenServiceA(hscm, "BEService", SERVICE_ALL_ACCESS);
    }

    result = TRUE;

cleanup:
    if (hservice != NULL)
        CloseServiceHandle(hservice);
    if (hscm != NULL)
        CloseServiceHandle(hscm);
    return result;
}

static BOOL InitBE(void)
{
    wchar_t modulefilename[MAX_PATH] = { 0 };
    const wchar_t* sep;
    const wchar_t* game;
    BYTE packet[1024];
    BYTE response[1024];
    DWORD pos = 0;
    DWORD byteswritten = 0;
    ULONGLONG endtime;
    size_t gameidlength;
    size_t gamelength;
    size_t servicepathlength;
    size_t packetsize;

    // Module filename = the exe name we report to BE
    if (!GetModuleFileNameW(NULL, modulefilename, MAX_PATH))
        return FALSE;
    sep = wcsrchr(modulefilename, L'\\');
    game = sep ? (sep + 1) : modulefilename;

    // Open the pipe (with 10s retry).
    endtime = GetTickCount64() + 10000;
    while (GetTickCount64() < endtime)
    {
        HPipeHandle = CreateFileA("\\\\.\\pipe\\BattlEye",
            GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, 0, NULL);
        if (HPipeHandle != INVALID_HANDLE_VALUE)
            break;
        Sleep(100);
    }
    if (HPipeHandle == INVALID_HANDLE_VALUE)
        return FALSE;

    gameidlength = wcslen(EmulatedGameID);
    gamelength = wcslen(game);
    servicepathlength = wcslen(ServicePath);
    packetsize = 1 + 1
        + gameidlength * sizeof(wchar_t)
        + sizeof(UINT16)
        + sizeof(UINT32)
        + 1
        + gamelength * sizeof(wchar_t)
        + servicepathlength * sizeof(wchar_t);

    if (gameidlength > 255 || gamelength > 255 || packetsize > sizeof(packet))
    {
        printf("InitBE: packet too large "
            "(gameid=%zu game=%zu svc=%zu packetsize=%zu)\n",
            gameidlength, gamelength, servicepathlength, packetsize);
        return FALSE;
    }

    packet[pos++] = 0x0;
    packet[pos++] = (UINT8)gameidlength;
    memcpy(packet + pos, EmulatedGameID, gameidlength * sizeof(wchar_t));
    pos += (DWORD)(gameidlength * sizeof(wchar_t));

    *(UINT16*)(packet + pos) = (UINT16)27038;
    pos += sizeof(UINT16);

    *(UINT32*)(packet + pos) = GetCurrentProcessId();
    pos += sizeof(UINT32);

    packet[pos++] = (UINT8)gamelength;
    memcpy(packet + pos, game, gamelength * sizeof(wchar_t));
    pos += (DWORD)(gamelength * sizeof(wchar_t));

    memcpy(packet + pos, ServicePath, servicepathlength * sizeof(wchar_t));
    pos += (DWORD)(servicepathlength * sizeof(wchar_t));

    if (!WriteFile(HPipeHandle, packet, pos, &byteswritten, NULL) || byteswritten != pos)
        return FALSE;

    // Wait on response
    memset(response, 0, sizeof(response));
    endtime = GetTickCount64() + 10000;
    while (GetTickCount64() < endtime)
    {
        DWORD bytesread = 0;
        DWORD bytesavail = 0;

        if (PeekNamedPipe(HPipeHandle, NULL, 0, NULL, &bytesavail, NULL) && bytesavail > 0)
        {
            if (ReadFile(HPipeHandle, response, sizeof(response), &bytesread, NULL) && bytesread > 0)
            {
                return TRUE;
            }
        }
        Sleep(100);
    }
    return FALSE;
}

static BOOL Load(void)
{
    wchar_t path[512] = { 0 };
    PRTL_USER_PROCESS_PARAMETERS launcherparams = NULL;
    PRTL_USER_PROCESS_PARAMETERS gameparams = NULL;
    HANDLE step1process = NULL;
    HANDLE step2process = NULL;
    HANDLE step2thread = NULL;
    HANDLE fakeBELauncher = INVALID_HANDLE_VALUE;
    UNICODE_STRING launcherpath;
    UNICODE_STRING gamepath;
    RTL_USER_PROCESS_INFORMATION  processinfo;
    NTSTATUS status = 0;
    BYTE packet[5];
    DWORD byteswritten = 0;
    DWORD pid = 0;
    BOOL result = FALSE;

    memset(&processinfo, 0, sizeof(processinfo));
    memset(&launcherpath, 0, sizeof(launcherpath));
    memset(&gamepath, 0, sizeof(gamepath));

    fakeBELauncher = OpenProcess(PROCESS_CREATE_PROCESS, FALSE,
        GetCurrentProcessId());
    if (fakeBELauncher == INVALID_HANDLE_VALUE)
        return FALSE;

    if (!GetModuleFileNameW(NULL, path, 512))
        goto cleanup;


    if (!RtlDosPathNameToNtPathName_U(path, &launcherpath, NULL, NULL) ||
        !RtlDosPathNameToNtPathName_U(TargetGamePath, &gamepath, NULL, NULL))
    {
        goto cleanup;
    }

    if (RtlCreateProcessParameters(&launcherparams, &launcherpath,
        NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL))
    {
        printf("RtlCreateProcessParameters (self) failed\n");
        goto cleanup;
    }

    if (RtlCreateProcessParameters(&gameparams, &gamepath, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL))
    {
        printf("RtlCreateProcessParameters (game) failed\n");
        goto cleanup;
    }

    memset(&processinfo, 0, sizeof(processinfo));
    status = RtlCreateUserProcess(&launcherpath, OBJ_CASE_INSENSITIVE, launcherparams, NULL, NULL, fakeBELauncher, TRUE, NULL, NULL, &processinfo);
    if (status)
    {
        printf("RtlCreateUserProcess (Fake Launcher) failed: %08lX\n",
            (ULONG)status);
        goto cleanup;
    }

    step1process = processinfo.ProcessHandle;
    if (processinfo.ThreadHandle)
        CloseHandle(processinfo.ThreadHandle);

    memset(&processinfo, 0, sizeof(processinfo));
    status = RtlCreateUserProcess(&gamepath, OBJ_CASE_INSENSITIVE, gameparams,
        NULL, NULL, step1process,
        TRUE, NULL, NULL, &processinfo);

    if (status)
    {
        printf("RtlCreateUserProcess (game) failed: %08lX\n", (ULONG)status);
        goto cleanup;
    }

    step2process = processinfo.ProcessHandle;
    step2thread = processinfo.ThreadHandle;

    printf("Image: %wZ\n", &gamepath);

    ResumeThread(step2thread);

    pid = GetProcessId(step2process);
    CloseHandle(step2process); step2process = NULL;
    CloseHandle(step2thread);  step2thread = NULL;

    printf("Process started with PID: %lu\n", pid);

    packet[0] = 0x3;
    *(UINT32*)(packet + 1) = pid;
    if (!WriteFile(HPipeHandle, packet, 5, &byteswritten, NULL) || byteswritten != 5)
    {
        HANDLE hkill = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (hkill)
        {
            TerminateProcess(hkill, 1);
            CloseHandle(hkill);
        }
        if (step1process)
        {
            TerminateProcess(step1process, 1);
        }
        printf("Failed to Load (%lu)\n", GetLastError());
        goto cleanup;
    }

    result = TRUE;

cleanup:
    if (launcherparams)
        RtlDestroyProcessParameters(launcherparams);
    if (gameparams)
        RtlDestroyProcessParameters(gameparams);
    if (step1process)
        CloseHandle(step1process);
    if (step2process)
        CloseHandle(step2process);
    if (step2thread)
        CloseHandle(step2thread);
    if (launcherpath.Buffer)
        RtlFreeUnicodeString(&launcherpath);
    if (gamepath.Buffer)
        RtlFreeUnicodeString(&gamepath);
    if (fakeBELauncher != INVALID_HANDLE_VALUE)
        CloseHandle(fakeBELauncher);
    return result;
}

int main(void)
{
    FILE* fdummy = NULL;
    AllocConsole();
    freopen_s(&fdummy, "CONIN$", "r", stdin);
    freopen_s(&fdummy, "CONOUT$", "w", stderr);
    freopen_s(&fdummy, "CONOUT$", "w", stdout);

    LoadLibraryA("crypt32.dll");
    LoadLibraryA("bcrypt.dll");
    LoadLibraryA("Tbs.dll");
    LoadLibraryA("Ws2_32.dll");

    if (!InstallService())
    {
        UninstallService();
        if (!InstallService())
        {
            printf("Failed to install BattlEye Service (%lu)\n", GetLastError());
            (void)getchar();
            return 1;
        }
    }

    if (!InitBE())
    {
        printf("Failed to communicate to BattlEye Service (%lu)\n", GetLastError());
        if (HPipeHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(HPipeHandle);
            HPipeHandle = INVALID_HANDLE_VALUE;
        }
        (void)getchar();
        return 1;
    }

    printf("Starting Process: %ls\n", TargetGamePath);
    if (!Load())
    {
        printf("Failed to launch the game (%lu)\n", GetLastError());
        if (HPipeHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(HPipeHandle);
            HPipeHandle = INVALID_HANDLE_VALUE;
        }
        (void)getchar();
        return 1;
    }

    if (HPipeHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(HPipeHandle);
        HPipeHandle = INVALID_HANDLE_VALUE;
    }
    return 0;
}