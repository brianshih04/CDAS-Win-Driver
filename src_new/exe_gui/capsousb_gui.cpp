/*++

   CDAS USB Test Tool — Win32 GUI

   A window-based Windows application for interactive testing of CDAS
   USB docking devices via the WinUSB transport layer.

   Features:
     - Device enumeration and connection
     - Serial number read / write
     - Image download from capsule (with progress)
     - Firmware update (boot / core / cam)
     - Raw command sending
     - Log output window

   Build: VS2022 v143, Win32, Windows subsystem

   --*/

#include "stdafx.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "..\include\bulkusb_api.h"
#include "..\include\winusb_compat.h"
#include "..\include\cdas_cmd.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "winusb.lib")
#pragma comment(lib, "shlwapi.lib")

/* ── Control IDs ────────────────────────────────────────────────── */

#define IDC_BTN_REFRESH       1001
#define IDC_BTN_CONNECT       1002
#define IDC_BTN_DISCONNECT    1003
#define IDC_STATUS_LABEL      1004

#define IDC_BTN_READ_SN       1010
#define IDC_BTN_WRITE_SN      1011
#define IDC_EDIT_SERIAL       1012

#define IDC_RADIO_IMG0        1020
#define IDC_RADIO_IMG1        1021
#define IDC_RADIO_IMG2        1022
#define IDC_EDIT_IMGFILE      1023
#define IDC_BTN_BROWSE_IMG    1024
#define IDC_BTN_DOWNLOAD_IMG  1025

#define IDC_COMBO_FW          1030
#define IDC_EDIT_FWFILE       1031
#define IDC_BTN_BROWSE_FW     1032
#define IDC_BTN_UPDATE_FW     1033

#define IDC_EDIT_RAW_CMD      1040
#define IDC_EDIT_RAW_SECTOR   1041
#define IDC_EDIT_RAW_COUNT    1042
#define IDC_BTN_SEND_RAW      1043

#define IDC_EDIT_LOG          1050
#define IDC_PROGRESS          1051

#define IDC_COMBO_DEVTYPE     1060
#define IDC_COMBO_VER         1061

#define IDT_PROGRESS          2002

/* ── Layout constants (pixels) ──────────────────────────────────── */

#define WIN_W   760
#define WIN_H   640
#define MARGIN   14
#define COL1     20
#define COL2    100
#define COL3    175
#define COL4    400
#define COL5    590
#define COL6    650
#define ROW0    12
#define ROW1     82
#define ROW2    140
#define ROW3    215
#define ROW4    285
#define ROW5    355
#define ROW6    548
#define ROW7    575
#define CTRL_H   24
#define BTN_W    60
#define GRP_PAD  20

/* ── Globals ─────────────────────────────────────────────────── */

static HINSTANCE g_hInst    = NULL;
static HCDAS      g_hDevice = NULL;
static HWND       g_hwndMain = NULL;
static HWND       g_hwndLog  = NULL;
static HWND       g_hwndProgress = NULL;
static HWND       g_hwndStatus = NULL;
static HWND       g_hBtnConnect = NULL;
static HWND       g_hBtnDisconnect = NULL;

/* ── Log helper ───────────────────────────────────────────────── */

static void AppendLog(const char* text)
{
    int len;
    if (!g_hwndLog) return;
    len = GetWindowTextLengthA(g_hwndLog);
    SendMessageA(g_hwndLog, EM_SETSEL, len, len);
    SendMessageA(g_hwndLog, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageA(g_hwndLog, EM_REPLACESEL, FALSE, (LPARAM)"\r\n");
    SendMessageA(g_hwndLog, EM_SCROLLCARET, 0, 0);
}

static void AppendLogF(const char* fmt, ...)
{
    char buf[1024];
    va_list a;
    va_start(a, fmt);
    _vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    AppendLog(buf);
}

static void SetConnected(BOOL connected)
{
    char t[64];
    _snprintf(t, sizeof(t), connected ? "Status: Connected" : "Status: Disconnected");
    SetWindowTextA(g_hwndStatus, t);
    EnableWindow(g_hBtnConnect, !connected);
    EnableWindow(g_hBtnDisconnect, connected);
}

/* ── Helpers: create child controls ────────────────────────────── */

static HWND CreateLabel(HWND parent, const char* text, int x, int y, int w, int h)
{
    return CreateWindowExA(0, "STATIC", text,
        WS_VISIBLE | WS_CHILD | SS_LEFT,
        x, y, w, h, parent, NULL, g_hInst, NULL);
}

static HWND CreateButton(HWND parent, const char* text, int x, int y, int w, int h, int id)
{
    return CreateWindowExA(0, "BUTTON", text,
        WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
}

static HWND CreateEdit(HWND parent, int x, int y, int w, int h, int id)
{
    return CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
}

static HWND CreateRadio(HWND parent, const char* text, int x, int y, int w, int h, int id)
{
    return CreateWindowExA(0, "BUTTON", text,
        WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
}

static HWND CreateGroup(HWND parent, const char* text, int x, int y, int w, int h)
{
    return CreateWindowExA(0, "BUTTON", text,
        WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        x, y, w, h, parent, NULL, g_hInst, NULL);
}

static HWND CreateCombo(HWND parent, int x, int y, int w, int h, int id)
{
    return CreateWindowExA(0, "COMBOBOX", "",
        WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
}

static HWND CreateLogEdit(HWND parent, int x, int y, int w, int h, int id)
{
    return CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_BORDER |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
}

static HWND CreateProgressBar(HWND parent, int x, int y, int w, int h, int id)
{
    return CreateWindowExA(0, PROGRESS_CLASSA, "",
        WS_VISIBLE | WS_CHILD,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, g_hInst, NULL);
}

/* ── Browse for file ──────────────────────────────────────────── */

static BOOL BrowseForFile(HWND parent, char* buf, int sz,
                          const char* filter, const char* defExt)
{
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = parent;
    ofn.lpstrFilter  = filter;
    ofn.lpstrDefExt  = defExt;
    ofn.lpstrFile    = buf;
    ofn.nMaxFile     = sz;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    return GetOpenFileNameA(&ofn);
}

/* ── Device actions ──────────────────────────────────────────────── */

static void DoConnect(void)
{
    int devType = (int)SendMessageA(GetDlgItem(g_hwndMain, IDC_COMBO_DEVTYPE),
                                    CB_GETCURSEL, 0, 0);
    int ver = (int)SendMessageA(GetDlgItem(g_hwndMain, IDC_COMBO_VER),
                                CB_GETCURSEL, 0, 0);
    if (devType < 0) devType = 0;
    if (ver < 0) ver = 0;

    g_hDevice = CdasOpen(devType, ver);
    if (g_hDevice) {
        SetConnected(TRUE);
        AppendLog("Device opened successfully.");
    } else {
        DWORD e = GetLastError();
        AppendLogF("Failed to open device. Error=%lu (0x%08lX)", e, e);
    }
}

static void DoDisconnect(void)
{
    if (g_hDevice) {
        CdasClose(g_hDevice);
        g_hDevice = NULL;
        SetConnected(FALSE);
        AppendLog("Device closed.");
    }
}

static void DoRefresh(void)
{
    int n = CdasEnumerateDevices();
    char msg[128];
    _snprintf(msg, sizeof(msg), "Found %d CDAS device(s)", n);
    AppendLog(msg);
    SetWindowTextA(g_hwndStatus, msg);
}

/* ── Serial number ────────────────────────────────────────────── */

static void DoReadSN(void)
{
    char buf[64] = "";
    if (!g_hDevice) { AppendLog("Error: No device connected."); return; }
    int r = CdasReadSerialNumber(g_hDevice, buf);
    if (r == CDAS_OK) {
        AppendLogF("Serial Number: %s", buf);
        SetDlgItemTextA(g_hwndMain, IDC_EDIT_SERIAL, buf);
    } else {
        AppendLogF("Read serial number failed: %s", CdasResultString(r));
    }
}

static void DoWriteSN(void)
{
    char buf[64] = "";
    if (!g_hDevice) { AppendLog("Error: No device connected."); return; }
    GetDlgItemTextA(g_hwndMain, IDC_EDIT_SERIAL, buf, sizeof(buf));
    int r = CdasWriteSerialNumber(g_hDevice, buf);
    if (r == CDAS_OK)
        AppendLogF("Serial number written: %s", buf);
    else
        AppendLogF("Write serial number failed: %s", CdasResultString(r));
}

/* ── Image download ─────────────────────────────────────────────── */

static int g_imgCur = 0, g_imgTotal = 0;

static void __stdcall ImgProgCb(int cur, int total, void* ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    g_imgCur = cur; g_imgTotal = total;
}

static void __stdcall ImgLogCb(const char* msg, void* ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    AppendLog(msg);
}

static void DoDownloadImage(void)
{
    char fn[MAX_PATH] = "";
    int opt = 0, r;

    if (!g_hDevice) { AppendLog("Error: No device connected."); return; }
    if (IsDlgButtonChecked(g_hwndMain, IDC_RADIO_IMG1) == BST_CHECKED) opt = 1;
    else if (IsDlgButtonChecked(g_hwndMain, IDC_RADIO_IMG2) == BST_CHECKED) opt = 2;

    GetDlgItemTextA(g_hwndMain, IDC_EDIT_IMGFILE, fn, MAX_PATH);
    if (fn[0] == '\0') { AppendLog("Error: Specify output filename."); return; }

    AppendLogF("Starting image download (option=%d) to %s ...", opt, fn);
    SendMessageA(g_hwndProgress, PBM_SETPOS, 0, 0);
    g_imgCur = 0; g_imgTotal = 0;

    SetTimer(g_hwndMain, IDT_PROGRESS, 200, NULL);
    r = CdasReadImage(g_hDevice, opt, fn, ImgProgCb, NULL, ImgLogCb, NULL);
    KillTimer(g_hwndMain, IDT_PROGRESS);

    if (r == CDAS_OK) {
        SendMessageA(g_hwndProgress, PBM_SETPOS, 100, 0);
        AppendLogF("Image saved: %s (%d pages)", fn, g_imgCur);
    } else {
        AppendLogF("Image download failed: %s", CdasResultString(r));
    }
}

/* ── Firmware update ───────────────────────────────────────────── */

static void __stdcall FwLogCb(const char* msg, void* ctx)
{
    UNREFERENCED_PARAMETER(ctx);
    AppendLog(msg);
}

static void DoUpdateFw(void)
{
    char fn[MAX_PATH] = "";
    char fwt[] = { CDAS_CMD_UPDATE_FW_BOOT, CDAS_CMD_UPDATE_FW_CORE, CDAS_CMD_UPDATE_FW_CAM };
    const char* fwn[] = { "Boot", "Core", "Cam" };
    int sel;

    if (!g_hDevice) { AppendLog("Error: No device connected."); return; }
    sel = (int)SendMessageA(GetDlgItem(g_hwndMain, IDC_COMBO_FW), CB_GETCURSEL, 0, 0);
    if (sel < 0 || sel > 2) sel = 0;

    GetDlgItemTextA(g_hwndMain, IDC_EDIT_FWFILE, fn, MAX_PATH);
    if (fn[0] == '\0') { AppendLog("Error: Specify firmware file."); return; }

    AppendLogF("Updating %s firmware from %s ...", fwn[sel], fn);
    SendMessageA(g_hwndProgress, PBM_SETPOS, 0, 0);

    int r = CdasUpdateFirmware(g_hDevice, fwt[sel], fn, NULL, NULL, FwLogCb, NULL);
    if (r == CDAS_OK)
        SendMessageA(g_hwndProgress, PBM_SETPOS, 100, 0);
    else
        AppendLogF("Firmware update failed: %s", CdasResultString(r));
}

/* ── Raw command ───────────────────────────────────────────────── */

static void DoSendRaw(void)
{
    char ct[16], st[16], nt[16];
    char buf[512 * 64];
    int cv, sv, r;
    short nv;

    if (!g_hDevice) { AppendLog("Error: No device connected."); return; }

    GetDlgItemTextA(g_hwndMain, IDC_EDIT_RAW_CMD, ct, sizeof(ct));
    GetDlgItemTextA(g_hwndMain, IDC_EDIT_RAW_SECTOR, st, sizeof(st));
    GetDlgItemTextA(g_hwndMain, IDC_EDIT_RAW_COUNT, nt, sizeof(nt));

    cv = strtol(ct, NULL, 0);
    sv = strtol(st, NULL, 0);
    nv = (short)strtol(nt, NULL, 0);

    if (nv > 0)
        r = CdasIssueCommand(g_hDevice, (char)cv, 1, sv, nv, buf);
    else
        r = CdasIssueCommand(g_hDevice, (char)cv, 1, sv, 0, NULL);

    if (r == CDAS_OK)
        AppendLogF("Command 0x%02X sent (sector=%d, count=%d)", cv, sv, nv);
    else
        AppendLogF("Command 0x%02X failed: %s", cv, CdasResultString(r));
}

/* ── Window procedure ────────────────────────────────────────── */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_CREATE:
    {
        HWND h;
        HFONT hMono;

        g_hwndMain = hwnd;

        /* ── Device Connection group ── */
        CreateGroup(hwnd, "Device Connection", MARGIN, ROW0, WIN_W - 2*MARGIN, 58);

        CreateLabel(hwnd, "Device:", COL1, ROW0 + GRP_PAD, 55, CTRL_H);
        h = CreateCombo(hwnd, COL1 + 58, ROW0 + GRP_PAD - 2, 160, 200, IDC_COMBO_DEVTYPE);
        SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)"Docking System");
        SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)"Production Test");
        SendMessageA(h, CB_SETCURSEL, 0, 0);

        CreateLabel(hwnd, "Version:", COL1 + 235, ROW0 + GRP_PAD, 55, CTRL_H);
        h = CreateCombo(hwnd, COL1 + 295, ROW0 + GRP_PAD - 2, 170, 200, IDC_COMBO_VER);
        SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)"Any");
        SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)"CDAS1 (03EB:941C)");
        SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)"CDAS2 (0638:0931)");
        SendMessageA(h, CB_SETCURSEL, 0, 0);

        CreateButton(hwnd, "Refresh",    COL5,           ROW0 + GRP_PAD, BTN_W, CTRL_H, IDC_BTN_REFRESH);
        g_hBtnConnect    = CreateButton(hwnd, "Connect",    COL5 + 65, ROW0 + GRP_PAD, BTN_W, CTRL_H, IDC_BTN_CONNECT);
        g_hBtnDisconnect = CreateButton(hwnd, "Disconnect", COL5 + 65, ROW0 + GRP_PAD + 26, BTN_W, CTRL_H, IDC_BTN_DISCONNECT);

        /* ── Serial Number group ── */
        CreateGroup(hwnd, "Serial Number", MARGIN, ROW1, WIN_W - 2*MARGIN, 48);
        CreateButton(hwnd, "Read", COL1, ROW1 + GRP_PAD, BTN_W, CTRL_H, IDC_BTN_READ_SN);
        CreateEdit(hwnd, COL1 + BTN_W + 8, ROW1 + GRP_PAD, 350, CTRL_H, IDC_EDIT_SERIAL);
        CreateButton(hwnd, "Write", COL1 + BTN_W + 368, ROW1 + GRP_PAD, BTN_W, CTRL_H, IDC_BTN_WRITE_SN);

        /* ── Image Download group ── */
        CreateGroup(hwnd, "Image Download", MARGIN, ROW2, WIN_W - 2*MARGIN, 62);
        CreateRadio(hwnd, "Normal",    COL1,        ROW2 + GRP_PAD, 75, CTRL_H, IDC_RADIO_IMG0);
        CreateRadio(hwnd, "Rigi-Flex", COL1 + 85,   ROW2 + GRP_PAD, 80, CTRL_H, IDC_RADIO_IMG1);
        CreateRadio(hwnd, "Fast GPIO", COL1 + 175,  ROW2 + GRP_PAD, 85, CTRL_H, IDC_RADIO_IMG2);
        CreateEdit(hwnd, COL1, ROW2 + GRP_PAD + 26, COL5 - COL1 - 80, CTRL_H, IDC_EDIT_IMGFILE);
        CreateButton(hwnd, "Browse",   COL5 - 72, ROW2 + GRP_PAD + 26, 68, CTRL_H, IDC_BTN_BROWSE_IMG);
        CreateButton(hwnd, "Download", COL5,       ROW2 + GRP_PAD + 26, BTN_W + 10, CTRL_H, IDC_BTN_DOWNLOAD_IMG);
        SetDlgItemTextA(hwnd, IDC_EDIT_IMGFILE, "image.bin");

        /* ── Firmware Update group ── */
        CreateGroup(hwnd, "Firmware Update", MARGIN, ROW3, WIN_W - 2*MARGIN, 58);
        h = CreateCombo(hwnd, COL1, ROW3 + GRP_PAD - 2, 150, 200, IDC_COMBO_FW);
        SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)"Boot (0x01)");
        SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)"Core (0x02)");
        SendMessageA(h, CB_ADDSTRING, 0, (LPARAM)"Cam (0x03)");
        SendMessageA(h, CB_SETCURSEL, 0, 0);
        CreateEdit(hwnd, COL1 + 160, ROW3 + GRP_PAD, COL5 - COL1 - 240, CTRL_H, IDC_EDIT_FWFILE);
        CreateButton(hwnd, "Browse",  COL5 - 72, ROW3 + GRP_PAD, 68, CTRL_H, IDC_BTN_BROWSE_FW);
        CreateButton(hwnd, "Update",  COL5,      ROW3 + GRP_PAD, BTN_W + 10, CTRL_H, IDC_BTN_UPDATE_FW);

        /* ── Raw Command group ── */
        CreateGroup(hwnd, "Raw Command", MARGIN, ROW4, WIN_W - 2*MARGIN, 58);
        CreateLabel(hwnd, "Cmd:",   COL1,       ROW4 + GRP_PAD, 35, CTRL_H);
        CreateEdit(hwnd, COL1 + 38, ROW4 + GRP_PAD, 55, CTRL_H, IDC_EDIT_RAW_CMD);
        CreateLabel(hwnd, "Sector:", COL1 + 105, ROW4 + GRP_PAD, 45, CTRL_H);
        CreateEdit(hwnd, COL1 + 155, ROW4 + GRP_PAD, 65, CTRL_H, IDC_EDIT_RAW_SECTOR);
        CreateLabel(hwnd, "Count:", COL1 + 230, ROW4 + GRP_PAD, 42, CTRL_H);
        CreateEdit(hwnd, COL1 + 278, ROW4 + GRP_PAD, 65, CTRL_H, IDC_EDIT_RAW_COUNT);
        CreateButton(hwnd, "Send",   COL5, ROW4 + GRP_PAD, BTN_W + 10, CTRL_H, IDC_BTN_SEND_RAW);

        /* ── Log output ── */
        g_hwndLog = CreateLogEdit(hwnd, MARGIN, ROW5, WIN_W - 2*MARGIN, 178, IDC_EDIT_LOG);
        hMono = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, "Consolas");
        SendMessageA(g_hwndLog, WM_SETFONT, (WPARAM)hMono, TRUE);

        /* ── Progress bar ── */
        g_hwndProgress = CreateProgressBar(hwnd, MARGIN, ROW6, WIN_W - 2*MARGIN, 20, IDC_PROGRESS);
        SendMessageA(g_hwndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

        /* ── Status label ── */
        g_hwndStatus = CreateLabel(hwnd, "Status: Disconnected", MARGIN, ROW7, WIN_W - 2*MARGIN, 20);

        SetConnected(FALSE);
        AppendLog("CDAS USB Test Tool started.");
        DoRefresh();
        return 0;
    }

    case WM_TIMER:
        if (wParam == IDT_PROGRESS) {
            if (g_imgTotal > 0) {
                int pct = (int)(100UL * g_imgCur / g_imgTotal);
                SendMessageA(g_hwndProgress, PBM_SETPOS, pct, 0);
            } else if (g_imgCur > 0) {
                SendMessageA(g_hwndProgress, PBM_SETPOS, g_imgCur % 100, 0);
            }
        }
        return 0;

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        switch (id) {
        case IDC_BTN_REFRESH:      DoRefresh(); break;
        case IDC_BTN_CONNECT:      DoConnect(); break;
        case IDC_BTN_DISCONNECT:   DoDisconnect(); break;
        case IDC_BTN_READ_SN:      DoReadSN(); break;
        case IDC_BTN_WRITE_SN:     DoWriteSN(); break;
        case IDC_BTN_BROWSE_IMG:
        {
            char fn[MAX_PATH] = "";
            if (BrowseForFile(hwnd, fn, MAX_PATH,
                              "Binary Files\0*.bin\0All Files\0*.*\0", "bin"))
                SetDlgItemTextA(hwnd, IDC_EDIT_IMGFILE, fn);
            break;
        }
        case IDC_BTN_DOWNLOAD_IMG: DoDownloadImage(); break;
        case IDC_BTN_BROWSE_FW:
        {
            char fn[MAX_PATH] = "";
            if (BrowseForFile(hwnd, fn, MAX_PATH,
                              "Firmware Files\0*.bin\0All Files\0*.*\0", "bin"))
                SetDlgItemTextA(hwnd, IDC_EDIT_FWFILE, fn);
            break;
        }
        case IDC_BTN_UPDATE_FW:    DoUpdateFw(); break;
        case IDC_BTN_SEND_RAW:     DoSendRaw(); break;
        }
        return 0;
    }

    case WM_CLOSE:
        DoDisconnect();
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

/* ── Entry point ──────────────────────────────────────────────── */

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEXA wc;
    HWND hwnd;
    MSG msg;
    INITCOMMONCONTROLSEX icc;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    g_hInst = hInstance;

    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_PROGRESS_CLASS;
    InitCommonControlsEx(&icc);

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance      = hInstance;
    wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName  = "CDAS_TestTool_Class";
    wc.hIcon          = LoadIcon(NULL, IDI_APPLICATION);
    wc.style          = CS_HREDRAW | CS_VREDRAW;

    if (!RegisterClassExA(&wc)) return 1;

    /* Center the window on screen */
    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int x = (scrW - WIN_W) / 2;
    int y = (scrH - WIN_H) / 2;

    hwnd = CreateWindowExA(
        0, wc.lpszClassName, "CDAS USB Test Tool",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, WIN_W, WIN_H,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    return (int)msg.wParam;
}
