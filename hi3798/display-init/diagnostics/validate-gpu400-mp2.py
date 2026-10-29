from pathlib import Path
import json, subprocess, re, time
root=Path('/home/ray/hi3798-lima')
root.mkdir(exist_ok=True)
info=json.loads(subprocess.check_output(['runuser','-u','ray','--','python3','/run/lima-getparam.py'],text=True))
print('DRM:',json.dumps(info),flush=True)
if info['gpu_id']!=2 or info['num_pp']!=2:
    raise SystemExit('Not a two-PP Mali450')
subprocess.run(['python3','/run/validate-daily-clocks.py','--gpu-only'],check=True)
df=Path('/sys/class/devfreq/f9200000.gpu')
(df/'governor').write_text('userspace\n')
(df/'userspace/set_freq').write_text('400000000\n')
subprocess.run(['runuser','-u','ray','--','/run/lima-render-large',str(root/'mp2-1024.ppm')],check=True)
deadline=time.monotonic()+110
while True:
    log=Path('/var/log/Xorg.0.log').read_text(errors='replace')
    if 'glamor X acceleration enabled on Mali450' in log and 'GLX: Initialized DRI2 GL provider' in log:
        break
    if time.monotonic()>deadline:
        raise SystemExit('Xorg glamor did not finish')
    time.sleep(0.5)
subprocess.run(['python3','/run/capture-es2tri.py'],check=True,timeout=90)
subprocess.run(['python3','/run/verify-direct-kms-window.py'],check=True)
stats={name:(Path('/sys/module/lima/parameters')/name).read_text().strip()
       for name in ['mv100_pp_poll_jobs','mv100_pp_seen_active']}
print('MP2 completion stats:',json.dumps(stats),flush=True)
print('GPU test frequency:',(df/'cur_freq').read_text().strip(),flush=True)
errors=[x for x in subprocess.check_output(['dmesg'],text=True).splitlines()
        if re.search(r'job timeout|task error|GPU DFS.*did not latch|Oops|Kernel panic|BUG:',x)]
if errors:
    raise SystemExit('\n'.join(errors))
(df/'governor').write_text('simple_ondemand\n')
(root/'mp2-accepted.json').write_text(json.dumps({'drm':info,'stats':stats,'gpu_cap_hz':400000000},indent=2))
print('GPU400 MP2 FUNCTION CHECK PASSED',flush=True)
