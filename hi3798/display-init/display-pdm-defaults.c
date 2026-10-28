/* Isolated display-init adapter; uses the SDK's own structure definitions. */
#include "hi_drv_pdm.h"
#include "hi_common.h"
#include <string.h>
#include "vdp_driver.h"
#include "hi_reg_common.h"

void mv100_sdk_gfx_start(unsigned int address)
{
    VDP_DISP_RECT_S rect;
    memset(&rect, 0, sizeof(rect));
    rect.u32IWth = rect.u32OWth = rect.u32DXL = 1920;
    rect.u32IHgt = rect.u32OHgt = rect.u32DYL = 1080;
    VDP_GFX_SetLayerEnable(0, HI_FALSE);
    VDP_GFX_SetNoSecFlag(0, HI_TRUE);
    VDP_GFX_SetDcmpEnable(0, HI_FALSE);
    VDP_GFX_SetInDataFmt(0, VDP_GFX_IFMT_ARGB_8888);
    /* MV100 G0 surface_stride is expressed in 16-byte units. */
    VDP_GFX_SetLayerAddr(0, address, 1920 * 4 / 16);
    VDP_GFX_SetLayerReso(0, rect);
    VDP_GFX_SetReadMode(0, VDP_RMODE_PROGRESSIVE);
    VDP_GFX_SetMuteEnable(0, HI_FALSE);
    VDP_GFX_SetPreMultEnable(0, HI_FALSE);
    VDP_GFX_SetLayerGalpha(0, 255);
    VDP_GFX_SetPalpha(0, HI_FALSE, HI_FALSE, 255, 255);
    VDP_GP_SetLayerReso(0, rect);
    VDP_GP_SetReadMode(0, VDP_RMODE_PROGRESSIVE);
    VDP_GP_SetLayerGalpha(0, 255);
    VDP_GP_SetIpOrder(0, 0, VDP_GP_ORDER_CSC);
    VDP_GP_SetZmeEnable(0, VDP_ZME_MODE_ALL, HI_FALSE);
    VDP_GP_SetZmeFirEnable(0, VDP_ZME_MODE_ALL, HI_FALSE);
    VDP_GP_SetZmeMidEnable(0, VDP_ZME_MODE_ALL, HI_FALSE);
    VDP_GP_SetCscMode(0, VDP_CSC_RGB2YUV_709);
    VDP_GP_SetCscEnable(0, HI_TRUE);
    VDP_CBM_ResetMixerPrio(VDP_CBM_MIXG0);
    VDP_CBM_SetMixerPrio(VDP_CBM_MIXG0, 0, 0);
    VDP_CBM_ResetMixerPrio(VDP_CBM_MIX0);
    VDP_CBM_SetMixerPrio(VDP_CBM_MIX0, VDP_CBM_GP0, 0);
    VDP_GFX_SetLayerEnable(0, HI_TRUE);
    VDP_GFX_SetRegUp(0);
    VDP_GP_SetRegUp(0);
    VDP_DISP_SetRegUp(0);
}

void mv100_sdk_gfx_dump(void)
{
    extern volatile S_VOU_CV200_REGS_TYPE *pVdpReg;
    printf("G0 ctrl=%08x addr=%08x stride=%08x ireso=%08x alpha=%08x\n",
        pVdpReg->G0_CTRL.u32, pVdpReg->G0_ADDR.u32, pVdpReg->G0_STRIDE.u32,
        pVdpReg->G0_IRESO.u32, pVdpReg->G0_CBMPARA.u32);
    printf("GP0 ctrl=%08x alpha=%08x mixg=%08x cbm1=%08x DHD0=%08x\n",
        pVdpReg->GP0_CTRL.u32, pVdpReg->GP0_GALPHA.u32, pVdpReg->MIXG0_MIX.u32,
        pVdpReg->CBM_MIX1.u32, pVdpReg->DHD0_CTRL.u32);
}

int mv100_sdk_hdmi_start(void)
{
    int result = HI_UNF_HDMI_Init();
    if (result)
        return result;
    udelay(100000);
    return HI_UNF_HDMI_Open(HI_UNF_HDMI_ID_0, HI_UNF_ENC_FMT_1080P_60);
}

HI_VOID HI_DRV_SYS_GetChipVersion(HI_CHIP_TYPE_E *type, HI_CHIP_VERSION_E *version)
{
    *type = HI_CHIP_TYPE_HI3798M;
    *version = HI_CHIP_VERSION_V100;
}

HI_S32 HI_DRV_PDM_GetDispParam(HI_UNF_DISP_E disp, HI_DISP_PARAM_S *param)
{
    int i;
    if (!param || disp > HI_UNF_DISPLAY1)
        return HI_FAILURE;
    memset(param, 0, sizeof(*param));
    param->enFormat = disp == HI_UNF_DISPLAY1 ? HI_UNF_ENC_FMT_1080P_60 : HI_UNF_ENC_FMT_PAL;
    param->enSrcDisp = HI_UNF_DISPLAY_BUTT;
    param->u32Brightness = param->u32Contrast = 50;
    param->u32Saturation = param->u32HuePlus = 50;
    param->u32VirtScreenWidth = 1920;
    param->u32VirtScreenHeight = 1080;
    param->stAspectRatio.enDispAspectRatio = HI_UNF_DISP_ASPECT_RATIO_16TO9;
    param->enPixelFormat = HI_GFX_PF_8888;
    param->enVidOutMode = HI_UNF_HDMI_VIDEO_MODE_RGB444;
    param->stBgColor.u8Blue = 255;
    for (i = 0; i < HI_UNF_DISP_INTF_TYPE_BUTT; ++i)
        param->stIntf[i].enIntfType = HI_UNF_DISP_INTF_TYPE_BUTT;
    if (disp == HI_UNF_DISPLAY1) {
        param->stIntf[HI_UNF_DISP_INTF_TYPE_HDMI].enIntfType = HI_UNF_DISP_INTF_TYPE_HDMI;
        param->stIntf[HI_UNF_DISP_INTF_TYPE_HDMI].unIntf.enHdmi = HI_UNF_HDMI_ID_0;
    }
    return HI_SUCCESS;
}
