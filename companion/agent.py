#!/usr/bin/python3
"""Install or control this user's HID Switcher LaunchAgent."""
import argparse
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import uuid

LABEL = 'io.github.logicallyincorrect.hid-switcher-companion'
HOME_DIR = Path.home()
APP = HOME_DIR / 'Applications/HID Switcher Companion.app'
PLIST = HOME_DIR / 'Library/LaunchAgents' / (LABEL + '.plist')
DOMAIN = 'gui/' + str(os.getuid())
parser = argparse.ArgumentParser()
parser.add_argument('command', choices=['install', 'start', 'stop', 'status', 'uninstall'])
parser.add_argument('--device', type=uuid.UUID)
args = parser.parse_args()

def launch(*arguments, check=True):
    return subprocess.run(['launchctl', *arguments], check=check)

if args.command == 'install':
    if not args.device:
        parser.error('install requires --device UUID from the companion list command')
    source = Path(__file__).resolve().parent / 'build/HID Switcher Companion.app'
    if not source.is_dir():
        parser.error('run sh companion/build.sh first')
    if APP.exists():
        with (APP / 'Contents/Info.plist').open('rb') as f:
            if plistlib.load(f).get('CFBundleIdentifier') != LABEL:
                parser.error('the install path contains a different application')
    launch('bootout', DOMAIN + '/' + LABEL, check=False)
    if APP.exists():
        shutil.rmtree(APP)
    APP.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, APP)
    logs = HOME_DIR / 'Library/Logs/HID Switcher'
    logs.mkdir(parents=True, exist_ok=True)
    PLIST.parent.mkdir(parents=True, exist_ok=True)
    config = {
        'Label': LABEL,
        'ProgramArguments': [str(APP / 'Contents/MacOS/hid-switcher-companion'), 'run', '--device', str(args.device)],
        'RunAtLoad': True,
        'KeepAlive': True,
        'ThrottleInterval': 10,
        'LimitLoadToSessionType': 'Aqua',
        'StandardOutPath': str(logs / 'companion.log'),
        'StandardErrorPath': str(logs / 'companion.log'),
    }
    temporary = PLIST.with_suffix('.tmp')
    with temporary.open('wb') as f:
        plistlib.dump(config, f)
    temporary.replace(PLIST)
    launch('bootstrap', DOMAIN, str(PLIST))
    print('Installed. Allow Bluetooth and Accessibility for HID Switcher Companion.')
elif args.command == 'start':
    if not PLIST.exists():
        parser.error('install the agent first')
    launch('bootstrap', DOMAIN, str(PLIST), check=False)
    launch('kickstart', DOMAIN + '/' + LABEL)
elif args.command == 'stop':
    launch('bootout', DOMAIN + '/' + LABEL)
elif args.command == 'status':
    launch('print', DOMAIN + '/' + LABEL)
elif args.command == 'uninstall':
    launch('bootout', DOMAIN + '/' + LABEL, check=False)
    PLIST.unlink(missing_ok=True)
    print('LaunchAgent removed. The application is retained in ~/Applications.')
