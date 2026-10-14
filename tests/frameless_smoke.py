"""Opt-in REAPER lifecycle acceptance for frameless hidden startup and Dock restoration."""
import hidden_start_smoke as hidden

hidden.PAGE = hidden.PAGE.replace('<title>Hidden startup acceptance</title>', '<title>Frameless acceptance</title>')
hidden.PAGE = hidden.PAGE.replace('<p>Hidden startup acceptance</p>', '<header data-reaweb-drag>Frameless acceptance <button onclick="reaper.window.close()">Close</button></header>')
hidden.PAGE = hidden.PAGE.replace("const state = await reaper.window.getState();", """const state = await reaper.window.getState();
      if (!state.frameless || state.resizable || state.framelessActive === state.docked)
        throw new Error('Invalid frameless state: ' + JSON.stringify(state));""")
hidden.SCRIPT = hidden.SCRIPT.replace('"hidden":true}', '"hidden":true,"frameless":true,"resizable":false}')
hidden.SCRIPT = hidden.SCRIPT.replace('hidden opens, ready', 'frameless fixed-size hidden opens, ready')

if __name__ == '__main__':
    raise SystemExit(hidden.main())
