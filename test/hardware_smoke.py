"""USB smoke test; changes receiver state and finishes on VHF/FM 65.6 MHz.
Run with a Python containing pyserial: python test/hardware_smoke.py PORT
Checks command/response state, not audible reception or antenna performance.
"""
import re
import sys
import time
import serial

with serial.Serial(sys.argv[1], 115200, timeout=0.1) as port:
    def check(command, **expected):
        port.reset_input_buffer()
        port.write(command.encode())
        deadline = time.monotonic() + 6
        next_query = time.monotonic() + 0.25
        while time.monotonic() < deadline:
            if time.monotonic() >= next_query:
                port.write(b'?')
                next_query = time.monotonic() + 0.25
            line = port.readline().decode(errors='replace').strip()
            if line:
                print(line, flush=True)
            if not line.startswith('RADIO '):
                continue
            values = dict(re.findall(r'(\w+)=(\S+)', line))
            if values.get('error') != 'OK':
                raise AssertionError(line)
            if all(values.get(key) == str(value) for key, value in expected.items()):
                assert values['requested'] == values['confirmed'], line
                assert values['tuning'] == '0', line
                return
        raise AssertionError(f'Timeout after {command!r}: {expected}')

    check('r', band='VHF', mode='FM', confirmed=100000000)
    check('1a', band='MW', mode='AM', confirmed=999000)
    check('0a', band='LW', mode='AM', confirmed=198000)
    check('2a', band='SW', mode='AM', confirmed=7100000)
    check('u', band='SW', mode='USB', confirmed=7100000)
    check('++++++++++++', band='SW', mode='USB', confirmed=7101200)
    check('+' * 90, band='SW', mode='USB', confirmed=7110200)  # re-anchor beyond BFO window
    check('-' * 90, band='SW', mode='USB', confirmed=7101200)  # positive BFO on the way back
    check('l', band='SW', mode='LSB', confirmed=7101200)
    check('a', band='SW', mode='AM', confirmed=7101000)
    check('u', band='SW', mode='USB', confirmed=7101000)  # reload patch
    check('3f', band='VHF', mode='FM', confirmed=100000000)
    check('+' * 80, band='VHF', mode='FM', confirmed=108000000)
    check('+' * 32, band='VHF', mode='FM', confirmed=108000000)
    check('-' * 80, band='VHF', mode='FM', confirmed=100000000)
    check('0[', band='LW', mode='AM', confirmed=153000)
    check('-' * 32, band='LW', mode='AM', confirmed=153000)
    check('0]', band='LW', mode='AM', confirmed=279000)
    check('+', band='MW', mode='AM', confirmed=520000)
    check('-', band='LW', mode='AM', confirmed=279000)
    check('1]', band='MW', mode='AM', confirmed=1710000)
    check('+', band='SW', mode='USB', confirmed=2300000)
    check(']', band='SW', mode='USB', confirmed=26100000)
    check('+', band='VHF', mode='FM', confirmed=64000000)
    check('-', band='SW', mode='USB', confirmed=26100000)
    check('3[', band='VHF', mode='FM', confirmed=64000000)
    check('+' * 16, band='VHF', mode='FM', confirmed=65600000)
    check('3f', band='VHF', mode='FM', confirmed=65600000)
    check('u', band='VHF', mode='FM', confirmed=65600000)  # reject illegal mode
    check('m', band='VHF', mode='FM', mute=1, confirmed=65600000)
    check('m', band='VHF', mode='FM', mute=0, confirmed=65600000)
    print('Hardware smoke passed; receiver left on VHF/FM 65.6 MHz', flush=True)
