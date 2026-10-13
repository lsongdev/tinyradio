"""Real-device seek/interaction checks via the same short/long-press handlers.
Start immediately after reboot. Does not verify physical button timing or RF audio.
"""
import re
import sys
import time
import serial

with serial.Serial(sys.argv[1], 115200, timeout=0.02) as port:
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

    action('e1a', focus=1, band='MW', mode='AM', confirmed=999000, seek=0)
    action('h', focus=1, seek=1, searching=0)
    action('+', settled=False, searching=1)
    stopped = action('e', focus=1, seek=0, searching=0, result=4)
    stopped_hz = int(stopped['confirmed'])
    action('+', focus=1, seek=0, confirmed=stopped_hz + 9000)
    action('h+', settled=False, seek=1, searching=1)
    action('-', settled=False, seek=1, searching=1)
    action('e', focus=1, seek=0, searching=0, result=4)
    action('0]', band='LW', confirmed=279000)
    action('h', seek=1, searching=0)
    action('+', seek=1, searching=0, result=3, confirmed=279000)  # halt at upper limit
    action('3]', band='VHF', mode='FM', confirmed=108000000)
    action('h', seek=1, searching=0)
    action('+', seek=1, searching=0, result=3, confirmed=108000000)
    action('2u', mode='USB', seek=0, confirmed=7100000, bw=2200)
    action('h', focus=1, seek=0, searching=0)  # SSB refuses seek entry
    action('eee', focus=4)
    action('h', focus=4, mute=1, seek=0)
    action('h', focus=4, mute=0, seek=0)
    action('eeeeeh', focus=0, mute=0, seek=0)  # only volume long-press mutes
    action('e3f', focus=1, band='VHF', mode='FM', confirmed=108000000, bw=0)
    action('-' * 80, focus=1, confirmed=100000000, seek=0, mute=0)
    print('Seek smoke passed; VHF/FM 100MHz, frequency focus, manual mode', flush=True)
