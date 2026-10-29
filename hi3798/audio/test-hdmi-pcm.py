"""One bounded, quiet HDMI tone; record real DMA progress, not just ALSA nodes."""
import array
import json
import math
import mmap
from pathlib import Path
import struct
import subprocess
import time
import wave

path=Path('/run/mv100-hdmi-tone.wav')
samples=array.array('h')
for i in range(48000*3):
    # Fade endpoints and keep peak below -26 dBFS.
    fade=min(1.,i/2400,(48000*3-1-i)/2400)
    value=int(1400*fade*math.sin(2*math.pi*440*i/48000))
    samples.extend([value,value])
with wave.open(str(path),'wb') as f:
    f.setnchannels(2);f.setsampwidth(2);f.setframerate(48000);f.writeframes(samples.tobytes())
rows=[]
proc=subprocess.Popen(['aplay','-v','-D','hw:MV100HDMI,0',str(path)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
end=time.monotonic()+8
while proc.poll() is None and time.monotonic()<end:
    status=Path('/proc/asound/card0/pcm0p/sub0/status').read_text()
    dma=Path('/sys/bus/platform/devices/f8cd0000.audio/dma_status')
    rows.append({'time':round(time.monotonic(),3),'status':status,
                 'dma':dma.read_text() if dma.exists() else 'unavailable'})
    time.sleep(.15)
if proc.poll() is None:
    proc.terminate()
output,_=proc.communicate(timeout=4)
result={'exit_code':proc.returncode,'aplay':output,'samples':rows,
        'distinct_status':len({row['status'] for row in rows}),
        'physical_audio_confirmed':False}
Path('/run/mv100-hdmi-tone-result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
