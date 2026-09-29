BEGIN
  GROUPBOX "ReaWebAPI settings", 1000, 0, 0, 330, 220
  CONTROL "Use sRGB for WebView rendering on Windows (requires REAPER restart)", 1001, "Button", BS_AUTOCHECKBOX | WS_TABSTOP, 10, 14, 300, 12
  GROUPBOX "External Clients", 1009, 10, 38, 310, 124
  CONTROL "Enable External Client Server (requires REAPER restart)", 1010, "Button", BS_AUTOCHECKBOX | WS_TABSTOP, 20, 48, 285, 12
  LTEXT "Port:", -1, 20, 68, 35, 12
  EDITTEXT 1011, 58, 65, 58, 14, ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP
  LTEXT "127.0.0.1 only", -1, 125, 68, 170, 12
  LTEXT "Access Token:", -1, 20, 85, 280, 12
  EDITTEXT 1012, 20, 99, 285, 14, ES_PASSWORD | ES_READONLY | ES_AUTOHSCROLL
  PUSHBUTTON "Copy", 1013, 20, 119, 50, 14
  PUSHBUTTON "Regenerate", 1014, 76, 119, 68, 14
  LTEXT "Regenerating the token will disconnect all external clients.", -1, 20, 140, 285, 12
  PUSHBUTTON "Restore defaults", 1003, 254, 200, 70, 14
END
