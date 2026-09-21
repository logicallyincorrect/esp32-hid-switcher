export class Lines {
  constructor(emit) { this.emit = emit; this.buffer = ''; this.discard = false; }
  push(text) {
    for (const c of text) {
      if (c === '\n') {
        if (!this.discard) this.emit(this.buffer.replace(/\r$/, ''));
        this.buffer = ''; this.discard = false;
      } else if (!this.discard) {
        this.buffer += c;
        if (this.buffer.length > 32768) { this.buffer = ''; this.discard = true; }
      }
    }
  }
}
export function response(line) {
  if (!line.startsWith('@HID1 ')) return null;
  try {
    const data = JSON.parse(line.slice(6));
    return Number.isInteger(data.id) && typeof data.ok === 'boolean' ? data : null;
  } catch { return null; }
}
export class SerialClient {
  constructor(onClose = () => {}, onLine = () => {}) { this.id = crypto.getRandomValues(new Uint32Array(1))[0] & 0x7fffffff; this.tail = Promise.resolve(); this.onClose = onClose; this.onLine = onLine; }
  async connect() {
    const port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    this.port = port;
    try {
      await port.setSignals({ dataTerminalReady: false, requestToSend: false });
      this.reader = port.readable.getReader(); this.writer = port.writable.getWriter();
      this.reading = this.read();
    } catch (error) { await this.disconnect(); throw error; }
  }
  async read() {
    const decoder = new TextDecoder();
    const lines = new Lines(line => {
      this.onLine(line);
      const data = response(line);
      if (data && this.pending?.id === data.id) {
        const p = this.pending; this.pending = null; clearTimeout(p.timer);
        if (data.ok) p.resolve(data.result); else p.reject(new Error(data.error || 'Device rejected request'));
      }
    });
    let readError = new Error('USB connection closed');
    try {
      while (true) {
        const { value, done } = await this.reader.read(); if (done) break;
        lines.push(decoder.decode(value, { stream: true }));
      }
    } catch (error) { readError = new Error(`USB read failed: ${error.message}`); }
    finally {
      this.reader.releaseLock(); this.reader = null;
      this.fail(readError);
      this.onClose();
    }
  }
  fail(error) { if (this.pending) { clearTimeout(this.pending.timer); this.pending.reject(error); this.pending = null; } }
  request(op, args = {}) {
    const work = this.tail.then(async () => {
      if (!this.writer || !this.reader) throw new Error('Connect the board first');
      const id = ++this.id;
      const packet = new TextEncoder().encode('@HID1 ' + JSON.stringify({ ...args, op, id }) + '\n');
      if (packet.length > 2049) throw new Error('Request is too large');
      return new Promise((resolve, reject) => {
        this.pending = { id, resolve, reject, timer: setTimeout(() => {
          this.pending = null; reject(new Error('Device did not reply. Check firmware/USB port. Refresh status before retrying a change.'));
        }, 5000) };
        this.writer.write(packet).catch(error => { if (this.pending?.id === id) this.fail(error); });
      });
    });
    this.tail = work.catch(() => {}); return work;
  }
  async disconnect() {
    this.fail(new Error('Disconnected'));
    if (this.reader) { await this.reader.cancel(); await this.reading; }
    if (this.writer) { this.writer.releaseLock(); this.writer = null; }
    if (this.port) { await this.port.close(); this.port = null; }
  }
}
