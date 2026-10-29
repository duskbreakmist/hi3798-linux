"""Read the Linux DRM Lima ABI, without submitting any GPU jobs."""
import fcntl, os, struct, json
fd=os.open('/dev/dri/renderD128',os.O_RDWR)
values={}
try:
    for param,name in [(0,'gpu_id'),(1,'num_pp'),(2,'gp_version'),(3,'pp_version')]:
        buffer=bytearray(struct.pack('=IIQ',param,0,0))
        fcntl.ioctl(fd,(3<<30)|(16<<16)|(ord('d')<<8)|0x40,buffer,True)
        values[name]=struct.unpack('=IIQ',buffer)[2]
finally:
    os.close(fd)
print(json.dumps(values),flush=True)
