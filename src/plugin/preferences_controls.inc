BEGIN
  GROUPBOX "ReaWebAPI settings", 1000, 0, 0, 330, 220
  CONTROL "Use sRGB for WebView rendering on Windows (requires REAPER restart)", 1001, "Button", BS_AUTOCHECKBOX | WS_TABSTOP, 10, 10, 300, 12
  LTEXT "Preloaded WebView containers:", -1, 10, 29, 104, 12
  EDITTEXT 1004, 118, 26, 30, 14, ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP
  LTEXT "(0 = off)", -1, 152, 29, 50, 12
  GROUPBOX "External Clients", 1009, 10, 44, 310, 120
  CONTROL "Enable External Client Server", 1010, "Button", BS_AUTOCHECKBOX | WS_TABSTOP, 20, 54, 285, 12
  LTEXT "Port:", -1, 20, 74, 20, 12
  EDITTEXT 1011, 40, 71, 30, 14, ES_NUMBER | ES_AUTOHSCROLL | WS_TABSTOP
  LTEXT "127.0.0.1 only", -1, 74, 74, 170, 12
  LTEXT "Access Token:", -1, 20, 91, 280, 12
  EDITTEXT 1012, 20, 105, 285, 14, ES_PASSWORD | ES_READONLY | ES_AUTOHSCROLL
  PUSHBUTTON "Copy", 1013, 20, 125, 50, 14
  PUSHBUTTON "Regenerate", 1014, 76, 125, 68, 14
  LTEXT "Regenerating the token will disconnect all external clients.", -1, 20, 146, 285, 12
  PUSHBUTTON "Restore defaults", 1003, 260, 200, 70, 14
END
