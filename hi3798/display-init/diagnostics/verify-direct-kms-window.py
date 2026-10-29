"""Check a small X11 update against the physical scanout's corner pixel."""
import ctypes as c, os, re, time, json
from pathlib import Path
os.environ['DISPLAY']=':0'
os.environ['XAUTHORITY']='/var/run/lightdm/root/:0'
x=c.CDLL('libX11.so.6')
x.XOpenDisplay.argtypes=[c.c_char_p]; x.XOpenDisplay.restype=c.c_void_p
x.XDefaultRootWindow.argtypes=[c.c_void_p]; x.XDefaultRootWindow.restype=c.c_ulong
x.XCreateSimpleWindow.argtypes=[c.c_void_p,c.c_ulong,c.c_int,c.c_int,c.c_uint,c.c_uint,c.c_uint,c.c_ulong,c.c_ulong]
x.XCreateSimpleWindow.restype=c.c_ulong
class Attributes(c.Structure):
    _fields_=[('background_pixmap',c.c_ulong),('background_pixel',c.c_ulong),
        ('border_pixmap',c.c_ulong),('border_pixel',c.c_ulong),('bit_gravity',c.c_int),
        ('win_gravity',c.c_int),('backing_store',c.c_int),('backing_planes',c.c_ulong),
        ('backing_pixel',c.c_ulong),('save_under',c.c_int),('event_mask',c.c_long),
        ('do_not_propagate_mask',c.c_long),('override_redirect',c.c_int),
        ('colormap',c.c_ulong),('cursor',c.c_ulong)]
x.XChangeWindowAttributes.argtypes=[c.c_void_p,c.c_ulong,c.c_ulong,c.POINTER(Attributes)]
x.XMapRaised.argtypes=[c.c_void_p,c.c_ulong]
x.XDestroyWindow.argtypes=[c.c_void_p,c.c_ulong]
x.XSync.argtypes=[c.c_void_p,c.c_int]
x.XCloseDisplay.argtypes=[c.c_void_p]
d=x.XOpenDisplay(None)
if not d: raise SystemExit('Cannot open X display')
def pixel():
    status=Path('/sys/module/mv100_display_bridge/parameters/status').read_text()
    return int(re.search(r'first_pixel=([0-9a-fA-F]+)',status)[1],16)
before=pixel()
w=x.XCreateSimpleWindow(d,x.XDefaultRootWindow(d),0,0,96,96,0,0,0xff0000)
a=Attributes(); a.override_redirect=1
x.XChangeWindowAttributes(d,w,1<<9,c.byref(a))
x.XMapRaised(d,w); x.XSync(d,0); time.sleep(0.5)
try:
    during=pixel()
finally:
    x.XDestroyWindow(d,w); x.XSync(d,0); time.sleep(0.25)
    after=pixel(); x.XCloseDisplay(d)
result=dict(before=f'{before:08x}',red_window=f'{during:08x}',after=f'{after:08x}',
    scanout_updated=(during&0xffffff)==0xff0000,
    restored=(after&0xffffff)==(before&0xffffff))
print(json.dumps(result))
raise SystemExit(0 if result['scanout_updated'] and result['restored'] else 1)
