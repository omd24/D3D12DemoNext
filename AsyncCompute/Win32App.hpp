#pragma once

/******************************************************************************
 * \win32-based application running the demo
 * \
 ******************************************************************************/

#include "DemoUtils.hpp"

//---------------------------------------------------------------------------//
// The required functions in terms of callback:
//---------------------------------------------------------------------------//
typedef void (*onInitFunc)(void);
typedef void (*onDestroyFunc)(void);
typedef void (*onUpdateFunc)(void);
typedef void (*onRenderFunc)(void);
typedef void (*onKeyDownFunc)(UINT8);
typedef void (*onKeyUpFunc)(UINT8);
//---------------------------------------------------------------------------//
// A registery to to pass around the callbacks
struct CallBackRegistery {
  onInitFunc onInit = nullptr;
  onDestroyFunc onDestroy = nullptr;
  onUpdateFunc onUpdate = nullptr;
  onRenderFunc onRender = nullptr;
  onKeyDownFunc onKeyDown = nullptr;
  onKeyUpFunc onKeyUp = nullptr;
};
//---------------------------------------------------------------------------//
// Windowy functions:
//---------------------------------------------------------------------------//
HWND g_WinHandle = nullptr;
//---------------------------------------------------------------------------//
inline void setWindowTitle(LPCWSTR p_Text, const std::wstring& p_Title) {
  std::wstring windowText = p_Title + L": " + p_Text;
  SetWindowText(g_WinHandle, windowText.c_str());
}
//---------------------------------------------------------------------------//
inline void msgBox(const std::string& p_Msg) {
  MessageBoxA(g_WinHandle, p_Msg.c_str(), "Error", MB_OK);
}
//---------------------------------------------------------------------------//
// Traces an error and convert the msg to a human-readable string
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
  CallBackRegistery* cbRegPtr = reinterpret_cast<CallBackRegistery*>(
      GetWindowLongPtr(p_Wnd, GWLP_USERDATA));
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
    if (cbRegPtr) {
      cbRegPtr->onKeyDown(static_cast<UINT8>(p_WParam));
    }
    return 0;

  case WM_KEYUP:
    if (cbRegPtr) {
      cbRegPtr->onKeyUp(static_cast<UINT8>(p_WParam));
    }
    return 0;

  case WM_PAINT:
    if (cbRegPtr) {
      cbRegPtr->onUpdate();
      cbRegPtr->onRender();
    }
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(p_Wnd, p_Message, p_WParam, p_LParam);
}
//---------------------------------------------------------------------------//
// Runs the win32-based app:
inline int
appExec(HINSTANCE p_Instance, int p_CmdShow, CallBackRegistery p_CbReg) {
  DEBUG_BREAK(g_DemoInfo != nullptr && g_DemoInfo->m_IsInitialized);

  // Parse the command line parameters
  int argc;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  demoParseCmdArgs(argv, argc);
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
      static_cast<LONG>(g_DemoInfo->m_Width),
      static_cast<LONG>(g_DemoInfo->m_Height)};
  AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

  // Create the window and store a handle to it.
  g_WinHandle = CreateWindow(
      windowClass.lpszClassName,
      g_DemoInfo->m_Title.c_str(),
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      windowRect.right - windowRect.left,
      windowRect.bottom - windowRect.top,
      nullptr, // We have no parent window.
      nullptr, // We aren't using menus.
      p_Instance,
      &p_CbReg);

  if (g_WinHandle == nullptr) {
    msgBox("CreateWindowEx() failed");
    return 0;
  }

  // Initialize the demo. OnInit is demo specific
  if (p_CbReg.onInit)
    p_CbReg.onInit();

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

  if (p_CbReg.onDestroy)
    p_CbReg.onDestroy();

  // Return this part of the WM_QUIT message to Windows.
  return static_cast<char>(msg.wParam);
}
//---------------------------------------------------------------------------//
