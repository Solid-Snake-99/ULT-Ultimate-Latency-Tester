// Controller Latency Tester - DirectX 12, minimal 1080p 3D scene
// Measures controller response latency:
//  - Push the LEFT ANALOG STICK to the RIGHT from center.
//  - The view (camera) rotates right; the stopwatch starts at the first
//    detected stick movement and stops when the view has turned exactly 90
//    degrees (the red center line touches the green 90-degree reference line
//    on the ground).
// Input: XInput (Xbox controllers / DS4 in XInput mode) and DirectInput
//        (native DualShock 4 and other gamepads), auto-detected.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <mmsystem.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")

using namespace std;
using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------- constants
constexpr UINT   WIDTH           = 1920;
constexpr UINT   HEIGHT          = 1080;
constexpr UINT   FRAME_COUNT     = 2;
constexpr double PI              = 3.14159265358979323846;
constexpr double TARGET_RADIANS  = PI / 2.0;          // 90 degrees
constexpr double DEADZONE        = 0.10;
constexpr double RESET_ZONE      = 0.04;
constexpr double SENS_DEFAULT    = PI / 2.0;          // full stick = 90 deg/s
constexpr double PITCH           = -0.12;             // camera looks slightly down
constexpr double VFOV_DEG        = 55.0;
constexpr double CAM_Y           = 1.6;
constexpr double ASPECT          = (double)WIDTH / (double)HEIGHT;
constexpr int    POLL_HZ         = 2000;              // input thread rate
constexpr int    TARGET_FPS      = 500;               // default frame cap
constexpr int    MENU_LINES      = 9;                 // HUD text lines available
constexpr int    FPS_COUNT       = 8;
constexpr int    FPS_LIST[FPS_COUNT] = { 30, 60, 120, 144, 180, 240, 500, 0 }; // 0 = unlimited

constexpr int RES_COUNT = 4;
struct ResOption { UINT w; UINT h; const wchar_t* name; };
static const ResOption RES_LIST[RES_COUNT] = {
    { 1920, 1080, L"1080p (1920x1080)" },
    { 1600, 900,  L"900p (1600x900)" },
    { 1280, 720,  L"720p (1280x720)" },
    { 960,  540,  L"540p (960x540)" }
};

constexpr int GRAPHICS_COUNT = 3;
static const wchar_t* GRAPHICS_LIST[GRAPHICS_COUNT] = {
    L"MINIMA ( Ultra Fast - Latenza Zero )",
    L"MEDIA",
    L"ALTA"
};

constexpr int INPUT_SRC_COUNT = 5;
static const wchar_t* INPUT_SRC_LIST[INPUT_SRC_COUNT] = {
    L"AUTO ( Nativo 1000Hz )",
    L"DS4 NATIVO ( DualShock 4 1000Hz )",
    L"DS5 NATIVO ( DualSense 5 1000Hz )",
    L"XINPUT ( Xbox Controller )",
    L"DIRECTINPUT"
};

constexpr int LANG_COUNT = 2;
static const wchar_t* LANG_LIST[LANG_COUNT] = {
    L"ITALIANO",
    L"ENGLISH"
};

constexpr int MENU_ROW_COUNT = 9;
constexpr int STICK_LATENCY_COUNT = 8;
constexpr double STICK_LATENCY_LIST[STICK_LATENCY_COUNT] = { 0.0, 2.5, 5.0, 7.5, 10.0, 12.5, 15.0, 20.0 };

static bool g_running = true;
static HWND g_hwnd = nullptr;
static double g_qpcFreq = 0.0;
static atomic<int> g_monitorHz{60};

// ESC menu settings (toggled from wndProc / inputThread)
static atomic<int>  g_fpsIdx{6};          // index into FPS_LIST (500 FPS)
static atomic<int>  g_resIdx{0};          // 0 = 1080p, 1 = 900p, 2 = 720p, 3 = 540p
static atomic<int>  g_reqResIdx{0};       // requested target resolution (main-thread deferred)
static atomic<int>  g_gfxIdx{0};          // 0 = MINIMA (Ultra Fast), 1 = MEDIA, 2 = ALTA
static atomic<int>  g_inputSrcIdx{0};     // 0 = AUTO, 1 = DS4 Native, 2 = DS5 Native, 3 = XInput, 4 = DirectInput
static atomic<int>  g_displayMode{0};     // 0 = windowed, 1 = (borderless) fullscreen
static atomic<int>  g_reqDisplayMode{0};  // requested display mode (main-thread deferred)
static atomic<int>  g_vsyncIdx{0};        // 0 = Disabled (Lowest Latency), 1 = Enabled
static atomic<int>  g_langIdx{0};         // 0 = Italian, 1 = English
static atomic<int>  g_stickCurveIdx{0};   // 0 = Linear Analog, 1 = Instant Max Speed
static atomic<double> g_userCustomStickLatencyMs{0.0};
static wchar_t      g_stickLatencyInputBuf[16] = L"0.0";
static atomic<double> g_lastJitterHumanMs{0.0};
static atomic<int>  g_measuredPollingHz{1000};
static atomic<uint64_t> g_rawPacketCount{0};
static atomic<uint64_t> g_xinputPacketCount{0};
static atomic<double> g_selectFlashT{0.0};
static atomic<int>  g_selectFlashRow{-1};
static atomic<bool> g_menuOpen{false};
static int          g_menuRow = 0;        // 0..8

// DS4 RawInput button state tracking
static BYTE g_ds4RawButtons = 0;
static bool g_ds4RawOptions = false;
static BYTE g_ds4RawDpad = 8;

// current render-target (swapchain) size, follows the window client area
static atomic<int> g_rtW{(int)WIDTH};
static atomic<int> g_rtH{(int)HEIGHT};
static atomic<bool> g_deviceChangeTriggered{false};

static inline void debugLog(const char* s) {
#ifdef _DEBUG
    OutputDebugStringA(s);
    OutputDebugStringA("\n");
#else
    (void)s;
#endif
}

static double QPC() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return (double)q.QuadPart / g_qpcFreq;
}

// ---------------------------------------------------- minimal CD3DX12-like helpers
static D3D12_HEAP_PROPERTIES HeapProp(D3D12_HEAP_TYPE t) {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = t;
    p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    p.CreationNodeMask = 1;
    p.VisibleNodeMask = 1;
    return p;
}
static D3D12_RESOURCE_DESC BufferDesc(UINT64 size) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Alignment = 0;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = D3D12_RESOURCE_FLAG_NONE;
    return d;
}
static D3D12_RESOURCE_DESC Tex2DDesc(DXGI_FORMAT fmt, UINT w, UINT h) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Alignment = 0;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = fmt;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = D3D12_RESOURCE_FLAG_NONE;
    return d;
}
static D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* res,
                                                D3D12_RESOURCE_STATES before,
                                                D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    return b;
}
static D3D12_DESCRIPTOR_RANGE DescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE t,
    UINT n, UINT baseReg) {
    D3D12_DESCRIPTOR_RANGE r{};
    r.RangeType = t;
    r.NumDescriptors = n;
    r.BaseShaderRegister = baseReg;
    r.RegisterSpace = 0;
    r.OffsetInDescriptorsFromTableStart = 0;
    return r;
}
static D3D12_ROOT_PARAMETER RootTable(const D3D12_DESCRIPTOR_RANGE* r, UINT n,
    D3D12_SHADER_VISIBILITY v) {
    D3D12_ROOT_PARAMETER p{};
    p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    p.DescriptorTable.NumDescriptorRanges = n;
    p.DescriptorTable.pDescriptorRanges = r;
    p.ShaderVisibility = v;
    return p;
}
static D3D12_ROOT_PARAMETER RootCBV(UINT reg, D3D12_SHADER_VISIBILITY v) {
    D3D12_ROOT_PARAMETER p{};
    p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    p.Descriptor.ShaderRegister = reg;
    p.ShaderVisibility = v;
    return p;
}
static D3D12_STATIC_SAMPLER_DESC StaticSampler(UINT reg, D3D12_FILTER f) {
    D3D12_STATIC_SAMPLER_DESC s{};
    s.Filter = f;
    s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.MipLODBias = 0;
    s.MaxAnisotropy = 1;
    s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    s.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    s.MinLOD = 0;
    s.MaxLOD = 32;
    s.ShaderRegister = reg;
    s.RegisterSpace = 0;
    s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    return s;
}
static D3D12_ROOT_SIGNATURE_DESC RootSigDesc(const D3D12_ROOT_PARAMETER* params,
    UINT n, const D3D12_STATIC_SAMPLER_DESC* samplers, UINT ns) {
    D3D12_ROOT_SIGNATURE_DESC d{};
    d.NumParameters = n;
    d.pParameters = params;
    d.NumStaticSamplers = ns;
    d.pStaticSamplers = samplers;
    d.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    return d;
}
static D3D12_RASTERIZER_DESC RasterizerDesc() {
    D3D12_RASTERIZER_DESC d{};
    d.FillMode = D3D12_FILL_MODE_SOLID;
    d.CullMode = D3D12_CULL_MODE_NONE;
    d.FrontCounterClockwise = FALSE;
    d.DepthBias = 0;
    d.DepthBiasClamp = 0.0f;
    d.SlopeScaledDepthBias = 0.0f;
    d.DepthClipEnable = TRUE;
    d.MultisampleEnable = FALSE;
    d.AntialiasedLineEnable = FALSE;
    d.ForcedSampleCount = 0;
    d.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return d;
}
static D3D12_BLEND_DESC BlendDescOpaque() {
    D3D12_BLEND_DESC d{};
    d.AlphaToCoverageEnable = FALSE;
    d.IndependentBlendEnable = FALSE;
    for (int i = 0; i < 8; i++) {
        d.RenderTarget[i].BlendEnable = FALSE;
        d.RenderTarget[i].LogicOpEnable = FALSE;
        d.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
        d.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
        d.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
        d.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
        d.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
        d.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        d.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
        d.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    return d;
}
static D3D12_DEPTH_STENCIL_DESC DepthStencilOff() {
    D3D12_DEPTH_STENCIL_DESC d{};
    d.DepthEnable = FALSE;
    d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    d.StencilEnable = FALSE;
    d.StencilReadMask = 0xFF;
    d.StencilWriteMask = 0xFF;
    return d;
}

// ------------------------------------------------------------- shared state
struct SharedState {
    atomic<int>    state{0};          // 0 ready, 1 measuring, 2 done
    atomic<int>    runSrc{0};         // 0 none, 1 xinput, 2 directinput, 4 raw ds4/ds5
    atomic<double> startT{0};
    atomic<double> endT{0};
    atomic<double> yaw{0};
    atomic<double> best{1e9};
    atomic<double> sens{SENS_DEFAULT};
    atomic<bool>   xConnected{false};
    atomic<bool>   dConnected{false};
    atomic<bool>   rConnected{false};     // raw input (HID DS4/DualSense)
    atomic<bool>   rConnectedDs4{false};  // DualShock 4
    atomic<bool>   rConnectedDs5{false};  // DualSense 5
};
static SharedState g_shared;

// ---------------------------------------------------------------- DInput
struct DiDev {
    ComPtr<IDirectInputDevice8> dev;
    GUID guid{};
    DIPROPRANGE xr{};
    bool haveRange = false;
    wstring prodName;
};
static ComPtr<IDirectInput8> g_di8;
static vector<DiDev> g_diDevs;
static CRITICAL_SECTION g_rawCs;

static BOOL CALLBACK enumCb(LPCDIDEVICEINSTANCEW inst, LPVOID) {
    EnterCriticalSection(&g_rawCs);
    for (const auto& d : g_diDevs) {
        if (d.guid == inst->guidInstance) {
            LeaveCriticalSection(&g_rawCs);
            return DIENUM_CONTINUE;
        }
    }
    LeaveCriticalSection(&g_rawCs);

    DiDev dd;
    dd.guid = inst->guidInstance;
    if (inst->tszProductName) dd.prodName = inst->tszProductName;
    if (FAILED(g_di8->CreateDevice(inst->guidInstance, &dd.dev, nullptr)))
        return DIENUM_CONTINUE;
    if (FAILED(dd.dev->SetDataFormat(&c_dfDIJoystick2)))
        return DIENUM_CONTINUE;
    dd.dev->SetCooperativeLevel(g_hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    
    DIPROPRANGE diprg{};
    diprg.diph.dwSize = sizeof(DIPROPRANGE);
    diprg.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    diprg.diph.dwHow = DIPH_DEVICE;
    diprg.diph.dwObj = 0;
    diprg.lMin = -32768;
    diprg.lMax = 32767;
    dd.dev->SetProperty(DIPROP_RANGE, &diprg.diph);

    dd.dev->Acquire();

    EnterCriticalSection(&g_rawCs);
    g_diDevs.push_back(dd);
    LeaveCriticalSection(&g_rawCs);

    return DIENUM_CONTINUE;
}

static void deviceWatcherThread() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (g_running) {
        if (g_deviceChangeTriggered.exchange(false)) {
            Sleep(400); // Debounce to allow PnP driver loading to finish
            if (g_di8) {
                g_di8->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCb, nullptr, DIEDFL_ATTACHEDONLY);
            }
        }
        Sleep(150);
    }
    CoUninitialize();
}

// ---------------------------------------------------------------- raw input
// DirectInput often does not see the native DualShock 4 / DualSense 5. Read the HID
// reports directly instead: works for Sony gamepads in USB and BT mode.
struct RawDev {
    HANDLE h = nullptr;
    DWORD  vid = 0, pid = 0;
    int    lx = 128, ly = 128, rx = 128, ry = 128;
    bool   fresh = false;
    bool   logged = false;
    double lastPacketT = 0.0;
};
static vector<RawDev> g_rawDevs;

// Diagnostics & Active Stick / Model tracking
static atomic<int>  g_testStick{1};       // 0 = L3 (Left Stick), 1 = R3 (Right Stick - Default)
static atomic<bool> g_isSonyModel{false};
static atomic<bool> g_isXboxModel{false};
static atomic<bool> g_isDs4Model{false};
static atomic<bool> g_isDs5Model{false};
static wchar_t g_modelNameStr[128] = { 0 };

static bool  g_sonyDetected = false;                // main thread only
static char  g_rawPadName[128] = { 0 };             // guarded by g_rawCs

static bool isDs4(DWORD vid, DWORD pid) {
    if (vid != 0x054C) return false;
    switch (pid) {
    case 0x05C4: // DualShock 4 v1
    case 0x09CC: // DualShock 4 v2
    case 0x0BA0: // DualShock 4 USB Wireless Adapter
    case 0x0A00:
        return true;
    }
    return false;
}

static bool isDs5(DWORD vid, DWORD pid) {
    if (vid != 0x054C) return false;
    switch (pid) {
    case 0x0CE6: // DualSense 5
    case 0x0DF2: // DualSense 5 Edge
        return true;
    }
    return false;
}

static bool isSonyPad(DWORD vid, DWORD pid) {
    return isDs4(vid, pid) || isDs5(vid, pid);
}

static const wchar_t* getSonyModelNameW(DWORD pid) {
    switch (pid) {
    case 0x05C4: return L"Dualshock 4 v1";
    case 0x09CC: return L"Dualshock 4 v2";
    case 0x0BA0: return L"Dualshock 4 Wireless Adapter";
    case 0x0A00: return L"Dualshock 4 Wireless";
    case 0x0CE6: return L"Dualsense 5";
    case 0x0DF2: return L"Dualsense 5 Edge";
    default:     return isDs5(0x054C, pid) ? L"Dualsense 5" : L"Dualshock 4";
    }
}

static const wchar_t* getXboxModelNameW(DWORD pid) {
    switch (pid) {
    case 0x0B00:
    case 0x0B05: return L"Xbox Elite Wireless Controller Series 2";
    case 0x02D1:
    case 0x02DD:
    case 0x02E3:
    case 0x02EA: return L"Xbox One Wireless Controller";
    default:     return L"Xbox Wireless Controller (Series X|S)";
    }
}

static void scanRawDevices(bool verbose) {
    UINT n = 0;
    if (GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1 || n == 0) {
        EnterCriticalSection(&g_rawCs);
        for (auto& d : g_rawDevs) {
            d.lastPacketT = 0.0;
            d.fresh = false;
        }
        LeaveCriticalSection(&g_rawCs);
        return;
    }
    vector<RAWINPUTDEVICELIST> list(n);
    n = GetRawInputDeviceList(list.data(), &n, sizeof(RAWINPUTDEVICELIST));
    if (n == (UINT)-1) return;

    EnterCriticalSection(&g_rawCs);
    for (auto& d : g_rawDevs) {
        bool found = false;
        for (UINT i = 0; i < n; i++) {
            if (list[i].hDevice == d.h) { found = true; break; }
        }
        if (!found) {
            d.lastPacketT = 0.0;
            d.fresh = false;
        }
    }
    LeaveCriticalSection(&g_rawCs);

    for (UINT i = 0; i < n; i++) {
        if (list[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info{};
        info.cbSize = sizeof(info);
        UINT sz = sizeof(info);
        if (sz != GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICEINFO, &info, &sz))
            continue;
        UINT nn = 0;
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, nullptr, &nn) == (UINT)-1 || nn == 0)
            continue;
        wstring name(nn, L'\0');
        if (GetRawInputDeviceInfoW(list[i].hDevice, RIDI_DEVICENAME, &name[0], &nn) == (UINT)-1)
            continue;

        DWORD vid = info.hid.dwVendorId & 0xFFFF;
        DWORD pid = info.hid.dwProductId & 0xFFFF;
        if (verbose) {
            char b[256];
            sprintf_s(b, "raw device: '%ls' vid=%04X pid=%04X up=%u u=%u",
                      name.c_str(), vid, pid, info.hid.usUsagePage, info.hid.usUsage);
            debugLog(b);
        }
        if (isSonyPad(vid, pid)) {
            bool isBt = (name.find(L"&MI_") == wstring::npos && name.find(L"&Col") == wstring::npos);
            const char* padType = isDs5(vid, pid) ? "DualSense 5" : "DualShock 4";

            char nb[128] = { 0 };
            sprintf_s(nb, "%s (%s)", padType, isBt ? "BT" : "USB");

            EnterCriticalSection(&g_rawCs);
            g_sonyDetected = true;
            memset(g_rawPadName, 0, sizeof(g_rawPadName));
            strcpy_s(g_rawPadName, nb);
            LeaveCriticalSection(&g_rawCs);
        }
    }
    debugLog("raw scan done");
}

static void rawEnsureInfo(RawDev& d) {
    if (d.vid != 0) return;
    d.vid = 0xFFFFFFFF;
    RID_DEVICE_INFO info{};
    info.cbSize = sizeof(info);
    UINT sz = sizeof(info);
    if (sz == GetRawInputDeviceInfoW(d.h, RIDI_DEVICEINFO, &info, &sz) &&
        info.dwType == RIM_TYPEHID) {
        d.vid = info.hid.dwVendorId & 0xFFFF;
        d.pid = info.hid.dwProductId & 0xFFFF;
    }
}

static void sendSonyLightbar(HANDLE hDevice, DWORD vid, DWORD pid) {
    UINT sz = 0;
    if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, nullptr, &sz) == (UINT)-1 || sz == 0) return;
    wstring devPath(sz, L'\0');
    if (GetRawInputDeviceInfoW(hDevice, RIDI_DEVICENAME, &devPath[0], &sz) == (UINT)-1) return;

    HANDLE hHid = CreateFileW(devPath.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (hHid == INVALID_HANDLE_VALUE) return;

    DWORD w = 0;
    if (isDs5(vid, pid)) {
        // DualSense 5 USB Output Report 0x02 (Electric Blue Lightbar!)
        BYTE usbReport[48]{};
        usbReport[0] = 0x02;
        usbReport[1] = 0xFF; // valid flags 0
        usbReport[2] = 0x15; // valid flags 1: lightbar + player led
        usbReport[42] = 0x04; // Center player LED on
        usbReport[44] = 0x00; // Red
        usbReport[45] = 0x80; // Green
        usbReport[46] = 0xFF; // Blue (Bright Electric Blue!)
        WriteFile(hHid, usbReport, sizeof(usbReport), &w, nullptr);

        // DualSense 5 BT Output Report 0x31
        BYTE btReport[78]{};
        btReport[0] = 0x31;
        btReport[1] = 0x02; // tag
        btReport[2] = 0xFF;
        btReport[3] = 0x15;
        btReport[43] = 0x04;
        btReport[45] = 0x00;
        btReport[46] = 0x80;
        btReport[47] = 0xFF;
        WriteFile(hHid, btReport, sizeof(btReport), &w, nullptr);
    } else {
        // DualShock 4 USB HID Output Report 0x05 (Electric Blue Lightbar!)
        BYTE usbReport[32]{};
        usbReport[0] = 0x05;
        usbReport[1] = 0xFF; // Enable flags (Lightbar + Rumble)
        usbReport[2] = 0x04;
        usbReport[5] = 0x00; // Red
        usbReport[6] = 0x60; // Green
        usbReport[7] = 0xFF; // Blue (Bright Electric Blue!)
        WriteFile(hHid, usbReport, sizeof(usbReport), &w, nullptr);

        // DualShock 4 Bluetooth HID Output Report 0x11
        BYTE btReport[78]{};
        btReport[0] = 0x11;
        btReport[1] = 0xC0;
        btReport[3] = 0xF0;
        btReport[6] = 0x00; // Red
        btReport[7] = 0x60; // Green
        btReport[8] = 0xFF; // Blue
        WriteFile(hHid, btReport, sizeof(btReport), &w, nullptr);
    }

    CloseHandle(hHid);
}

static void handleRawInput(HRAWINPUT hri) {
    UINT sz = 0;
    if (GetRawInputData(hri, RID_INPUT, nullptr, &sz,
                        sizeof(RAWINPUTHEADER)) == (UINT)-1 || sz == 0) return;
    static vector<BYTE> buf;
    buf.resize(sz);
    if (GetRawInputData(hri, RID_INPUT, buf.data(), &sz,
                        sizeof(RAWINPUTHEADER)) != sz) return;
    RAWINPUT* ri = (RAWINPUT*)buf.data();
    if (ri->header.dwType != RIM_TYPEHID) return;

    size_t didx = SIZE_MAX;
    EnterCriticalSection(&g_rawCs);
    for (size_t i = 0; i < g_rawDevs.size(); i++) {
        if (g_rawDevs[i].h == ri->header.hDevice) { didx = i; break; }
    }
    if (didx == SIZE_MAX) {
        RawDev rd;
        rd.h = ri->header.hDevice;
        rawEnsureInfo(rd);
        g_rawDevs.push_back(rd);
        didx = g_rawDevs.size() - 1;
    }

    RawDev& rd = g_rawDevs[didx];
    if (rd.vid == 0) rawEnsureInfo(rd);
    bool isSony = isSonyPad(rd.vid, rd.pid);
    bool shouldSendLight = (!rd.logged && isSony);
    if (shouldSendLight) {
        rd.logged = true;
        g_sonyDetected = true;
    }
    DWORD curVid = rd.vid;
    DWORD curPid = rd.pid;
    LeaveCriticalSection(&g_rawCs);

    if (shouldSendLight) {
        HANDLE hDev = rd.h;
        thread([hDev, curVid, curPid]() { sendSonyLightbar(hDev, curVid, curPid); }).detach();
        char b[96];
        sprintf_s(b, "raw input: Sony pad vid=%04X pid=%04X (%s)", curVid, curPid, isDs5(curVid, curPid) ? "DualSense 5" : "DualShock 4");
        debugLog(b);
    }

    const BYTE* p = ri->data.hid.bRawData;
    UINT hl = ri->data.hid.dwSizeHid;
    int lx = -1, rx = -1;
    BYTE btnByte = 0;
    BYTE dpadVal = 8;
    bool optVal = false;

    bool isDualSense = isDs5(curVid, curPid);

    if (p[0] == 0x01) {
        if (isDualSense && hl >= 10) {
            // DualSense 5 USB Report 0x01
            lx = p[1];
            rx = p[3];
            dpadVal = p[8] & 0x0F;
            btnByte = p[8] & 0xF0;
            optVal  = (p[9] & 0x20) != 0;
        } else if (hl >= 7) {
            // DualShock 4 USB Report 0x01
            lx = p[1];
            rx = p[3];
            dpadVal = p[5] & 0x0F;
            btnByte = p[5] & 0xF0;
            optVal  = (p[6] & 0x20) != 0;
        } else if (hl >= 2) {
            lx = p[1];
        }
    } else if (p[0] == 0x11 && hl >= 9) {
        // DualShock 4 Bluetooth Report 0x11
        lx = p[3];
        rx = p[5];
        dpadVal = p[7] & 0x0F;
        btnByte = p[7] & 0xF0;
        optVal  = (p[8] & 0x20) != 0;
    } else if (p[0] == 0x31 && hl >= 10) {
        // DualSense 5 Bluetooth Extended Report 0x31
        lx = p[2];
        rx = p[4];
        dpadVal = p[8] & 0x0F;
        btnByte = p[8] & 0xF0;
        optVal  = (p[9] & 0x20) != 0;
    }

    if (lx < 0) return;

    BYTE btnVal = btnByte;

    EnterCriticalSection(&g_rawCs);
    g_ds4RawButtons = btnVal;
    g_ds4RawDpad = (dpadVal <= 7) ? dpadVal : 8; // 8 = Neutral (released)
    g_ds4RawOptions = optVal;
    if (lx >= 0) g_rawDevs[didx].lx = lx;
    if (rx >= 0) g_rawDevs[didx].rx = rx;
    g_rawDevs[didx].fresh = true;
    g_rawDevs[didx].lastPacketT = QPC();
    g_rawPacketCount.fetch_add(1, memory_order_relaxed);
    LeaveCriticalSection(&g_rawCs);
}

static double pollRaw(bool* present, bool* isDs4Out, bool* isDs5Out, int filterMode, double now) {
    bool any = false;
    bool haveDs4 = false;
    bool haveDs5 = false;
    double x = 0;
    EnterCriticalSection(&g_rawCs);
    for (auto& d : g_rawDevs) {
        if (isSonyPad(d.vid, d.pid) || d.vid == 0x054C) {
            // Must have received a packet recently (within 0.15s)
            if (d.lastPacketT <= 0.0 || (now - d.lastPacketT > 0.15)) {
                continue;
            }

            bool ds4 = isDs4(d.vid, d.pid);
            bool ds5 = isDs5(d.vid, d.pid);
            if (ds4) haveDs4 = true;
            if (ds5) haveDs5 = true;

            if (filterMode == 1 && !ds4) continue; // DS4 only
            if (filterMode == 2 && !ds5) continue; // DS5 only

            any = true;
            int val = (g_testStick.load() == 0) ? d.lx : d.rx;
            double v = ((double)val - 128.0) / 127.5;
            if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
            if (fabs(v) > fabs(x)) x = v;
        }
    }
    LeaveCriticalSection(&g_rawCs);
    if (present) *present = any;
    if (isDs4Out) *isDs4Out = haveDs4;
    if (isDs5Out) *isDs5Out = haveDs5;
    return x;
}

// ------------------------------------------------------------- input poll
static double pollXInput(bool* present) {
    double x = 0;
    bool any = false;
    for (UINT i = 0; i < 4; i++) {
        XINPUT_STATE st{};
        if (XInputGetState(i, &st) != ERROR_SUCCESS) continue;
        any = true;
        static DWORD s_lastXPacket[4] = { 0, 0, 0, 0 };
        if (st.dwPacketNumber != s_lastXPacket[i]) {
            s_lastXPacket[i] = st.dwPacketNumber;
            g_xinputPacketCount.fetch_add(1, memory_order_relaxed);
        }
        SHORT thumbVal = (g_testStick.load() == 0) ? st.Gamepad.sThumbLX : st.Gamepad.sThumbRX;
        double v = (double)thumbVal / 32767.0;
        if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
        if (fabs(v) > fabs(x)) x = v;
    }
    if (present) *present = any;
    return x;
}

static double pollDInput(bool* present, wstring* outName = nullptr) {
    double x = 0;
    bool any = false;
    for (auto& d : g_diDevs) {
        if (!d.dev) continue;
        HRESULT hr = d.dev->Poll();
        if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {
            d.dev->Acquire();
            continue;
        }
        if (FAILED(hr)) continue;
        DIJOYSTATE2 st{};
        if (FAILED(d.dev->GetDeviceState(sizeof(st), &st))) continue;
        any = true;
        if (outName && outName->empty() && !d.prodName.empty()) {
            *outName = d.prodName;
        }
        LONG diVal = (g_testStick.load() == 0) ? st.lX : st.lRx;
        double v = (double)diVal / 32767.0;
        if (v > 1.0) v = 1.0; else if (v < -1.0) v = -1.0;
        if (fabs(v) > fabs(x)) x = v;
    }
    if (present) *present = any;
    return x;
}

// ---------------------------------------------------------------- shaders
static const char* g_shader = R"(
cbuffer Cb : register(b0) {
    float4 camRight;
    float4 camUp;
    float4 camForward;
    float4 camPos;
    float4 proj;      // tanH, tanV, width, height
    float4 misc;      // yaw, state, sensMult, menuOpen
    float4 timerInfo; // currentMs, bestMs, fps, inputSource
    float4 menuInfo;  // menuRow, fpsIdx, displayMode, unused
};

Texture2D<float4> uiTex : register(t0);
SamplerState uiSam : register(s0);

struct VSOut { float4 pos : SV_Position; };

VSOut vs_main(uint id : SV_VertexID) {
    VSOut o;
    float2 p = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
    o.pos = float4(p, 0.0, 1.0);
    return o;
}

float4 ps_main(VSOut i) : SV_Target {
    float2 uv = i.pos.xy;
    float nx = uv.x * 2.0 / proj.z - 1.0;
    float ny = 1.0 - uv.y * 2.0 / proj.w;

    float3 dir = normalize(camForward.xyz + nx * proj.x * camRight.xyz
                                         + ny * proj.y * camUp.xyz);

    float4 col;
    if (dir.y < -0.00001) {
        float t = -camPos.y / dir.y;
        float3 p = camPos.xyz + dir * t;
        float px = p.x, pz = p.z;
        float d = sqrt(px * px + pz * pz);

        float par = fmod(floor(px) + floor(pz), 2.0);
        float chek = (par < 0.5) ? 1.35 : 1.0;
        col = float4(0.055, 0.065, 0.085, 1.0) * chek;

        if (frac(d * 0.25) < 0.02) col = float4(0.12, 0.13, 0.16, 1.0);

        float ang = atan2(px, pz);
        [unroll]
        for (int k = 0; k < 7; k++) {
            float la = k * 0.5235987755982988;
            float dd = abs(ang - la);
            dd = min(dd, 6.283185307179586 - dd);
            if (dd < 0.0032)
                col = (k == 3) ? float4(0.16, 0.85, 0.32, 1.0)  // 90 deg target line: bright green
                               : float4(0.24, 0.27, 0.32, 1.0);
        }
    } else {
        float hf = clamp(ny * 0.5 + 0.5, 0.0, 1.0);
        col = lerp(float4(0.085, 0.11, 0.16, 1.0), float4(0.03, 0.04, 0.06, 1.0), hf);
        if (abs(dir.y) < 0.02) col = float4(0.14, 0.17, 0.22, 1.0);
    }

    // Red vertical sightline (center of camera view)
    float ad = abs(nx);
    if (ad < 0.0045) {
        float core = (ad < 0.0013) ? 1.0 : 0.0;
        float glow = saturate((0.0045 - ad) / (0.0045 - 0.0013));
        float a = max(core, glow * 0.25);
        col = lerp(col, float4(1.0, 0.15, 0.15, 1.0), a);
    }

    // ---------------------------------------------------------------- UI OVERLAY SAMPLE
    float2 uiUv = uv / float2(proj.z, proj.w);
    float4 uiCol = uiTex.SampleLevel(uiSam, uiUv, 0);
    float uiAlpha = saturate(uiCol.w + dot(uiCol.xyz, float3(100.0, 100.0, 100.0)));
    col.xyz = lerp(col.xyz, uiCol.xyz, uiAlpha);

    return col;
}
)";

// ---------------------------------------------------------------- D3D12
struct Dx {
    ComPtr<ID3D12Device>            dev;
    ComPtr<ID3D12CommandQueue>      queue;
    ComPtr<IDXGISwapChain3>         sc;
    ComPtr<ID3D12Resource>          rt[FRAME_COUNT];
    ComPtr<ID3D12DescriptorHeap>    rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE     rtvHeapStart{};
    UINT                            rtvInc = 0;
    ComPtr<ID3D12CommandAllocator>  alloc[FRAME_COUNT];
    ComPtr<ID3D12GraphicsCommandList> cl;
    ComPtr<ID3D12RootSignature>     rootSig;
    ComPtr<ID3D12PipelineState>     pso;
    ComPtr<ID3D12Resource>          cb;
    void*                           cbPtr = nullptr;
    ComPtr<ID3D12DescriptorHeap>    srvHeap;
    ComPtr<ID3D12Resource>          fontTex;
    ComPtr<ID3D12Fence>             fence;
    HANDLE                          fenceEvt = nullptr;
    UINT64                          fenceVal[FRAME_COUNT] = { 0, 0 };
    UINT64                          fenceCounter = 0;
    bool                            tearing = true;
    bool                            presented = false;   // first Present done
    D3D12_VIEWPORT                  vp{};
    D3D12_RECT                      sr{};
};

static Dx* g_dxPtr = nullptr;
static bool initUiOverlay(Dx& dx);
static void applyDisplayMode(int mode);
static void applyResolution(int idx);
static void flushGpu(Dx& dx);

static ComPtr<ID3DBlob> compileShader(const char* src, const char* entry, const char* target) {
    ComPtr<ID3DBlob> blob, err;
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    if (FAILED(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, entry, target,
                          flags, 0, &blob, &err))) {
        OutputDebugStringA((char*)err->GetBufferPointer());
        FILE* f; fopen_s(&f, "latency_debug.log", "a");
        if (f) {
            char b[512]; sprintf_s(b, "shader %s FAIL:\n", entry);
            fprintf(f, "%s%.*s\n", b, 400, (char*)err->GetBufferPointer());
            fclose(f);
        }
        return nullptr;
    }
    return blob;
}

static bool initDx12(Dx& dx, HWND hwnd) {
    debugLog("dx12: factory");
    ComPtr<IDXGIFactory4> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    ComPtr<IDXGIAdapter1> best;
    SIZE_T bestMem = 0;
    for (UINT i = 0; ; i++) {
        ComPtr<IDXGIAdapter1> a;
        if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d;
        if (FAILED(a->GetDesc1(&d))) continue;
        if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        ComPtr<ID3D12Device> probe;
        if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0,
                                        IID_PPV_ARGS(&probe)))) {
            if (!best || d.DedicatedVideoMemory > bestMem) {
                best = a;
                bestMem = d.DedicatedVideoMemory;
            }
        }
    }
    debugLog("dx12: adapters scanned");
    if (!best) {
        ComPtr<IDXGIAdapter> warp;
        if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)))) return false;
        if (FAILED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(&dx.dev)))) return false;
    } else {
        if (FAILED(D3D12CreateDevice(best.Get(), D3D_FEATURE_LEVEL_11_0,
                                     IID_PPV_ARGS(&dx.dev)))) return false;
    }
    debugLog("dx12: device created");

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(dx.dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&dx.queue)))) { debugLog("dx12 FAIL queue"); return false; }
    debugLog("dx12: queue ok");

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = (UINT)g_rtW.load();
    scd.Height = (UINT)g_rtH.load();
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.Stereo = FALSE;
    scd.SampleDesc = { 1, 0 };
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = FRAME_COUNT;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    {
        ComPtr<IDXGISwapChain1> sc1;
        dx.tearing = true;
        hr = factory->CreateSwapChainForHwnd(dx.queue.Get(), hwnd, &scd, nullptr, nullptr, &sc1);
        if (FAILED(hr)) {
            dx.tearing = false;
            scd.Flags = 0;
            hr = factory->CreateSwapChainForHwnd(dx.queue.Get(), hwnd, &scd, nullptr, nullptr, &sc1);
            if (FAILED(hr)) { char b[64]; sprintf_s(b, "dx12 FAIL swapchain hr=%08X", (UINT)hr); debugLog(b); return false; }
        }
        if (FAILED(sc1.As(&dx.sc))) { debugLog("dx12 FAIL sc.As"); return false; }
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    }
    debugLog("dx12: swapchain ok");

    D3D12_DESCRIPTOR_HEAP_DESC rh{};
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rh.NumDescriptors = FRAME_COUNT;
    if (FAILED(dx.dev->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&dx.rtvHeap)))) return false;
    dx.rtvHeapStart = dx.rtvHeap->GetCPUDescriptorHandleForHeapStart();
    dx.rtvInc = dx.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (UINT i = 0; i < FRAME_COUNT; i++) {
        if (FAILED(dx.sc->GetBuffer(i, IID_PPV_ARGS(&dx.rt[i])))) return false;
        D3D12_CPU_DESCRIPTOR_HANDLE h = dx.rtvHeapStart;
        h.ptr += (SIZE_T)i * dx.rtvInc;
        dx.dev->CreateRenderTargetView(dx.rt[i].Get(), nullptr, h);
    }

    for (UINT i = 0; i < FRAME_COUNT; i++)
        if (FAILED(dx.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&dx.alloc[i])))) { debugLog("dx12 FAIL alloc"); return false; }
    if (FAILED(dx.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         dx.alloc[0].Get(), nullptr,
                                         IID_PPV_ARGS(&dx.cl)))) { debugLog("dx12 FAIL cl"); return false; }
    dx.cl->Close();
    debugLog("dx12: alloc+cl ok");

    // root signature
    D3D12_DESCRIPTOR_RANGE range = DescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0] = RootTable(&range, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    params[1] = RootCBV(0, D3D12_SHADER_VISIBILITY_PIXEL);
    D3D12_STATIC_SAMPLER_DESC sampler = StaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    D3D12_ROOT_SIGNATURE_DESC rsDesc = RootSigDesc(params, 2, &sampler, 1);
    ComPtr<ID3DBlob> sig, err;
    if (FAILED(D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &sig, &err))) return false;
    if (FAILED(dx.dev->CreateRootSignature(0, sig->GetBufferPointer(),
                                           sig->GetBufferSize(),
                                           IID_PPV_ARGS(&dx.rootSig)))) { debugLog("dx12 FAIL rootsig"); return false; }
    debugLog("dx12: rootsig ok");

    auto vs = compileShader(g_shader, "vs_main", "vs_5_0");
    auto ps = compileShader(g_shader, "ps_main", "ps_5_0");
    if (!vs || !ps) { debugLog("dx12 FAIL shaders"); return false; }
    debugLog("dx12: shaders ok");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psd{};
    psd.pRootSignature = dx.rootSig.Get();
    psd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psd.RasterizerState = RasterizerDesc();
    psd.BlendState = BlendDescOpaque();
    psd.DepthStencilState = DepthStencilOff();
    psd.SampleMask = 0xFFFFFFFF;
    psd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psd.NumRenderTargets = 1;
    psd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psd.SampleDesc = { 1, 0 };
    if (FAILED(dx.dev->CreateGraphicsPipelineState(&psd, IID_PPV_ARGS(&dx.pso)))) { debugLog("dx12 FAIL pso"); return false; }
    debugLog("dx12: pso ok");

    // SRV descriptor heap (font)
    D3D12_DESCRIPTOR_HEAP_DESC sh{};
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = 1;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(dx.dev->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&dx.srvHeap)))) return false;

    // constant buffer (upload, mapped)
    auto cbDesc = BufferDesc(4096);
    if (FAILED(dx.dev->CreateCommittedResource(
        &HeapProp(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &cbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&dx.cb)))) return false;
    if (FAILED(dx.cb->Map(0, nullptr, &dx.cbPtr))) return false;

    if (FAILED(dx.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dx.fence)))) { debugLog("dx12 FAIL fence"); return false; }
    dx.fenceEvt = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!dx.fenceEvt) { debugLog("dx12 FAIL fenceEvt"); return false; }
    debugLog("dx12: cb+fence ok");

    dx.vp = { 0, 0, (float)g_rtW.load(), (float)g_rtH.load(), 0, 1 };
    dx.sr = { 0, 0, (LONG)g_rtW.load(), (LONG)g_rtH.load() };
    if (!initUiOverlay(dx)) { debugLog("initUiOverlay FAILED"); return false; }
    return true;
}

// ---------------------------------------------------------------- resize
static bool resizeSwapchain(Dx& dx, UINT w, UINT h) {
    static bool s_inResize = false;
    if (s_inResize) return true; // Re-entrancy guard against WM_SIZE recursive deadlock
    if (!dx.sc || w == 0 || h == 0) return false;
    if ((int)w == g_rtW.load() && (int)h == g_rtH.load()) return true;

    s_inResize = true;

    flushGpu(dx);

    for (UINT i = 0; i < FRAME_COUNT; i++) {
        dx.rt[i].Reset();
    }

    HRESULT hr = dx.sc->ResizeBuffers(FRAME_COUNT, w, h, DXGI_FORMAT_R8G8B8A8_UNORM,
                                      dx.tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) {
        char b[96];
        sprintf_s(b, "resize FAILED hr=%08X", (UINT)hr);
        debugLog(b);
        for (UINT i = 0; i < FRAME_COUNT; i++) {
            if (SUCCEEDED(dx.sc->GetBuffer(i, IID_PPV_ARGS(&dx.rt[i])))) {
                D3D12_CPU_DESCRIPTOR_HANDLE hh = dx.rtvHeapStart;
                hh.ptr += (SIZE_T)i * dx.rtvInc;
                dx.dev->CreateRenderTargetView(dx.rt[i].Get(), nullptr, hh);
            }
        }
        s_inResize = false;
        return false;
    }
    for (UINT i = 0; i < FRAME_COUNT; i++) {
        if (FAILED(dx.sc->GetBuffer(i, IID_PPV_ARGS(&dx.rt[i])))) {
            s_inResize = false;
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE hh = dx.rtvHeapStart;
        hh.ptr += (SIZE_T)i * dx.rtvInc;
        dx.dev->CreateRenderTargetView(dx.rt[i].Get(), nullptr, hh);
    }
    g_rtW = (int)w;
    g_rtH = (int)h;
    dx.vp = { 0, 0, (float)w, (float)h, 0, 1 };
    dx.sr = { 0, 0, (LONG)w, (LONG)h };
    {
        char b[96];
        sprintf_s(b, "swapchain resized %ux%u", w, h);
        debugLog(b);
    }
    s_inResize = false;
    return true;
}

static void flushGpu(Dx& dx) {
    if (dx.sc) dx.sc->SetFullscreenState(FALSE, nullptr);
    if (!dx.queue || !dx.fence) return;
    UINT64 fenceVal = ++dx.fenceCounter;
    dx.queue->Signal(dx.fence.Get(), fenceVal);
    if (dx.fence->GetCompletedValue() < fenceVal) {
        HANDLE hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (hEvent) {
            dx.fence->SetEventOnCompletion(fenceVal, hEvent);
            WaitForSingleObject(hEvent, 1000);
            CloseHandle(hEvent);
        }
    }
}

// ---------------------------------------------------------------- GDI UI Overlay
constexpr UINT UI_W = 1920;
constexpr UINT UI_H = 1080;

struct UiOverlay {
    HDC memDc = nullptr;
    HBITMAP bmp = nullptr;
    void* bits = nullptr;
    HFONT fontBig = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontMed = nullptr;
    HFONT fontSemi = nullptr;
    HFONT fontSm = nullptr;
    ComPtr<ID3D12Resource> tex;
    ComPtr<ID3D12Resource> uploadHeap;
    void* uploadPtr = nullptr;
};
static UiOverlay g_ui;

static int queryCurrentMonitorHz() {
    if (!g_hwnd) return 60;
    HMONITOR hMon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
    if (!hMon) return 60;
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hMon, &mi)) {
        DEVMODEW dm{ sizeof(dm) };
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 0) {
            return (int)dm.dmDisplayFrequency;
        }
    }
    return 60;
}

static bool initUiOverlay(Dx& dx) {
    HDC screen = GetDC(nullptr);
    g_ui.memDc = CreateCompatibleDC(screen);
    if (!g_ui.memDc) { ReleaseDC(nullptr, screen); return false; }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)UI_W;
    bmi.bmiHeader.biHeight = -(LONG)UI_H; // Top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_ui.bmp = CreateDIBSection(g_ui.memDc, &bmi, DIB_RGB_COLORS, &g_ui.bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!g_ui.bmp) return false;

    SelectObject(g_ui.memDc, g_ui.bmp);

    g_ui.fontBig = CreateFontW(-56, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_ui.fontTitle = CreateFontW(-30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_ui.fontMed = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_ui.fontSemi = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_ui.fontSm = CreateFontW(-18, 0, 0, 0, FW_REGULAR, FALSE, FALSE, FALSE,
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    auto td = Tex2DDesc(DXGI_FORMAT_B8G8R8A8_UNORM, UI_W, UI_H);
    if (FAILED(dx.dev->CreateCommittedResource(
        &HeapProp(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&g_ui.tex)))) return false;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 pitch = 0, total = 0;
    dx.dev->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, &pitch, &total);

    if (FAILED(dx.dev->CreateCommittedResource(
        &HeapProp(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &BufferDesc(total), D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&g_ui.uploadHeap)))) return false;

    if (FAILED(g_ui.uploadHeap->Map(0, nullptr, &g_ui.uploadPtr))) return false;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D = { 0, 1, 0, 0 };
    dx.dev->CreateShaderResourceView(g_ui.tex.Get(), &srv,
                                     dx.srvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

static void drawRoundedBox(DWORD* px, int x0, int y0, int x1, int y1, int r, DWORD fillCol, DWORD borderCol) {
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 > 1920) x1 = 1920; if (y1 > 1080) y1 = 1080;
    if (x0 >= x1 || y0 >= y1) return;

    if (r <= 0) {
        for (int y = y0; y < y1; y++) {
            DWORD* row = &px[y * 1920 + x0];
            int w = x1 - x0;
            if (borderCol != 0 && (y == y0 || y == y1 - 1)) {
                std::fill_n(row, w, borderCol);
            } else {
                if (borderCol != 0) {
                    row[0] = borderCol;
                    if (w > 2) std::fill_n(&row[1], w - 2, fillCol);
                    if (w > 1) row[w - 1] = borderCol;
                } else {
                    std::fill_n(row, w, fillCol);
                }
            }
        }
        return;
    }

    int r2 = r * r;
    int rInner2 = (r - 1) * (r - 1);

    for (int y = y0; y < y1; y++) {
        int cy = (y < y0 + r) ? (y0 + r) : ((y >= y1 - r) ? (y1 - 1 - r) : -1);
        int dy = (cy != -1) ? (y - cy) : 0;
        int dy2 = dy * dy;

        for (int x = x0; x < x1; x++) {
            int cx = (x < x0 + r) ? (x0 + r) : ((x >= x1 - r) ? (x1 - 1 - r) : -1);
            int dx = (cx != -1) ? (x - cx) : 0;

            if (cx != -1 && cy != -1) {
                int d2 = dx * dx + dy2;
                if (d2 > r2) continue;
                if (borderCol != 0 && d2 >= rInner2) {
                    px[y * 1920 + x] = borderCol;
                    continue;
                }
            } else if (borderCol != 0 && (x == x0 || x == x1 - 1 || y == y0 || y == y1 - 1)) {
                px[y * 1920 + x] = borderCol;
                continue;
            }
            px[y * 1920 + x] = fillCol;
        }
    }
}

static void drawItalianFlag(DWORD* px, int x, int y, int w = 22, int h = 14) {
    if (x < 0 || y < 0 || x + w >= 1920 || y + h >= 1080) return;
    for (int dy = -1; dy <= h; dy++) {
        px[(y + dy) * 1920 + (x - 1)] = 0xFF101520;
        px[(y + dy) * 1920 + (x + w)] = 0xFF101520;
    }
    for (int dx = -1; dx <= w; dx++) {
        px[(y - 1) * 1920 + (x + dx)] = 0xFF101520;
        px[(y + h) * 1920 + (x + dx)] = 0xFF101520;
    }
    int w3 = w / 3;
    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        for (int dx = 0; dx < w; dx++) {
            DWORD col;
            if (dx < w3) col = 0xFF009246; // Green
            else if (dx < w3 * 2) col = 0xFFFFFFFF; // White
            else col = 0xFFCE2B00; // Red
            px[py * 1920 + (x + dx)] = col;
        }
    }
}

static void drawUkFlag(DWORD* px, int x, int y, int w = 22, int h = 14) {
    if (x < 0 || y < 0 || x + w >= 1920 || y + h >= 1080) return;
    for (int dy = -1; dy <= h; dy++) {
        px[(y + dy) * 1920 + (x - 1)] = 0xFF101520;
        px[(y + dy) * 1920 + (x + w)] = 0xFF101520;
    }
    for (int dx = -1; dx <= w; dx++) {
        px[(y - 1) * 1920 + (x + dx)] = 0xFF101520;
        px[(y + h) * 1920 + (x + dx)] = 0xFF101520;
    }
    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        for (int dx = 0; dx < w; dx++) {
            DWORD col = 0xFF00247D; // Royal Blue Field
            int d1 = abs(dy * w - dx * h);
            int d2 = abs(dy * w - (w - 1 - dx) * h);
            if (d1 < h * 3 || d2 < h * 3) col = 0xFFFFFFFF; // White Saltire
            if (d1 < h * 1.5f || d2 < h * 1.5f) col = 0xFFCF142B; // Red Saltire
            if (dx >= (w / 2 - 3) && dx <= (w / 2 + 2)) col = 0xFFFFFFFF; // White Cross V
            if (dy >= (h / 2 - 3) && dy <= (h / 2 + 2)) col = 0xFFFFFFFF; // White Cross H
            if (dx >= (w / 2 - 1) && dx <= (w / 2)) col = 0xFFCF142B; // Red Cross V
            if (dy >= (h / 2 - 1) && dy <= (h / 2)) col = 0xFFCF142B; // Red Cross H
            px[py * 1920 + (x + dx)] = col;
        }
    }
}

static void drawModernMenuRow(HDC hdc, DWORD* px, int cardX0, int cardX1, int y, int rowIdx, int activeRow,
                              const wchar_t* label, const wchar_t* options[], int optionCount, int selectedOptIdx, double now)
{
    bool isRowActive = (rowIdx == activeRow);
    bool isFlashing = (now - g_selectFlashT.load() < 0.25) && (rowIdx == g_selectFlashRow.load());

    // Row selection plate (subtle frosted glass highlight + vibrant cyan accent pill)
    if (isRowActive) {
        drawRoundedBox(px, cardX0 + 16, y - 4, cardX1 - 16, y + 40, 6, 0xD80D1A2B, 0x9000C8FF);

        // Electric cyan vertical accent indicator
        for (int py = y + 2; py <= y + 34; py++) {
            for (int px_x = cardX0 + 20; px_x <= cardX0 + 24; px_x++) {
                px[py * 1920 + px_x] = 0xFF00E5FF;
            }
        }
    }

    // Row Category Label
    SelectObject(hdc, g_ui.fontSemi);
    SetTextColor(hdc, isRowActive ? RGB(255, 255, 255) : RGB(175, 195, 225));
    RECT rcLabel{ cardX0 + 36, y + 2, cardX0 + 360, y + 36 };
    DrawTextW(hdc, label, -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Segmented option pills
    SelectObject(hdc, g_ui.fontSm);
    int curX = cardX0 + 370;

    for (int i = 0; i < optionCount; i++) {
        bool isSel = (i == selectedOptIdx);
        SIZE sz{};
        GetTextExtentPoint32W(hdc, options[i], (int)wcslen(options[i]), &sz);

        bool isLangRow = (rowIdx == 6);
        int flagExtraW = isLangRow ? 32 : 0;

        int pillW = sz.cx + 28 + flagExtraW;
        int pillH = 30;

        DWORD pillFill, pillBorder;
        COLORREF textCol;

        if (isSel) {
            pillFill = isFlashing ? 0xFFFFFFFF : 0xE80C345B;
            pillBorder = isFlashing ? 0xFFFFFFFF : (isRowActive ? 0xFF00E5FF : 0xFF0090D0);
            textCol = isFlashing ? RGB(0, 0, 0) : RGB(0, 235, 255);
        } else {
            pillFill = 0x650E1724;
            pillBorder = 0x401E2E44;
            textCol = RGB(135, 160, 190);
        }

        drawRoundedBox(px, curX, y + 2, curX + pillW, y + 2 + pillH, 5, pillFill, pillBorder);

        if (isSel && !isFlashing) {
            int dotX = curX + 10;
            int dotY = y + 2 + (pillH / 2);
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    if (dx * dx + dy * dy <= 4) {
                        px[(dotY + dy) * 1920 + (dotX + dx)] = 0xFF00E5FF;
                    }
                }
            }
        }

        SetTextColor(hdc, textCol);
        RECT rcOptText{ isSel ? (curX + 16) : (curX + (isLangRow ? 12 : 6)), y + 2, curX + pillW - 4 - flagExtraW, y + 2 + pillH };
        DrawTextW(hdc, options[i], -1, &rcOptText, (isSel || isLangRow) ? (DT_LEFT | DT_VCENTER | DT_SINGLELINE) : (DT_CENTER | DT_VCENTER | DT_SINGLELINE));

        if (isLangRow) {
            int flagX = curX + pillW - 28;
            int flagY = y + 2 + (pillH - 14) / 2;
            if (i == 0) {
                drawItalianFlag(px, flagX, flagY, 22, 14);
            } else if (i == 1) {
                drawUkFlag(px, flagX, flagY, 22, 14);
            }
        }

        curX += pillW + 10;
    }
}

enum BtnIconType {
    BTN_ICON_CONFIRM = 0,  // Cross (X) / A
    BTN_ICON_CANCEL = 1,   // Circle (O) / B
    BTN_ICON_SQUARE_X = 2, // Square (⬛) / X
    BTN_ICON_TRIANGLE_Y = 3// Triangle (▲) / Y
};

static void drawButtonIcon(HDC hdc, DWORD* px, int cx, int cy, BtnIconType type, bool isXbox) {
    int r = 11;
    for (int dy = -r; dy <= r; dy++) {
        int py = cy + dy;
        if (py < 0 || py >= 1080) continue;
        for (int dx = -r; dx <= r; dx++) {
            int px_x = cx + dx;
            if (px_x < 0 || px_x >= 1920) continue;
            int dist2 = dx * dx + dy * dy;
            if (dist2 <= r * r) {
                DWORD baseCol = 0xFF2A2A2A; // PS Dark Gray Base
                if (isXbox) {
                    if (type == BTN_ICON_CONFIRM) baseCol = 0xFF3CDC28;      // Xbox A (Green)
                    else if (type == BTN_ICON_CANCEL) baseCol = 0xFF2832F0;   // Xbox B (Red)
                    else if (type == BTN_ICON_SQUARE_X) baseCol = 0xFFFF640A; // Xbox X (Blue)
                    else if (type == BTN_ICON_TRIANGLE_Y) baseCol = 0xFF0AC8FF;// Xbox Y (Yellow)
                }
                px[py * 1920 + px_x] = baseCol;
            }
        }
    }

    SelectObject(hdc, g_ui.fontSm);
    SetBkMode(hdc, TRANSPARENT);
    RECT rcText{ cx - 12, cy - 13, cx + 12, cy + 11 };
    if (isXbox) {
        SetTextColor(hdc, RGB(255, 255, 255));
        const wchar_t* letter = (type == BTN_ICON_CONFIRM) ? L"A" :
                                ((type == BTN_ICON_CANCEL) ? L"B" :
                                ((type == BTN_ICON_SQUARE_X) ? L"X" : L"Y"));
        DrawTextW(hdc, letter, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        if (type == BTN_ICON_CONFIRM) { // Cross (Blue X)
            SetTextColor(hdc, RGB(110, 150, 240));
            DrawTextW(hdc, L"✕", -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (type == BTN_ICON_CANCEL) { // Circle (Red O)
            SetTextColor(hdc, RGB(225, 65, 75));
            DrawTextW(hdc, L"◯", -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (type == BTN_ICON_SQUARE_X) { // Square (Pink Square)
            SetTextColor(hdc, RGB(215, 120, 190));
            DrawTextW(hdc, L"□", -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (type == BTN_ICON_TRIANGLE_Y) { // Triangle (Green ▲)
            SetTextColor(hdc, RGB(60, 200, 140));
            DrawTextW(hdc, L"△", -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

static void updateUiOverlay(Dx& dx, double now, int fps) {
    if (!g_ui.memDc || !g_ui.bits) return;

    static double s_lastGdiT = 0.0;
    static int s_lastRow = -1;
    static int s_lastFps = -1;
    static double s_lastFlashT = 0.0;

    bool isMenu = g_menuOpen.load();
    int curRow = g_menuRow;
    double flashT = g_selectFlashT.load();

    // Throttling heavy GDI font rasterization when menu is open to prevent CPU bottlenecks
    if (isMenu) {
        if (now - s_lastGdiT < 0.016 && curRow == s_lastRow && curRow != 8 && flashT == s_lastFlashT && abs(fps - s_lastFps) < 10) {
            return;
        }
    }
    s_lastGdiT = now;
    s_lastRow = curRow;
    s_lastFps = fps;
    s_lastFlashT = flashT;

    DWORD* px = (DWORD*)g_ui.bits;

    // Fast clear active HUD regions only
    RECT rcClearTopL{ 20, 10, 455, 75 };
    for (int y = rcClearTopL.top; y < rcClearTopL.bottom; y++)
        memset(&px[y * UI_W + rcClearTopL.left], 0, (rcClearTopL.right - rcClearTopL.left) * 4);

    RECT rcClearStopwatch{ 460, 15, 1460, 145 };
    for (int y = rcClearStopwatch.top; y < rcClearStopwatch.bottom; y++)
        memset(&px[y * UI_W + rcClearStopwatch.left], 0, (rcClearStopwatch.right - rcClearStopwatch.left) * 4);

    RECT rcClearFps{ 1100, 10, 1900, 245 };
    for (int y = rcClearFps.top; y < rcClearFps.bottom; y++)
        memset(&px[y * UI_W + rcClearFps.left], 0, (rcClearFps.right - rcClearFps.left) * 4);

    RECT rcClearBtmL{ 20, 1025, 650, 1075 };
    for (int y = rcClearBtmL.top; y < rcClearBtmL.bottom; y++)
        memset(&px[y * UI_W + rcClearBtmL.left], 0, (rcClearBtmL.right - rcClearBtmL.left) * 4);

    RECT rcClearBtmR{ 1400, 1025, 1900, 1075 };
    for (int y = rcClearBtmR.top; y < rcClearBtmR.bottom; y++)
        memset(&px[y * UI_W + rcClearBtmR.left], 0, (rcClearBtmR.right - rcClearBtmR.left) * 4);

    RECT rcClearLeftWidget{ 20, 280, 720, 470 };
    for (int y = rcClearLeftWidget.top; y < rcClearLeftWidget.bottom; y++)
        memset(&px[y * UI_W + rcClearLeftWidget.left], 0, (rcClearLeftWidget.right - rcClearLeftWidget.left) * 4);

    static bool prevMenuOpen = false;
    bool curMenuOpen = g_menuOpen.load();
    if (curMenuOpen || prevMenuOpen) {
        RECT rcClearMenu{ 160, 90, 1760, 890 };
        for (int y = rcClearMenu.top; y < rcClearMenu.bottom; y++)
            memset(&px[y * UI_W + rcClearMenu.left], 0, (rcClearMenu.right - rcClearMenu.left) * 4);
    }
    prevMenuOpen = curMenuOpen;

    SetBkMode(g_ui.memDc, TRANSPARENT);

    int lang = g_langIdx.load();

    // ---------------------------------------------------------------- TOP LEFT TITLE BRANDING
    SelectObject(g_ui.memDc, g_ui.fontMed);
    SetTextColor(g_ui.memDc, RGB(80, 220, 255));
    RECT rcTopTitle{ 30, 14, 450, 44 };
    DrawTextW(g_ui.memDc, L"ULT Ultimate Latency Tester", -1, &rcTopTitle, DT_LEFT | DT_SINGLELINE);

    SelectObject(g_ui.memDc, g_ui.fontSm);
    SetTextColor(g_ui.memDc, RGB(130, 165, 200));
    RECT rcTopSub{ 30, 44, 450, 68 };
    DrawTextW(g_ui.memDc, (lang == 0) ? L"DirectX 12 • Diagnostica Hardware" : L"DirectX 12 • Hardware Latency Bench", -1, &rcTopSub, DT_LEFT | DT_SINGLELINE);

    int st = g_shared.state.load();
    double startT = g_shared.startT.load();
    double endT = g_shared.endT.load();
    double best = g_shared.best.load();
    bool xc = g_shared.xConnected.load();
    bool dc = g_shared.dConnected.load();
    bool rc = g_shared.rConnected.load();
    bool isSony = g_isSonyModel.load();
    bool isXbox = g_isXboxModel.load();

    double el = 0;
    if (st == 1) el = now - startT;
    else if (st == 2) el = endT - startT;

    // ---------------------------------------------------------------- LEFT EDGE WIDGET (ANALOG STICK 2D + CONTROLLER MODEL)
    double liveStickVal = g_shared.yaw.load() / TARGET_RADIANS;
    if (liveStickVal > 1.0) liveStickVal = 1.0; else if (liveStickVal < -1.0) liveStickVal = -1.0;

    // Continuous smooth demonstration loop: tilts right to 1.0 and returns to 0.0 (center)
    double animVal = (sin(now * 3.5) + 1.0) * 0.5;
    if (fabs(liveStickVal) > 0.05) {
        animVal = liveStickVal;
    }

    int stickCx = 95;
    int stickCy = 345;
    int knobX = stickCx + (int)(animVal * 28.0);
    int knobY = stickCy;

    // High-definition smooth anti-aliased 2D stick rendering (Outer Circle Ring)
    for (int dy = -36; dy <= 36; dy++) {
        int py = stickCy + dy;
        if (py < 0 || py >= (int)UI_H) continue;
        for (int dx = -36; dx <= 36; dx++) {
            int px_x = stickCx + dx;
            if (px_x < 0 || px_x >= (int)UI_W) continue;
            int dist2 = dx * dx + dy * dy;
            if (dist2 >= 30 * 30 && dist2 <= 36 * 36) {
                DWORD col = (dist2 >= 32 * 32 && dist2 <= 35 * 35) ? 0xFF00E5FF : 0xFF0088CC;
                px[py * UI_W + px_x] = col;
            } else if (dist2 < 30 * 30) {
                px[py * UI_W + px_x] = 0x990C1828;
            }
        }
    }

    // Inner Circle Knob (Analog Cap Cap Gradient)
    for (int dy = -16; dy <= 16; dy++) {
        int py = knobY + dy;
        if (py < 0 || py >= (int)UI_H) continue;
        for (int dx = -16; dx <= 16; dx++) {
            int px_x = knobX + dx;
            if (px_x < 0 || px_x >= (int)UI_W) continue;
            int dist2 = dx * dx + dy * dy;
            if (dist2 <= 16 * 16) {
                DWORD col = (dist2 >= 13 * 13) ? 0xFFFFFFFF : (dist2 >= 10 * 10 ? 0xFF00E5FF : 0xFF00A0CC);
                px[py * UI_W + px_x] = col;
            }
        }
    }

    // Controller Model Name Text (displayed ONLY when a controller is connected)
    bool anyConnected = (xc || dc || rc) && (g_modelNameStr[0] != L'\0');
    if (anyConnected) {
        SelectObject(g_ui.memDc, g_ui.fontMed);
        COLORREF modelCol = isXbox ? RGB(50, 255, 120) : (isSony ? RGB(80, 200, 255) : RGB(100, 220, 255));
        SetTextColor(g_ui.memDc, modelCol);

        RECT rcModelTitle{ 150, 328, 710, 365 };
        DrawTextW(g_ui.memDc, g_modelNameStr, -1, &rcModelTitle, DT_LEFT | DT_SINGLELINE);
    }

    // Active Stick & Square/X Button Hint (English & Italian support)
    SelectObject(g_ui.memDc, g_ui.fontSm);
    int curStick = g_testStick.load();
    const wchar_t* stickSideStr = (lang == 0) ?
        ((curStick == 0) ? L"L3 ( Sinistra )" : L"R3 ( Destra )") :
        ((curStick == 0) ? L"L3 ( Left )" : L"R3 ( Right )");

    const wchar_t* stickP1 = L"[ ";
    wchar_t stickP2[128];
    swprintf_s(stickP2, (lang == 0) ? L" / Q ] Levetta: %s" : L" / Q ] Stick: %s", stickSideStr);

    SIZE szSt1{};
    GetTextExtentPoint32W(g_ui.memDc, stickP1, (int)wcslen(stickP1), &szSt1);

    int stX = 30;
    int stY = 400;
    RECT rcSt1{ stX, stY, stX + szSt1.cx + 5, stY + 35 };
    SetTextColor(g_ui.memDc, RGB(220, 230, 245));
    DrawTextW(g_ui.memDc, stickP1, -1, &rcSt1, DT_LEFT | DT_SINGLELINE);

    drawButtonIcon(g_ui.memDc, px, stX + szSt1.cx + 11, stY + 13, BTN_ICON_SQUARE_X, isXbox);

    RECT rcSt2{ stX + szSt1.cx + 22, stY, stX + 650, stY + 35 };
    SetTextColor(g_ui.memDc, RGB(220, 230, 245));
    DrawTextW(g_ui.memDc, stickP2, -1, &rcSt2, DT_LEFT | DT_SINGLELINE);

    // ---------------------------------------------------------------- BOTTOM LEFT & RIGHT HUD COUNTERS
    SelectObject(g_ui.memDc, g_ui.fontSm);
    SetTextColor(g_ui.memDc, RGB(160, 190, 230));
    RECT rcBtmLeft{ 30, 1032, 650, 1068 };
    if (lang == 0) {
        DrawTextW(g_ui.memDc, L"[ OPTIONS / ESC ] Menu   [ F11 ] Schermo Intero", -1, &rcBtmLeft, DT_LEFT | DT_SINGLELINE);
    } else {
        DrawTextW(g_ui.memDc, L"[ OPTIONS / ESC ] Menu   [ F11 ] Fullscreen", -1, &rcBtmLeft, DT_LEFT | DT_SINGLELINE);
    }

    const wchar_t* creditText = (lang == 0) ? L"Programmato da SolidSnake99  " : L"Coded by SolidSnake99  ";
    SIZE szCredit{};
    GetTextExtentPoint32W(g_ui.memDc, creditText, (int)wcslen(creditText), &szCredit);

    RECT rcBtmRight{ 1400, 1032, 1890 - 32, 1068 };
    DrawTextW(g_ui.memDc, creditText, -1, &rcBtmRight, DT_RIGHT | DT_SINGLELINE);

    // Draw crisp custom pixel Italian Flag (27px wide, 18px high)
    int flagX = 1890 - 28;
    int flagY = 1038;
    int flagW = 27;
    int flagH = 18;

    // Dark outline
    for (int y = flagY - 1; y <= flagY + flagH; y++) {
        px[y * UI_W + (flagX - 1)] = 0xFF101520;
        px[y * UI_W + (flagX + flagW)] = 0xFF101520;
    }
    for (int x = flagX - 1; x <= flagX + flagW; x++) {
        px[(flagY - 1) * UI_W + x] = 0xFF101520;
        px[(flagY + flagH) * UI_W + x] = 0xFF101520;
    }
    // Green / White / Red stripes
    for (int y = flagY; y < flagY + flagH; y++) {
        for (int x = flagX; x < flagX + 9; x++) px[y * UI_W + x] = 0xFF009246;       // Italian Green
        for (int x = flagX + 9; x < flagX + 18; x++) px[y * UI_W + x] = 0xFFFFFFFF;   // Italian White
        for (int x = flagX + 18; x < flagX + 27; x++) px[y * UI_W + x] = 0xFFCE2B00; // Italian Red
    }

    // ---------------------------------------------------------------- ALWAYS VISIBLE FPS & LATENCY HUD (TOP RIGHT)
    wchar_t fpsBuf[32];
    swprintf_s(fpsBuf, L"%d FPS", fps);
    SelectObject(g_ui.memDc, g_ui.fontMed);
    SetTextColor(g_ui.memDc, RGB(80, 220, 255));
    RECT rcFps{ 1200, 14, 1890, 44 };
    DrawTextW(g_ui.memDc, fpsBuf, -1, &rcFps, DT_RIGHT | DT_SINGLELINE);

    int pollHz = g_measuredPollingHz.load();
    bool hasValidController = anyConnected && (pollHz > 0);

    double inputDelayMs = (hasValidController) ? (1000.0 / (double)pollHz) : 0.0;
    double renderLatencyMs = (fps > 0) ? (1000.0 / (double)fps) : 0.0;

    int curMonHz = g_monitorHz.load();
    if (curMonHz <= 0) curMonHz = 60;
    double monLatencyMs = 1000.0 / (2.0 * (double)curMonHz);
    double userStickLatencyMs = g_userCustomStickLatencyMs.load();
    double jitterHumanMs = g_lastJitterHumanMs.load();
    double totalEstLatencyMs = inputDelayMs + renderLatencyMs + monLatencyMs + userStickLatencyMs + jitterHumanMs;

    SelectObject(g_ui.memDc, g_ui.fontSm);
    SetTextColor(g_ui.memDc, RGB(150, 190, 230));

    // 1. Polling USB/BT (Red ? when disconnected)
    const wchar_t* p1Prefix = (lang == 0) ? L"Polling USB/BT: " : L"USB/BT Polling Rate: ";
    if (!hasValidController) {
        SIZE szPre{}, szQ{};
        GetTextExtentPoint32W(g_ui.memDc, p1Prefix, (int)wcslen(p1Prefix), &szPre);
        GetTextExtentPoint32W(g_ui.memDc, L"?", 1, &szQ);
        int totalW = szPre.cx + szQ.cx;
        int startX = 1890 - totalW;
        RECT rPre{ startX, 46, startX + szPre.cx + 5, 70 };
        RECT rQ{ startX + szPre.cx, 46, 1890, 70 };
        SetTextColor(g_ui.memDc, RGB(150, 190, 230));
        DrawTextW(g_ui.memDc, p1Prefix, -1, &rPre, DT_LEFT | DT_SINGLELINE);
        SetTextColor(g_ui.memDc, RGB(255, 50, 50));
        DrawTextW(g_ui.memDc, L"?", -1, &rQ, DT_LEFT | DT_SINGLELINE);
    } else {
        wchar_t b1[128];
        swprintf_s(b1, L"%s%d Hz", p1Prefix, pollHz);
        RECT rL1{ 1200, 46, 1890, 70 };
        SetTextColor(g_ui.memDc, RGB(150, 190, 230));
        DrawTextW(g_ui.memDc, b1, -1, &rL1, DT_RIGHT | DT_SINGLELINE);
    }

    // 2. Latenza Input Controller (Red ? when disconnected)
    const wchar_t* p2Prefix = (lang == 0) ? L"Latenza Input Controller: " : L"Controller Input Delay: ";
    if (!hasValidController) {
        SIZE szPre{}, szQ{};
        GetTextExtentPoint32W(g_ui.memDc, p2Prefix, (int)wcslen(p2Prefix), &szPre);
        GetTextExtentPoint32W(g_ui.memDc, L"?", 1, &szQ);
        int totalW = szPre.cx + szQ.cx;
        int startX = 1890 - totalW;
        RECT rPre{ startX, 70, startX + szPre.cx + 5, 94 };
        RECT rQ{ startX + szPre.cx, 70, 1890, 94 };
        SetTextColor(g_ui.memDc, RGB(150, 190, 230));
        DrawTextW(g_ui.memDc, p2Prefix, -1, &rPre, DT_LEFT | DT_SINGLELINE);
        SetTextColor(g_ui.memDc, RGB(255, 50, 50));
        DrawTextW(g_ui.memDc, L"?", -1, &rQ, DT_LEFT | DT_SINGLELINE);
    } else {
        wchar_t b2[128];
        swprintf_s(b2, L"%s%.2f ms", p2Prefix, inputDelayMs);
        RECT rL2{ 1200, 70, 1890, 94 };
        SetTextColor(g_ui.memDc, RGB(150, 190, 230));
        DrawTextW(g_ui.memDc, b2, -1, &rL2, DT_RIGHT | DT_SINGLELINE);
    }

    // 3. Latenza Render D3D12
    wchar_t b3[128];
    if (lang == 0) swprintf_s(b3, L"Latenza Render D3D12: %.2f ms", renderLatencyMs);
    else swprintf_s(b3, L"D3D12 Render Latency: %.2f ms", renderLatencyMs);
    RECT rL3{ 1200, 94, 1890, 118 };
    SetTextColor(g_ui.memDc, RGB(150, 190, 230));
    DrawTextW(g_ui.memDc, b3, -1, &rL3, DT_RIGHT | DT_SINGLELINE);

    // 4. Latenza Monitor
    wchar_t b4[128];
    if (lang == 0) swprintf_s(b4, L"Latenza Monitor (%dHz): %.2f ms", curMonHz, monLatencyMs);
    else swprintf_s(b4, L"Monitor Latency (%dHz): %.2f ms", curMonHz, monLatencyMs);
    RECT rL4{ 1200, 118, 1890, 142 };
    SetTextColor(g_ui.memDc, RGB(150, 190, 230));
    DrawTextW(g_ui.memDc, b4, -1, &rL4, DT_RIGHT | DT_SINGLELINE);

    // 5. Latenza Levetta Sconosciuta
    RECT rL5{ 1200, 142, 1890, 166 };
    if (userStickLatencyMs <= 0.0001) {
        const wchar_t* prefix = (lang == 0) ? L"Latenza Levetta Sconosciuta: " : L"Unknown Stick Latency: ";
        const wchar_t* qMark = L"?";

        SIZE szPrefix{}, szQ{};
        GetTextExtentPoint32W(g_ui.memDc, prefix, (int)wcslen(prefix), &szPrefix);
        GetTextExtentPoint32W(g_ui.memDc, qMark, 1, &szQ);

        int totalW = szPrefix.cx + szQ.cx;
        int startX = 1890 - totalW;

        RECT rPrefix{ startX, 142, startX + szPrefix.cx + 5, 166 };
        RECT rQ{ startX + szPrefix.cx, 142, 1890, 166 };

        SetTextColor(g_ui.memDc, RGB(150, 190, 230));
        DrawTextW(g_ui.memDc, prefix, -1, &rPrefix, DT_LEFT | DT_SINGLELINE);

        SetTextColor(g_ui.memDc, RGB(255, 50, 50)); // Bright Red Accent for Question Mark ?
        DrawTextW(g_ui.memDc, qMark, -1, &rQ, DT_LEFT | DT_SINGLELINE);
    } else {
        wchar_t b5[128];
        if (lang == 0) swprintf_s(b5, L"Latenza Levetta Sconosciuta: %.2f ms", userStickLatencyMs);
        else swprintf_s(b5, L"Unknown Stick Latency: %.2f ms", userStickLatencyMs);

        SetTextColor(g_ui.memDc, RGB(150, 190, 230));
        DrawTextW(g_ui.memDc, b5, -1, &rL5, DT_RIGHT | DT_SINGLELINE);
    }

    // 6. Jitter + Ritardo Umano
    wchar_t b6[128];
    if (lang == 0) swprintf_s(b6, L"Jitter + Ritardo Umano: %.2f ms", jitterHumanMs);
    else swprintf_s(b6, L"Jitter + Human Delay: %.2f ms", jitterHumanMs);
    RECT rL6{ 1200, 166, 1890, 190 };
    SetTextColor(g_ui.memDc, RGB(150, 190, 230));
    DrawTextW(g_ui.memDc, b6, -1, &rL6, DT_RIGHT | DT_SINGLELINE);

    SelectObject(g_ui.memDc, g_ui.fontMed);
    SetTextColor(g_ui.memDc, RGB(50, 255, 140)); // Bright Electric Neon Green
    RECT rTot{ 1200, 194, 1890, 228 };
    wchar_t bTot[128];
    if (lang == 0) swprintf_s(bTot, L"LATENZA TOTALE STIMATA: %.2f ms", totalEstLatencyMs);
    else swprintf_s(bTot, L"ESTIMATED TOTAL LATENCY: %.2f ms", totalEstLatencyMs);
    DrawTextW(g_ui.memDc, bTot, -1, &rTot, DT_RIGHT | DT_SINGLELINE);

    // ---------------------------------------------------------------- STOPWATCH CARD (TOP CENTER)
    RECT rcStopwatch{ 460, 18, 1460, 142 };
    for (int y = rcStopwatch.top; y < rcStopwatch.bottom; y++) {
        DWORD* row = &px[y * UI_W + rcStopwatch.left];
        std::fill_n(row, rcStopwatch.right - rcStopwatch.left, 0xDC0C121E);
    }

    wchar_t timerStr[128];
    if (st == 2) {
        double resultJitterMs = (endT - startT) * 1000.0 - 1000.0;
        swprintf_s(timerStr, (lang == 0) ? L"Jitter + ritardo umano = %.3f ms" : L"Jitter + human delay = %.3f ms", resultJitterMs);
    } else if (st == 1) {
        swprintf_s(timerStr, L"%.3f ms", el * 1000.0);
    } else {
        swprintf_s(timerStr, L"0.000 ms");
    }

    SelectObject(g_ui.memDc, g_ui.fontBig);
    COLORREF timerCol = RGB(80, 200, 255); // Ready: Cyan
    if (st == 1) timerCol = RGB(255, 220, 40); // Measuring: Yellow
    else if (st == 2) timerCol = RGB(50, 255, 120); // Result: Green

    SetTextColor(g_ui.memDc, timerCol);
    RECT rcText{ 460, 24, 1460, 95 };
    DrawTextW(g_ui.memDc, timerStr, -1, &rcText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(g_ui.memDc, g_ui.fontSm);

    if (best < 1e8) {
        double bestJitterMs = best * 1000.0 - 1000.0;
        wchar_t swP1[128], swP2[128];
        swprintf_s(swP1, (lang == 0) ? L"MIGLIORE: Jitter + ritardo umano = %.3f ms   |   [OPTIONS / ESC] Menu   [ " : L"BEST: Jitter + human delay = %.3f ms   |   [OPTIONS / ESC] Menu   [ ", bestJitterMs);
        swprintf_s(swP2, (lang == 0) ? L" / R ] Reset Cronometro" : L" / R ] Reset Timer");

        SIZE szSw1{}, szSw2{};
        GetTextExtentPoint32W(g_ui.memDc, swP1, (int)wcslen(swP1), &szSw1);
        GetTextExtentPoint32W(g_ui.memDc, swP2, (int)wcslen(swP2), &szSw2);

        int totalW = szSw1.cx + 22 + szSw2.cx;
        int swStartX = 460 + (1000 - totalW) / 2;
        int swY = 104;

        RECT rcSw1{ swStartX, swY, swStartX + szSw1.cx + 5, swY + 30 };
        SetTextColor(g_ui.memDc, RGB(220, 230, 240));
        DrawTextW(g_ui.memDc, swP1, -1, &rcSw1, DT_LEFT | DT_SINGLELINE);

        drawButtonIcon(g_ui.memDc, px, swStartX + szSw1.cx + 11, swY + 11, BTN_ICON_TRIANGLE_Y, isXbox);

        RECT rcSw2{ swStartX + szSw1.cx + 22, swY, swStartX + totalW + 10, swY + 30 };
        SetTextColor(g_ui.memDc, RGB(220, 230, 240));
        DrawTextW(g_ui.memDc, swP2, -1, &rcSw2, DT_LEFT | DT_SINGLELINE);
    } else {
        const wchar_t* subStr = (lang == 0) ? L"PRONTO: Spingi levetta a destra   |   [OPTIONS / ESC] Menu"
                                            : L"READY: Push stick to the right   |   [OPTIONS / ESC] Menu";
        RECT rcSub{ 460, 104, 1460, 134 };
        SetTextColor(g_ui.memDc, RGB(220, 230, 240));
        DrawTextW(g_ui.memDc, subStr, -1, &rcSub, DT_CENTER | DT_SINGLELINE);
    }

    // ---------------------------------------------------------------- ESC / OPTIONS MENU CARD
    if (g_menuOpen.load()) {
        int cardX0 = 180;
        int cardY0 = 105;
        int cardX1 = 1740;
        int cardY1 = 710;

        // 1. Dark Acrylic Frosted Glass Background with Soft Rounded Corners (r=12)
        drawRoundedBox(px, cardX0, cardY0, cardX1, cardY1, 12, 0xF4090E18, 0xFF1C2C44);

        // 2. Top Sleek Accent Lightbar (Cyan to Dark Blue gradient)
        for (int py = cardY0 + 2; py <= cardY0 + 4; py++) {
            for (int px_x = cardX0 + 16; px_x < cardX1 - 16; px_x++) {
                float tNorm = (float)(px_x - (cardX0 + 16)) / (float)(cardX1 - cardX0 - 32);
                float fade = (float)sin(tNorm * 3.14159f);
                DWORD rCol = (DWORD)(0 * fade);
                DWORD gCol = (DWORD)(229 * fade);
                DWORD bCol = (DWORD)(255 * fade);
                px[py * UI_W + px_x] = (0xFF << 24) | (rCol << 16) | (gCol << 8) | bCol;
            }
        }

        // 3. Header Section (Title + Subtitle on Left, Status Pills on Right)
        SelectObject(g_ui.memDc, g_ui.fontTitle);
        SetTextColor(g_ui.memDc, RGB(80, 220, 255));
        RECT rcTitle{ cardX0 + 36, cardY0 + 16, cardX0 + 600, cardY0 + 48 };
        DrawTextW(g_ui.memDc, (lang == 0) ? L"IMPOSTAZIONI  ULT" : L"ULT  SETTINGS", -1, &rcTitle, DT_LEFT | DT_SINGLELINE);

        SelectObject(g_ui.memDc, g_ui.fontSm);
        SetTextColor(g_ui.memDc, RGB(130, 165, 200));
        RECT rcSubtitle{ cardX0 + 38, cardY0 + 48, cardX0 + 600, cardY0 + 72 };
        DrawTextW(g_ui.memDc, (lang == 0) ? L"Ultimate Latency Tester • Diagnostica a Basso Overhead"
                                          : L"Ultimate Latency Tester • Ultra Low Overhead Diagnostics", -1, &rcSubtitle, DT_LEFT | DT_SINGLELINE);

        // Right Status Pill Badges:
        const wchar_t* stName = (st == 0) ? ((lang == 0) ? L"PRONTO" : L"READY")
                                          : ((st == 1) ? ((lang == 0) ? L"MISURAZIONE" : L"MEASURING")
                                                       : ((lang == 0) ? L"RISULTATO" : L"RESULT"));
        COLORREF stCol = (st == 0) ? RGB(50, 255, 140) : ((st == 1) ? RGB(255, 210, 40) : RGB(80, 210, 255));

        int srcMode = g_inputSrcIdx.load();
        const wchar_t* srcName = (lang == 0) ? L"Disconnesso" : L"Disconnected";
        if (srcMode == 1 || (srcMode == 0 && rc && g_shared.rConnectedDs4.load())) {
            srcName = L"DualShock 4 Nativo (1000Hz)";
        } else if (srcMode == 2 || (srcMode == 0 && rc && g_shared.rConnectedDs5.load())) {
            srcName = L"DualSense 5 Nativo (1000Hz)";
        } else if (srcMode == 3 || (srcMode == 0 && xc)) {
            srcName = L"XInput (Xbox Gamepad)";
        } else if (srcMode == 4 || (srcMode == 0 && dc)) {
            srcName = L"DirectInput Controller";
        }

        int badgeY = cardY0 + 22;
        int badgeH = 32;

        // Badge 1: Real-time FPS & Latency
        wchar_t bPerf[64];
        swprintf_s(bPerf, L"%d FPS • %.2f ms", fps, el * 1000.0);
        SIZE szPerf{};
        GetTextExtentPoint32W(g_ui.memDc, bPerf, (int)wcslen(bPerf), &szPerf);
        int b1W = szPerf.cx + 24;
        int b1X = cardX1 - 36 - b1W;
        drawRoundedBox(px, b1X, badgeY, b1X + b1W, badgeY + badgeH, 6, 0x80101826, 0x601E2E44);
        SetTextColor(g_ui.memDc, RGB(180, 205, 235));
        RECT rcB1{ b1X + 12, badgeY, b1X + b1W - 8, badgeY + badgeH };
        DrawTextW(g_ui.memDc, bPerf, -1, &rcB1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Badge 2: Controller Connection Name
        wchar_t bInput[128];
        swprintf_s(bInput, L"%s", srcName);
        SIZE szInput{};
        GetTextExtentPoint32W(g_ui.memDc, bInput, (int)wcslen(bInput), &szInput);
        int b2W = szInput.cx + 34;
        int b2X = b1X - 10 - b2W;
        drawRoundedBox(px, b2X, badgeY, b2X + b2W, badgeY + badgeH, 6, 0x800E1D30, (rc || xc || dc) ? 0x8000A0E0 : 0x401E2E44);

        // Status green/cyan dot
        int dotX = b2X + 12;
        int dotY = badgeY + (badgeH / 2);
        DWORD dotCol = (rc || xc || dc) ? 0xFF00E5FF : 0xFF8090A0;
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                if (dx * dx + dy * dy <= 4) {
                    px[(dotY + dy) * UI_W + (dotX + dx)] = dotCol;
                }
            }
        }
        SetTextColor(g_ui.memDc, (rc || xc || dc) ? RGB(100, 220, 255) : RGB(140, 160, 180));
        RECT rcB2{ b2X + 22, badgeY, b2X + b2W - 8, badgeY + badgeH };
        DrawTextW(g_ui.memDc, bInput, -1, &rcB2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Badge 3: Status state (READY / MEASURING / RESULT)
        SIZE szSt{};
        GetTextExtentPoint32W(g_ui.memDc, stName, (int)wcslen(stName), &szSt);
        int b3W = szSt.cx + 24;
        int b3X = b2X - 10 - b3W;
        drawRoundedBox(px, b3X, badgeY, b3X + b3W, badgeY + badgeH, 6, 0x80101C26, 0x60203648);
        SetTextColor(g_ui.memDc, stCol);
        RECT rcB3{ b3X + 12, badgeY, b3X + b3W - 8, badgeY + badgeH };
        DrawTextW(g_ui.memDc, stName, -1, &rcB3, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // 4. Sleek Divider Line (y = cardY0 + 74)
        int divY = cardY0 + 74;
        for (int px_x = cardX0 + 20; px_x < cardX1 - 20; px_x++) {
            px[divY * UI_W + px_x] = 0xFF18263A;
        }

        // 5. Render 9 Menu Rows with Fluid Segmented Option Pills
        int curRow = g_menuRow;
        int rowYStart = cardY0 + 88;
        int rowSpacing = 44;

        // Row 0: FPS LIMIT
        const wchar_t* fpsNames[FPS_COUNT] = { L"30", L"60", L"120", L"144", L"180", L"240", L"500", (lang == 0) ? L"Illimitato" : L"Unlimited" };
        drawModernMenuRow(g_ui.memDc, px, cardX0, cardX1, rowYStart + 0 * rowSpacing, 0, curRow, (lang == 0) ? L"Limite FPS:" : L"FPS Limit:", fpsNames, FPS_COUNT, g_fpsIdx.load(), now);

        // Row 1: RISOLUZIONE
        const wchar_t* resNames[RES_COUNT] = { L"1080p", L"900p", L"720p", L"540p" };
        drawModernMenuRow(g_ui.memDc, px, cardX0, cardX1, rowYStart + 1 * rowSpacing, 1, curRow, (lang == 0) ? L"Risoluzione:" : L"Resolution:", resNames, RES_COUNT, g_resIdx.load(), now);

        // Row 2: QUALITÀ GRAFICA
        const wchar_t* gfxNames[GRAPHICS_COUNT] = { (lang == 0) ? L"Minima (Ultra Fast)" : L"Low (Ultra Fast)",
                                                    (lang == 0) ? L"Media" : L"Medium",
                                                    (lang == 0) ? L"Alta" : L"High" };
        drawModernMenuRow(g_ui.memDc, px, cardX0, cardX1, rowYStart + 2 * rowSpacing, 2, curRow, (lang == 0) ? L"Qualita Grafica:" : L"Graphics Quality:", gfxNames, GRAPHICS_COUNT, g_gfxIdx.load(), now);

        // Row 3: SORGENTE INPUT
        const wchar_t* srcNames[INPUT_SRC_COUNT] = { L"Auto", L"DS4 Nativo", L"DS5 Nativo", L"XInput", L"DirectInput" };
        drawModernMenuRow(g_ui.memDc, px, cardX0, cardX1, rowYStart + 3 * rowSpacing, 3, curRow, (lang == 0) ? L"Tipo Collegamento:" : L"Input Connection:", srcNames, INPUT_SRC_COUNT, g_inputSrcIdx.load(), now);

        // Row 4: MODALITÀ DISPLAY
        const wchar_t* dispNames[2] = { (lang == 0) ? L"Finestra (Desktop)" : L"Windowed (Desktop)",
                                        (lang == 0) ? L"Fullscreen Esclusivo" : L"Exclusive Fullscreen" };
        drawModernMenuRow(g_ui.memDc, px, cardX0, cardX1, rowYStart + 4 * rowSpacing, 4, curRow, (lang == 0) ? L"Modalita Display:" : L"Display Mode:", dispNames, 2, g_displayMode.load(), now);

        // Row 5: V-SYNC
        const wchar_t* vsyncNames[2] = { (lang == 0) ? L"Disabilitato (Min Latency)" : L"Disabled (Min Latency)",
                                         (lang == 0) ? L"Abilitato" : L"Enabled" };
        drawModernMenuRow(g_ui.memDc, px, cardX0, cardX1, rowYStart + 5 * rowSpacing, 5, curRow, L"Sincronia V-Sync:", vsyncNames, 2, g_vsyncIdx.load(), now);

        // Row 6: LINGUA / LANGUAGE
        const wchar_t* langNames[2] = { L"Italiano", L"English" };
        drawModernMenuRow(g_ui.memDc, px, cardX0, cardX1, rowYStart + 6 * rowSpacing, 6, curRow, (lang == 0) ? L"Lingua / Language:" : L"Language / Lingua:", langNames, 2, g_langIdx.load(), now);

        // Row 7: CURVA LEVETTA / STICK CURVE
        const wchar_t* curveNames[2] = { (lang == 0) ? L"Lineare (Analogica)" : L"Linear (Analog)",
                                         (lang == 0) ? L"Istantanea (Max Speed)" : L"Instant (Max Speed)" };
        drawModernMenuRow(g_ui.memDc, px, cardX0, cardX1, rowYStart + 7 * rowSpacing, 7, curRow, (lang == 0) ? L"Curva Risposta Levetta:" : L"Stick Response Curve:", curveNames, 2, g_stickCurveIdx.load(), now);

        // Row 8: LATENZA LEVETTA HARDWARE (Interactive Numeric Input Pill)
        {
            int r8Y = rowYStart + 8 * rowSpacing;
            bool isRowActive = (curRow == 8);
            if (isRowActive) {
                drawRoundedBox(px, cardX0 + 16, r8Y - 4, cardX1 - 16, r8Y + 40, 6, 0xD80D1A2B, 0x9000C8FF);
                for (int py = r8Y + 2; py <= r8Y + 34; py++) {
                    for (int px_x = cardX0 + 20; px_x <= cardX0 + 24; px_x++) {
                        px[py * UI_W + px_x] = 0xFF00E5FF;
                    }
                }
            }

            SelectObject(g_ui.memDc, g_ui.fontSemi);
            SetTextColor(g_ui.memDc, isRowActive ? RGB(255, 255, 255) : RGB(175, 195, 225));
            RECT rcLabel{ cardX0 + 36, r8Y + 2, cardX0 + 360, r8Y + 36 };
            DrawTextW(g_ui.memDc, (lang == 0) ? L"Latenza Hardware Levetta:" : L"Hardware Stick Latency:", -1, &rcLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SelectObject(g_ui.memDc, g_ui.fontSm);
            int curX = cardX0 + 370;

            wchar_t inputDisplay[64];
            bool cursorBlink = ((int)(now * 2.5) % 2 == 0);
            swprintf_s(inputDisplay, L"%s ms%s", g_stickLatencyInputBuf, (isRowActive && cursorBlink) ? L" |" : L"  ");

            SIZE szInput{};
            GetTextExtentPoint32W(g_ui.memDc, inputDisplay, (int)wcslen(inputDisplay), &szInput);
            int pillW = szInput.cx + 28;
            int pillH = 30;

            DWORD pillFill = isRowActive ? 0xE80C345B : 0x650E1724;
            DWORD pillBorder = isRowActive ? 0xFF00E5FF : 0x401E2E44;
            COLORREF textCol = isRowActive ? RGB(0, 240, 255) : RGB(140, 165, 195);

            drawRoundedBox(px, curX, r8Y + 2, curX + pillW, r8Y + 2 + pillH, 5, pillFill, pillBorder);

            SetTextColor(g_ui.memDc, textCol);
            RECT rcInputText{ curX + 10, r8Y + 2, curX + pillW - 6, r8Y + 2 + pillH };
            DrawTextW(g_ui.memDc, inputDisplay, -1, &rcInputText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            const wchar_t* hintStr = (lang == 0) ? L"[ ⌨ Digita 0 - 100 ms ]   [ ← / → ±0.5 ms ]"
                                                 : L"[ ⌨ Type 0 - 100 ms ]   [ ← / → ±0.5 ms ]";
            SIZE szHint{};
            GetTextExtentPoint32W(g_ui.memDc, hintStr, (int)wcslen(hintStr), &szHint);
            int hintW = szHint.cx + 20;

            drawRoundedBox(px, curX + pillW + 10, r8Y + 2, curX + pillW + 10 + hintW, r8Y + 2 + pillH, 5, 0x400C1420, 0x25182436);
            SetTextColor(g_ui.memDc, RGB(125, 150, 180));
            RECT rcHintText{ curX + pillW + 18, r8Y + 2, curX + pillW + 10 + hintW - 8, r8Y + 2 + pillH };
            DrawTextW(g_ui.memDc, hintStr, -1, &rcHintText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }

        // 6. Modern Info / Warning / Tip Callout Banner (y = 585 to 653, 68px tall for spacious multi-line text)
        int banY = rowYStart + 9 * rowSpacing - 4;
        int banH = 68;
        SelectObject(g_ui.memDc, g_ui.fontSm);

        if (curRow == 8) {
            drawRoundedBox(px, cardX0 + 20, banY, cardX1 - 20, banY + banH, 6, 0xD8081624, 0xA000B0E8);
            SetTextColor(g_ui.memDc, RGB(0, 225, 255));
            RECT rTag{ cardX0 + 36, banY + 8, cardX0 + 200, banY + 60 };
            DrawTextW(g_ui.memDc, (lang == 0) ? L"[ INFO SLOW-MO ]" : L"[ SLOW-MO INFO ]", -1, &rTag, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SetTextColor(g_ui.memDc, RGB(190, 215, 240));
            RECT rTxt{ cardX0 + 195, banY + 8, cardX1 - 36, banY + 64 };
            const wchar_t* iText = (lang == 0) ?
                L"Misura il ritardo fisico del sensore levetta (Potenziometro, Hall Effect o TMR + chip controller).\nInserisci i millisecondi rilevati con videocamera a rallentatore per includerli nel calcolo della latenza totale." :
                L"Measures physical stick hardware delay (Potentiometer, Hall Effect, or TMR + controller IC).\nEnter the milliseconds measured via slow-motion camera to include in the estimated total latency.";
            DrawTextW(g_ui.memDc, iText, -1, &rTxt, DT_LEFT | DT_WORDBREAK);
        } else if (g_stickCurveIdx.load() == 1 || curRow == 7) {
            drawRoundedBox(px, cardX0 + 20, banY, cardX1 - 20, banY + banH, 6, 0xD81E1508, 0xC0FFAA00);
            SetTextColor(g_ui.memDc, RGB(255, 190, 40));
            RECT rTag{ cardX0 + 36, banY + 8, cardX0 + 200, banY + 60 };
            DrawTextW(g_ui.memDc, (lang == 0) ? L"[ ATTENZIONE ]" : L"[ WARNING ]", -1, &rTag, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SetTextColor(g_ui.memDc, RGB(245, 225, 190));
            RECT rTxt{ cardX0 + 195, banY + 8, cardX1 - 36, banY + 64 };
            const wchar_t* wText = (lang == 0) ?
                L"La modalità Istantanea (Max Speed) NON è uno scenario di gioco analogico reale.\nAttiva il 100% della corsa appena superata la deadzone per testare il limite digitale teorico del sistema." :
                L"Instant (Max Speed) mode is NOT a realistic in-game analog scenario.\nApplies 100% velocity past the deadzone to measure the theoretical digital floor of the system.";
            DrawTextW(g_ui.memDc, wText, -1, &rTxt, DT_LEFT | DT_WORDBREAK);
        } else {
            drawRoundedBox(px, cardX0 + 20, banY, cardX1 - 20, banY + banH, 6, 0xB00B1320, 0x501C2E46);
            SetTextColor(g_ui.memDc, RGB(80, 200, 255));
            RECT rTag{ cardX0 + 36, banY + 8, cardX0 + 200, banY + 60 };
            DrawTextW(g_ui.memDc, (lang == 0) ? L"[ GUIDA RAPIDA ]" : L"[ QUICK GUIDE ]", -1, &rTag, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            SetTextColor(g_ui.memDc, RGB(180, 200, 225));
            RECT rTxt{ cardX0 + 195, banY + 8, cardX1 - 36, banY + 64 };
            const wchar_t* gText = (lang == 0) ?
                L"Usa [ DPAD / FRECCE ] per navigare tra le voci, [ ← / → ] per modificare, [ INVIO / ✕ ] per confermare.\nPremi [ OPTIONS / ESC / ◯ ] per chiudere le impostazioni e tornare al test in tempo reale." :
                L"Use [ DPAD / ARROWS ] to navigate, [ ← / → ] to modify options, [ ENTER / ✕ ] to confirm.\nPress [ OPTIONS / ESC / ◯ ] to close settings and return to the live latency test.";
            DrawTextW(g_ui.memDc, gText, -1, &rTxt, DT_LEFT | DT_WORDBREAK);
        }

        // 7. Modern Bottom Controls Legend Bar (y = 663 to 695)
        int legY = banY + banH + 10;
        SelectObject(g_ui.memDc, g_ui.fontSm);

        const wchar_t* legP1 = (lang == 0) ? L"CONTROLLI: [ OPTIONS / ESC ] Menu   [ DPAD / FRECCE ] Naviga   [ ← / → ] Modifica   [ "
                                           : L"CONTROLS: [ OPTIONS / ESC ] Menu   [ DPAD / ARROWS ] Navigate   [ ← / → ] Change   [ ";
        const wchar_t* legP2 = (lang == 0) ? L" / INVIO ] Seleziona   [ "
                                           : L" / ENTER ] Select   [ ";
        const wchar_t* legP3 = (lang == 0) ? L" / ESC / B ] Chiudi"
                                           : L" / ESC / B ] Close";

        SIZE szL1{}, szL2{}, szL3{};
        GetTextExtentPoint32W(g_ui.memDc, legP1, (int)wcslen(legP1), &szL1);
        GetTextExtentPoint32W(g_ui.memDc, legP2, (int)wcslen(legP2), &szL2);
        GetTextExtentPoint32W(g_ui.memDc, legP3, (int)wcslen(legP3), &szL3);

        int totalLegW = szL1.cx + 22 + szL2.cx + 22 + szL3.cx;
        int legStartX = cardX0 + (cardX1 - cardX0 - totalLegW) / 2;

        RECT rcL1{ legStartX, legY, legStartX + szL1.cx + 5, legY + 28 };
        SetTextColor(g_ui.memDc, RGB(160, 185, 215));
        DrawTextW(g_ui.memDc, legP1, -1, &rcL1, DT_LEFT | DT_SINGLELINE);

        drawButtonIcon(g_ui.memDc, px, legStartX + szL1.cx + 11, legY + 11, BTN_ICON_CONFIRM, isXbox);

        int p2X = legStartX + szL1.cx + 22;
        RECT rcL2{ p2X, legY, p2X + szL2.cx + 5, legY + 28 };
        SetTextColor(g_ui.memDc, RGB(160, 185, 215));
        DrawTextW(g_ui.memDc, legP2, -1, &rcL2, DT_LEFT | DT_SINGLELINE);

        drawButtonIcon(g_ui.memDc, px, p2X + szL2.cx + 11, legY + 11, BTN_ICON_CANCEL, isXbox);

        int p3X = p2X + szL2.cx + 22;
        RECT rcL3{ p3X, legY, p3X + szL3.cx + 5, legY + 28 };
        SetTextColor(g_ui.memDc, RGB(160, 185, 215));
        DrawTextW(g_ui.memDc, legP3, -1, &rcL3, DT_LEFT | DT_SINGLELINE);
    }

    if (g_ui.uploadPtr) {
        memcpy(g_ui.uploadPtr, g_ui.bits, UI_W * UI_H * 4);
    }
}

// ---------------------------------------------------------------- constant buffer
struct __declspec(align(16)) CbData {
    float camRight[4];
    float camUp[4];
    float camForward[4];
    float camPos[4];
    float proj[4];
    float misc[4];        // yaw, state, sensMult, menuOpen
    float timerInfo[4];   // currentMs, bestMs, fps, inputSource
    float menuInfo[4];    // menuRow, fpsIdx, displayMode, unused
};

static void fillCb(CbData& cb, double now, int fps) {
    int st = g_shared.state.load();
    double yaw = g_shared.yaw.load();
    double startT = g_shared.startT.load();
    double endT = g_shared.endT.load();
    double best = g_shared.best.load();
    double sens = g_shared.sens.load();
    bool xc = g_shared.xConnected.load();
    bool dc = g_shared.dConnected.load();
    bool rc = g_shared.rConnected.load();

    double cost = cos(yaw), sint = sin(yaw);
    double cosp = cos(PITCH), sinp = sin(PITCH);
    cb.camRight[0] = (float)cost;   cb.camRight[1] = 0;         cb.camRight[2] = (float)-sint; cb.camRight[3] = 0;
    cb.camUp[0] = (float)(-sinp * sint); cb.camUp[1] = (float)cosp; cb.camUp[2] = 0;           cb.camUp[3] = 0;
    cb.camForward[0] = (float)(cosp * sint); cb.camForward[1] = (float)sinp; cb.camForward[2] = (float)(cosp * cost); cb.camForward[3] = 0;
    cb.camPos[0] = 0; cb.camPos[1] = (float)CAM_Y; cb.camPos[2] = 0; cb.camPos[3] = 0;

    double tanV = tan(VFOV_DEG * PI / 360.0);
    double aspect = (double)g_rtW.load() / (double)g_rtH.load();
    cb.proj[0] = (float)(tanV * aspect);
    cb.proj[1] = (float)tanV;
    cb.proj[2] = (float)g_rtW.load();
    cb.proj[3] = (float)g_rtH.load();

    cb.misc[0] = (float)yaw;
    cb.misc[1] = (float)st;
    cb.misc[2] = (float)(sens / SENS_DEFAULT);
    cb.misc[3] = (float)(g_menuOpen.load() ? 1.0 : 0.0);

    double el = 0;
    if (st == 1) el = now - startT;
    else if (st == 2) el = endT - startT;

    int src = rc ? 1 : (xc ? 2 : (dc ? 3 : 0));

    cb.timerInfo[0] = (float)(el * 1000.0);
    cb.timerInfo[1] = (float)(best < 1e8 ? best * 1000.0 : -1.0);
    cb.timerInfo[2] = (float)fps;
    cb.timerInfo[3] = (float)src;

    cb.menuInfo[0] = (float)g_menuRow;
    cb.menuInfo[1] = (float)g_fpsIdx.load();
    cb.menuInfo[2] = (float)g_displayMode.load();
    cb.menuInfo[3] = 0.0f;
}

// ---------------------------------------------------------------- render
static void applyResolutionInternal(int idx);
static void applyDisplayModeInternal(int mode);

static void renderFrame(Dx& dx) {
    int reqDisp = g_reqDisplayMode.load();
    if (reqDisp != g_displayMode.load()) {
        applyDisplayModeInternal(reqDisp);
    }
    int reqRes = g_reqResIdx.load();
    if (reqRes != g_resIdx.load()) {
        applyResolutionInternal(reqRes);
    }

    static int fpsOut = 0;
    static int fc = 0;
    static double fpsT0 = 0;

    double now = QPC();
    fc++;
    if (now - fpsT0 >= 0.5) {
        fpsOut = (int)(fc / (now - fpsT0) + 0.5);
        fc = 0;
        fpsT0 = now;
        g_monitorHz.store(queryCurrentMonitorHz());
    }

    updateUiOverlay(dx, now, fpsOut);

    UINT bi = dx.sc->GetCurrentBackBufferIndex();
    if (!dx.rt[bi] || !dx.rt[bi].Get()) return;
    UINT other = bi ^ 1u;
    if (dx.fenceVal[other]) {
        if (dx.fence->GetCompletedValue() < dx.fenceVal[other]) {
            dx.fence->SetEventOnCompletion(dx.fenceVal[other], dx.fenceEvt);
            WaitForSingleObject(dx.fenceEvt, INFINITE);
        }
    }

    dx.alloc[bi]->Reset();
    dx.cl->Reset(dx.alloc[bi].Get(), dx.pso.Get());

    ID3D12GraphicsCommandList* cl = dx.cl.Get();

    D3D12_TEXTURE_COPY_LOCATION d{};
    d.pResource = g_ui.tex.Get();
    d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    d.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION s{};
    s.pResource = g_ui.uploadHeap.Get();
    s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dx.dev->GetCopyableFootprints(&Tex2DDesc(DXGI_FORMAT_B8G8R8A8_UNORM, UI_W, UI_H), 0, 1, 0, &s.PlacedFootprint, nullptr, nullptr, nullptr);

    auto b1_copy = TransitionBarrier(g_ui.tex.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->ResourceBarrier(1, &b1_copy);

    D3D12_BOX box{ 0, 0, 0, (LONG)UI_W, (LONG)UI_H, 1 };
    cl->CopyTextureRegion(&d, 0, 0, 0, &s, &box);

    auto b2_copy = TransitionBarrier(g_ui.tex.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cl->ResourceBarrier(1, &b2_copy);

    CbData cb{};
    fillCb(cb, now, fpsOut);
    memcpy(dx.cbPtr, &cb, sizeof(cb));

    cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->SetGraphicsRootSignature(dx.rootSig.Get());
    ID3D12DescriptorHeap* heaps[] = { dx.srvHeap.Get() };
    cl->SetDescriptorHeaps(1, heaps);
    cl->SetGraphicsRootDescriptorTable(0, dx.srvHeap->GetGPUDescriptorHandleForHeapStart());
    cl->SetGraphicsRootConstantBufferView(1, dx.cb->GetGPUVirtualAddress());

    auto b1 = TransitionBarrier(dx.rt[bi].Get(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cl->ResourceBarrier(1, &b1);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx.rtvHeapStart;
    rtv.ptr += (SIZE_T)bi * dx.rtvInc;
    cl->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    cl->RSSetViewports(1, &dx.vp);
    cl->RSSetScissorRects(1, &dx.sr);
    cl->DrawInstanced(3, 1, 0, 0);

    auto b2 = TransitionBarrier(dx.rt[bi].Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cl->ResourceBarrier(1, &b2);

    cl->Close();
    ID3D12CommandList* lists[] = { cl };
    dx.queue->ExecuteCommandLists(1, lists);

    bool vsyncOn = (g_vsyncIdx.load() == 1);
    UINT syncInterval = vsyncOn ? 1 : 0;
    UINT presentFlags = (vsyncOn || !dx.tearing) ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    dx.sc->Present(syncInterval, presentFlags);
    dx.presented = true;
    dx.fenceVal[bi] = ++dx.fenceCounter;
    dx.queue->Signal(dx.fence.Get(), dx.fenceCounter);
}

// ---------------------------------------------------------------- input thread
static void inputThread() {
    timeBeginPeriod(1);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    double t_last = QPC();
    double x_prev = 0, t_prev = 0;
    int enumTicker = 0;

    while (g_running) {
        double t = QPC();
        double dt = t - t_last;
        if (dt < 1.0 / POLL_HZ) { Sleep(0); continue; }
        t_last = t;

        bool haveDs4 = false, haveDs5 = false;
        bool xc = false, dc = false, rc = false;
        wstring diName;
        double xi = pollXInput(&xc);
        double xd = pollDInput(&dc, &diName);
        double xr = pollRaw(&rc, &haveDs4, &haveDs5, g_inputSrcIdx.load(), t);

        bool rConn = rc;
        bool xConn = xc;
        bool dConn = dc;

        g_shared.xConnected = xConn;
        g_shared.dConnected = dConn;
        g_shared.rConnected = rConn;
        g_shared.rConnectedDs4 = rConn && haveDs4;
        g_shared.rConnectedDs5 = rConn && haveDs5;

        bool anyConn = rConn || xConn || dConn;
        if (!anyConn) {
            g_isSonyModel = false;
            g_isXboxModel = false;
            g_isDs4Model = false;
            g_isDs5Model = false;
            EnterCriticalSection(&g_rawCs);
            g_modelNameStr[0] = L'\0';
            g_ds4RawButtons = 0;
            g_ds4RawDpad = 8;
            g_ds4RawOptions = false;
            LeaveCriticalSection(&g_rawCs);
        } else {
            if (rConn) {
                g_isSonyModel = true;
                g_isXboxModel = false;
                EnterCriticalSection(&g_rawCs);
                for (const auto& d : g_rawDevs) {
                    if ((isSonyPad(d.vid, d.pid) || d.vid == 0x054C) && (t - d.lastPacketT < 0.20)) {
                        wcscpy_s(g_modelNameStr, getSonyModelNameW(d.pid));
                        if (isDs5(d.vid, d.pid)) {
                            g_isDs5Model = true;
                            g_isDs4Model = false;
                        } else {
                            g_isDs4Model = true;
                            g_isDs5Model = false;
                        }
                        break;
                    }
                }
                LeaveCriticalSection(&g_rawCs);
            } else if (xConn) {
                g_isXboxModel = true;
                g_isSonyModel = false;
                g_isDs4Model = false;
                g_isDs5Model = false;
                wcscpy_s(g_modelNameStr, getXboxModelNameW(0));
            } else if (dConn) {
                g_isSonyModel = false;
                g_isXboxModel = false;
                g_isDs4Model = false;
                g_isDs5Model = false;
                if (!diName.empty()) {
                    wcscpy_s(g_modelNameStr, diName.c_str());
                } else {
                    wcscpy_s(g_modelNameStr, L"DirectInput Gamepad");
                }
            }
        }

        static double s_lastHzT = 0.0;
        static uint64_t s_lastRawCnt = 0;
        static uint64_t s_lastXCnt = 0;
        if (t - s_lastHzT >= 0.5) {
            double dtHz = t - s_lastHzT;
            s_lastHzT = t;
            uint64_t curRaw = g_rawPacketCount.load();
            uint64_t curX = g_xinputPacketCount.load();
            uint64_t diffRaw = curRaw - s_lastRawCnt;
            uint64_t diffX = curX - s_lastXCnt;
            s_lastRawCnt = curRaw;
            s_lastXCnt = curX;

            int hz = 0;
            if (anyConn) {
                int srcSel = g_inputSrcIdx.load();
                if (srcSel == 1 || srcSel == 2 || (srcSel == 0 && rConn)) {
                    hz = (int)round((double)diffRaw / dtHz);
                    if (hz <= 0) hz = 1000;
                } else if (srcSel == 3 || (srcSel == 0 && xConn)) {
                    hz = (int)round((double)diffX / dtHz);
                    if (hz <= 0) hz = 250;
                } else if (dConn) {
                    hz = 125;
                }
            } else {
                hz = 0;
            }
            g_measuredPollingHz.store(hz);
        }

        bool btnOpt = false, btnUp = false, btnDown = false, btnL = false, btnR = false, btnConf = false, btnBack = false, btnRst = false, btnSquare = false;

        // 1. RawInput DS4/DS5 buttons
        EnterCriticalSection(&g_rawCs);
        if (rConn) {
            if (g_ds4RawOptions) btnOpt = true;
            if (g_ds4RawButtons & 0x10) btnSquare = true; // Square (⬛) = bit 4 (0x10)
            if (g_ds4RawButtons & 0x20) btnConf = true;   // Cross (X) = bit 5 (0x20)
            if (g_ds4RawButtons & 0x40) btnBack = true;   // Circle (O) = bit 6 (0x40)
            if (g_ds4RawButtons & 0x80) btnRst = true;    // Triangle = bit 7 (0x80)
            if (g_ds4RawDpad == 0 || g_ds4RawDpad == 1 || g_ds4RawDpad == 7) btnUp = true;
            if (g_ds4RawDpad == 4 || g_ds4RawDpad == 3 || g_ds4RawDpad == 5) btnDown = true;
            if (g_ds4RawDpad == 6 || g_ds4RawDpad == 5 || g_ds4RawDpad == 7) btnL = true;
            if (g_ds4RawDpad == 2 || g_ds4RawDpad == 1 || g_ds4RawDpad == 3) btnR = true;
        }
        LeaveCriticalSection(&g_rawCs);

        // 2. XInput buttons
        for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
            XINPUT_STATE xs;
            if (XInputGetState(i, &xs) == ERROR_SUCCESS) {
                WORD b = xs.Gamepad.wButtons;
                if (b & XINPUT_GAMEPAD_START) btnOpt = true;
                if (b & XINPUT_GAMEPAD_DPAD_UP) btnUp = true;
                if (b & XINPUT_GAMEPAD_DPAD_DOWN) btnDown = true;
                if (b & XINPUT_GAMEPAD_DPAD_LEFT) btnL = true;
                if (b & XINPUT_GAMEPAD_DPAD_RIGHT) btnR = true;
                if (b & XINPUT_GAMEPAD_X) btnSquare = true;
                if (b & XINPUT_GAMEPAD_A) btnConf = true;
                if (b & XINPUT_GAMEPAD_B) btnBack = true;
                if (b & XINPUT_GAMEPAD_Y) btnRst = true;
            }
        }

        // 3. DirectInput buttons (only when RawInput is not active)
        if (!rConn) {
            EnterCriticalSection(&g_rawCs);
            for (auto& d : g_diDevs) {
                if (!d.dev) continue;
                DIJOYSTATE2 js{};
                if (SUCCEEDED(d.dev->GetDeviceState(sizeof(js), &js))) {
                    if (js.rgbButtons[9] || js.rgbButtons[8]) btnOpt = true;
                    if (js.rgbButtons[2] || js.rgbButtons[3]) btnSquare = true;
                    if (js.rgbButtons[0]) btnConf = true;
                    if (js.rgbButtons[1]) btnBack = true;
                    if (js.rgbButtons[3]) btnRst = true;
                    if (js.rgdwPOV[0] != 0xFFFFFFFF) {
                        DWORD pov = js.rgdwPOV[0];
                        if (pov == 0 || pov == 36000 || pov == 4500 || pov == 31500) btnUp = true;
                        if (pov == 18000 || pov == 13500 || pov == 22500) btnDown = true;
                        if (pov == 27000 || pov == 22500 || pov == 31500) btnL = true;
                        if (pov == 9000 || pov == 4500 || pov == 13500) btnR = true;
                    }
                }
            }
            LeaveCriticalSection(&g_rawCs);
        }

        static bool pOpt = false, pUp = false, pDown = false, pL = false, pR = false, pConf = false, pBack = false, pRst = false, pSquare = false;
        if (btnOpt && !pOpt) { g_menuOpen = !g_menuOpen.load(); if (g_menuOpen.load()) g_menuRow = 0; }
        if (btnRst && !pRst) { g_shared.yaw = 0; g_shared.state = 0; g_shared.best = 1e9; g_lastJitterHumanMs = 0.0; }
        if (btnSquare && !pSquare) { g_testStick = 1 - g_testStick.load(); }

        if (g_menuOpen.load()) {
            if (btnUp && !pUp) g_menuRow = (g_menuRow - 1 + MENU_ROW_COUNT) % MENU_ROW_COUNT;
            if (btnDown && !pDown) g_menuRow = (g_menuRow + 1) % MENU_ROW_COUNT;
            if (btnL && !pL) {
                if (g_menuRow == 0) g_fpsIdx = (g_fpsIdx.load() - 1 + FPS_COUNT) % FPS_COUNT;
                else if (g_menuRow == 1) applyResolution((g_resIdx.load() - 1 + RES_COUNT) % RES_COUNT);
                else if (g_menuRow == 2) g_gfxIdx = (g_gfxIdx.load() - 1 + GRAPHICS_COUNT) % GRAPHICS_COUNT;
                else if (g_menuRow == 3) g_inputSrcIdx = (g_inputSrcIdx.load() - 1 + INPUT_SRC_COUNT) % INPUT_SRC_COUNT;
                else if (g_menuRow == 4) applyDisplayMode(1 - g_displayMode.load());
                else if (g_menuRow == 5) g_vsyncIdx = 1 - g_vsyncIdx.load();
                else if (g_menuRow == 6) g_langIdx = (g_langIdx.load() - 1 + LANG_COUNT) % LANG_COUNT;
                else if (g_menuRow == 7) g_stickCurveIdx = 1 - g_stickCurveIdx.load();
                else if (g_menuRow == 8) {
                    double val = g_userCustomStickLatencyMs.load() - 0.5;
                    if (val < 0.0) val = 0.0;
                    swprintf_s(g_stickLatencyInputBuf, L"%.1f", val);
                    g_userCustomStickLatencyMs.store(val);
                }
            }
            if (btnR && !pR) {
                if (g_menuRow == 0) g_fpsIdx = (g_fpsIdx.load() + 1) % FPS_COUNT;
                else if (g_menuRow == 1) applyResolution((g_resIdx.load() + 1) % RES_COUNT);
                else if (g_menuRow == 2) g_gfxIdx = (g_gfxIdx.load() + 1) % GRAPHICS_COUNT;
                else if (g_menuRow == 3) g_inputSrcIdx = (g_inputSrcIdx.load() + 1) % INPUT_SRC_COUNT;
                else if (g_menuRow == 4) applyDisplayMode(1 - g_displayMode.load());
                else if (g_menuRow == 5) g_vsyncIdx = 1 - g_vsyncIdx.load();
                else if (g_menuRow == 6) g_langIdx = (g_langIdx.load() + 1) % LANG_COUNT;
                else if (g_menuRow == 7) g_stickCurveIdx = 1 - g_stickCurveIdx.load();
                else if (g_menuRow == 8) {
                    double val = g_userCustomStickLatencyMs.load() + 0.5;
                    if (val > 100.0) val = 100.0;
                    swprintf_s(g_stickLatencyInputBuf, L"%.1f", val);
                    g_userCustomStickLatencyMs.store(val);
                }
            }
            static double t_lastConf = 0.0;
            if (btnConf && !pConf && (t - t_lastConf > 0.22)) {
                t_lastConf = t;
                g_selectFlashT = t;
                g_selectFlashRow = g_menuRow;
                if (g_menuRow == 0) g_fpsIdx = (g_fpsIdx.load() + 1) % FPS_COUNT;
                else if (g_menuRow == 1) applyResolution((g_resIdx.load() + 1) % RES_COUNT);
                else if (g_menuRow == 2) g_gfxIdx = (g_gfxIdx.load() + 1) % GRAPHICS_COUNT;
                else if (g_menuRow == 3) g_inputSrcIdx = (g_inputSrcIdx.load() + 1) % INPUT_SRC_COUNT;
                else if (g_menuRow == 4) applyDisplayMode(1 - g_displayMode.load());
                else if (g_menuRow == 5) g_vsyncIdx = 1 - g_vsyncIdx.load();
                else if (g_menuRow == 6) g_langIdx = (g_langIdx.load() + 1) % LANG_COUNT;
                else if (g_menuRow == 7) g_stickCurveIdx = 1 - g_stickCurveIdx.load();
                else if (g_menuRow == 8) {
                    double val = g_userCustomStickLatencyMs.load() + 1.0;
                    if (val > 100.0) val = 0.0;
                    swprintf_s(g_stickLatencyInputBuf, L"%.1f", val);
                    g_userCustomStickLatencyMs.store(val);
                }
            }
            if (btnBack && !pBack) {
                g_menuOpen = false; // Circle (O) / B button CLOSES MENU!
            }
        }
        pOpt = btnOpt; pUp = btnUp; pDown = btnDown; pL = btnL; pR = btnR; pConf = btnConf; pBack = btnBack; pRst = btnRst; pSquare = btnSquare;

        if (g_menuOpen.load()) {
            if (g_shared.state.load() != 0) {
                g_shared.yaw = 0;
                g_shared.state = 0;
            }
            g_shared.runSrc = 0;
            x_prev = 0;
            t_prev = t;
            Sleep(1);
            continue;
        }

        double cand[5] = { 0, xi, xd, 0, xr };
        bool conn[5] = { false, xConn, dConn, false, rConn };

        int srcMode = g_inputSrcIdx.load();
        int targetSrc = 0;
        if (srcMode == 0) { // AUTO
            if (rConn) targetSrc = 4;      // RawInput 1000Hz Nativo (DS4/DS5)
            else if (xConn) targetSrc = 1; // XInput
            else if (dConn) targetSrc = 2; // DirectInput
        } else if (srcMode == 1) { // DS4 NATIVO
            targetSrc = (rConn && haveDs4) ? 4 : 0;
        } else if (srcMode == 2) { // DS5 NATIVO
            targetSrc = (rConn && haveDs5) ? 4 : 0;
        } else if (srcMode == 3) { // XINPUT
            targetSrc = 1;
        } else if (srcMode == 4) { // DIRECTINPUT
            targetSrc = 2;
        }

        double bestIn = (targetSrc >= 1 && targetSrc <= 4 && conn[targetSrc]) ? cand[targetSrc] : 0;
        int bestSrc = targetSrc;

        int curSrc = g_shared.runSrc.load();
        double x = (curSrc >= 1 && curSrc <= 4) ? cand[curSrc] : bestIn;

        if (fabs(x) < DEADZONE) {
            x = 0;
        } else if (g_stickCurveIdx.load() == 1) { // Instant Max Speed Mode
            x = (x > 0) ? 1.0 : -1.0;
        }

        int st = g_shared.state.load();

        if (st == 0) {
            g_shared.runSrc = 0;
            x = bestIn;
            if (bestIn > DEADZONE) {
                double f = 0.5;
                if (fabs(x - x_prev) > 1e-9) {
                    f = (DEADZONE - x_prev) / (x - x_prev);
                    if (f < 0) f = 0; else if (f > 1) f = 1;
                }
                g_shared.startT = t_prev + f * (t - t_prev);
                g_shared.yaw = 0;
                g_shared.runSrc = bestSrc;
                g_shared.state = 1;
            }
        } else if (st == 1) {
            double yaw = g_shared.yaw.load();
            if (x < -0.15) { // Reset measurement only if pushed opposite direction
                g_shared.yaw = 0;
                g_shared.state = 0;
            } else {
                double yawNew = yaw + max(0.0, x) * g_shared.sens.load() * dt;
                if (yawNew >= TARGET_RADIANS) {
                    double f = (TARGET_RADIANS - yaw) / (yawNew - yaw);
                    if (f < 0) f = 0; else if (f > 1) f = 1;
                    double endT = t_prev + f * dt;
                    g_shared.endT = endT;
                    g_shared.yaw = TARGET_RADIANS;
                    g_shared.state = 2;
                    double el = endT - g_shared.startT.load();
                    double jitterHumanMs = el * 1000.0 - 1000.0;
                    g_lastJitterHumanMs.store(jitterHumanMs);
                    if (el > 0.0001) {
                        double b = g_shared.best.load();
                        while (el < b && !g_shared.best.compare_exchange_weak(b, el)) {}
                    }
                } else {
                    g_shared.yaw = yawNew;
                }
            }
        } else { // st == 2
            if (x < 0.05) {
                g_shared.yaw = 0;
                g_shared.state = 0;
            }
        }

        x_prev = x;
        t_prev = t;
    }

    CoUninitialize();
    timeEndPeriod(1);
}

// ---------------------------------------------------------------- display mode
static void windowedClientSize(LONG& cw, LONG& ch) {
    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    LONG waw = wa.right - wa.left, wah = wa.bottom - wa.top;
    RECT t{ 0, 0, (LONG)WIDTH, (LONG)HEIGHT };
    DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&t, st, FALSE);
    LONG bw = (t.right - t.left) - (LONG)WIDTH;
    LONG bh = (t.bottom - t.top) - (LONG)HEIGHT;
    cw = min((LONG)WIDTH, waw - bw);
    ch = min((LONG)HEIGHT, wah - bh);
    if (cw < 320) cw = 320;
    if (ch < 240) ch = 240;
}

static void applyResolutionInternal(int idx) {
    if (idx < 0 || idx >= RES_COUNT) return;
    g_resIdx = idx;
    g_reqResIdx = idx;
    UINT w = RES_LIST[idx].w;
    UINT h = RES_LIST[idx].h;
    if (g_dxPtr && g_dxPtr->presented) {
        resizeSwapchain(*g_dxPtr, w, h);
    }
}

static void applyDisplayModeInternal(int mode) {
    g_displayMode = mode;
    g_reqDisplayMode = mode;
    if (!g_hwnd) return;

    if (mode == 1) { // FULLSCREEN (Direct Flip Hardware Low Latency)
        HMONITOR hMon = MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(hMon, &mi);

        SetWindowLongPtrW(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else { // FINESTRA (Desktop)
        DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, style);

        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        SetWindowPos(g_hwnd, HWND_TOP,
                     wa.left, wa.top,
                     wa.right - wa.left, wa.bottom - wa.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
        SetWindowTextW(g_hwnd, L"ULT Ultimate Latency Tester");
    }
    applyResolutionInternal(g_reqResIdx.load());
}

static void applyResolution(int idx) {
    if (idx < 0 || idx >= RES_COUNT) return;
    g_reqResIdx = idx;
}

static void applyDisplayMode(int mode) {
    g_reqDisplayMode = mode;
}

// ---------------------------------------------------------------- wnd proc
static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_NCCREATE:
        return TRUE;
    case WM_INPUT:
        handleRawInput((HRAWINPUT)l);
        return 0;
    case WM_DEVICECHANGE:
        g_deviceChangeTriggered.store(true);
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_MOVE:
        g_monitorHz.store(queryCurrentMonitorHz());
        break;
    case WM_SIZE:
        if (g_dxPtr && g_dxPtr->presented && w != SIZE_MINIMIZED) {
            applyResolution(g_resIdx.load());
        }
        g_monitorHz.store(queryCurrentMonitorHz());
        return 0;
    case WM_CHAR:
        if (g_menuOpen.load() && g_menuRow == 8) {
            wchar_t ch = (wchar_t)w;
            size_t len = wcslen(g_stickLatencyInputBuf);

            if (ch == VK_BACK) {
                if (len > 0) {
                    g_stickLatencyInputBuf[len - 1] = L'\0';
                }
            } else if ((ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L',') {
                if (ch == L',') ch = L'.';
                if (len < 8) {
                    if (ch == L'.' && wcschr(g_stickLatencyInputBuf, L'.') != nullptr) {
                        return 0;
                    }
                    if (len == 1 && g_stickLatencyInputBuf[0] == L'0' && ch != L'.') {
                        g_stickLatencyInputBuf[0] = ch;
                    } else {
                        g_stickLatencyInputBuf[len] = ch;
                        g_stickLatencyInputBuf[len + 1] = L'\0';
                    }
                }
            }

            double val = _wtof(g_stickLatencyInputBuf);
            if (val < 0.0) val = 0.0;
            if (val > 100.0) {
                val = 100.0;
                swprintf_s(g_stickLatencyInputBuf, L"100.0");
            }
            g_userCustomStickLatencyMs.store(val);
            return 0;
        }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_F11) {
            applyDisplayMode(1 - g_displayMode.load());
            return 0;
        }
        if (w == 'Q' || w == 'q') {
            g_testStick = 1 - g_testStick.load();
            return 0;
        }
        if (!g_menuOpen.load()) {
            if (w == VK_ESCAPE) {
                g_menuRow = 0;
                g_menuOpen = true;
            } else if (w == 'R' || w == 'r') {
                g_shared.yaw = 0;
                g_shared.state = 0;
                g_shared.best = 1e9;
                g_lastJitterHumanMs = 0.0;
            }
            return 0;
        }
        if (w == VK_ESCAPE) {
            g_menuOpen = false;
            return 0;
        }
        if (w == VK_UP) {
            g_menuRow = (g_menuRow - 1 + MENU_ROW_COUNT) % MENU_ROW_COUNT;
            return 0;
        }
        if (w == VK_DOWN) {
            g_menuRow = (g_menuRow + 1) % MENU_ROW_COUNT;
            return 0;
        }
        if (w == VK_LEFT || w == VK_RIGHT) {
            int dir = (w == VK_LEFT) ? -1 : 1;
            if (g_menuRow == 0) {
                g_fpsIdx = (g_fpsIdx.load() + dir + FPS_COUNT) % FPS_COUNT;
            } else if (g_menuRow == 1) {
                applyResolution((g_resIdx.load() + dir + RES_COUNT) % RES_COUNT);
            } else if (g_menuRow == 2) {
                g_gfxIdx = (g_gfxIdx.load() + dir + GRAPHICS_COUNT) % GRAPHICS_COUNT;
            } else if (g_menuRow == 3) {
                g_inputSrcIdx = (g_inputSrcIdx.load() + dir + INPUT_SRC_COUNT) % INPUT_SRC_COUNT;
            } else if (g_menuRow == 4) {
                applyDisplayMode(1 - g_displayMode.load());
            } else if (g_menuRow == 5) {
                g_vsyncIdx = 1 - g_vsyncIdx.load();
            } else if (g_menuRow == 6) {
                g_langIdx = (g_langIdx.load() + dir + LANG_COUNT) % LANG_COUNT;
            } else if (g_menuRow == 7) {
                g_stickCurveIdx = 1 - g_stickCurveIdx.load();
            } else if (g_menuRow == 8) {
                double val = g_userCustomStickLatencyMs.load() + (dir * 0.5);
                if (val < 0.0) val = 0.0;
                if (val > 100.0) val = 100.0;
                swprintf_s(g_stickLatencyInputBuf, L"%.1f", val);
                g_userCustomStickLatencyMs.store(val);
            }
            return 0;
        }
        if (w == VK_RETURN) {
            g_selectFlashT = QPC();
            g_selectFlashRow = g_menuRow;
            if (g_menuRow == 0) g_fpsIdx = (g_fpsIdx.load() + 1) % FPS_COUNT;
            else if (g_menuRow == 1) applyResolution((g_resIdx.load() + 1) % RES_COUNT);
            else if (g_menuRow == 2) g_gfxIdx = (g_gfxIdx.load() + 1) % GRAPHICS_COUNT;
            else if (g_menuRow == 3) g_inputSrcIdx = (g_inputSrcIdx.load() + 1) % INPUT_SRC_COUNT;
            else if (g_menuRow == 4) applyDisplayMode(1 - g_displayMode.load());
            else if (g_menuRow == 5) g_vsyncIdx = 1 - g_vsyncIdx.load();
            else if (g_menuRow == 6) g_langIdx = (g_langIdx.load() + 1) % LANG_COUNT;
            else if (g_menuRow == 7) g_stickCurveIdx = 1 - g_stickCurveIdx.load();
            else if (g_menuRow == 8) {
                double val = g_userCustomStickLatencyMs.load() + 1.0;
                if (val > 100.0) val = 0.0;
                swprintf_s(g_stickLatencyInputBuf, L"%.1f", val);
                g_userCustomStickLatencyMs.store(val);
            }
            return 0;
        }
        return 0;
    case WM_CLOSE:
        g_running = false;
        if (g_dxPtr) flushGpu(*g_dxPtr);
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_running = false;
        if (g_dxPtr) flushGpu(*g_dxPtr);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- main
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
        auto pSetDpi = (PFN_SetProcessDpiAwarenessContext)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pSetDpi) {
            pSetDpi((HANDLE)-4);
        } else {
            SetProcessDPIAware();
        }
    } else {
        SetProcessDPIAware();
    }
    timeBeginPeriod(1);
    debugLog("start");

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreq = (double)freq.QuadPart;

    HICON hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!hIcon) {
        hIcon = (HICON)LoadImageW(nullptr, L"app.ico", IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_SHARED);
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInst;
    wc.hIcon = hIcon;
    wc.hIconSm = hIcon;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"ULT_UltimateLatencyTesterClass";
    if (!RegisterClassExW(&wc)) { debugLog("RegisterClassExW FAILED"); return 1; }
    debugLog("class registered");

    RECT wa{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int posX = wa.left;
    int posY = wa.top;
    int posW = wa.right - wa.left;
    int posH = wa.bottom - wa.top;

    g_rtW = posW;
    g_rtH = posH;
    DWORD style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;

    g_hwnd = CreateWindowExW(0, L"ULT_UltimateLatencyTesterClass",
        L"ULT Ultimate Latency Tester", style,
        posX, posY, posW, posH, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) { char buf[64]; sprintf_s(buf, "CreateWindowExW FAILED err=%lu", GetLastError()); debugLog(buf); return 1; }
    debugLog("window created");
    SetWindowTextW(g_hwnd, L"ULT Ultimate Latency Tester");

    if (hIcon) {
        SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    ShowWindow(g_hwnd, SW_MAXIMIZE);
    debugLog("maximized");

    g_monitorHz.store(queryCurrentMonitorHz());

    InitializeCriticalSection(&g_rawCs);

    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x05; // gamepad (generic)
    rid[0].dwFlags = RIDEV_INPUTSINK; rid[0].hwndTarget = g_hwnd;
    rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x04; // joystick / DS4 / DS5
    rid[1].dwFlags = RIDEV_INPUTSINK; rid[1].hwndTarget = g_hwnd;
    if (RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)))
        debugLog("raw input registered");
    else
        debugLog("raw input register FAILED");
    scanRawDevices(true);

    Dx dx;
    debugLog("dx constructed");
    if (!initDx12(dx, g_hwnd)) {
        debugLog("initDx12 FAILED");
        MessageBoxW(g_hwnd, L"DirectX 12 initialization failed.",
                    L"ULT Ultimate Latency Tester", MB_ICONERROR);
        return 1;
    }
    g_dxPtr = &dx;
    debugLog("initDx12 ok");

    if (SUCCEEDED(DirectInput8Create(hInst, DIRECTINPUT_VERSION, IID_IDirectInput8,
                                     (void**)&g_di8, nullptr))) {
        g_di8->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCb, nullptr, DIEDFL_ATTACHEDONLY);
        debugLog("dinput enum gamectrl done");
    } else {
        debugLog("dinput create FAILED");
    }
    debugLog("dinput ok");

    thread th(inputThread);
    thread thWatch(deviceWatcherThread);
    debugLog("threads started");

    double frameT0 = QPC();
    int frames = 0;
    while (g_running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        renderFrame(dx);
        int fpsSel = (g_vsyncIdx.load() == 1) ? 0 : FPS_LIST[g_fpsIdx.load()];
        double target = fpsSel > 0 ? 1.0 / fpsSel : 0.0;
        if (target > 0.0) while (QPC() - frameT0 < target) Sleep(0);
        frameT0 = QPC();
    }

    g_running = false;
    th.join();
    thWatch.join();
    flushGpu(dx);
    g_dxPtr = nullptr;
    return 0;
}
