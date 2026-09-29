BEGIN
  GROUPBOX "ReaWebAPI settings", 1000, 0, 0, 330, 220
  CONTROL "Use sRGB for WebView rendering on Windows (requires REAPER restart)", 1001, "Button", BS_AUTOCHECKBOX | WS_TABSTOP, 10, 10, 300, 12
  GROUPBOX "External Clients", 1009, 10, 20, 290, 110
  CONTROL "Enable External Client Server", 1010, "Button", BS_AUTOCHECKBOX | WS_TABSTOP, 20, 30, 285, 12
  LTEXT "Port:", -1, 20, 50, 24, 12
  EDITTEXT 1011, 34, 47, 34, 14, ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP
  LTEXT "127.0.0.1 only", -1, 74, 50, 170, 12
  LTEXT "Access Token:", -1, 20, 67, 280, 12
  EDITTEXT 1012, 20, 81, 285, 14, ES_PASSWORD | ES_READONLY | ES_AUTOHSCROLL
  PUSHBUTTON "Copy", 1013, 20, 101, 50, 14
  PUSHBUTTON "Regenerate", 1014, 76, 101, 68, 14
  LTEXT "Regenerating the token will disconnect all external clients.", -1, 20, 122, 285, 12
  PUSHBUTTON "Restore defaults", 1003, 260, 200, 70, 14
END
