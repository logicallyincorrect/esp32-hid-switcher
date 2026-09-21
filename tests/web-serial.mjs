import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Lines, response, SerialClient } from '../web/serial.mjs';
test('fragmented serial data ignores logs and malformed messages', () => {
  const found = []; const lines = new Lines(line => { const result = response(line); if (result) found.push(result); });
  lines.push('[boot] ready\n@HI'); lines.push('D1 {"id":7,"ok":true,"result":{"name":"Combo"}}\r\n');
  lines.push('@HID1 invalid\n@HID1 {"id":null,"ok":false}\n');
  assert.deepEqual(found, [{ id:7, ok:true, result:{name:'Combo'} }]);
});
test('oversized log line is discarded and parser recovers', () => {
  const lines = []; const parser = new Lines(line => lines.push(line));
  parser.push('x'.repeat(40000)); parser.push('\nokay\n'); assert.deepEqual(lines,['okay']);
});
test('requests are serialized and device errors do not retry writes', async () => {
  const client = new SerialClient(); client.reader = {};
  const writes = [];
  client.writer = { async write(bytes) {
    const req = JSON.parse(new TextDecoder().decode(bytes).slice(6)); writes.push(req);
    queueMicrotask(() => { const p = client.pending; clearTimeout(p.timer); client.pending = null;
      if(req.op==='bad') p.reject(new Error('Rejected')); else p.resolve({selected:1}); });
  }};
  const first = client.request('bad'); const second = client.request('status');
  await assert.rejects(first,/Rejected/); assert.deepEqual(await second,{selected:1});
  assert.deepEqual(writes.map(r=>r.op),['bad','status']); assert.notEqual(writes[0].id,writes[1].id);
});
test('disconnection rejects in-flight requests', async () => {
  const client = new SerialClient();client.reader={};client.writer={write:async()=>{}};
  const pending=client.request('status'); await Promise.resolve();client.fail(new Error('Unplugged'));
  await assert.rejects(pending,/Unplugged/);
});
