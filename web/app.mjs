import { SerialClient } from './serial.mjs';
import { MonitorArrangement } from './monitor-layout.mjs';
const $ = id => document.getElementById(id);
let editingAction = 0, editingKind = 0, recordingBusy = false, recordingPoll = false, bindingSignature = '';
let connected = false, snapshot, slotSignature = '', token = '', polling = false, deviceSignature = '', inputSnapshot = null, firmwareCatalog = [], installing = false, pendingSlotNames = null;
const client = new SerialClient(() => {
  connected = false; $('ble-dialog').close(); token = ''; editingKind = 0; bindingSignature = '';  $('settings').disabled = true; $('disconnect').disabled = true; $('connect').disabled = false; $('installer').hidden = false;
  $('connection').textContent = 'Disconnected. Reconnect to read current settings.';
}, line => {
  if (line.startsWith('[BLE computer pairing] ')) $('computer-passkey').textContent = line.slice(23);
});
function notice(text) { $('notice').textContent = text; $('notice').hidden = !text; }
function on(id, callback, event = 'click') {
  $(id).addEventListener(event, async e => {
    e.preventDefault();
    try { await callback(); } catch (error) { notice(error.message); }
  });
}
async function command(op, args = {}) {
  const result = await client.request(op, args);
  notice(''); await refresh(); return result;
}
function button(label, callback) {
  const b = document.createElement('button'); b.textContent = label;
  b.onclick = () => Promise.resolve().then(callback).catch(error => notice(error.message)); return b;
}
function iconButton(label, path, callback) {
  const b = document.createElement('button'); b.type = 'button'; b.className = 'icon-action'; b.setAttribute('aria-label', label); b.title = label;
  b.innerHTML = `<svg viewBox="0 0 24 24" aria-hidden="true"><path d="${path}"/></svg>`;
  b.onclick = () => Promise.resolve().then(callback).catch(error => notice(error.message)); return b;
}
function updateProgressVisibility() {
  const sections = [...document.querySelectorAll('#settings > section')];
  const inputs = sections[0], outputs = sections[1], switching = sections[2];
  if (!inputs || !outputs || !switching) return;
  inputs.hidden = !connected;
  outputs.hidden = !connected;
  const hasOutput = !!snapshot?.slots?.some(slot => slot.paired || slot.connected);
  const hasInput = !!(snapshot?.usb?.interfaces?.length || inputSnapshot?.saved || inputSnapshot?.ready);
  switching.hidden = !connected || !hasOutput || !hasInput;
}
updateProgressVisibility();
function renderSlots(status) {
  const signature = JSON.stringify([status.slots, status.selected]);
  if (signature === slotSignature) return;
  // Preserve unsaved names during periodic status refreshes.
  const drafts = pendingSlotNames || [...$('slots').querySelectorAll('.output-row input')].map(i => i.value); pendingSlotNames = null;
  slotSignature = signature; $('slots').replaceChildren();
  status.slots.forEach((slot, i) => {
    const row = document.createElement('div'); row.className = 'slot output-row'; row.dataset.slot = i;
    const handle = document.createElement('button'); handle.type = 'button'; handle.className = 'drag-handle'; handle.draggable = true; handle.setAttribute('aria-label', `Reorder slot ${i + 1}`); handle.textContent = '⠿';
    handle.addEventListener('dragstart', e => { e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', String(i)); row.classList.add('dragging'); });
    handle.addEventListener('dragend', () => row.classList.remove('dragging'));
    row.addEventListener('dragover', e => { e.preventDefault(); row.classList.add('drag-over'); });
    row.addEventListener('dragleave', () => row.classList.remove('drag-over'));
    row.addEventListener('drop', async e => {
      e.preventDefault(); row.classList.remove('drag-over');
      const from = Number(e.dataTransfer.getData('text/plain')); if (!Number.isInteger(from) || from === i) return;
      const order = [0, 1, 2]; const [moved] = order.splice(from, 1); order.splice(i, 0, moved);
      const currentNames = [...$('slots').querySelectorAll('.output-row input')].map(input => input.value);
      pendingSlotNames = order.map(n => currentNames[n]);
      await command('slots', { order, names: pendingSlotNames, generation: snapshot.generation });
    });
    const heading = document.createElement('h3'); heading.className = 'slot-heading'; heading.textContent = String(i + 1); heading.title = `Slot ${i + 1}`;
    const states = document.createElement('span'); states.className = 'slot-states';
    const selectedState = document.createElement('span'); selectedState.className = `status-icon ${i === status.selected ? 'active' : 'inactive'}`; selectedState.title = i === status.selected ? 'Selected output' : 'Not selected'; selectedState.setAttribute('aria-label', selectedState.title); selectedState.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2"/></svg>';
    const connectedState = document.createElement('span'); connectedState.className = `status-icon ${slot.connected ? 'active' : 'inactive'}`; connectedState.title = slot.connected ? 'Connected output' : 'Disconnected output'; connectedState.setAttribute('aria-label', connectedState.title); connectedState.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M8 12h8M12 8v8M6 6l3-3m9 3-3-3M6 18l3 3m9-3-3 3"/></svg>';
    states.append(selectedState, connectedState); heading.append(states);
    const label = document.createElement('label'); label.textContent = 'Computer name';
    const input = document.createElement('input'); input.value = drafts[i] ?? slot.name; input.maxLength = 32; label.append(input);
    const actions = document.createElement('div'); actions.className = 'actions output-actions';
    if (i !== status.selected) actions.append(iconButton(slot.paired || slot.connected ? 'Switch here' : 'Pair computer', slot.paired || slot.connected ? 'M5 12h14M13 6l6 6-6 6' : 'M12 5v14M5 12h14', () => command('select', { slot: i })));
    const save = iconButton('Save name', 'M5 12l4 4L19 6', async () => {
      const names = [...$('slots').querySelectorAll('.output-row input')].map((field, index) => index === i ? input.value : field.value);
      await command('slots', { names, order: [0,1,2], generation: snapshot.generation });
    });
    save.hidden = input.value === slot.name;
    input.addEventListener('input', () => { save.hidden = input.value === snapshot.slots[i].name; });
    actions.append(save);
    if (slot.paired || slot.connected) actions.append(iconButton('Forget pairing', 'M6 7h12M9 7V4h6v3m-8 0 1 13h8l1-13', async () => {
      if (confirm(`Forget the computer in slot ${i + 1}?`)) await command('forget', { slot: i, confirm: true });
    }));
    if (i === status.selected && !slot.paired && !slot.connected) {
      const hint = document.createElement('p'); hint.className = 'small'; hint.textContent = 'Ready to pair in this computer’s Bluetooth settings.'; row.append(hint);
    }
    row.append(handle,heading,label,actions); $('slots').append(row);
  });
}
const arrangement = new MonitorArrangement($('monitor-arrangement'), args => command('monitor-layout', args));
async function refresh() {
  snapshot = await client.request('status');
  if (snapshot.protocol !== 1) throw new Error('Unsupported firmware protocol');
  $('advertised-name').textContent = snapshot.name;
  if (!$('device-name').value) $('device-name').value = snapshot.name;
  if (!$('edge').value) $('edge').value = snapshot.edge_distance;
  $('connection').textContent = `Connected · ${snapshot.name} · Settings saved on board`;
  renderSlots(snapshot);
  arrangement.update(snapshot);
  const installed = snapshot.firmware;
  const known = installed && firmwareCatalog.find(f => f.build_id === installed.build_id);
  $('installed-firmware').textContent = installed ? `Installed: ${known?.version || installed.version} · ${installed.date} ${installed.time}` : 'Installed version unavailable on this firmware.';
  const usb = snapshot.usb;
  const usbSignature = JSON.stringify(usb?.interfaces);
  if ($('usb-devices').dataset.signature !== usbSignature) {
    $('usb-devices').dataset.signature = usbSignature;
    $('usb-devices').replaceChildren();
    const groups = new Map();
    for (const entry of usb?.interfaces || []) {
      const key = `${entry.address}:${entry.vid}:${entry.pid}`;
      if (!groups.has(key)) groups.set(key, []);
      groups.get(key).push(entry);
    }
    for (const entries of groups.values()) {
      const kinds = [...new Set(entries.map(e => e.kind).filter(k => k !== 'Other HID'))];
      const row = document.createElement('div'); row.className = 'device-card';
      const title = document.createElement('strong'); title.textContent = `USB ${kinds.length ? kinds.join(' + ').toLowerCase() : 'input device'}`;
      const state = document.createElement('p'); state.className = 'small';
      const device = entries[0];
      state.textContent = `${entries.some(e => e.active) ? 'Connected' : 'Unavailable'} · ${device.vid.toString(16).padStart(4,'0')}:${device.pid.toString(16).padStart(4,'0')}`;
      row.append(title,state); $('usb-devices').append(row);
    }
  }
  $('seamless-toggle').setAttribute('aria-checked', String(snapshot.seamless));
  $('seamless-toggle').disabled = !snapshot.absolute || snapshot.calibration_active;
  $('seamless-options').hidden = !snapshot.seamless && !snapshot.calibration_active;
  $('switch-help').hidden = snapshot.seamless || snapshot.calibration_active;
  $('switch-help').textContent = snapshot.absolute ? 'Turn on to switch across screen edges. Calibration is optional.' : 'This firmware does not support seamless switching.';
  $('edge-form').hidden = snapshot.calibration_active;
  $('calibration-summary').textContent = snapshot.calibration_active ? 'Calibration in progress' : 'Estimate starting screen proportions and shared speed with a corner-to-corner sweep. Saved screen dimensions stay unchanged.';
  $('calibrate').textContent = 'Calibrate sensitivity';
  $('speed-form').hidden = snapshot.calibration_active;
  if(document.activeElement!==$('pointer-speed')&&!$('pointer-speed').dataset.dirty)$('pointer-speed').value=snapshot.pointer_speed ?? 100;
  $('speed-save').hidden=Number($('pointer-speed').value)===(snapshot.pointer_speed ?? 100);
  $('calibrate').hidden = snapshot.calibration_active || !snapshot.calibration_ready;
  $('calibration-cancel').hidden = !snapshot.calibration_active;
  $('calibration-guide').hidden = !snapshot.calibration_active;
  $('calibration').hidden = !snapshot.calibration_active;
  $('rename-save').hidden = $('device-name').value === snapshot.name;
  $('edge-save').hidden = Number($('edge').value) === snapshot.edge_distance;
  $('switch-state').textContent = snapshot.seamless ? 'On' : 'Off';
  $('calibration').textContent = snapshot.calibration_active
    ? `Slot ${snapshot.selected + 1}: ${snapshot.calibration_stage === 1 ? 'Move to TOP LEFT; left-click and release.' : snapshot.calibration_stage === 2 ? 'Move to BOTTOM RIGHT; left-click and release.' : 'Saving calibration…'}`
    : `Calibration: ${snapshot.calibration_result}`;
  const actionNames = ['Cycle computers','Next computer','Previous computer','Select slot'];
  const keyName = key => key >= 4 && key <= 29 ? String.fromCharCode(65 + key - 4)
    : key >= 30 && key <= 39 ? String((key - 29) % 10)
    : ({40:'Enter',41:'Escape',43:'Tab',44:'Space'}[key] || `Key ${key}`);
  const signature = JSON.stringify([snapshot.shortcuts.bindings, editingAction, editingKind, token, recordingBusy]);
  if (signature !== bindingSignature) {
    bindingSignature = signature; $('bindings').replaceChildren();
    actionNames.forEach((name, action) => {
      const row = document.createElement('tr'); const title = document.createElement('th'); title.scope = 'row';
      title.textContent = name; row.append(title);
      for (const kind of [1,2]) {
        const cell = document.createElement('td');
        const binding = snapshot.shortcuts.bindings.find(b => b[0] === action && b[1] === kind);
        if (action === 3 && kind === 2) cell.textContent = '—';
        else {
          const [, , mods = 0, keys = [], buttons = 0] = binding || [];
          const parts = ['Ctrl','Shift','Alt','Command / Windows'].filter((_,bit) => mods & (1 << bit));
          if (kind === 1) parts.push(...keys.map(keyName));
          else for (let bit=0;bit<8;++bit) if (buttons & (1 << bit)) parts.push(`Mouse ${bit+1}`);
          const value = parts.length ? parts.join(' + ') + (action===3 ? ' + 1 / 2 / 3' : '') : 'Not set';
          const active = editingKind === kind && editingAction === action && (token || recordingBusy);
          const label = document.createElement('span'); label.textContent = active ? 'Listening… release to save' : value;
          const controls = document.createElement('div'); controls.className = 'binding-actions';
          const edit = button('✎', () => recordShortcut(action,kind)); edit.className = 'icon-action'; edit.setAttribute('aria-label', `Edit ${name.toLowerCase()} ${kind===1?'keyboard':'mouse'}`); edit.title = edit.getAttribute('aria-label'); edit.hidden = !!active; edit.disabled = !!token || recordingBusy;
          const remove = button('×', () => active ? cancelShortcut() : command('shortcut-clear', { action,kind })); remove.className = 'icon-action'; remove.setAttribute('aria-label', `${active ? 'Cancel recording' : 'Remove '+name.toLowerCase()+' '+(kind===1?'keyboard':'mouse')}`); remove.title = remove.getAttribute('aria-label'); remove.hidden = !active && !parts.length; remove.disabled = recordingBusy || (!!token && !active);
          const icon = (path) => {
            const svg = document.createElementNS('http://www.w3.org/2000/svg','svg');
            svg.setAttribute('viewBox','0 0 24 24'); svg.setAttribute('aria-hidden','true');
            const stroke = document.createElementNS(svg.namespaceURI,'path'); stroke.setAttribute('d',path); svg.append(stroke); return svg;
          };
          edit.replaceChildren(icon('M16 3l5 5M4 16L17 3a2.1 2.1 0 0 1 3 3L7 19l-4 1z'));
          remove.replaceChildren(icon('M6 6l12 12M18 6L6 18'));
          controls.append(edit,remove);
          const content = document.createElement('div'); content.className = 'binding-cell';
          label.className = 'binding-label'; content.append(label,controls); cell.append(content);

        }
        row.append(cell);
      }
      $('bindings').append(row);
    });
  }
  $('calibrate').disabled = !snapshot.absolute;
  if (snapshot.ble_input) {
    const input = await client.request('input-status');
    inputSnapshot = input;
    $('add-ble').disabled = input.busy || !!input.ready || !!input.saved;
    $('add-ble').title = input.saved ? 'One BLE input is supported. Forget the saved input to replace it.' : '';
    $('no-inputs').hidden = !!snapshot.usb?.interfaces?.length || !!input.ready || !!input.saved;
    $('no-inputs').textContent = snapshot.usb ? 'No input devices connected. Plug in a USB device or add a BLE device.' : 'Update firmware to list connected USB devices.';
    $('ble-success').hidden = !input.ready;
    $('input-status').textContent = input.status.startsWith('Scan: ')
      ? (input.count ? `Found ${input.count} ${input.count === 1 ? 'device' : 'devices'}.` : 'No devices found. Keep your device in pairing mode and try again.')
      : input.status.split(' (security=')[0];
    $('input-status').hidden = !!input.ready || !!input.pairing?.active;
    $('input-diagnostics').textContent = input.status;
    $('input-heading').textContent = input.ready ? 'Device connected' : input.busy ? (input.state === 'scanning' ? 'Looking for devices…' : 'Connecting your keyboard…') : input.saved ? 'Your saved keyboard' : 'Add your keyboard';
    const device = input.ready ? input.device : input.saved_name;
    const destination = snapshot.slots[snapshot.selected];
    $('input-saved').hidden = !input.saved && !input.ready;
    $('input-add').hidden = !!input.saved || !!input.ready || input.busy;
    $('input-help').hidden = !!input.ready || input.busy;
    $('reconnect').hidden = !!input.ready;
    $('input-disconnect').hidden = !input.ready;
    $('input-cancel').hidden = !input.busy || !!input.pairing?.active;
    $('input-summary').textContent = `Bluetooth · ${device || 'No saved input'} · ${input.state || (input.busy ? 'Working' : 'Ready for setup')}${input.ready ? ` → ${destination.name} (slot ${snapshot.selected + 1}${destination.connected ? '' : ', offline'})` : ''}`;
    for (const id of ['scan','scan-all']) $(id).disabled = input.busy || !!input.ready;
    $('reconnect').disabled = input.busy || !!input.ready || input.saved === false;
    $('input-disconnect').disabled = input.busy || input.ready === false;
    $('forget-input').disabled = input.busy || input.saved === false;
    $('input-cancel').disabled = !input.busy;
    const pairing = input.pairing;
    $('input-pairing').hidden = !pairing?.active;
    $('input-confirm').hidden = pairing?.kind !== 'compare';
    $('pairing-history').hidden = !pairing || pairing.kind === 'none' || !!pairing.active;
    if (pairing && pairing.kind !== 'none') {
      const code = String(pairing.code).padStart(6, '0');
      $('pairing-code').textContent = code;
      $('pairing-instruction').textContent = pairing.kind === 'compare'
        ? 'Check that your input device shows this same code. Confirm on both devices only if they match.'
        : 'Type this code on the wireless keyboard, then press Enter.';
      $('pairing-time').textContent = `${pairing.seconds_left} seconds remaining`;
      $('pairing-history-text').textContent = `Code ${code} from the previous prompt is no longer awaiting confirmation. ${input.status}`;
    }
    $('input-results').hidden = input.busy || !!input.ready || !input.count;
    const signature = JSON.stringify([input.busy,input.ready,input.devices]);
    if (signature !== deviceSignature) {
      deviceSignature = signature; $('devices').replaceChildren();
      if (!input.busy && !input.ready) for (const line of input.devices.split('\n')) {
        const match = /^(\d+) (.+)$/.exec(line); if (!match) continue;
        $('devices').append(button(match[2], () => command('input', { command: `connect ${match[1]}` })));
      }
    }
  } else {
    $('add-ble').disabled = true;
    $('input-status').textContent = 'This firmware does not include BLE input. Install the BLE input build above.';
    for (const id of ['scan','scan-all','reconnect','input-disconnect','forget-input','input-cancel']) $(id).disabled = true;
  }
  updateProgressVisibility();
}
on('connect', async () => {
  $('connect').disabled = true;
  $('connection').textContent = 'Choose the board’s UART/programming port…';
  try {
    await client.disconnect();
    $('connect').disabled = true;
    token = ''; slotSignature = ''; deviceSignature = ''; $('slots').replaceChildren();
    $('device-name').value = ''; $('edge').value = ''; $('computer-passkey').textContent = '';
    await client.connect();
    $('connection').textContent = 'USB port opened. Waiting for the setup firmware…';
    notice('Connected to USB. Waiting for firmware…');
    // Opening some UART bridges resets the ESP32. Retry only a timed-out read.
    let failure;
    for (let attempt = 0; attempt < 3; ++attempt) {
      try { await refresh(); failure = null; break; }
      catch (error) { failure = error; if (!error.message.includes('Device did not reply')) break; }
    }
    if (failure) throw failure;
    connected = true; $('settings').disabled = false; $('disconnect').disabled = false; updateProgressVisibility();
    $('connect').disabled = true; $('installer').hidden = false; notice('');
  } catch (error) {
    await client.disconnect().catch(() => {});
    $('connect').disabled = false; $('disconnect').disabled = true;
    let message = error.message;
    if (message.includes('Device did not reply')) message = 'USB opened, but the firmware did not answer. If you have not installed the new firmware, use Install firmware above, then reconnect. Otherwise check that you selected the UART/programming port.';
    if (error.name === 'NotFoundError') message = 'No port selected. Click Connect board to try again.';
    $('connection').textContent = message;
    notice(message);
  }
});
async function disconnectSetup() {
  try {
    if (connected) {
      if (token) await client.request('shortcut-cancel', { token });
      await client.request('calibration-cancel'); await client.request('input-cancel');
    }
  } finally {
    await client.disconnect(); connected = false; $('settings').disabled = true; updateProgressVisibility(); $('connect').disabled = false; $('disconnect').disabled = true; $('installer').hidden = false;
  }
}
on('disconnect', async () => { await disconnectSetup(); notice('Disconnected. Your saved settings remain on the board.'); });
on('open-firmware', () => $('firmware-dialog').showModal());
on('close-firmware', () => $('firmware-dialog').close());
on('add-ble', () => { $('ble-dialog').showModal(); });
async function closeBle() {
  if (inputSnapshot?.busy) await command('input-cancel');
  $('ble-dialog').close(); $('add-ble').focus();
}
on('close-ble', closeBle);
$('ble-dialog').addEventListener('cancel', e => { e.preventDefault(); closeBle().catch(error => notice(error.message)); });
on('scan', () => command('input', { command: 'scan' }));
on('scan-all', () => command('input', { command: 'scan all' }));
on('reconnect', () => command('input', { command: 'reconnect' }));
on('input-disconnect', () => command('input', { command: 'disconnect' }));
on('input-cancel', () => command('input-cancel'));
on('input-confirm', async () => {
  const prompt = inputSnapshot?.pairing;
  if (!prompt?.active || prompt.kind !== 'compare') throw new Error('No pairing confirmation is pending.');
  $('input-confirm').disabled = true;
  try { await command('input-confirm', { attempt: prompt.id, code: prompt.code }); }
  finally { $('input-confirm').disabled = false; }
});
on('pairing-cancel', () => command('input-cancel'));
on('forget-input', () => confirm(`Forget ${inputSnapshot?.saved_name || 'the saved BLE input'}? Computer pairings are kept.`) && command('input', { command: 'forget' }));
on('rename', () => command('name', { name: $('device-name').value }), 'submit');
on('speed-form', async () => {await command('pointer-speed', {value:Number($('pointer-speed').value)});delete $('pointer-speed').dataset.dirty;}, 'submit');
$('pointer-speed').addEventListener('input',()=>{$('pointer-speed').dataset.dirty='1';$('speed-save').hidden=Number($('pointer-speed').value)===snapshot?.pointer_speed;});
on('edge-form', () => command('edge', { value: Number($('edge').value) }), 'submit');
on('seamless-toggle', () => command('seamless', { enabled: !snapshot.seamless }));
on('calibrate', () => command('calibration-start', { mask: snapshot.calibration_ready, enable: false }));
on('calibration-cancel', () => command('calibration-cancel'));
$('device-name').addEventListener('input', () => { $('rename-save').hidden = $('device-name').value === snapshot?.name; });
$('edge').addEventListener('input', () => { $('edge-save').hidden = Number($('edge').value) === snapshot?.edge_distance; });
async function recordShortcut(action,kind) {
  if (token || recordingBusy) return;
  editingAction = action; editingKind = kind; recordingBusy = true;
  try {
    const result = await client.request('shortcut-record', { action,kind }); token = result.token;
    $('recording').textContent = action === 3 ? 'Hold and release the modifier keys (for example Ctrl + Command). Slot numbers are added automatically.' : `Press and release the ${kind===1?'keys':'mouse buttons'} on the switcher. Escape cancels.`;
  } finally { recordingBusy = false; await refresh(); }
}
async function cancelShortcut() {
  if (recordingBusy) return;
  recordingBusy = true;
  try { if (token) await client.request('shortcut-cancel', { token }); token = ''; editingKind = 0; $('recording').textContent = 'Recording cancelled.'; }
  finally { recordingBusy = false; await refresh(); }
}
setInterval(async () => {
  if (!connected || !token || recordingBusy || recordingPoll) return;
  recordingPoll = true;
  const session = token;
  try {
    const state = await client.request('shortcut-status', { token: session });
    if (token !== session || recordingBusy) return;
    if (state.state === 'ready') {
      recordingBusy = true;
      try {
        await client.request('shortcut-save', { token: session });
        $('recording').textContent = `Saved: ${state.label}`;
      } catch (error) {
        await client.request('shortcut-cancel', { token: session });
        $('recording').textContent = `Not saved: ${error.message}. Edit the binding to try again.`;
      } finally { token = ''; editingKind = 0; recordingBusy = false; }
      await refresh();
    } else if (['expired','cancelled','idle'].includes(state.state)) {
      token = ''; editingKind = 0; $('recording').textContent = 'Recording ended. Binding unchanged.'; await refresh();
    }
  } catch (error) { notice(error.message); }
  finally { recordingPoll = false; }
}, 200);
document.addEventListener('keydown', e => { if (e.key === 'Escape' && token) { e.preventDefault(); cancelShortcut().catch(error=>notice(error.message)); } });
on('reset-shortcuts', () => { $('reset-shortcuts-confirm').hidden = false; });
on('cancel-reset-shortcuts', () => { $('reset-shortcuts-confirm').hidden = true; });
on('confirm-reset-shortcuts', async () => { await command('shortcuts-reset'); $('reset-shortcuts-confirm').hidden = true; });
setInterval(async () => {
  if (!connected || polling) return;
  polling = true;
  try { await refresh(); } catch (error) { notice(error.message); } finally { polling = false; }
}, 1500);
if (!('serial' in navigator) || !window.isSecureContext) {
  $('connect').disabled = true; notice('Use desktop Chrome or Edge over HTTPS (or localhost) for USB setup.');
}
async function installer() {
  try {
    const response = await fetch('firmware/versions.json', { cache: 'no-cache' });
    if (!response.ok) throw new Error('No firmware catalog available. Configuration is still available for boards flashed from source.');
    const catalog = await response.json();
    if (!Array.isArray(catalog.versions) || !catalog.versions.length) throw new Error('No firmware versions published yet.');
    firmwareCatalog = catalog.versions;
    const select = $('firmware-version'); select.replaceChildren();
    for (const entry of firmwareCatalog) {
      const url = new URL(entry.manifest, location.href);
      if (url.origin !== location.origin || !url.pathname.includes('/firmware/')) throw new Error('Invalid firmware manifest location');
      const option = document.createElement('option'); option.value = entry.manifest;
      option.textContent = `${entry.version} · ${entry.build_id.slice(0,8)}${entry.manifest === catalog.current ? ' (latest package)' : ''}`;
      select.append(option);
    }
    select.value = catalog.current; select.disabled = false;
    $('firmware-status').textContent = `${firmwareCatalog.length} available ${firmwareCatalog.length === 1 ? 'build' : 'builds'}. Choose a previous version here if you need to downgrade.`;
    await import('https://unpkg.com/esp-web-tools@10.1.0/dist/web/install-button.js?module');
    const render = () => {
      const install = document.createElement('esp-web-install-button'); install.setAttribute('manifest',select.value);
      const activate = document.createElement('button'); activate.slot = 'activate'; activate.textContent = 'Install selected version'; install.append(activate);
      // The stock installer requests a port on click. When setup already owns
      // one, intercept that click and transfer the same granted port instead.
      activate.addEventListener('click', async event => {
        if (installing) { event.preventDefault(); event.stopImmediatePropagation(); return; }
        if (!client.port) { $('firmware-dialog').close(); return; }
        event.preventDefault(); event.stopImmediatePropagation();
        if (!confirm('Disconnect USB setup and open the firmware installer? The board will restart during installation.')) return;
        $('firmware-dialog').close();
        const port = client.port, manifest = select.value;
        installing = true; activate.disabled = true; select.disabled = true;
        const restore = () => { installing = false; activate.disabled = false; select.disabled = false; $('connect').disabled = false; };
        let opened = false;
        try {
          await disconnectSetup();
          $('connect').disabled = true;
          notice('Opening the installer for the connected board…');
          await import('https://unpkg.com/esp-web-tools@10.1.0/dist/web/install-dialog-CQzvAv3p.js?module');
          await port.open({ baudRate: 115200 }); opened = true;
          const dialog = document.createElement('ewt-install-dialog');
          dialog.port = port; dialog.manifestPath = manifest;
          dialog.addEventListener('closed', async () => {
            try { await port.close(); } catch (error) { notice(error.message); }
            finally { restore(); notice('Installer closed. Connect board to continue setup.'); }
          }, { once: true });
          document.body.append(dialog);
          notice('USB setup disconnected. Continue in the firmware installer.');
        } catch (error) {
          if (opened) await port.close().catch(() => {});
          restore(); notice(`Could not open installer: ${error.message}`);
        }
      });
      $('installer').replaceChildren(install);
    };
    select.addEventListener('change',render); render();
  } catch (error) { $('firmware-status').textContent = error.message; }
}
installer();
