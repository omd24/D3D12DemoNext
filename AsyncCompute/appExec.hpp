#pragma once

/******************************************************************************
 * \Application is executed here...
 * \
 ******************************************************************************/

#include "DemoUtils.hpp"

//---------------------------------------------------------------------------//
// Windowy functions:
//---------------------------------------------------------------------------//
inline void setWindowTitle(LPCWSTR p_Text, const std::wstring& p_Title) {
  std::wstring windowText = p_Title + L": " + p_Text;
  SetWindowText(g_WinHandle, windowText.c_str());
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
  WIN32_MSG_BOX(errMsg.c_str());
}
//---------------------------------------------------------------------------//
static LRESULT CALLBACK
msgProc(HWND p_Wnd, UINT p_Message, WPARAM p_WParam, LPARAM p_LParam) {
  CallBackRegistery* funcRegPtr = reinterpret_cast<CallBackRegistery*>(
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
    if (funcRegPtr->onKeyDown) {
      funcRegPtr->onKeyDown(static_cast<UINT8>(p_WParam));
    }
    return 0;

  case WM_KEYUP:
    if (funcRegPtr->onKeyUp) {
      funcRegPtr->onKeyUp(static_cast<UINT8>(p_WParam));
    }
    return 0;

  case WM_PAINT:
    if (funcRegPtr->onUpdate && funcRegPtr->onRender) {
      funcRegPtr->onUpdate();
      funcRegPtr->onRender();
    }
    return 0;

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(p_Wnd, p_Message, p_WParam, p_LParam);
}
//---------------------------------------------------------------------------//
// Execute the application:
//---------------------------------------------------------------------------//
inline int
appExec(HINSTANCE p_Instance, int p_CmdShow, CallBackRegistery p_FuncReg) {
  DEBUG_BREAK(g_DemoInfo != nullptr && g_DemoInfo->m_IsInitialized);

  // Parse the command line parameters
  int argc;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  demoParseCmdArgs(g_DemoInfo, argv, argc);
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
    WIN32_MSG_BOX("RegisterClassEx() failed");
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
      &p_FuncReg);

  if (g_WinHandle == nullptr) {
    WIN32_MSG_BOX("CreateWindowEx() failed");
    return 0;
  }

  // Initialize the demo. OnInit is demo specific
  if (p_FuncReg.onInit)
    p_FuncReg.onInit();

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

  if (p_FuncReg.onDestroy)
    p_FuncReg.onDestroy();

  // Return this part of the WM_QUIT message to Windows.
  return static_cast<char>(msg.wParam);
}
//---------------------------------------------------------------------------//
