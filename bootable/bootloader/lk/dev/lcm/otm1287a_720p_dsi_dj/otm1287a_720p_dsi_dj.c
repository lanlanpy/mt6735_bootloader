#ifndef BUILD_LK
#include <linux/string.h>
#endif
#include "lcm_drv.h"

#ifdef BUILD_LK
	#include <mt_gpio.h>
#elif defined(BUILD_UBOOT)
	#include <asm/arch/mt_gpio.h>
#else
	#include <mach/mt_gpio.h>
#endif

#ifdef BUILD_LK
#include <stdio.h>
#include <string.h>
#else
#include <linux/string.h>
#include <linux/kernel.h>
#endif

//#define USE_LCM_THREE_LANE 1

#define _LCM_DEBUG_

#include <cust_gpio_usage.h>

// ---------------------------------------------------------------------------
//  Local Constants
// ---------------------------------------------------------------------------

#define FRAME_WIDTH  										(720)
#define FRAME_HEIGHT 										(1280)

#define REGFLAG_DELAY             							0XFEFF
#define REGFLAG_END_OF_TABLE      							0xFFFF   // END OF REGISTERS MARKER

#define LCM_DSI_CMD_MODE									0

#define LCM_ID_OTM1287A 0x8B
//#define GPIO_LCM_RST_PIN         (GPIO141 | 0x80000000)

// ---------------------------------------------------------------------------
//  Local Variables
// ---------------------------------------------------------------------------

static LCM_UTIL_FUNCS lcm_util = {0};

#define SET_RESET_PIN(v)    								(lcm_util.set_reset_pin((v)))
#define SET_GPIO_OUT(gpio_num,val)    						(lcm_util.set_gpio_out((gpio_num),(val)))


#define UDELAY(n) 											(lcm_util.udelay(n))
#define MDELAY(n) 											(lcm_util.mdelay(n))



//#define _SYNA_INFO_
//#define _SYNA_DEBUG_
//#define _LCM_DEBUG_
//#define _LCM_INFO_
/*
#ifdef _LCM_DEBUG_
#define lcm_debug(fmt, args...) printk(fmt, ##args)
#else
#define lcm_debug(fmt, args...) do { } while (0)
#endif

#ifdef _LCM_INFO_
#define lcm_info(fmt, args...) printk(fmt, ##args)
#else
#define lcm_info(fmt, args...) do { } while (0)
#endif
#define lcm_err(fmt, args...) printk(fmt, ##args) */

#ifdef _LCM_DEBUG_
  #ifdef BUILD_LK
  #define LCM_PRINT printf
  #else
  #define LCM_PRINT printk
  #endif
#else
	#define LCM_PRINT
#endif

// ---------------------------------------------------------------------------
//  Local Functions
// ---------------------------------------------------------------------------

#define dsi_set_cmdq_V2(cmd, count, ppara, force_update)	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update)		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd)										lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums)					lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg											lcm_util.dsi_read_reg()
#define read_reg_v2(cmd, buffer, buffer_size)   			lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)    
       

static struct LCM_setting_table {
    unsigned cmd;
    unsigned char count;
    unsigned char para_list[64];
};

static struct LCM_setting_table lcm_initialization_setting[] = {
	
	/*
	Note :

	Data ID will depends on the following rule.
	
		count of parameters > 1	=> Data ID = 0x39
		count of parameters = 1	=> Data ID = 0x15
		count of parameters = 0	=> Data ID = 0x05

	Structure Format :

	{DCS command, count of parameters, {parameter list}}
	{REGFLAG_DELAY, milliseconds of time, {}},

	...

	Setting ending by predefined flag
	
	{REGFLAG_END_OF_TABLE, 0x00, {}}
	*/
	{0x00,1,{0x00}},
	{0xff,3,{0x12,0x87,0x01}},       //EXTC=1
	{0x00,1,{0x80}},                //Orise mode enable
	{0xff,2,{0x12,0x87}},
            
//-------------------- panel setting --------------------//
	{0x00,1, {0x80}},             //TCON Setting
//#ifdef USE_LCM_THREE_LANE
//{0xff,2,{0x20, 0x02}}, 					//MIPI 3 Lane
//#else
//{0xff,2,{0x30, 0x02}}, 					//MIPI 4 Lane
//#endif
	{0xc0,9, {0x00,0x64,0x00,0x10,0x10,0x00,0x64,0x10,0x10}},

	{0x00,1, {0x90}},             //Panel Timing Setting
	{0xc0,6, {0x00,0x4b,0x00,0x01,0x00,0x04}},

	{0x00,1, {0xb3}},             //Interval Scan Frame: 0 frame, column inversion
	{0xc0,2, {0x00,0x55}},

	{0x00,1, {0x81}},             //frame rate:60Hz
	{0xc1,1, {0x55}},

//-------------------- power setting --------------------//
	{0x00,1, {0xa0}},             //dcdc setting
	{0xc4,14, {0x05,0x10,0x04,0x02,0x05,0x15,0x11,0x05,0x10,0x07,0x02,0x05,0x15,0x11}},

	{0x00,1, {0xb0}},             //clamp voltage setting
	{0xc4,2, {0x00,0x00}},

	{0x00,1, {0x91}},             //VGH=18V, VGL=-10V, pump ratio:VGH=8x, VGL=-5x
	{0xc5,2, {0xa6,0xd2}},

	{0x00,1, {0x00}},             //GVDD=5.008V, NGVDD=-5.008V
	{0xd8,2, {0xc7,0xc7}},     

	{0x00,1, {0x00}},             //VCOM=-0.9V
	{0xd9,1, {0x79}},             

	{0x00,1, {0xb3}},             //VDD_18V=1.7V, LVDSVDD=1.6V
	{0xc5,1, {0x84}},

	{0x00,1, {0xbb}},             //LVD voltage level setting
	{0xc5,1, {0x8a}},

	{0x00,1, {0x82}},             //chopper
	{0xc4,1, {0x0a}},

	{0x00,1, {0xc6}},		//debounce 
	{0xB0,1, {0x03}}, 

//-------------------- control setting --------------------//
	{0x00,1, {0x00}},             //ID1
	{0xd0,1, {0x40}},

	{0x00,1, {0x00}},             //ID2, ID3
	{0xd1,2, {0x00,0x00}},

//-------------------- power on setting --------------------//
	{0x00,1, {0xb2}},             //VGLO1
	{0xf5,2, {0x00,0x00}},

	{0x00,1, {0xb6}},             //VGLO2
	{0xf5,2, {0x00,0x00}},

	{0x00,1, {0x94}},             //VCL pump dis
	{0xf5,2, {0x00,0x00}},

	{0x00,1, {0xd2}},             //VCL reg. en
	{0xf5,2, {0x06,0x15}},

	{0x00,1, {0xb4}},             //VGLO1/2 Pull low setting
	{0xc5,1, {0xcc}},	       //d[7] vglo1 d[6] vglo2 => 0: pull vss, 1: pull vgl

//-------------------- for Power IC ---------------------------------
	{0x00,1, {0x90}},             //Mode-3
	{0xf5,4, {0x02,0x11,0x02,0x15}},

	{0x00,1, {0x90}},             //2xVPNL, 1.5*=00, 2*=50, 3*=a0
	{0xc5,1, {0x50}},

	{0x00,1, {0x94}},             //Frequency
	{0xc5,1, {0x66}},

//-------------------- panel timing state control --------------------//
	{0x00,1, {0x80}},             //panel timing state control
	{0xcb,11, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1, {0x90}},             //panel timing state control
	{0xcb,15, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1, {0xa0}},             //panel timing state control
	{0xcb,15, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1, {0xb0}},             //panel timing state control
	{0xcb,15, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1, {0xc0}},             //panel timing state control
	{0xcb,15, {0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x00,0x05,0x00,0x00,0x00,0x00}},

	{0x00,1, {0xd0}},             //panel timing state control
	{0xcb,15, {0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x05,0x05,0x05,0x05,0x05,0x05,0x05,0x05}},

	{0x00,1, {0xe0}},             //panel timing state control
	{0xcb,14, {0x05,0x00,0x05,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x05,0x00,0x00}},

	{0x00,1, {0xf0}},             //panel timing state control
	{0xcb,11, {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff}},

//-------------------- panel pad mapping control --------------------//
	{0x00,1, {0x80}},             //panel pad mapping control
	{0xcc,15, {0x29,0x2a,0x0a,0x0c,0x0e,0x10,0x12,0x14,0x06,0x00,0x08,0x00,0x00,0x00,0x00}},

	{0x00,1, {0x90}},             //panel pad mapping control
	{0xcc,15, {0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x29,0x2a,0x09,0x0b,0x0d,0x0f,0x11,0x13}},

	{0x00,1, {0xa0}},             //panel pad mapping control
	{0xcc,14, {0x05,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00}},

  	{0x00,1, {0xb0}},             //panel pad mapping control
	{0xcc,15, {0x29,0x2a,0x13,0x11,0x0f,0x0d,0x0b,0x09,0x01,0x00,0x07,0x00,0x00,0x00,0x00}},

	{0x00,1, {0xc0}},             //panel pad mapping control
	{0xcc,15, {0x00,0x00,0x00,0x00,0x05,0x00,0x00,0x29,0x2a,0x14,0x12,0x10,0x0e,0x0c,0x0a}},

	{0x00,1, {0xd0}},             //panel pad mapping control
	{0xcc,14, {0x02,0x00,0x08,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x00,0x00}},

//-------------------- panel timing setting --------------------//
	{0x00,1, {0x80}},             //panel VST setting
	{0xce,12, {0x89,0x05,0x10,0x88,0x05,0x10,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1, {0x90}},             //panel VEND setting
	{0xce,14, {0x54,0xfc,0x10,0x54,0xfd,0x10,0x55,0x00,0x10,0x55,0x01,0x10,0x00,0x00}},

	{0x00,1, {0xa0}},             //panel CLKA1/2 setting
	{0xce,14, {0x58,0x07,0x04,0xfc,0x00,0x10,0x00,0x58,0x06,0x04,0xfd,0x00,0x10,0x00}},

	{0x00,1, {0xb0}},             //panel CLKA3/4 setting
	{0xce,14, {0x58,0x05,0x04,0xfe,0x00,0x10,0x00,0x58,0x04,0x04,0xff,0x00,0x10,0x00}},

	{0x00,1, {0xc0}},             //panel CLKb1/2 setting
	{0xce,14, {0x58,0x03,0x05,0x00,0x00,0x10,0x00,0x58,0x02,0x05,0x01,0x00,0x10,0x00}},

	{0x00,1, {0xd0}},             //panel CLKb3/4 setting
	{0xce,14, {0x58,0x01,0x05,0x02,0x00,0x10,0x00,0x58,0x00,0x05,0x03,0x00,0x10,0x00}},

	{0x00,1, {0x80}},             //panel CLKc1/2 setting
	{0xcf,14, {0x50,0x00,0x05,0x04,0x00,0x10,0x00,0x50,0x01,0x05,0x05,0x00,0x10,0x00}},

	{0x00,1, {0x90}},             //panel CLKc3/4 setting
	{0xcf,14, {0x50,0x02,0x05,0x06,0x00,0x10,0x00,0x50,0x03,0x05,0x07,0x00,0x10,0x00}},

	{0x00,1, {0xa0}},             //panel CLKd1/2 setting
	{0xcf,14, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1, {0xb0}},             //panel CLKd3/4 setting
	{0xcf,14, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},

	{0x00,1, {0xc0}},             //panel ECLK setting
	{0xcf,11, {0x39,0x39,0x20,0x20,0x00,0x00,0x01,0x01,0x20,0x00,0x00}},

	{0x00,1, {0xb5}},
	{0xc5,6, {0x0b,0x95,0xff,0x0b,0x95,0xff}},
	
//-------------------- Gamma --------------------//	
	{0x00,1, {0x00}},			//G2.2+      
	{0xE1,20, {0x1C,0x30,0x3c,0x46,0x56,0x62,0x62,0x88,0x76,0x8e,0x76,0x61,0x74,0x51,0x4f,0x42,0x35,0x29,0x21,0x1F}},

	{0x00,1, {0x00}},		        //G2.2-      
	{0xE2,20, {0x1C,0x2f,0x3b,0x47,0x55,0x61,0x62,0x89,0x77,0x8F,0x76,0x62,0x75,0x51,0x4f,0x41,0x34,0x28,0x22,0x1F}},


	{0x00,1, {0x00}},             //Orise mode disable
	{0xff,3, {0xff,0xff,0xff}},
};

static struct LCM_setting_table lcm_set_window[] = {
	{0x2A,	4,	{0x00, 0x00, (FRAME_WIDTH>>8), (FRAME_WIDTH&0xFF)}},
	{0x2B,	4,	{0x00, 0x00, (FRAME_HEIGHT>>8), (FRAME_HEIGHT&0xFF)}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_sleep_out_setting[] = {
    // Sleep Out
	{0x11, 1, {0x00}},
  {REGFLAG_DELAY, 120, {}},

    // Display ON
	{0x29, 1, {0x00}},
	{REGFLAG_DELAY, 10, {}},
	
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};


static struct LCM_setting_table lcm_sleep_in_setting[] = {
	// Display off sequence
	{0x28, 1, {0x00}},
	{REGFLAG_DELAY, 10, {}},

    // Sleep Mode On
	{0x10, 1, {0x00}},
	{REGFLAG_DELAY, 120, {}},

	{REGFLAG_END_OF_TABLE, 0x00, {}}
};
static void push_table(struct LCM_setting_table *table, unsigned int count, unsigned char force_update)
{
	unsigned int i;
	LCM_PRINT("%s %d\n", __func__,__LINE__);
    for(i = 0; i < count; i++) {
		
        unsigned cmd;
        cmd = table[i].cmd;
		
        switch (cmd) {
			
            case REGFLAG_DELAY :
                MDELAY(table[i].count);
                break;
				
            case REGFLAG_END_OF_TABLE :
                break;
				
            default:
				dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
       	}
    }
	
}


// ---------------------------------------------------------------------------
//  LCM Driver Implementations
// ---------------------------------------------------------------------------

static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	LCM_PRINT("%s %d\n", __func__,__LINE__);
    memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}


static void lcm_get_params(LCM_PARAMS *params)
{
  	LCM_PRINT("junTang %s %d\n", __func__,__LINE__);
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type   = LCM_TYPE_DSI;

	params->width  = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	// enable tearing-free
	 //params->dbi.te_mode 				= LCM_DBI_TE_MODE_VSYNC_ONLY;
	 //params->dbi.te_edge_polarity		= LCM_POLARITY_RISING;

#if (LCM_DSI_CMD_MODE)
	params->dsi.mode   = CMD_MODE;
#else
	params->dsi.mode   = BURST_VDO_MODE;
//	params->dsi.mode   = SYNC_PULSE_VDO_MODE;	
#endif
	
	// DSI
	/* Command mode setting */
#ifdef USE_LCM_THREE_LANE
	params->dsi.LANE_NUM				= LCM_THREE_LANE; 
#else
	params->dsi.LANE_NUM				= LCM_FOUR_LANE;
#endif
	//The following defined the fomat for data coming from LCD engine.
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq   = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding     = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format      = LCM_DSI_FORMAT_RGB888;

	// Highly depends on LCD driver capability.
	// Not support in MT6573
	params->dsi.packet_size=256;

	// Video mode setting		
	params->dsi.intermediat_buffer_num = 0;

	params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active				= 8;//2;//2;
	params->dsi.vertical_backporch					= 14;//14;
	params->dsi.vertical_frontporch					= 16;
	params->dsi.vertical_active_line				= FRAME_HEIGHT; 

	params->dsi.horizontal_sync_active				= 8;//2;//2;
	params->dsi.horizontal_backporch				= 74;//42;//44;//42;
	params->dsi.horizontal_frontporch				= 74;//44;
	params->dsi.horizontal_active_pixel				= FRAME_WIDTH;
#ifdef USE_LCM_THREE_LANE
    params->dsi.PLL_CLOCK = 250;//156;
#else
		params->dsi.PLL_CLOCK = 208;//156;
#endif
	
#if 1
    params->dsi.esd_check_enable =0;
	params->dsi.customization_esd_check_enable =1;
	params->dsi.lcm_esd_check_table[0].cmd = 0xAC;
	params->dsi.lcm_esd_check_table[0].count =1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x00;
#else
	params->dsi.cont_clock=1;
	params->dsi.clk_lp_per_line_enable = 0;
	params->dsi.esd_check_enable = 0;
	params->dsi.customization_esd_check_enable = 0;
	params->dsi.lcm_esd_check_table[0].cmd          = 0x53;
	params->dsi.lcm_esd_check_table[0].count        = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x24;
#endif
	params->dsi.ssc_disable = 1;
//	params->dsi.clk_lp_per_line_enable=1;
	// Bit rate calculation
	// params->dsi.pll_div1=0;		// fref=26MHz, fvco=fref*(div1+1)	(div1=0~63, fvco=500MHZ~1GHz)
	// params->dsi.pll_div2=1; 		// div2=0~15: fout=fvo/(2*div2)
	// params->dsi.fbk_div =11;    //fref=26MHz, fvco=fref*(fbk_div+1)*2/(div1_real*div2_real)

}
	


static unsigned int lcm_compare_id(void)
{
	unsigned int id = 0;
	unsigned char buffer[2];
	unsigned int array[16];
	int gpio;
	
	LCM_PRINT("%s %d\n", __func__,__LINE__);
	/*
	SET_GPIO_OUT(GPIO_LCM_RST_PIN,1); //NOTE:should reset LCM firstly
	MDELAY(10);
	SET_GPIO_OUT(GPIO_LCM_RST_PIN,0); 
	MDELAY(20);
	SET_GPIO_OUT(GPIO_LCM_RST_PIN,1); 
	MDELAY(150);
      */
  
  SET_RESET_PIN(1);
	MDELAY(10);
    SET_RESET_PIN(0);
	MDELAY(20);//50
    SET_RESET_PIN(1);
	MDELAY(100);//100
	
//	push_table(lcm_compare_id_setting, sizeof(lcm_compare_id_setting) / sizeof(struct LCM_setting_table), 1);

	array[0] = 0x00023700;// read id return two byte,version and id
	dsi_set_cmdq(array, 1, 1);
	MDELAY(20);
	read_reg_v2(0xA1, buffer, 2);
	
	gpio = mt_get_gpio_in(GPIO_DISP_ID0_PIN);

	id = buffer[1]; //we only need ID
	LCM_PRINT("%s,  id otm1287A= 0x%08x\n", __func__, buffer[0]);
	LCM_PRINT("%s,  id otm1287A= 0x%08x\n", __func__, buffer[1]);
	LCM_PRINT("%s,  gpio= %d\n", __func__, gpio);
	
	return ((LCM_ID_OTM1287A == id) && (gpio == 1))?1:0;
}

static void lcm_init(void)
{
	unsigned int data_array[16];
	LCM_PRINT(" %s %d\n", __func__,__LINE__);
#if 1
	SET_RESET_PIN(1);
	MDELAY(10);
    SET_RESET_PIN(0);
	MDELAY(20);//50
    SET_RESET_PIN(1);
	MDELAY(100);//100
#endif
  //lcm_compare_id();
	//lcm_init_registers();
	//data_array[0] = 0x00352500;
	//dsi_set_cmdq(&data_array, 1, 1);
	push_table(lcm_initialization_setting,sizeof(lcm_initialization_setting)/sizeof(lcm_initialization_setting[0]),1);
	push_table(lcm_sleep_out_setting, sizeof(lcm_sleep_out_setting) / sizeof(struct LCM_setting_table), 1);
}


static void lcm_suspend(void)
{
//	LCM_PRINT("%s %d\n", __func__,__LINE__);
//	SET_GPIO_OUT(GPIO_LCM_RST_PIN,1); 	
//	MDELAY(1);	
//	SET_GPIO_OUT(GPIO_LCM_RST_PIN,1); 
    push_table(lcm_sleep_in_setting, sizeof(lcm_sleep_in_setting) / sizeof(struct LCM_setting_table), 1);
	SET_RESET_PIN(1);
	MDELAY(20);//50
    SET_RESET_PIN(0);
	MDELAY(20);//50
    SET_RESET_PIN(1);
	MDELAY(100);//100
//	push_table(lcm_sleep_in_setting, sizeof(lcm_sleep_in_setting) / sizeof(struct LCM_setting_table), 1);
    
//	SET_GPIO_OUT(GPIO_LCM_PWR_EN,0);//Disable LCM Power
}



static void lcm_resume(void)
{
//	LCM_PRINT("%s %d\n", __func__,__LINE__);
//	SET_GPIO_OUT(GPIO_LCM_RST_PIN,1);  //Enable LCM Power
	lcm_init();
//	push_table(lcm_sleep_out_setting, sizeof(lcm_sleep_out_setting) / sizeof(struct LCM_setting_table), 1);
}

static void lcm_update(unsigned int x, unsigned int y,
                       unsigned int width, unsigned int height)
{
	LCM_PRINT("%s %d\n", __func__,__LINE__);
	unsigned int x0 = x;
	unsigned int y0 = y;
	unsigned int x1 = x0 + width - 1;
	unsigned int y1 = y0 + height - 1;

	unsigned char x0_MSB = ((x0>>8)&0xFF);
	unsigned char x0_LSB = (x0&0xFF);
	unsigned char x1_MSB = ((x1>>8)&0xFF);
	unsigned char x1_LSB = (x1&0xFF);
	unsigned char y0_MSB = ((y0>>8)&0xFF);
	unsigned char y0_LSB = (y0&0xFF);
	unsigned char y1_MSB = ((y1>>8)&0xFF);
	unsigned char y1_LSB = (y1&0xFF);

	unsigned int data_array[16];

	data_array[0]= 0x00053902;
	data_array[1]= (x1_MSB<<24)|(x0_LSB<<16)|(x0_MSB<<8)|0x2a;
	data_array[2]= (x1_LSB);
	data_array[3]= 0x00000000;
	data_array[4]= 0x00053902;
	data_array[5]= (y1_MSB<<24)|(y0_LSB<<16)|(y0_MSB<<8)|0x2b;
	data_array[6]= (y1_LSB);
	data_array[7]= 0x00000000;
	data_array[8]= 0x002c3909;

	dsi_set_cmdq(&data_array, 9, 0);

}

LCM_DRIVER otm1287a_720p_dsi_dj_lcm_drv = 
{
    .name			= "otm1287a_720p_dsi_dj",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params     = lcm_get_params,
	.init           = lcm_init,
	.suspend        = lcm_suspend,
	.resume         = lcm_resume,
	.compare_id     = lcm_compare_id,
#if (LCM_DSI_CMD_MODE)
	.set_backlight	= lcm_setbacklight,
    .update         = lcm_update,
#endif
};