// **********************************************************
//
//  Comonitor
//  COM-port devices monitor tool
//  (c) 2022 ACE
//  ace@imlazy.ru
//
//  16.02.2022 v1.0
//
// **********************************************************

#define NTDDI_VERSION NTDDI_VISTA
#include <windows.h>
#include <setupapi.h>
#include <dbt.h>
#include <strsafe.h>
#include <sysinfoapi.h>
#include <windowsx.h>
#include "resource.h"

#define VER "v1.0"
#define FONT_SIZE 18
#define NEW_MS (5*60*1000) // time, device counted as new, ms
#define ITEM_SIZE (FONT_SIZE * 2 + 4)
#define WM_TRAY WM_USER

#define WSEXs WS_EX_CLIENTEDGE | WS_EX_TOOLWINDOW | WS_EX_APPWINDOW //| WS_EX_TOPMOST
#define WSs WS_OVERLAPPEDWINDOW

HWND hMainWnd, hListWnd;
HINSTANCE hInst;

const GUID GUID_DEVINTERFACE_SERENUM_BUS_ENUMERATOR =
            { 0x4D36E978, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 }};

// ----------------- port list class ----------------------
enum State { psNew, psOld, psDelete };
typedef struct Port
{
  Port *next;
  TCHAR *port, *name, *mfg;
  int num;
  State state;
  DWORD added;
} Port;

class Ports
{
private:
    Port *ports;
    void DeletePortsFrom(Port *first);
    void freeItem(Port *item);
public:
    Ports() { ports = 0; }
    ~Ports() { this->DeleteAllPorts(); }
    void DeleteAllPorts();
    Port *AddPort(TCHAR *port, TCHAR *name, TCHAR *mfg, bool juststarted = FALSE);
    int Count();
    Port *item(int index);
    void Sort();
    void MarkAllToDelete();
    void RemoveDeleted();
    Port *Search(TCHAR *port);
};

Ports ports;

void Ports::freeItem(Port *item)
{
    if (!item) return;
    if (item->port) free(item->port);
    if (item->name) free(item->name);
    if (item->mfg) free(item->mfg);
    free(item);
}

void Ports::DeleteAllPorts()
{
    this->DeletePortsFrom(ports);
    this->ports = 0;
}

void Ports::DeletePortsFrom(Port *first)
{
    if (!first) return;
    if (first->next) DeletePortsFrom(first->next);
    freeItem(first);
}

Port *Ports::AddPort(TCHAR *port, TCHAR *name, TCHAR *mfg, bool juststarted)
{
    Port **p;
    p = &this->ports;
    while (*p != NULL) p = &((*p)->next);
    *p = (Port*)malloc(sizeof(Port));
    (*p)->next = 0;
    (*p)->port = port;
    (*p)->name = name;
    (*p)->mfg = mfg;
    (*p)->num = 0;
    if (port && _tcslen(port) > 3 && port[0] == 'C' && port[1] == 'O' && port[2] == 'M')
    {
        int i = 3, n = 0;
        while (port[i] >= '0' && port[i] <='9')
            n = n*10 + port[i++] - '0';
        if (port[i] == 0) (*p)->num = n;
    }
    (*p)->state = psNew;
    if (juststarted)
        (*p)->added = 0;
    else
        if (((*p)->added = GetTickCount()) == 0) (*p)->added++; // in case GetTickCount equals 0 at overflow
    return *p;
}

int Ports::Count()
{
    Port *p;
    int cnt = 0;
    p = this->ports;
    while (p != NULL)
    {
        p = p->next;
        cnt++;
    }
    return cnt;
}

void Ports::MarkAllToDelete()
{
    Port *p;
    p = this->ports;
    while (p != NULL)
    {
        p->state = psDelete;
        p = p->next;
    }
}

Port *Ports::item(int index)
{
    Port *p;
    p = this->ports;
    while (index>0)
    {
        if (p == NULL) return NULL;
        p = p->next;
        index--;
    }
    return p;
}

void Ports::Sort()
{
    Port **s, **d, **min;
    Port *res = NULL;
    d = &res;
    while (this->ports)
    {
        s = &this->ports;
        min = s;
        while (*s != NULL)
        {
            if ((*min)->num > (*s)->num) min = s;
            s = &((*s)->next);
        }
        *d = *min;
        *min = (*min)->next;
        (*d)->next = 0;
        d = &(*d)->next;
    }
    this->ports = res;
}

void Ports::RemoveDeleted()
{
    Port **s, *t;
    s = &this->ports;
    while (*s != NULL)
    {
        if ((*s)->state == psDelete)
        {
            t = (*s)->next;
            freeItem(*s);
            *s = t;
        } else
            s = &((*s)->next);
    }
}

Port *Ports::Search(TCHAR *port)
{
    Port *s;
    s = this->ports;
    while (s != NULL)
    {
        if (_tcscmp(s->port, port) == 0) return s;
        s = s->next;
    }
    return NULL;
}

// ----------------- tray icon ----------------------

void FillNID(NOTIFYICONDATA &nid)
{
    TCHAR buf[10];
    nid.cbSize = sizeof(nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    nid.hWnd = hMainWnd;
    nid.uFlags = /*NIF_ICON |*/ NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP | NIF_GUID /*| NIF_INFO */;
    static const GUID myGUID = {0x2bab4184, 0x0ACE, 0x4041, {0xf4, 0x92, 0xbd, 0x5b, 0x7b, 0x17, 0x36, 0x69}};
    nid.guidItem = myGUID;
    nid.dwInfoFlags = NIIF_USER | NIIF_NOSOUND | NIIF_LARGE_ICON;
    nid.uCallbackMessage = WM_TRAY;

    // This text will be shown as the icon's tooltip.
    StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), L"COM-port devices monitor\nCOM-ports: ");
    _itot(ports.Count(), buf, 10);
    StringCchCat(nid.szTip, ARRAYSIZE(nid.szTip), buf);

    // Load the icon for high DPI.
//    LoadIconMetric(hInst, MAKEINTRESOURCE(IDI_SMALL), LIM_SMALL, &(nid.hIcon));
}

void AddNotify()
{
    NOTIFYICONDATA nid = {};
    FillNID(nid);
    nid.uFlags |= NIF_ICON;
    nid.hIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    //LoadIconMetric(hInst, MAKEINTRESOURCE(IDI_ICON), LIM_LARGE, &(nid.hIcon));
    Shell_NotifyIcon(NIM_ADD, &nid);
    Shell_NotifyIcon(NIM_SETVERSION, &nid);
}

void UpdateNotify()
{ // update tray icon hint
    NOTIFYICONDATA nid = {};
    FillNID(nid);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void ShowNotify(Port *new_port)
{
    NOTIFYICONDATA nid = {};
    FillNID(nid);
    nid.uFlags |= NIF_INFO;
    StringCchCopy(nid.szInfo, ARRAYSIZE(nid.szInfo), new_port->mfg);
    StringCchCat(nid.szInfo, ARRAYSIZE(nid.szInfo), L"\n");
    StringCchCat(nid.szInfo, ARRAYSIZE(nid.szInfo), new_port->name);
    StringCchCopy(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), new_port->port);
    //StringCchCat(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), L" device connected");
    nid.hBalloonIcon = (HICON)LoadImage(hInst, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR | LR_DEFAULTSIZE);;
    //LoadIconMetric(hInst, MAKEINTRESOURCE(IDI_ICON), LIM_LARGE, &(nid.hBalloonIcon));
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void DeleteNotify()
{
    NOTIFYICONDATA nid = {};
    FillNID(nid);
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// ----------------- devices ----------------------

void GetDeviceProperty(HDEVINFO DeviceInfoSet, PSP_DEVINFO_DATA deviceInfoData, DWORD Property, TCHAR **val)
{
    DWORD dwType = 0;
    DWORD dwSize;
    TCHAR *v = 0;
    if (SetupDiGetDeviceRegistryProperty(DeviceInfoSet, deviceInfoData, Property, &dwType, NULL, 0, &dwSize) ||
            GetLastError() == ERROR_INSUFFICIENT_BUFFER)
    {
        v = (TCHAR*)malloc(dwSize);
        if (!SetupDiGetDeviceRegistryProperty(DeviceInfoSet, deviceInfoData, Property, &dwType, (unsigned char *)v, dwSize, &dwSize) ||
            dwType != REG_SZ)
        {
            free(v);
            v = 0;
        }
    }
    *val = v;
}

TCHAR *allocAndSet(const TCHAR *val)
{
    TCHAR *buf;
    int sz;
    sz = _tcslen(val) * sizeof(TCHAR) + 2;
    buf = (TCHAR *)malloc(sz);
    StringCbCopy(buf, sz, val);
    return buf;
}

void UpdatePortList(bool juststarted = FALSE)
{
    Port *new_port = 0;
    ports.MarkAllToDelete();

    HDEVINFO hDevInfoSet = SetupDiGetClassDevs(&GUID_DEVINTERFACE_SERENUM_BUS_ENUMERATOR, nullptr, nullptr, DIGCF_PRESENT);
    if (hDevInfoSet == INVALID_HANDLE_VALUE) return;

    int nIndex = 0;
    SP_DEVINFO_DATA devInfo{};
    while (1)
    {
        // Enumerate the current device
        devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
        if (!SetupDiEnumDeviceInfo(hDevInfoSet, nIndex, &devInfo)) break;

        TCHAR *port, *name, *mfg;
        DWORD dwSize;

        //Get the registry key which stores the ports settings
        HKEY key = SetupDiOpenDevRegKey(hDevInfoSet, &devInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
        port = 0;
        if (key != INVALID_HANDLE_VALUE)
        {
            if (RegGetValue(key, NULL, _T("PortName"), RRF_RT_REG_SZ, NULL, NULL, &dwSize) == ERROR_SUCCESS)
            {
                port = (TCHAR*)malloc(dwSize);
                if (RegGetValue(key, NULL, _T("PortName"), RRF_RT_REG_SZ, NULL, port, &dwSize) != ERROR_SUCCESS)
                {
                    free(port);
                    port = 0;
                }
            }
            RegCloseKey(key);
        }
        GetDeviceProperty(hDevInfoSet, &devInfo, SPDRP_MFG, &mfg);
        GetDeviceProperty(hDevInfoSet, &devInfo, SPDRP_DEVICEDESC, &name);
        if (!port) port = allocAndSet(TEXT("<unknown>"));
        if (!name) name = allocAndSet(TEXT("<unknown COM-port>"));
        if (!mfg) mfg = allocAndSet(TEXT("<unknown>"));
        Port *p = ports.Search(port);
        if (p)
        { // port exists, just update info, if needed
//            MessageBox(NULL, name, port, MB_OK);
            p->state = psOld;
            free(port);
            if (_tcscmp(p->mfg, mfg) != 0)
            { // update mfg name
                free(p->mfg);
                p->mfg = mfg;
            }
            else
                free(mfg);
            if (_tcscmp(p->name, name) != 0)
            { // update port name
                free(p->name);
                p->name = name;
            }
            else
                free(name);
        } else
        {
            new_port = ports.AddPort(port, name, mfg, juststarted);
        }

        nIndex++;
    }

    SetupDiDestroyDeviceInfoList(hDevInfoSet);
    ports.RemoveDeleted();
    ports.Sort();
    if (new_port && !juststarted)
        ShowNotify(new_port);
    else
        UpdateNotify();
}

BOOL DoRegisterDeviceInterfaceToHwnd(
    IN GUID InterfaceClassGuid,
    IN HWND hWnd,
    OUT HDEVNOTIFY* hDeviceNotify
)
//     Registers an HWND for notification of changes in the device interfaces
//     for the specified interface class GUID.
{
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;

    ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = InterfaceClassGuid;

    *hDeviceNotify = RegisterDeviceNotification(
        hWnd,                       // events recipient
        &NotificationFilter,        // type of device
        DEVICE_NOTIFY_WINDOW_HANDLE // type of recipient handle
    );

    if (NULL == *hDeviceNotify)
    {
        //ErrorHandler(L"RegisterDeviceNotification");
        return FALSE;
    }

    return TRUE;
}

// ----------------- windows ----------------------

#define MAIN_WND_CLASS_NAME TEXT("ComonitorAppWindowClass")
#define LIST_WND_CLASS_NAME TEXT("ComonitorListWindowClass")
#define g_pszAppName TEXT("Comonitor")

void MessagePump(HWND /*hWnd*/)
{
    MSG msg;
    int retVal;

    while ((retVal = GetMessage(&msg, NULL, 0, 0)) != 0)
    {
        if (retVal == -1)
            break;
        else
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

HWND hList;

void AdjustListWndSize(int &cx, int &cy)
{
    RECT r;
    cx = 400;
    cy = ITEM_SIZE * ports.Count();
    r.left = r.top = 0;
    r.right = cx;
    r.bottom = cy;
    AdjustWindowRectEx(&r, WSs, FALSE, WSEXs);
    cx = r.right - r.left;
    cy = r.bottom - r.top;
}

void AdjustListWndPos()
{
    RECT r, w;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &r, 0);
    GetWindowRect(hListWnd, &w);
    if (w.bottom > r.bottom) { w.top = r.bottom - (w.bottom - w.top); }
    SetWindowPos(hListWnd, 0, w.left, w.top, w.right, w.bottom, SWP_NOSIZE);
}

void UpdateListBox()
{
    int cx, cy;
    SendMessage(hList, LB_RESETCONTENT, 0, 0);
    for (int i=0; i < ports.Count(); i++)
        SendMessage(hList, LB_ADDSTRING, 0, (LPARAM)TEXT(""));
    AdjustListWndSize(cx, cy);
    SetWindowPos(hListWnd, HWND_TOP, 0, 0, cx, cy, SWP_NOMOVE);
    AdjustListWndPos();
}

INT_PTR WINAPI ListWinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT lRet = 1;

    switch (message)
    {
    case WM_CREATE:
        hList = CreateWindowEx(0, TEXT("listbox"), TEXT(""),
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL | LBS_NOSEL | LBS_OWNERDRAWFIXED,
                               10, 10, 450, 450, hWnd, NULL, NULL, NULL);
        UpdateListBox();
        break;

    case WM_SIZE:
        MoveWindow(hList,
            0, 0,                  // starting x- and y-coordinates
            LOWORD(lParam),        // width of client area
            HIWORD(lParam),        // height of client area
            TRUE);                 // repaint window
        break;
    case WM_ACTIVATE:
        //if (LOWORD(wParam) == WA_INACTIVE) ShowWindow(hListWnd, SW_HIDE);
        break;
    case WM_CLOSE:
        ShowWindow(hListWnd, SW_HIDE);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_MEASUREITEM:
        LPMEASUREITEMSTRUCT mis;
        mis = (LPMEASUREITEMSTRUCT) lParam;
        mis->itemHeight = ITEM_SIZE;
        lRet = TRUE;
        break;
    case WM_DRAWITEM:
    {
        LPDRAWITEMSTRUCT dis;
        HFONT hFont, hfontOld;
        Port *p;
        dis = (LPDRAWITEMSTRUCT) lParam;
        p = ports.item(dis->itemID);
        if (!p) break;

        DrawEdge(dis->hDC, &dis->rcItem, EDGE_RAISED, BF_BOTTOM);
        hFont = CreateFont(FONT_SIZE,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_OUTLINE_PRECIS,
                           CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY, VARIABLE_PITCH,TEXT("Tahoma"));
        hfontOld = (HFONT)SelectObject(dis->hDC, hFont);
        ExtTextOut(dis->hDC, dis->rcItem.left+3, dis->rcItem.top + 1, 0,
                   &dis->rcItem, p->port,
                   _tcslen(p->port), NULL);
        SelectObject(dis->hDC, hfontOld);
        DeleteObject(hFont);
        hFont = CreateFont(FONT_SIZE,0,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_OUTLINE_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY, VARIABLE_PITCH,TEXT("Tahoma"));
        SelectObject(dis->hDC, hFont);
        ExtTextOut(dis->hDC, dis->rcItem.left+80, dis->rcItem.top + 1, 0,
                        &dis->rcItem, p->mfg,
                        _tcslen(p->mfg), NULL);
        ExtTextOut(dis->hDC, dis->rcItem.left+3, dis->rcItem.top + FONT_SIZE, 0,
                        &dis->rcItem, p->name,
                        _tcslen(p->name), NULL);
        SYSTEMTIME st;
        GetSystemTime(&st);

        // if port added in last NEW_MS ms then mark it as NEW
        if (p->added != 0 && GetTickCount() - p->added < NEW_MS) ExtTextOut(dis->hDC, dis->rcItem.right-50, dis->rcItem.top + 1, 0,
                        &dis->rcItem, TEXT("NEW"), 3, NULL);
        SelectObject(dis->hDC, hfontOld);
        DeleteObject(hFont);
        lRet = TRUE;
    }
        break;
    default:
        // Send all other messages on to the default windows handler.
        lRet = DefWindowProc(hWnd, message, wParam, lParam);
        break;
    }

    return lRet;
}

INT_PTR WINAPI MainWinProcCallback(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT lRet = 1;
    static HDEVNOTIFY hDeviceNotify;

    switch (message)
    {
    case WM_CREATE:
        if (!DoRegisterDeviceInterfaceToHwnd(
            GUID_DEVINTERFACE_SERENUM_BUS_ENUMERATOR,
            hWnd,
            &hDeviceNotify))
        {
            // Terminate on failure.
            MessageBox(hWnd, TEXT("DoRegisterDeviceInterfaceToHwnd() failed"), TEXT("Error"), MB_OK | MB_ICONEXCLAMATION);
            ExitProcess(1);
        }
        break;

    case WM_DEVICECHANGE:
        if (wParam == DBT_DEVNODES_CHANGED)
        {
            UpdatePortList();
            UpdateListBox();
        }
        break;
    case WM_CLOSE:
        UnregisterDeviceNotification(hDeviceNotify);
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_COMMAND:
        if (HIWORD(wParam) == 0 && LOWORD(wParam) == ID_EXIT) DestroyWindow(hMainWnd);
        if (HIWORD(wParam) == 0 && LOWORD(wParam) == ID_ABOUT)
            MessageBox(hMainWnd, TEXT("COM-port devices monitor " VER "\n(C)2022 ACE\nmailto: ace@imlazy.ru\nhttps://github.com/ACE1046/Comonitor"), TEXT("Comonitor"), MB_ICONINFORMATION | MB_OK);
        break;

    case WM_TRAY:
    {
        WORD msg = LOWORD(lParam);
//        int x = GET_X_LPARAM(wParam);
//        int y = GET_Y_LPARAM(wParam);
//        int x = GET_X_LPARAM(GetMessagePos());
//        int y = GET_Y_LPARAM(GetMessagePos());
        POINT p;
        GetCursorPos(&p);
        int x = p.x;
        int y = p.y;
        int cx, cy;
        switch (msg)
        {
        case WM_CONTEXTMENU: // right click
        {
            HMENU hmenu = LoadMenu(hInst, MAKEINTRESOURCE(IDR_MENU));
            SetForegroundWindow(hMainWnd);
            TrackPopupMenuEx(GetSubMenu(hmenu, 0), TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_NOANIMATION,
                             x, y, hMainWnd, NULL);
            PostMessage(hMainWnd, WM_NULL, 0, 0);
            DestroyMenu(hmenu);
        }
            break;
        case NIN_SELECT:
        case NIN_KEYSELECT: // left click
            AdjustListWndSize(cx, cy);
            SetWindowPos(hListWnd, HWND_TOP, x-cx, y-cy, cx, cy, 0);
            AdjustListWndPos();
            ShowWindow(hListWnd, SW_SHOW);
            SetForegroundWindow(hListWnd);
            BringWindowToTop(hListWnd);
            break;
        }
    }
        break;
    default:
        // Send all other messages on to the default windows handler.
        lRet = DefWindowProc(hWnd, message, wParam, lParam);
        break;
    }

    return lRet;
}

BOOL InitWindowClass()
{
    WNDCLASSEX wndClass;

    wndClass.cbSize = sizeof(WNDCLASSEX);
    wndClass.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wndClass.hInstance = hInst;
    wndClass.lpfnWndProc = reinterpret_cast<WNDPROC>(MainWinProcCallback);
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hIcon = (HICON)LoadImage(wndClass.hInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON,
                               0, 0, LR_DEFAULTCOLOR | LR_DEFAULTSIZE);
//    wndClass.hIcon = (HICON)LoadIcon(wndClass.hInstance, MAKEINTRESOURCE(IDI_ICON));
    wndClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wndClass.hCursor = LoadCursor(0, IDC_ARROW);
    wndClass.lpszClassName = MAIN_WND_CLASS_NAME;
    wndClass.lpszMenuName = NULL;
    wndClass.hIconSm = (HICON)LoadImage(wndClass.hInstance, MAKEINTRESOURCE(IDI_ICON), IMAGE_ICON,
                                 GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    if (!RegisterClassEx(&wndClass)) return FALSE;

    wndClass.lpfnWndProc = reinterpret_cast<WNDPROC>(ListWinProcCallback);
    wndClass.lpszClassName = LIST_WND_CLASS_NAME;

    if (!RegisterClassEx(&wndClass)) return FALSE;

    return TRUE;
}

int main(int /*argc*/, char */*argv*/ [])
{
    hInst = (HINSTANCE)GetModuleHandle(0);
    UpdatePortList(TRUE); // just started, mark all devices as not new

    if (!InitWindowClass())
    {
        MessageBox(NULL, TEXT("InitWindowClass() failed"), TEXT("Error"), MB_OK | MB_ICONEXCLAMATION);
        return -1;
    }

    hMainWnd = CreateWindowEx(
        WS_EX_CLIENTEDGE | WS_EX_APPWINDOW,
        MAIN_WND_CLASS_NAME,
        g_pszAppName,
        WS_OVERLAPPEDWINDOW, // style
        CW_USEDEFAULT, 0,
        640, 480,
        NULL, NULL,
        NULL,
        NULL);

    if (hMainWnd == NULL)
    {
        MessageBox(NULL, TEXT("Create main window failed"), TEXT("Error"), MB_OK | MB_ICONEXCLAMATION);
        return -1;
    }

    hListWnd = CreateWindowEx(
        WSEXs,
        LIST_WND_CLASS_NAME,
        g_pszAppName,
        WSs  , // style
        CW_USEDEFAULT, 0,
        640, 480,
        NULL/*hMainWnd*/, NULL,
        NULL,
        NULL);

    if (hListWnd == NULL)
    {
        MessageBox(NULL, TEXT("Create list window failed"), TEXT("Error"), MB_OK | MB_ICONEXCLAMATION);
        return -1;
    }

    //ShowWindow(hMainWnd, SW_SHOWNORMAL);
    ShowWindow(hMainWnd, SW_HIDE);
    ShowWindow(hListWnd, SW_HIDE);
    UpdateWindow(hMainWnd);
    UpdateWindow(hListWnd);

    AddNotify();

    MessagePump(hMainWnd);

    DeleteNotify();

    return 0;
}
