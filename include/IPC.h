#pragma once

#include <windows.h>

#define FINDER_PIPE_NAME "\\\\.\\pipe\\DuiLibFinderPipe"
#define CMD_QUERY_CONTROL  1

#pragma pack(push, 1)
struct FinderRequest {
    DWORD cmd;
    POINT pt;
};

struct FinderResponse {
    DWORD result;
    WCHAR className[64];
    WCHAR name[64];
};
#pragma pack(pop)