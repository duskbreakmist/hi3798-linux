"""One CPU comparison, one GPU frequency transition, one GLES render check."""
from pathlib import Path
import subprocess, json, re, sys
root=Path('/home/ray/hi3798-lima')
df=Path('/sys/class/devfreq/f9200000.gpu')
cpu=Path('/sys/devices/system/cpu/cpufreq/policy0')
available=(cpu/'scaling_available_frequencies').read_text()
gpu_only='--gpu-only' in sys.argv
if not gpu_only and ('1200000' not in available or '1500000' in available):
    raise SystemExit('Unexpected normal CPU frequency table: '+available)
print('CPU available:',available.strip(),flush=True)
if not gpu_only and '--skip-cpu' not in sys.argv:
    subprocess.run(['/run/daily-cpu-check'],check=True)
if not df.is_dir():
    raise SystemExit('GPU devfreq node missing')
print('GPU available:',(df/'available_frequencies').read_text().strip(),flush=True)
(df/'governor').write_text('userspace\n')
(df/'userspace/set_freq').write_text('200000000\n')
print('GPU low:',(df/'cur_freq').read_text().strip(),flush=True)
(df/'userspace/set_freq').write_text('400000000\n')
print('GPU high:',(df/'cur_freq').read_text().strip(),flush=True)
if int((df/'cur_freq').read_text())!=400000000:
    raise SystemExit('GPU did not report 400 MHz')
command=['runuser','-u','ray','--','/usr/local/bin/mv100-gles2-test',str(root/'daily-400mhz.ppm')]
subprocess.run(command,check=True)
subprocess.run(['insmod','/run/mv100-gpu-soc-probe.ko'],check=True)
try:
    log=subprocess.check_output(['dmesg'],text=True)
    line=[x for x in log.splitlines() if 'mv100_gpu_soc_probe: CRG48=' in x][-1]
    print(line,flush=True)
    status=int(re.search(r'CRG154=([0-9a-f]+)',line)[1],16)
    if (status>>5)&7 != 1:
        raise SystemExit('Hardware GPU selector is not the SDK 400 MHz value')
    if not gpu_only and (status>>1)&7 != 5:
        raise SystemExit('Hardware CPU selector is not the SDK 1200 MHz value')
finally:
    subprocess.run(['rmmod','mv100_gpu_soc_probe'],check=True)
(df/'governor').write_text('simple_ondemand\n')
print('CPU performance cap:',(cpu/'scaling_max_freq').read_text().strip(),flush=True)
print('GPU governor:',(df/'governor').read_text().strip(),flush=True)
errors=[x for x in subprocess.check_output(['dmesg'],text=True).splitlines()
        if re.search(r'job timeout|task error|GPU DFS.*did not latch|Oops|Kernel panic',x)]
if errors:
    raise SystemExit('\n'.join(errors))
print('DAILY CLOCK FUNCTION CHECK PASSED',flush=True)
