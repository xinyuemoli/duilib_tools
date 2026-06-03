#include "ThreadEnum.h"
#include <tlhelp32.h>

std::vector<HANDLE> GetAllOtherThreadsInProcess() {
    std::vector<HANDLE> otherThreads;

    DWORD currentProcessId = GetCurrentProcessId();
    DWORD currentThreadId = GetCurrentThreadId();

    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("[HookDll] CreateToolhelp32Snapshot failed!\n");
        return otherThreads;
    }

    THREADENTRY32 te32;
    te32.dwSize = sizeof(te32);

    if (Thread32First(hThreadSnap, &te32)) {
        do {
            if (te32.th32OwnerProcessID == currentProcessId) {
                if (te32.th32ThreadID != currentThreadId) {
                    HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te32.th32ThreadID);
                    if (hThread != NULL) {
                        otherThreads.push_back(hThread);
                    }
                }
            }
        } while (Thread32Next(hThreadSnap, &te32));
    }

    CloseHandle(hThreadSnap);
    return otherThreads;
}