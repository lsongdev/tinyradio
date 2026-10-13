"""Real-device NVS save/reboot/restore checks. Changes tuning and user settings.
Reboots the ESP32 twice and finishes at FM 100MHz, volume 18, brightness 128.
"""
import re
import sys
import time
import serial
import subprocess
from pathlib import Path

port = serial.Serial(sys.argv[1], 115200, timeout=0.02)
try:
    def action(command, settled=True, **expected):
        port.reset_input_buffer()
        port.write(command.encode())
        deadline = time.monotonic() + 12
        pending_line = b''
        next_query = time.monotonic()
        while time.monotonic() < deadline:
            if time.monotonic() >= next_query:
                port.write(b'?')
                next_query = time.monotonic() + 0.05
            pending_line += port.readline()
            if not pending_line.endswith(b'\n'):
                continue
            line = pending_line.decode(errors='replace').strip()
            pending_line = b''
            if not line.startswith('RADIO '):
                if line:
                    print(line, flush=True)
                continue
            state = dict(re.findall(r'(\w+)=(\S+)', line))
            if state.get('error') != 'OK':
                raise AssertionError(line)
            if not all(state.get(key) == str(value) for key, value in expected.items()):
                continue
            if settled and (state['tuning'] != '0' or state['requested'] != state['confirmed']):
                continue
            print(command, line, flush=True)
            return state
        raise AssertionError(f'Timeout: {command!r}, expected {expected}')

    def focus(index):
        state = action('')
        action('e' * ((index - int(state['focus'])) % 9), focus=index)

    def step(hz):
        focus(6)
        for _ in range(5):
            if int(action('')['step']) == hz:
                return
            action('>')
        raise AssertionError('Step unavailable')

    def reboot():
        global port
        port.close()
        package = Path.home() / '.platformio/packages/tool-esptoolpy/esptool.py'
        subprocess.run([sys.executable, str(package), '--chip', 'esp32s3', '--port', sys.argv[1], 'run'], check=True)
        time.sleep(1)
        port = serial.Serial(sys.argv[1], 115200, timeout=0.02)

    # Per-band state includes exact BFO-derived SSB frequencies and step values.
    action('0a'); step(1000); action('[' + '+' * 7, band='LW', confirmed=160000)
    action('1l'); step(100); action('[' + '+' * 2, band='MW', mode='LSB', confirmed=520200)
    action('3f'); step(50000); action('[' + '+' * 15, band='VHF', confirmed=64750000)
    focus(3); state = action('')
    action('>' * ((3 - int(state['bwsel'])) % 5), bw=84000)
    focus(4); action('<' * 40); action('>' * 8, gainset=7)
    focus(5); action('<' * 40); action('>>>>', smset=4)
    action('2u'); step(500); action('[' + '+' * 3, band='SW', mode='USB', confirmed=2301500)
    focus(3); state = action('')
    action('>' * ((4 - int(state['bwsel'])) % 6), bw=3000)
    focus(4); action('<' * 40); action('>' * 11, gainset=10)
    focus(5); action('<' * 40); action('>' * 11, smset=11)
    focus(7); action('<' * 70); action('>' * 23, volume=23)
    focus(8); action('<' * 40); action('>' * 24, brightness=200)
    if action('')['mute'] == '0': action('m', mute=1)
    action('', stored=1, save=0)
    reboot()
    action('', band='SW', mode='USB', confirmed=2301500, step=500, volume=23,
           brightness=200, mute=1, bw=3000, gainset=10, smset=11, stored=1, save=0, focus=0, seek=0, searching=0)
    action('0', band='LW', mode='AM', confirmed=160000, step=1000)
    action('1', band='MW', mode='LSB', confirmed=520200, step=100)
    action('3', band='VHF', mode='FM', confirmed=64750000, step=50000, bw=84000, gainset=7, smset=4)
    action('2', band='SW', mode='USB', confirmed=2301500, step=500, bw=3000, gainset=10, smset=11)
    action('', stored=1, save=0)
    reboot()
    action('', band='SW', mode='USB', confirmed=2301500, volume=23, brightness=200, mute=1, stored=1, writes=0)

    # Continuous edits must not write NVS until they stop for two seconds.
    focus(7)
    writes = int(action('')['writes'])
    for i in range(10):
        action('>', volume=24 + i, writes=writes)
        time.sleep(0.3)
    action('', volume=33, stored=1, save=0, writes=writes + 1)

    # Return the active receiver and audio/backlight to convenient defaults.
    action('3f'); step(200000); action('[' + '+' * 180, confirmed=100000000)
    step(100000)
    focus(3); state = action('')
    action('>' * ((0 - int(state['bwsel'])) % 5), bw=0)
    focus(4); action('<' * 40, gainset=-1)
    focus(5); action('<' * 40); action('>' * 16, smset=16)
    focus(7); action('<' * 70); action('>' * 18, volume=18)
    focus(8); action('<' * 40); action('>' * 15, brightness=128)
    action('m', mute=0)
    focus(1)
    action('', stored=1, save=0, confirmed=100000000, volume=18, brightness=128, mute=0)
    print('Persistence smoke passed; FM100MHz, volume18, brightness128, unmuted', flush=True)
finally:
    port.close()
