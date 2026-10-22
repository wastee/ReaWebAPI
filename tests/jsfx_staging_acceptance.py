"""Exercise native/EEL packet boundaries in an opt-in disposable REAPER profile."""
from pathlib import Path
import json
import sys
import time
from jsfx_probe_client import call

root = Path(sys.argv[1])
call(root, 'setup', count=2)
lengths = [0, 1, 63, 64, 65, 127, 255, 256, 257, 511, 512, 513,
           4095, 4096, 4097, 8191, 8192, 1, 0]
results = []
for request, size in enumerate(lengths, 1):
    index = request % 2
    values = [(-1 if i % 2 else 1) * (request * 10000 + i + .25) for i in range(size)]
    assert call(root, 'send', index=index, request=request, capability=5, values=values)
    deadline = time.monotonic() + 5
    while True:
        reply = call(root, 'reply', index=index)
        if reply['ready']:
            break
        if time.monotonic() > deadline:
            raise TimeoutError((request, size))
        time.sleep(.01)
    assert reply['request'] == request and reply['code'] == 0, reply
    assert reply['values'] == values, (request, size)
    results.append({'length': size, 'instance': index, 'ok': True})
try:
    call(root, 'send', request=100, capability=5, values=[0] * 8193)
except RuntimeError as error:
    assert 'Invalid JSFX command payload' in str(error), str(error)
else:
    raise AssertionError('Oversized packet accepted')
time.sleep(.5)
state = call(root, 'status')
assert all(entry['frames'] > 0 and entry['errors'] == 0 for entry in state), state
result = {'packets': results, 'oversizedRejected': True, 'streams': state}
(root.parent / 'staging-acceptance.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print('PASS: 19 exact packet round trips, two instances, oversized rejection and live stream validation')
