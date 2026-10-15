"""Real-device bandwidth/AGC/soft-mute checks via the same short/long-press handlers.
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

    action('ee', focus=3, band='VHF', bw=0)
    action('>', focus=3, bw=40000, bwsel=1)
    action('>>>>', bw=0, bwsel=0)
    action('e>', focus=4, gainset=0, agc=0, gain=0)
    action('>' * 40, gainset=26, agc=0, gain=26)
    action('<' * 40, gainset=-1, agc=1)
    action('e>', focus=5, smset=17)
    action('>' * 40, smset=31)
    action('<' * 40, smset=0)
    action('>' * 16, smset=16)
    action('eeeeee1a', focus=1, band='MW', mode='AM', bw=3000, gainset=-1, smset=0)
    action('ee>', focus=3, bw=4000)
    action('<', bw=3000)
    action('e>' + '>' * 40, focus=4, gainset=37, agc=0, gain=37)
    action('<' * 40, gainset=-1, agc=1)
    action('e>', focus=5, smset=1)
    action('<', smset=0)
    action('2u', mode='USB', bw=2200, gainset=-1, smset=0)
    action('eeeeeeee>', focus=3, bw=3000)
    action('<', bw=2200)
    action('e>', focus=4, gainset=0, agc=0, gain=0)
    action('<', gainset=-1, agc=1)
    action('e>', focus=5, smset=1)
    action('<', smset=0)
    action('3f', mode='FM', bw=0, gainset=-1, smset=16)
    print('Reception settings smoke passed', flush=True)
