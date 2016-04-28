#ifndef BUILD_LK
#include <linux/string.h>
#endif
#ifdef BUILD_LK
#include <platform/mt_gpio.h>
#include <platform/mt_pmic.h>
#include <platform/mt_i2c.h>
#include <platform/upmu_common.h>
#include <debug.h>
#elif (defined BUILD_UBOOT)
#include <asm/arch/mt6577_gpio.h>
#else
#include <mach/mt_gpio.h>
#include <linux/xlog.h>
#include "mt8193_iic.h"
#include <mach/mt_pm_ldo.h>
#include <mach/upmu_common.h>
#endif
#include "lcm_drv.h"
#include "mt8193_lvds.h"

// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#define FRAME_WIDTH  (1024)
#define FRAME_HEIGHT (600)

#ifdef GPIO_LCM_PWR
#define GPIO_LCD_PWR      GPIO_LCM_PWR
#else
#define GPIO_LCD_PWR      0xFFFFFFFF
#endif

#define HSYNC_PULSE_WIDTH 10 
#define HSYNC_BACK_PORCH  160
#define HSYNC_FRONT_PORCH 160
#define VSYNC_PULSE_WIDTH 5
#define VSYNC_BACK_PORCH  23
#define VSYNC_FRONT_PORCH 12



#define LCD_DATA_FORMAT LCD_DATA_FORMAT_VESA8BIT

#define V_TOTAL (FRAME_HEIGHT + VSYNC_PULSE_WIDTH + VSYNC_BACK_PORCH + VSYNC_FRONT_PORCH)
#define H_TOTAL (FRAME_WIDTH + HSYNC_PULSE_WIDTH + HSYNC_BACK_PORCH + HSYNC_FRONT_PORCH)

#define H_START (HSYNC_PULSE_WIDTH + HSYNC_BACK_PORCH)
#define H_END (H_START + FRAME_WIDTH - 1)

#define V_START (VSYNC_PULSE_WIDTH + VSYNC_BACK_PORCH)
#define V_END (V_START + FRAME_HEIGHT - 1)

#define V_DELAY  0x0002  //Fixed Value
#define H_DELAY  (FRAME_WIDTH + HSYNC_PULSE_WIDTH + HSYNC_BACK_PORCH + HSYNC_FRONT_PORCH - V_DELAY)

#ifdef BUILD_LK
#define MT8193_REG_WRITE(add, data) mt8193_reg_i2c_write(add, data)
#define MT8193_REG_READ(add) mt8193_reg_i2c_read(add)
#elif (defined BUILD_UBOOT)
    // do nothing in uboot
#else
extern int mt8193_i2c_write(u16 addr, u32 data);
extern int mt8193_i2c_read(u16 addr, u32 *data);

#define MT8193_REG_WRITE(add, data) mt8193_i2c_write(add, data)
#define MT8193_REG_READ(add) lcm_mt8193_i2c_read(add)
#endif
// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------

static LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)    (lcm_util.set_reset_pin((v)))

#define UDELAY(n) (lcm_util.udelay(n))
#define MDELAY(n) (lcm_util.mdelay(n))


// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------

static __inline void send_ctrl_cmd(unsigned int cmd)
{

}

static __inline void send_data_cmd(unsigned int data)
{

}

static __inline void set_lcm_register(unsigned int regIndex,
                                      unsigned int regData)
{

}

static void lcm_set_gpio_output(unsigned int GPIO, unsigned int output)
{
    mt_set_gpio_mode(GPIO, GPIO_MODE_00);
    mt_set_gpio_dir(GPIO, GPIO_DIR_OUT);
    mt_set_gpio_out(GPIO, (output>0)? GPIO_OUT_ONE: GPIO_OUT_ZERO);
}
// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
    memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}

#ifdef BUILD_LK
#define I2C_CH                I2C3
#define MT8193_I2C_ADDR       0x3A

unsigned long mt_i2c_write (unsigned char channel,unsigned char chip, unsigned char *buffer, int len, unsigned char dir)
{
    U32 ret_code = 0;
    static struct mt_i2c_t i2c;
    i2c.id = channel;
    i2c.addr = chip>>1;	
    i2c.mode = ST_MODE;
    i2c.speed = 100;
    i2c.dma_en = 0;



    ret_code = i2c_write(&i2c, buffer, len);
    //ret_code = mt_i2c_write(I2C_CH, MT8193_I2C_ADDR, buffer, lens, 0); // 0:I2C_PATH_NORMAL
    return ret_code;
}

unsigned long mt_i2c_read(unsigned char channel,unsigned char chip, unsigned char *buffer, int len , unsigned char dir)
{
    U32 ret_code = 0;
    static struct mt_i2c_t i2c;
    i2c.id = channel;
    i2c.addr = chip>>1;	
    i2c.mode = ST_MODE;
    i2c.speed = 100;
    i2c.dma_en = 0;



    ret_code = i2c_read(&i2c, buffer, len);
    return ret_code;
}

int mt8193_reg_i2c_write(u16 addr, u32 data)
{
    u8 buffer[8];
    u8 lens;
	U32 ret_code = 0;

    if(((addr >> 8) & 0xFF) >= 0x80) // 8 bit : fast mode
    {
        buffer[0] = (addr >> 8) & 0xFF;
        buffer[1] = (data >> 24) & 0xFF;
        buffer[2] = (data >> 16) & 0xFF;
        buffer[3] = (data >> 8) & 0xFF;
        buffer[4] = data & 0xFF;
        lens = 5;
    }
    else // 16 bit : noraml mode
    {
        buffer[0] = (addr >> 8) & 0xFF;
        buffer[1] = addr & 0xFF;
        buffer[2] = (data >> 24) & 0xFF;
        buffer[3] = (data >> 16) & 0xFF;
        buffer[4] = (data >> 8) & 0xFF;
        buffer[5] = data & 0xFF;        
        lens = 6;
    }

	ret_code = mt_i2c_write(I2C_CH, MT8193_I2C_ADDR, buffer, lens, 0); // 0:I2C_PATH_NORMAL
    if (ret_code != 0)
    {
        printf("[LK/LCM] mt8193_reg_i2c_write reg[0x%X] fail, Error code [0x%X] \n", addr, ret_code);
        return ret_code;
    }
    printf("[LK/LCM] mt8193_reg_i2c_write reg[0x%X] = 0x%X\n", addr, data);
	return 0;
}



u32 mt8193_reg_i2c_read(u16 addr)
{
    
    u8 buffer[8] = {0};
    u8 lens;
    u32 ret_code = 0;
    u32 data;

    if(((addr >> 8) & 0xFF) >= 0x80) // 8 bit : fast mode
    {
        buffer[0] = (addr >> 8) & 0xFF;
        lens = 1;
    }
    else // 16 bit : noraml mode
    {
        buffer[0] = ( addr >> 8 ) & 0xFF;
        buffer[1] = addr & 0xFF;     
        lens = 2;
    }

    ret_code = mt_i2c_write(I2C_CH, MT8193_I2C_ADDR, buffer, lens, 0);    // set register command
    if (ret_code != 0)
        return ret_code;

    lens = 4;
    ret_code = mt_i2c_read(I2C_CH, MT8193_I2C_ADDR, buffer, lens, 0);
    if (ret_code != 0)
    {
        printf("[LK/LCM] mt8193_reg_i2c_read reg[0x%X] fail, Error code [0x%X] \n", addr, ret_code);
        return ret_code;
    }
    
    data = (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | (buffer[0]); //LSB fisrt
    printf("[LK/LCM] mt8193_reg_i2c_read reg[0x%X] = 0x%X\n", addr, data);

    return data;
    
}
#elif (defined BUILD_UBOOT)
    // do nothing in uboot
#else
u32 lcm_mt8193_i2c_read(u16 addr)
{
   u32 u4Reg = 0;
   u32 ret_code = 0;
	
   ret_code = mt8193_i2c_read(addr, &u4Reg);
   if (ret_code != 0)
   {
	   return ret_code;
   }

   return u4Reg;
}
#endif

static void lcm_mt8193_set_ckgen(void)
{
	u32 u4Reg = 0;

	u4Reg = MT8193_REG_READ(REG_PLL_GPANACFG0);
	u4Reg&=(~(0x7c000000));  //clear bit 26~30
	u4Reg |= (RG_PLL1_FBDIV | RG_PLL1_PREDIV | RG_PLL1_RST_DLY | RG_PLL1_LF | RG_PLL1_MONCKEN | RG_PLL1_VODEN | RG_NFIPLL_EN);
	MT8193_REG_WRITE(REG_PLL_GPANACFG0, u4Reg);

	u4Reg = MT8193_REG_READ(RG_LVDSWRAP_CTRL1);
	u4Reg&=(~(0x0000000f));  //clear bit 0~3
	u4Reg |= (RG_DCXO_POR_MON_EN | RG_PLL1_DIV);
	MT8193_REG_WRITE(RG_LVDSWRAP_CTRL1, u4Reg);
	UDELAY(200);

    MT8193_REG_WRITE(REG_LVDS_ANACFG2, (RG_VPLL_BC | RG_VPLL_BIC | RG_VPLL_BIR | RG_VPLL_BP | RG_VPLL_BR));
	
	u4Reg = 0;
	u4Reg |= (RG_VPLL_DIV | RG_VPLL_DPIX_CKSEL | RG_LVDS_DELAY | RG_VPLL_MKVCO |RG_VPLL_POSTDIV_EN);
	MT8193_REG_WRITE(REG_LVDS_ANACFG3, u4Reg);
	UDELAY(200);
	u4Reg &= (~(RG_VPLL_POSTDIV_EN));
	MT8193_REG_WRITE(REG_LVDS_ANACFG3, u4Reg);	
}

static void lcm_mt8193_set_lvds_analog(void)
{
    MT8193_REG_WRITE(REG_LVDS_ANACFG4, (RG_BYPASS | RG_LVDS_BYPASS));
    MT8193_REG_WRITE(REG_LVDS_ANACFG0, (RG_LVDS_ATERM_EN | RG_LVDS_APSRC | RG_LVDS_ANSRC | RG_LVDS_ATVCM | RG_LVDS_ATVO));
    MT8193_REG_WRITE(REG_LVDS_ANACFG1, 0x00000000);
	MDELAY(10);
}

static void lcm_mt8193_ckgen_power_on(void)
{
    u32 u4Reg = 0;

	u4Reg = MT8193_REG_READ(REG_PLL_GPANACFG2);
    u4Reg |= (PLLGP_ANACFG2_PLLGP_BIAS_EN);
    MT8193_REG_WRITE(REG_PLL_GPANACFG2, u4Reg);
	UDELAY(200);
	
	u4Reg = MT8193_REG_READ(REG_PLL_GPANACFG0);
	u4Reg |= RG_PLL1_EN;
	MT8193_REG_WRITE(REG_PLL_GPANACFG0, u4Reg);	
	UDELAY(200);

	u4Reg = MT8193_REG_READ(REG_LVDS_ANACFG2);
	u4Reg &= (~(RG_VPLL_BG_PD | RG_VPLL_BIAS_PD));
	MT8193_REG_WRITE(REG_LVDS_ANACFG2, u4Reg);	
    UDELAY(200);

	u4Reg = MT8193_REG_READ(REG_LVDS_ANACFG4);
	u4Reg &= (~(RG_VPLL_PD));
	MT8193_REG_WRITE(REG_LVDS_ANACFG4, u4Reg);
    UDELAY(200);
	u4Reg &= (~(RG_VPLL_RST));
	MT8193_REG_WRITE(REG_LVDS_ANACFG4, u4Reg);
	UDELAY(200);
}

static void lcm_mt8193_ckgen_power_off(void)
{
    u32 u4Reg = 0;
	
	u4Reg = MT8193_REG_READ(REG_LVDS_ANACFG4);
	u4Reg |= (RG_VPLL_RST);	
	MT8193_REG_WRITE(REG_LVDS_ANACFG4, u4Reg);
	UDELAY(200);
	u4Reg |= (RG_VPLL_PD);
	MT8193_REG_WRITE(REG_LVDS_ANACFG4, u4Reg);
	UDELAY(200);
		
	u4Reg = MT8193_REG_READ(REG_LVDS_ANACFG2);
	u4Reg |= (RG_VPLL_BG_PD | RG_VPLL_BIAS_PD);
	MT8193_REG_WRITE(REG_LVDS_ANACFG2, u4Reg);	
	UDELAY(200);

	u4Reg = MT8193_REG_READ(REG_PLL_GPANACFG0);
	u4Reg &= (~(RG_PLL1_EN));
	MT8193_REG_WRITE(REG_PLL_GPANACFG0, u4Reg);	
	UDELAY(200);
}

static void lcm_mt8193_lvds_analog_power_on(void)
{
	u32 u4Reg = 0;

	u4Reg = MT8193_REG_READ(REG_LVDS_ANACFG0);
	u4Reg &= (~(RG_LVDS_APD | RG_LVDS_BIASA_PD));
	MT8193_REG_WRITE(REG_LVDS_ANACFG0, u4Reg);
	UDELAY(200);
}

static void lcm_mt8193_lvds_analog_power_off(void)
{
	u32 u4Reg = 0;
	
	u4Reg = MT8193_REG_READ(REG_LVDS_ANACFG0);
	u4Reg |= (RG_LVDS_APD | RG_LVDS_BIASA_PD);
	MT8193_REG_WRITE(REG_LVDS_ANACFG0, u4Reg);
	UDELAY(200);
}

static void lcm_mt8193_set_lvds_digital(void)
{
    MT8193_REG_WRITE(LVDS_CLK_CTRL, (RG_TEST_CK_EN | RG_RX_CK_EN | RG_TX_CK_EN));
	MT8193_REG_WRITE(LVDS_CH_SWAP, RG_SWAP_SEL);	
	MT8193_REG_WRITE(LVDS_CLK_RESET, (RG_CTSCLK_RESET_B | RG_PCLK_RESET_B));	
}

static void lcm_mt8193_set_dgi0(void)
{	
	MT8193_REG_WRITE(DGI0_DATA_OUT_CTRL, DATA_OUT_SWAP);
	MT8193_REG_WRITE(DGI0_TG_CTRL00, PRGS_OUT);
	MT8193_REG_WRITE(DGI0_TG_CTRL02, ((V_TOTAL<<16) | H_TOTAL));
	MT8193_REG_WRITE(DGI0_TG_CTRL03, ((VSYNC_PULSE_WIDTH<<16) | HSYNC_PULSE_WIDTH));
	MT8193_REG_WRITE(DGI0_TG_CTRL05, ((H_START<<16) | H_END));	
	MT8193_REG_WRITE(DGI0_TG_CTRL06, ((V_START<<16) | V_END));
	MT8193_REG_WRITE(DGI0_TG_CTRL07, ((V_START<<16) | V_END));	
	MT8193_REG_WRITE(DGI0_TG_CTRL01, ((V_DELAY<<16) | H_DELAY));	
	MT8193_REG_WRITE(DGI0_TTL_ANAIF_CTRL, TTL_CLK_INV_ENABLE);
	MT8193_REG_WRITE(DGI0_TTL_ANAIF_CTRL1, PAD_TTL_EN_PP);
}

static void lcm_mt8193_anaif_clock_enable(void)
{
    MT8193_REG_WRITE(DGI0_ANAIF_CTRL0, DGI0_PAD_CLK_ENABLE);
}

static void lcm_mt8193_anaif_clock_disable(void)
{
    MT8193_REG_WRITE(DGI0_ANAIF_CTRL0, DGI0_PAD_CLK_DISABLE);
}

static void lcm_mt8193_dgi0_clock_enable(void)
{
    MT8193_REG_WRITE(DGI0_CLK_RST_CTRL, (DGI0_CLK_OUT_ENABLE | DGI0_CLK_IN_INV_ENABLE | DGI0_CLK_IN_ENABLE));
}

static void lcm_mt8193_dgi0_clock_disable(void)
{
    MT8193_REG_WRITE(DGI0_CLK_RST_CTRL, DGI0_CLK_OUT_DISABLE);
}

static void lcm_mt8193_dgi0_fifo_write_disable(void)
{
	MT8193_REG_WRITE(DGI0_DEC_CTRL, 0x0);
}

static void lcm_mt8193_dgi0_fifo_write_enable(void)
{
    MT8193_REG_WRITE(DGI0_DEC_CTRL, FIFO_WRITE_EN);
}

static void lcm_mt8193_sw_reset(void)
{
    MT8193_REG_WRITE(DGI0_FIFO_CTRL, (SW_RST | FIFO_RESET_ON | RD_START));
	UDELAY(200);
	MT8193_REG_WRITE(DGI0_FIFO_CTRL, (FIFO_RESET_ON | RD_START));
}

static void lcm_mt8193_lvds_power_off(void)
{
    MT8193_REG_WRITE(REG_LVDS_PWR_CTRL, 0x00000006);
	UDELAY(200);
	MT8193_REG_WRITE(REG_LVDS_PWR_CTRL, 0x00000007);
	UDELAY(200);
    MT8193_REG_WRITE(REG_LVDS_PWR_RST_B, 0x00000000);
	UDELAY(200);
	MT8193_REG_WRITE(REG_LVDS_PWR_CTRL, 0x00000005);
	MDELAY(5);
}

static void lcm_mt8193_lvds_power_on(void)
{
    MT8193_REG_WRITE(REG_LVDS_PWR_CTRL, 0x00000007);
	UDELAY(200);
	MT8193_REG_WRITE(REG_LVDS_PWR_RST_B, 0x00000001);
	UDELAY(200);
    MT8193_REG_WRITE(REG_LVDS_PWR_CTRL, 0x00000006);
	UDELAY(200);
	MT8193_REG_WRITE(REG_LVDS_PWR_CTRL, 0x00000002);
	MDELAY(5);
}

static void lcm_mt8193_lvds_top_clock_disable(void)
{ 
	MT8193_REG_WRITE(LVDS_CLK_CTRL, 0x0);
}

static void lcm_mt8193_lvds_top_clock_enable(void)
{
    MT8193_REG_WRITE(LVDS_CLK_CTRL, (RG_TEST_CK_EN | RG_RX_CK_EN | RG_TX_CK_EN));
}

static void lcm_mt8193_lvds_out_disable(void)
{
    MT8193_REG_WRITE(LVDS_OUTPUT_CTRL, 0x0);
}

static void lcm_mt8193_lvds_out_enable(void)
{
	MT8193_REG_WRITE(LVDS_OUTPUT_CTRL, (RG_LVDSRX_FIFO_EN | RG_SYNC_TRIG_MODE | RG_OUT_FIFO_EN | RG_LVDS_E));
}

static void lcm_mt8193_enable_output(void)
{
	lcm_mt8193_lvds_power_on();		
	lcm_mt8193_anaif_clock_enable();
	lcm_mt8193_set_ckgen();
	lcm_mt8193_ckgen_power_on();
	lcm_mt8193_dgi0_clock_enable();
	lcm_mt8193_dgi0_fifo_write_disable();
	lcm_mt8193_set_dgi0();
	lcm_mt8193_dgi0_fifo_write_enable();
	lcm_mt8193_lvds_top_clock_enable();
	lcm_mt8193_lvds_out_disable();
	lcm_mt8193_set_lvds_digital();
	lcm_mt8193_lvds_analog_power_on();
	lcm_mt8193_set_lvds_analog();
	MDELAY(1);
	lcm_mt8193_lvds_out_enable();	
	lcm_mt8193_sw_reset();
}

static void lcm_mt8193_disable_output(void)
{
	lcm_mt8193_lvds_analog_power_off();		
	lcm_mt8193_lvds_out_disable();
	lcm_mt8193_lvds_top_clock_disable();
	lcm_mt8193_dgi0_fifo_write_disable();
    lcm_mt8193_dgi0_clock_disable();
	lcm_mt8193_ckgen_power_off();	
    lcm_mt8193_anaif_clock_disable();
    lcm_mt8193_lvds_power_off();
}

static void lcm_get_params(LCM_PARAMS *params)
{
    memset(params, 0, sizeof(LCM_PARAMS));

    params->type   = LCM_TYPE_DPI;
    params->width  = FRAME_WIDTH;
    params->height = FRAME_HEIGHT;
    params->dpi.width  = FRAME_WIDTH;
    params->dpi.height = FRAME_HEIGHT;

    params->dpi.clk_pol           = LCM_POLARITY_RISING;
    params->dpi.de_pol            = LCM_POLARITY_RISING;
    params->dpi.vsync_pol         = LCM_POLARITY_FALLING;
    params->dpi.hsync_pol         = LCM_POLARITY_FALLING;

    params->dpi.hsync_pulse_width = HSYNC_PULSE_WIDTH;
    params->dpi.hsync_back_porch  = HSYNC_BACK_PORCH;
    params->dpi.hsync_front_porch = HSYNC_FRONT_PORCH;
    params->dpi.vsync_pulse_width = VSYNC_PULSE_WIDTH;
    params->dpi.vsync_back_porch  = VSYNC_BACK_PORCH;
    params->dpi.vsync_front_porch = VSYNC_FRONT_PORCH;
    
    params->dpi.i2x_en = 1;
    params->dpi.lvds_tx_en = 1;
    params->dpi.format            = LCM_DPI_FORMAT_RGB888;   // format is 24 bit
    params->dpi.rgb_order         = LCM_COLOR_ORDER_RGB;

    params->dpi.io_driving_current = LCM_DRIVING_CURRENT_2MA;
	
    params->dpi.PLL_CLOCK = 52;
}

unsigned int reg[21] =
{
	0x0800,0x0804,0x0808,0x080c,0x0810,0x0814,0x0818,0x081c,0x0820,0x082c,0x0830,
	0x0834,0x0904,0x0908,0x090c,0x0910,0x0914,0x0918,0x091c,0x0920,0x0934
};
static void lcm_mt8193_dump_register(void)
{
	u32 u4Reg = 0;
	u32 i;

	for(i=0;i<21;i++)
	{
		u4Reg = MT8193_REG_READ(reg[i]);
#ifdef BUILD_LK
		printf("[LK/LCM] 8193 LVDS TX Setting read Reg[0x%X] = 0x%X \n", reg[i], u4Reg);
#else
		printk("[LCM]8193 LVDS TX Setting read Reg[0x%X] = 0x%X	\n", reg[i], u4Reg);
#endif
	}

for (i = 0x0400; i < 0x0524; i += 4)
    {
    u4Reg = MT8193_REG_READ(i);
#ifdef BUILD_LK
    printf("[LK/LCM] 8193 DGI0 Setting read Reg[0x%X] = 0x%X \n", i, u4Reg);
#else
	printk("[LCM] 8193 DGI0 Setting read Reg[0x%X] = 0x%X \n", i, u4Reg);
#endif
 	}

for (i = 0x1000; i < 0x138c; i += 4)
    {
    u4Reg = MT8193_REG_READ(i);
#ifdef BUILD_LK
    printf("[LK/LCM] 8193 Clock Setting read Reg[0x%X] = 0x%X \n", i, u4Reg);
#else
	printk("[LCM] 8193 Clock Setting read Reg[0x%X] = 0x%X \n", i, u4Reg);
#endif
  	}
}

static void lcm_init_power(void)
{
#ifdef BUILD_LK 
	printf("[LK/LCM] lcm_init_power() enter\n");
	lcm_set_gpio_output(GPIO_LCD_PWR, GPIO_OUT_ONE);
	MDELAY(20);
	lcm_Enable_HW(3300);
		
#else
	printk("[Kernel/LCM] lcm_init_power() enter\n");
	lcm_set_gpio_output(GPIO_LCD_PWR, GPIO_OUT_ONE);
	MDELAY(20);
	
#endif

}

static void lcm_suspend_power(void)
{
#ifdef BUILD_LK 
	printf("[LK/LCM] lcm_suspend_power() enter\n");
	lcm_set_gpio_output(GPIO_LCD_PWR, GPIO_OUT_ZERO);
	MDELAY(20);
	lcm_Disable_HW();
			
#else
	printk("[Kernel/LCM] lcm_suspend_power() enter\n");
	lcm_set_gpio_output(GPIO_LCD_PWR, GPIO_OUT_ZERO);
	MDELAY(20);
	
	hwPowerDown(MT6328_POWER_LDO_VGP1 ,"LCM");
	MDELAY(20);
		
#endif
}

static void lcm_resume_power(void)
{
#ifdef BUILD_LK 
	printf("[LK/LCM] lcm_resume_power() enter\n");
	lcm_set_gpio_output(GPIO_LCD_PWR, GPIO_OUT_ONE);
	MDELAY(20);
	lcm_Enable_HW(3300);
				
#else
	printk("[Kernel/LCM] lcm_resume_power() enter\n");
	lcm_set_gpio_output(GPIO_LCD_PWR, GPIO_OUT_ONE);
	MDELAY(20);
	
	hwPowerOn(MT6328_POWER_LDO_VGP1, VOL_3300, "LCM");
	MDELAY(20);
			
#endif
}

static void lcm_init(void)
{
#ifdef BUILD_LK
	printf("[LK/LCM] lcm_init()  \n");

	lcm_mt8193_enable_output();	

    MDELAY(20);
    
	SET_RESET_PIN(1);
	MDELAY(20);

	SET_RESET_PIN(0);
	MDELAY(20);

	SET_RESET_PIN(1);
	MDELAY(20);
#else
    printk("[LCM] kernel lcm_init() enter\n");

#endif
}


static void lcm_suspend(void)
{
#ifdef BUILD_LK
	printf("[LK/LCM] lcm_suspend() enter\n");

    lcm_mt8193_disable_output();
	
	SET_RESET_PIN(1);
	MDELAY(10);
	
	SET_RESET_PIN(0);
	MDELAY(10);
#else
	printk("[kernel/LCM] lcm_suspend() enter\n");

    lcm_mt8193_disable_output();
	
	SET_RESET_PIN(1);
	MDELAY(10);
	
	SET_RESET_PIN(0);
	MDELAY(10);
	
#endif
}


static void lcm_resume(void)
{
#ifdef BUILD_LK
    u32 u4Reg = 0;

	printf("[LK/LCM] lcm_resume() enter\n");

	u4Reg = MT8193_REG_READ(REG_LVDS_PWR_RST_B);
	
	lcm_mt8193_enable_output();	
	
	SET_RESET_PIN(1);
	MDELAY(20);

	SET_RESET_PIN(0);
	MDELAY(20);

	SET_RESET_PIN(1);
	MDELAY(20);
	
#else
	printk("[kernel/LCM] MT8193 EJ101IA lcm_resume() enter\n");

	lcm_mt8193_enable_output();	

	SET_RESET_PIN(1);
	MDELAY(20);
	 
	SET_RESET_PIN(0);
	MDELAY(20);
	 
	SET_RESET_PIN(1);
	MDELAY(20);
	
#endif
}

LCM_DRIVER ptg101f08_lvds_lcm_drv = 
{
    .name		= "ptg101f08_lvds",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params     = lcm_get_params,
	.init           = lcm_init,
	.suspend        = lcm_suspend,
	.resume         = lcm_resume,
	.init_power 	= lcm_init_power,
	.resume_power	= lcm_resume_power,
	.suspend_power	= lcm_suspend_power,
};

