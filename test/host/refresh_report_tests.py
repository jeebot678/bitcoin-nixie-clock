"""The physical verifier must reject stale/no-motion data despite perfect timers."""
import importlib.util
import sys
import types
sys.modules.setdefault('serial',types.SimpleNamespace())
spec=importlib.util.spec_from_file_location('verify','tools/verify_device_refresh.py')
verify=importlib.util.module_from_spec(spec);spec.loader.exec_module(verify)
header='Firmware=1.0.9 Wi-Fi=connected clock=synced\nstream=connected quotes=1 accepted=1\n'
tail='stream=connected quotes=10 accepted=10\n'
frames=''.join(f'Nixie frame at {1000+i*500} ms: 80000.00 interval=500 ms gap=500 ms quoteAge=100 ms source={"Kraken" if i%2 else "Coinbase"} digits={80000+i%2} cosmetic={"yes" if i%2 else "no"}\n' for i in range(30))
def summary(data):return verify.summarize(data,15,'1.0.9',500,20,10,5000,2,0.9)
report=summary(header+frames+tail)
assert report['pass'] and report['whole_dollar_motion_ratio']==1 and report['cosmetic_motion_frames']==15
assert report['maximum_display_rounding_error_usd']==1
assert not summary(header+frames.replace('quoteAge=100','quoteAge=5001')+tail)['pass']
assert not summary(header+frames.replace('digits=80001','digits=80000')+tail)['pass']
assert not summary(header+frames.replace('gap=500','gap=511')+tail)['pass']
assert not summary(header+frames.replace('digits=80001','digits=80002')+tail)['pass']
assert not summary(header+frames+tail+'task_wdt: triggered')['pass']
assert not summary(header+frames.replace('source=Coinbase','source=Kraken')+tail)['pass']
print('PASS: physical report checks actual digits, cosmetic error bounds, source diversity, freshness, deadlines and device faults')
