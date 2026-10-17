/** Connect only from a JSFX Hosted WebView. Values always originate in REAPER. */
export async function connectJSFX(reaper = globalThis.reaper, onError = console.error) {
  const service = reaper.host.service('jsfx');
  const listeners = new Set(), visibility = new Set(), pending = new Map(), observed = new Map();
  let state, revision = 0, editing = -1, running, opening, disposed = false, activation = 0, active = false;
  const notify = () => { for (const listener of listeners) listener(state); };
  const present = async () => {
    if (!active || disposed) return;
    const current = activation;
    try { await service.invoke('present', { activation: current }); }
    catch (error) { if (active && activation === current && !disposed) throw error; }
  };
  const apply = next => {
    if (next.activation < activation) return;
    if (next.activation > activation) { revision = 0; observed.clear(); }
    if (state && next.activation === activation && next.revision < revision) return;
    // Events can arrive before the initial snapshot. Keep their newer parameter values.
    for (const parameter of next.parameters) {
      const newer = observed.get(parameter.index);
      if (newer && newer.revision > next.revision) Object.assign(parameter, newer.parameter);
    }
    state = next; revision = Math.max(revision, next.revision); state.revision = revision;
    activation = next.activation; active = next.active;
    notify();
  };
  const refresh = async () => {
    const next = await service.invoke('getState');
    if (!disposed) apply(next);
    return state;
  };
  const off = await service.on('paramChanged', event => {
    if (disposed || event.activation < activation || (state && (!active || event.activation !== activation)) || event.revision <= revision) return;
    if (event.activation > activation) { activation = event.activation; observed.clear(); }
    revision = event.revision;
    for (const parameter of event.parameters) {
      observed.set(parameter.index, { revision, parameter });
      if (state) Object.assign(state.parameters[parameter.index] || {}, parameter);
    }
    if (state) { state.revision = revision; notify(); }
  });
  let offVisibility;
  try {
    offVisibility = await service.on('visibilityChanged', event => {
      if (disposed || event.activation < activation) return;
      if (event.activation > activation) { revision = 0; observed.clear(); }
      activation = event.activation; active = event.active;
      pending.clear(); editing = -1; opening = undefined;
      if (state) { state.active = active; state.activation = activation; }
      if (event.state) apply(event.state);
      for (const listener of visibility) listener(active);
      if (active) present().catch(onError);
    });
    await refresh();
  } catch (error) { await off(); if (offVisibility) await offVisibility(); throw error; }
  // Let the caller render its initial state before acknowledging presentation.
  setTimeout(() => present().catch(onError), 0);
  const requireActive = () => {
    if (disposed || !active) throw new Error('JSFX UI is inactive');
    return activation;
  };
  const drain = () => {
    if (running) return running;
    running = (async () => {
      if (opening) await opening;
      while (pending.size && !disposed && active) {
        const [index, value] = pending.entries().next().value;
        pending.delete(index);
        const current = activation;
        const before = revision;
        try {
          const result = await service.invoke('setParam', { index, value, normalized: true, activation: current });
          const newer = observed.get(index);
          const resultRevision = result.revision ?? before;
          if (!disposed && current === activation && active && !pending.has(index) &&
              (!newer || newer.revision <= resultRevision)) {
            Object.assign(state.parameters[index], result);
            observed.set(index, { revision: resultRevision, parameter: result });
            revision = Math.max(revision, resultRevision); state.revision = revision; notify();
          }
        } catch (error) { if (current === activation && active) throw error; }
      }
    })().finally(() => { running = undefined; });
    return running;
  };
  return {
    get state() { return state; },
    get active() { return active && !disposed; },
    getState: refresh,
    present,
    onChange(listener) { listeners.add(listener); return () => listeners.delete(listener); },
    onVisibilityChange(listener) { visibility.add(listener); return () => visibility.delete(listener); },
    async beginEdit(index) {
      const current = requireActive();
      if (editing >= 0) throw new Error('Finish the active parameter gesture first');
      editing = index;
      const operation = service.invoke('beginEdit', { index, activation: current });
      opening = operation;
      try { await operation; } catch (error) { if (current === activation && active) { editing = -1; throw error; } }
      finally { if (opening === operation) opening = undefined; }
    },
    setParam(index, normalized) {
      if (disposed || !active) return;
      if (!Number.isInteger(index) || index < 0 || index >= state.count || !Number.isFinite(normalized) || normalized < 0 || normalized > 1)
        throw new RangeError('Expected a valid parameter index and normalized value');
      pending.set(index, normalized);
      if (!running) drain().catch(onError);
    },
    async endEdit() {
      const current = requireActive();
      try { if (opening) await opening; await drain(); }
      finally {
        if (current === activation && active) {
          try { await service.invoke('endEdit', { activation: current }); } finally { editing = -1; }
        }
      }
      if (current !== activation || !active) return state;
      return refresh();
    },
    async reset(index) {
      const current = requireActive();
      const value = state.parameters[index]?.defaultNormalized;
      if (value === null || value === undefined) throw new Error('REAPER does not expose a default for this parameter');
      await service.invoke('setParam', { index, value, normalized: true, activation: current });
      return refresh();
    },
    async dispose() {
      if (disposed) return;
      disposed = true; pending.clear();
      try { if (opening) await opening; if (running) await running; } finally {
        try { if (active) await service.invoke('endEdit', { activation }); }
        finally {
          try { await off(); } finally { await offVisibility(); listeners.clear(); visibility.clear(); }
        }
      }
    }
  };
}
