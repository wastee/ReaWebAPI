"""Opt-in live appearance acceptance in an isolated REAPER profile."""
import hidden_start_smoke as smoke

smoke.PAGE = smoke.PAGE.replace('<script>', '<input id="retained" value="retain me"><script>')
smoke.PAGE = smoke.PAGE.replace("  await reaper.lifecycle.ready;", """
  await reaper.lifecycle.ready;
  const identity = {};
  window.appearanceIdentity = identity;
  await reaper.window.setCornerRadius(24);
  await reaper.window.setFrameless(true);""")
smoke.PAGE = smoke.PAGE.replace("      if (command === 'reload')", """
      if (command === 'focused') {
        const w = reaper.window, p = await w.getPosition(), s = await w.getSize();
        for (let i=0; i<3; ++i) {
          await w.setFrameless(false);
          if ((await w.getState()).framelessActive) throw Error('Title bar state');
          await w.setFrameless(true);
        }
        const next = await w.getPosition(), size = await w.getSize();
        if (next.x !== p.x || next.y !== p.y || size.width !== s.width || size.height !== s.height)
          throw Error('Appearance changed outer bounds');
        if (window.appearanceIdentity !== identity || document.getElementById('retained').value !== 'retain me')
          throw Error('Document state lost');
      }
      if (command === 'reload')""")
smoke.PAGE = smoke.PAGE.replace("      const state = await reaper.window.getState();", """
      const state = await reaper.window.getState();
      if (!state.frameless || state.cornerRadius !== 24 || (state.docked && state.cornerRadiusActive))
        throw Error('Invalid appearance state: ' + JSON.stringify(state));
      if (state.visible && !state.docked && !state.cornerRadiusActive)
        throw Error('Floating corners were not restored');""")
smoke.SCRIPT = smoke.SCRIPT.replace('hidden opens, ready', 'live appearance cycles, hidden opens, ready')
if __name__ == '__main__':
    raise SystemExit(smoke.main())
