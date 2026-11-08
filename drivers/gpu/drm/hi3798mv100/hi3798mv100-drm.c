// SPDX-License-Identifier: GPL-2.0-only
/* MV100 VDP atomic scanout. Register facts derived from the MV100 SDK map.
 * HDMI clocks/timing are initially provided by the existing initialization
 * bridge; buffer ownership, planes, vblank and page flips belong to DRM.
 */
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_connector.h>
#include <drm/drm_color_mgmt.h>
#include <drm/drm_drv.h>
#include <drm/drm_encoder.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#define VINT_STATUS 0x08
#define VINT_MASK 0x0c
#define FRAME_IRQ BIT(2)
#define G_CTRL 0x6000
#define G_UPD 0x6004
#define G_ADDR 0x6010
#define G_STRIDE 0x601c
#define G_IRESO 0x6020
#define G_ALPHA 0x6030
#define G_DFPOS 0x6080
#define G_DLPOS 0x6084
#define G_VFPOS 0x6088
#define G_VLPOS 0x608c
#define GP_UPD 0x9004
#define V_CTRL 0x800
#define V_UPD 0x804
#define V_IRESO 0x828
#define V_ORESO 0x82c
#define V_CBMPARA 0x838
#define V_CPOS 0x844
#define V_DFPOS 0x860
#define V_DLPOS 0x864
#define V_VFPOS 0x868
#define V_VLPOS 0x86c
#define V_CSC_IDC 0x880
#define V_HSP 0x8c0
#define V_VSP 0x8d8
#define V_VSR 0x8dc
#define V_P0RESO 0xa00
#define V_LADDR 0xa04
#define V_CADDR 0xa08
#define V_STRIDE 0xa0c
#define V_P0VFPOS 0xa10
#define V_P0VLPOS 0xa14
#define V_REGION_EN 0xf00
#define CBM_MIX 0xb408
#define MIX_VIDEO 0xb008
#define DHD_CTRL 0xc000
#define DHD_THD3 0xc018
#define DHD_STATE 0xc0f0

extern bool mv100_display_is_ready(void);
/* Bring-up switch for the non-FIR unity sampling experiment. */
static bool force_unity_scaler;
module_param(force_unity_scaler,bool,0644);
struct mv100_drm {
 struct drm_device drm;
 struct drm_plane primary,video;
 struct drm_crtc crtc;
 struct drm_encoder encoder;
 struct drm_connector connector;
 void __iomem *regs;
 struct drm_pending_vblank_event *event;
 bool event_ref,active;
 u32 dirty,event_latch,saved_mask,saved_threshold;
 u64 vblanks,flips;
};
static inline struct mv100_drm *to_mv(struct drm_device *dev)
{return container_of(dev,struct mv100_drm,drm);}
static inline u32 rd(struct mv100_drm *d,u32 reg){return readl(d->regs+reg);}
static inline void wr(struct mv100_drm *d,u32 reg,u32 v){writel(v,d->regs+reg);}
static void bits(struct mv100_drm *d,u32 reg,u32 mask,u32 val)
{wr(d,reg,(rd(d,reg)&~mask)|(val&mask));}
static u32 xy(unsigned int x,unsigned int y){return x|(y<<12);}
static u32 size(unsigned int w,unsigned int h){return xy(w-1,h-1);}

static int enable_vblank(struct drm_crtc *crtc)
{
 struct mv100_drm *d=to_mv(crtc->dev);
 wr(d,VINT_STATUS,FRAME_IRQ);bits(d,VINT_MASK,FRAME_IRQ,FRAME_IRQ);return 0;
}
static void disable_vblank(struct drm_crtc *crtc)
{struct mv100_drm *d=to_mv(crtc->dev);bits(d,VINT_MASK,FRAME_IRQ,0);}

static irqreturn_t mv100_irq(int irq,void *arg)
{
 struct mv100_drm *d=arg;unsigned long flags;
 if(!(rd(d,VINT_STATUS)&FRAME_IRQ))return IRQ_NONE;
 wr(d,VINT_STATUS,FRAME_IRQ);
 if(!d->active)return IRQ_HANDLED;
 d->vblanks++;drm_crtc_handle_vblank(&d->crtc);
 spin_lock_irqsave(&d->drm.event_lock,flags);
 /* UPD bit clears only after the shadow surface has been latched. Do not
  * release the previous scanout merely because a programming write finished. */
 if(d->event&&(!(d->event_latch&1)||!(rd(d,G_UPD)&1))&&
    (!(d->event_latch&2)||!(rd(d,V_UPD)&1))){
  drm_crtc_send_vblank_event(&d->crtc,d->event);d->event=NULL;d->flips++;
  if(d->event_ref){d->event_ref=false;drm_crtc_vblank_put(&d->crtc);}
 }
 spin_unlock_irqrestore(&d->drm.event_lock,flags);return IRQ_HANDLED;
}

static int plane_check(struct drm_plane *plane,struct drm_atomic_state *state)
{
 struct drm_plane_state *s=drm_atomic_get_new_plane_state(state,plane);
 struct drm_crtc_state *cs;int ret,i;dma_addr_t addr;
 if(!s->crtc||!s->fb)return 0;
 cs=drm_atomic_get_crtc_state(state,s->crtc);if(IS_ERR(cs))return PTR_ERR(cs);
 ret=drm_atomic_helper_check_plane_state(s,cs,
                                      plane->type==DRM_PLANE_TYPE_PRIMARY?DRM_PLANE_NO_SCALING:1<<14,
                                      plane->type==DRM_PLANE_TYPE_PRIMARY?DRM_PLANE_NO_SCALING:4<<16,
                                      plane->type!=DRM_PLANE_TYPE_PRIMARY,true);
 if(ret||!s->visible)return ret;
 if((s->src_x|s->src_y|s->src_w|s->src_h)&0xffff)return -EINVAL;
 if(s->crtc_x<0||s->crtc_y<0)return -EINVAL;
 if(s->fb->format->format==DRM_FORMAT_NV12&&
    (((s->src_x|s->src_y|s->src_w|s->src_h)>>16)&1))return -EINVAL;
 for(i=0;i<s->fb->format->num_planes;i++){
  struct drm_gem_dma_object *obj=drm_fb_dma_get_gem_obj(s->fb,i);
  if(!obj||s->fb->pitches[i]%16)return -EINVAL;
  addr=drm_fb_dma_get_gem_addr(s->fb,s,i);
  if(upper_32_bits(addr)||(addr&15))return -EINVAL;
 }
 return 0;
}
static void plane_disable(struct drm_plane *plane,struct drm_atomic_state *state);
static void plane_update(struct drm_plane *plane,struct drm_atomic_state *state)
{
 struct mv100_drm *d=to_mv(plane->dev);struct drm_plane_state *s=plane->state;
 u32 w=s->src_w>>16,h=s->src_h>>16,x=s->crtc_x,y=s->crtc_y;
 if(!s->visible){plane_disable(plane,state);return;}
 if(plane==&d->primary){
  u32 alpha=255;
  if(s->fb->format->format==DRM_FORMAT_ARGB8888)alpha|=BIT(12)|BIT(13);
  wr(d,G_ADDR,lower_32_bits(drm_fb_dma_get_gem_addr(s->fb,s,0)));
  bits(d,G_STRIDE,0xffff,s->fb->pitches[0]/16);
  wr(d,G_IRESO,size(w,h));wr(d,G_DFPOS,xy(x,y));wr(d,G_DLPOS,xy(x+w-1,y+h-1));
  wr(d,G_VFPOS,xy(x,y));wr(d,G_VLPOS,xy(x+w-1,y+h-1));wr(d,G_ALPHA,alpha);
  /* ARGB8888 reader; XRGB ignores per-pixel alpha. Progressive, secure
   * translation disabled exactly as in the working physical boot scanout. */
  wr(d,G_CTRL,0x68|BIT(26)|BIT(30)|BIT(31));d->dirty|=1;
 }else{
  wr(d,V_LADDR,lower_32_bits(drm_fb_dma_get_gem_addr(s->fb,s,0)));
  wr(d,V_CADDR,lower_32_bits(drm_fb_dma_get_gem_addr(s->fb,s,1)));
  unsigned int ow=s->crtc_w,oh=s->crtc_h;
  /* Video reader strides are bytes; unlike G0 they are not 16-byte units. */
  wr(d,V_STRIDE,s->fb->pitches[0]|(s->fb->pitches[1]<<16));
  wr(d,V_IRESO,size(w,h));wr(d,V_ORESO,size(ow,oh));wr(d,V_CPOS,(w-1)<<12);
  wr(d,V_DFPOS,xy(x,y));wr(d,V_DLPOS,xy(x+ow-1,y+oh-1));
  wr(d,V_VFPOS,xy(x,y));wr(d,V_VLPOS,xy(x+ow-1,y+oh-1));
  wr(d,V_P0VFPOS,xy(x,y));wr(d,V_P0VLPOS,xy(x+ow-1,y+oh-1));wr(d,V_P0RESO,w-1);
  /* Initial scaler uses hardware nearest sampling, without uninitialized
   * FIR coefficient tables. Chroma always expands 4:2:0 into 4:2:2. */
  wr(d,V_HSP,div_u64((u64)w<<20,ow)|
     ((w!=ow||force_unity_scaler)?BIT(30)|BIT(31):0));
  wr(d,V_VSP,(1<<19)|BIT(30)|((h!=oh||force_unity_scaler)?BIT(31):0));
  wr(d,V_VSR,(h<<12)/oh);
  wr(d,0x8c4,0);wr(d,0x8c8,0);wr(d,0x8e0,0);wr(d,0x8e4,0);
  bits(d,V_CSC_IDC,BIT(22),0);wr(d,V_CBMPARA,255);wr(d,V_REGION_EN,1);
  wr(d,0x4000,255);wr(d,0x4020,size(1920,1080));
  wr(d,0x4200,0);wr(d,0x4204,size(1920,1080));
  wr(d,0x4208,0);wr(d,0x420c,size(1920,1080));
  wr(d,0x4214,0);bits(d,0x4300,BIT(22),0);bits(d,0x4320,BIT(22),0);
  bits(d,0x4004,1,1);
  /* SDK NV12 selects uv_order=1; NV21 selects 0. */
  /* With SR disabled the SDK assigns the whole line buffer to V0 ZME. */
  bits(d,0xd80,BIT(31),0);
  /* IFIR_DISEN does not feed a full frame through this 420 reader: measured
   * WBC output stops at row540. COPY supplies the complete frame without
   * depending on the SDK's six-tap coefficient tables. */
  wr(d,V_CTRL,3|BIT(10)|BIT(11)|BIT(12)|BIT(14)|BIT(17)|BIT(18)|BIT(21)|BIT(31));
  /* GP0 at mixer priority0, video0 at priority1. Overlay is explicitly
   * owned by the DRM master, not by an independent vendor window. */
  bits(d,MIX_VIDEO,0xff,1);d->dirty|=2;
 }
}
static void plane_disable(struct drm_plane *plane,struct drm_atomic_state *state)
{
 struct mv100_drm *d=to_mv(plane->dev);
 if(plane==&d->primary){bits(d,G_CTRL,BIT(31),0);d->dirty|=1;}
 else{bits(d,V_CTRL,BIT(31),0);bits(d,CBM_MIX,0xff,2);d->dirty|=2;}
}
static const struct drm_plane_helper_funcs plane_helpers={
 .prepare_fb=drm_gem_plane_helper_prepare_fb,.atomic_check=plane_check,
 .atomic_update=plane_update,.atomic_disable=plane_disable,
};
static const struct drm_plane_funcs plane_funcs={
 .update_plane=drm_atomic_helper_update_plane,.disable_plane=drm_atomic_helper_disable_plane,
 .destroy=drm_plane_cleanup,.reset=drm_atomic_helper_plane_reset,
 .atomic_duplicate_state=drm_atomic_helper_plane_duplicate_state,
 .atomic_destroy_state=drm_atomic_helper_plane_destroy_state,
};
static void flush(struct drm_crtc *crtc,struct drm_atomic_state *state)
{
 struct mv100_drm *d=to_mv(crtc->dev);unsigned long flags;u32 latch=d->dirty;
 spin_lock_irqsave(&d->drm.event_lock,flags);
 if(d->video.state->visible&&d->video.state->crtc&&d->video.state->fb)
  bits(d,CBM_MIX,0xff,d->primary.state->normalized_zpos>d->video.state->normalized_zpos?0x21:0x12);
 if(d->dirty&1)bits(d,G_UPD,1,1);
 if(d->dirty&2)bits(d,V_UPD,1,1);
 d->dirty=0;
 if(d->active&&crtc->state->event){
  if(!WARN_ON(d->event)){
   d->event=crtc->state->event;crtc->state->event=NULL;d->event_latch=latch;
   d->event_ref=!drm_crtc_vblank_get(crtc);
  }
 }
 spin_unlock_irqrestore(&d->drm.event_lock,flags);
}
static void crtc_enable(struct drm_crtc *crtc,struct drm_atomic_state *state)
{
 struct mv100_drm *d=to_mv(crtc->dev);
 d->active=true;drm_crtc_vblank_on(crtc);flush(crtc,state);
}
static void crtc_disable(struct drm_crtc *crtc,struct drm_atomic_state *state)
{
 struct mv100_drm *d=to_mv(crtc->dev);unsigned long flags;
 bits(d,G_CTRL,BIT(31),0);bits(d,V_CTRL,BIT(31),0);bits(d,G_UPD,1,1);bits(d,V_UPD,1,1);
 spin_lock_irqsave(&d->drm.event_lock,flags);
 if(d->event){drm_crtc_send_vblank_event(crtc,d->event);d->event=NULL;}
 if(crtc->state->event){
  drm_crtc_send_vblank_event(crtc,crtc->state->event);crtc->state->event=NULL;
 }
 if(d->event_ref){d->event_ref=false;drm_crtc_vblank_put(crtc);}
 spin_unlock_irqrestore(&d->drm.event_lock,flags);
 drm_crtc_vblank_off(crtc);d->active=false;
}
static const struct drm_crtc_funcs crtc_funcs={
 .reset=drm_atomic_helper_crtc_reset,.destroy=drm_crtc_cleanup,
 .set_config=drm_atomic_helper_set_config,.page_flip=drm_atomic_helper_page_flip,
 .atomic_duplicate_state=drm_atomic_helper_crtc_duplicate_state,
 .atomic_destroy_state=drm_atomic_helper_crtc_destroy_state,
 .enable_vblank=enable_vblank,.disable_vblank=disable_vblank,
};
static const struct drm_crtc_helper_funcs crtc_helpers={
 .atomic_enable=crtc_enable,.atomic_disable=crtc_disable,.atomic_flush=flush,
};
static const struct drm_display_mode mode={
 .clock=148500,.hdisplay=1920,.hsync_start=2008,.hsync_end=2052,.htotal=2200,
 .vdisplay=1080,.vsync_start=1084,.vsync_end=1089,.vtotal=1125,
 .flags=DRM_MODE_FLAG_PHSYNC|DRM_MODE_FLAG_PVSYNC,.type=DRM_MODE_TYPE_DRIVER|DRM_MODE_TYPE_PREFERRED,
};
static int get_modes(struct drm_connector *connector)
{
 struct drm_display_mode *m=drm_mode_duplicate(connector->dev,&mode);
 if(!m)return 0;
 drm_mode_set_name(m);drm_mode_probed_add(connector,m);return 1;
}
static enum drm_mode_status mode_valid(struct drm_connector *connector,struct drm_display_mode *m)
{return drm_mode_equal(m,&mode)?MODE_OK:MODE_BAD;}
static const struct drm_connector_helper_funcs connector_helpers={.get_modes=get_modes,.mode_valid=mode_valid};
static const struct drm_connector_funcs connector_funcs={
 .fill_modes=drm_helper_probe_single_connector_modes,.destroy=drm_connector_cleanup,
 .reset=drm_atomic_helper_connector_reset,.atomic_duplicate_state=drm_atomic_helper_connector_duplicate_state,
 .atomic_destroy_state=drm_atomic_helper_connector_destroy_state,
};
static const struct drm_encoder_funcs encoder_funcs={.destroy=drm_encoder_cleanup};
static const struct drm_mode_config_funcs mode_funcs={
 .fb_create=drm_gem_fb_create,.atomic_check=drm_atomic_helper_check,.atomic_commit=drm_atomic_helper_commit,
};
DEFINE_DRM_GEM_DMA_FOPS(mv100_fops);
static const struct drm_driver mv100_driver={
 .driver_features=DRIVER_GEM|DRIVER_MODESET|DRIVER_ATOMIC,
 .fops=&mv100_fops,DRM_GEM_DMA_DRIVER_OPS,DRM_FBDEV_DMA_DRIVER_OPS,
 .name="hi3798mv100",.desc="MV100 native VDP atomic scanout",.date="20261108",.major=1,.minor=0,
};
static ssize_t scanout_state_show(struct device *dev,struct device_attribute *attr,char *buf)
{
 struct mv100_drm *d=dev_get_drvdata(dev);
 return sysfs_emit(buf,"vblanks=%llu flips=%llu g0_addr=%08x g0_stride=%u g0_upd=%08x v0_y=%08x v0_uv=%08x v0_ctrl=%08x mask=%08x scan=%08x\n",
   d->vblanks,d->flips,rd(d,G_ADDR),(rd(d,G_STRIDE)&65535)*16,rd(d,G_UPD),
   rd(d,V_LADDR),rd(d,V_CADDR),rd(d,V_CTRL),rd(d,VINT_MASK),rd(d,DHD_STATE));
}
static DEVICE_ATTR_RO(scanout_state);
static struct attribute *mv100_attrs[]={&dev_attr_scanout_state.attr,NULL};
ATTRIBUTE_GROUPS(mv100);
static int mv100_probe(struct platform_device *pdev)
{
 struct device *dev=&pdev->dev;struct mv100_drm *d;int ret,irq;
 static const u32 rgb[]={DRM_FORMAT_XRGB8888,DRM_FORMAT_ARGB8888};
 static const u32 yuv[]={DRM_FORMAT_NV12};
 static const u64 modifiers[]={DRM_FORMAT_MOD_LINEAR,DRM_FORMAT_MOD_INVALID};
 if(!mv100_display_is_ready())return -EPROBE_DEFER;
 d=devm_drm_dev_alloc(dev,&mv100_driver,struct mv100_drm,drm);if(IS_ERR(d))return PTR_ERR(d);
 platform_set_drvdata(pdev,d);
 ret=dma_set_mask_and_coherent(dev,DMA_BIT_MASK(32));if(ret)return ret;
 d->regs=devm_platform_ioremap_resource(pdev,0);if(IS_ERR(d->regs))return PTR_ERR(d->regs);
 irq=platform_get_irq(pdev,0);if(irq<0)return irq;
 ret=drmm_mode_config_init(&d->drm);if(ret)return ret;
 d->drm.mode_config.funcs=&mode_funcs;d->drm.mode_config.min_width=64;d->drm.mode_config.max_width=1920;
 d->drm.mode_config.min_height=64;d->drm.mode_config.max_height=1080;
 d->drm.mode_config.normalize_zpos=true;
 ret=drm_universal_plane_init(&d->drm,&d->primary,1,&plane_funcs,rgb,ARRAY_SIZE(rgb),modifiers,DRM_PLANE_TYPE_PRIMARY,NULL);if(ret)return ret;
 drm_plane_helper_add(&d->primary,&plane_helpers);drm_plane_create_zpos_property(&d->primary,0,0,1);
 ret=drm_universal_plane_init(&d->drm,&d->video,1,&plane_funcs,yuv,ARRAY_SIZE(yuv),modifiers,DRM_PLANE_TYPE_OVERLAY,NULL);if(ret)return ret;
 drm_plane_helper_add(&d->video,&plane_helpers);drm_plane_create_zpos_property(&d->video,1,0,1);
 ret=drm_plane_create_color_properties(&d->video,BIT(DRM_COLOR_YCBCR_BT709),
      BIT(DRM_COLOR_YCBCR_LIMITED_RANGE),DRM_COLOR_YCBCR_BT709,DRM_COLOR_YCBCR_LIMITED_RANGE);
 if(ret)return ret;
 ret=drm_crtc_init_with_planes(&d->drm,&d->crtc,&d->primary,NULL,&crtc_funcs,NULL);if(ret)return ret;
 drm_crtc_helper_add(&d->crtc,&crtc_helpers);
 ret=drm_encoder_init(&d->drm,&d->encoder,&encoder_funcs,DRM_MODE_ENCODER_TMDS,NULL);if(ret)return ret;
 d->encoder.possible_crtcs=1;
 ret=drm_connector_init(&d->drm,&d->connector,&connector_funcs,DRM_MODE_CONNECTOR_HDMIA);if(ret)return ret;
 drm_connector_helper_add(&d->connector,&connector_helpers);d->connector.status=connector_status_connected;
 ret=drm_connector_attach_encoder(&d->connector,&d->encoder);if(ret)return ret;
 drm_mode_config_reset(&d->drm);
 ret=drm_vblank_init(&d->drm,1);if(ret)return ret;
 d->saved_mask=rd(d,VINT_MASK);d->saved_threshold=rd(d,DHD_THD3);
 bits(d,VINT_MASK,FRAME_IRQ,0);
 ret=devm_request_irq(dev,irq,mv100_irq,IRQF_SHARED,"mv100-vdp",d);if(ret)return ret;
 /* Hardware probe verified threshold3=1 interrupts at vcnt1122, inside
  * vertical blank, at 60Hz. This is not a software-generated vblank. */
 bits(d,DHD_THD3,0x9fff,1);bits(d,DHD_CTRL,1,1);wr(d,VINT_STATUS,FRAME_IRQ);
 ret=drm_dev_register(&d->drm,0);if(ret)return ret;
 drm_fbdev_dma_setup(&d->drm,32);
 dev_info(dev,"native RGB DMA scanout and NV12 plane; hardware vblank IRQ%d\n",irq);
 return 0;
}
static void mv100_remove(struct platform_device *pdev)
{
 struct mv100_drm *d=platform_get_drvdata(pdev);
 drm_dev_unregister(&d->drm);drm_atomic_helper_shutdown(&d->drm);
 wr(d,VINT_MASK,d->saved_mask);wr(d,DHD_THD3,d->saved_threshold);bits(d,DHD_CTRL,1,1);
}
static void mv100_shutdown(struct platform_device *pdev)
{struct mv100_drm *d=platform_get_drvdata(pdev);drm_atomic_helper_shutdown(&d->drm);}
static const struct of_device_id mv100_ids[]={{.compatible="hisilicon,hi3798mv100-drm"},{}};
MODULE_DEVICE_TABLE(of,mv100_ids);
static struct platform_driver mv100_platform_driver={
 .probe=mv100_probe,.remove=mv100_remove,.shutdown=mv100_shutdown,
 .driver={.name="hi3798mv100-drm",.of_match_table=mv100_ids,.dev_groups=mv100_groups},
};
module_platform_driver(mv100_platform_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hi3798MV100 native atomic VDP scanout and video plane");
