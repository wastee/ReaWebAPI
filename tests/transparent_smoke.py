"""Real REAPER acceptance for transparent-window state, Dock rejection and lifecycle."""
import hidden_start_smoke as hidden

hidden.PAGE = hidden.PAGE.replace('html{background:#182838;color:white}', 'html,body{background:transparent;color:white}')
hidden.PAGE = hidden.PAGE.replace("if (command === 'dock') await reaper.window.setDocked(true);", """if (command === 'dock') {
        let refused = false;
        try { await reaper.window.setDocked(true); } catch (error) { refused = error.code === 'DOCK_UNAVAILABLE'; }
        if (!refused) throw new Error('Dock was not rejected');
      }""")
hidden.PAGE = hidden.PAGE.replace('const state = await reaper.window.getState();', """let state = await reaper.window.getState();
      if (command === 'shown' || command === 'reused') {
        for (let n = 0; n < 500 && !state.visible; ++n) {
          await new Promise(resolve => setTimeout(resolve, 10)); state = await reaper.window.getState();
        }
      }
      if (!state.transparent || state.docked || (state.visible && !state.transparentActive))
        throw new Error('Invalid Alpha state: ' + JSON.stringify(state));""", 1)
hidden.SCRIPT = hidden.SCRIPT.replace('"hidden":true}', '"hidden":true,"frameless":true,"transparent":true}')
hidden.SCRIPT = hidden.SCRIPT.replace("check(reaper.ReaWeb_Show(id)); command('shown', 'shown')", """check(not reaper.ReaWeb_SetDocked(id, true), 'Lua Dock was not rejected')
    check(reaper.ReaWeb_GetLastError():find('standalone'), reaper.ReaWeb_GetLastError())
    check(reaper.ReaWeb_Show(id)); command('shown', 'shown')""")
hidden.SCRIPT = hidden.SCRIPT.replace('hidden opens, ready, messages, Show/Focus, reuse, Dock restore, reload and close',
    'transparent hidden opens, state, Lua/JS Dock rejection, Show/Focus, reuse, reload and close')

if __name__ == '__main__':
    raise SystemExit(hidden.main())
