#pragma once

/******************************************************************************
 * \win32-based application manager responsible for running the demo
 * \
 ******************************************************************************/

#include "Demo.hpp"

//---------------------------------------------------------------------------//
// Demo callbacks:
//---------------------------------------------------------------------------//
typedef void (*onInitFunc)(void);
typedef void (*onUpdateFunc)(void);
typedef void (*onRenderFunc)(void);

//---------------------------------------------------------------------------//
// Dispaly a Message Box
inline void msgBox(const std::string& p_Msg) {
  MessageBoxA(g_WinHandle, p_Msg.c_str(), "Error", MB_OK);
}
//---------------------------------------------------------------------------//
// Trace an error and convert the msg to a human-readable string
inline void traceHr(const std::string& p_Msg, HRESULT p_Hr) {
  char hrMsg[512];
  FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM,
      nullptr,
      p_Hr,
      0,
      hrMsg,
      arrayCount32(hrMsg),
      nullptr);
  std::string errMsg = p_Msg + ".\nError! " + hrMsg;
  msgBox(errMsg);
}
//---------------------------------------------------------------------------//
static LRESULT CALLBACK
msgProc(HWND p_Wnd, UINT p_Message, WPARAM p_WParam, LPARAM p_LParam) {
  switch (p_Message) {
  case WM_CREATE: {
    // Save the data passed in to CreateWindow.
    LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(p_LParam);
    SetWindowLongPtr(
        p_Wnd,
        GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
  }
    return 0;

  case WM_KEYDOWN:
    if (g_Demo) {
      demoKeyDown(static_cast<UINT8>(p_WParam));
    }
    return 0;

  case WM_KEYUP:
    if (g_Demo) {
      demoKeyUp(static_cast<UINT8>(p_WParam));
    }
    return 0;

  case WM_PAINT:
    if (g_Demo) {
      demoUpdate();
      demoRender();
    }
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(p_Wnd, p_Message, p_WParam, p_LParam);
}

//---------------------------------------------------------------------------//
// WinApi app manager:
//---------------------------------------------------------------------------//
inline int appMgrRun(
    HINSTANCE p_Instance,
    int p_CmdShow,
    onRenderFunc p_OnInit = nullptr,
    onRenderFunc p_OnUpdate = nullptr,
    onRenderFunc p_OnRender = nullptr) {
  DEBUG_BREAK(g_Demo != nullptr && g_Demo->m_IsInitialized);

  // Parse the command line parameters
  int argc;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  demoParseCommandLineArgs(argv, argc);
  LocalFree(argv);

  // Initialize the window class.
  WNDCLASSEX windowClass = {0};
  windowClass.cbSize = sizeof(WNDCLASSEX);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = msgProc;
  windowClass.hInstance = p_Instance;
  windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
  windowClass.lpszClassName = L"DXSampleClass";
  if (RegisterClassEx(&windowClass) == 0) {
    msgBox("RegisterClassEx() failed");
    return 0;
  }

  RECT windowRect = {
      0,
      0,
      static_cast<LONG>(g_Demo->m_Width),
      static_cast<LONG>(g_Demo->m_Height)};
  AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

  // Create the window and store a handle to it.
  g_WinHandle = CreateWindow(
      windowClass.lpszClassName,
      g_Demo->m_Title.c_str(),
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      windowRect.right - windowRect.left,
      windowRect.bottom - windowRect.top,
      nullptr, // We have no parent window.
      nullptr, // We aren't using menus.
      p_Instance,
      0);

  if (g_WinHandle == nullptr) {
    msgBox("CreateWindowEx() failed");
    return 0;
  }

  // Initialize the demo. OnInit is demo specific
  if (p_OnInit)
    p_OnInit();

  ShowWindow(g_WinHandle, p_CmdShow);

  // Main sample loop.
  MSG msg = {};
  while (msg.message != WM_QUIT) {
    // Process any messages in the queue.
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }

  demoDestroy();

  // Return this part of the WM_QUIT message to Windows.
  return static_cast<char>(msg.wParam);
}
