#!/usr/bin/env python3
"""USB configuration client. Slot indices are zero based. No automatic writes/retries."""
import argparse
import json
import sys
import time


def request(port, sequence, command):
    payload = dict(command, id=sequence)
    wire = ('@HID1 ' + json.dumps(payload, separators=(',', ':')) + '\n').encode()
    if len(wire) > 2049:
        raise ValueError('Request exceeds 2048 bytes')
    port.write(wire)
    deadline = time.monotonic() + 6
    buffered = b''
    discarding = False
    while time.monotonic() < deadline:
        chunk = port.read_until(b'\n', 32768)
        if len(buffered) + len(chunk) > 32768:
            buffered = b''; discarding = True
        if not discarding:
            buffered += chunk
        if not chunk.endswith(b'\n'):
            continue
        if discarding:
            discarding = False; continue
        line = buffered.decode('utf-8', errors='replace').strip()
        buffered = b''
        if line.startswith('[BLE computer pairing]'):
            print(line, file=sys.stderr)
        if not line.startswith('@HID1 '):
            continue
        try:
            response = json.loads(line[6:])
        except json.JSONDecodeError:
            continue
        if response.get('id') == sequence:
            return response
    raise TimeoutError('No reply. Check UART port/firmware. Query status before retrying a change.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--watch', action='store_true', help='Poll status or input-status once a second')
    parser.add_argument('op', nargs='?', default='status', help='Operation; shell keeps one port open for interactive JSON commands')
    parser.add_argument('args', nargs='?', default='{}', help='JSON object of arguments')
    args = parser.parse_args()
    if args.watch and args.op not in ('status', 'input-status', 'shortcut-status'):
        parser.error('--watch is only for read operations')
    import serial
    import secrets
    port = serial.Serial(port=None, baudrate=115200, timeout=0.5, write_timeout=2)
    port.dtr = False; port.rts = False; port.port = args.port
    with port:
        time.sleep(2)  # UART bridges can reset the board when opened.
        sequence = secrets.randbelow(0x7fffffff)
        while True:
            if args.op == 'shell':
                try:
                    raw = input('hid> ')
                except EOFError:
                    break
                if raw in ('exit', 'quit'):
                    break
                try:
                    command = json.loads(raw)
                except json.JSONDecodeError as error:
                    print(error, file=sys.stderr); continue
            else:
                command = json.loads(args.args)
                if not isinstance(command, dict):
                    raise ValueError('Arguments must be a JSON object')
                command['op'] = args.op
            if not isinstance(command, dict) or not isinstance(command.get('op'), str):
                raise ValueError('Command must be an object with an op string')
            sequence += 1
            result = request(port, sequence, command)
            print(json.dumps(result, indent=2))
            if args.watch:
                time.sleep(1)
            elif args.op != 'shell':
                return 0 if result.get('ok') else 1
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
