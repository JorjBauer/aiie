#ifndef _RA8875_REGISTERS_H
#define _RA8875_REGISTERS_H

	#define CENTER 				9998
	#define ARC_ANGLE_MAX 		360		
	#define ARC_ANGLE_OFFSET 	-90	
	#define ANGLE_OFFSET		-90
#define RA8875_PWRR             		0x01//Power and Display Control Register
	#define RA8875_PWRR_DISPON      0x80
	#define RA8875_PWRR_DISPOFF     0x00
	#define RA8875_PWRR_SLEEP       0x02
	#define RA8875_PWRR_NORMAL      0x00
	#define RA8875_PWRR_SOFTRESET   0x01
#define RA8875_MRWC             		0x02//Memory Read/Write Command
	#define RA8875_CMDWRITE         	0x80
	#define RA8875_CMDREAD          	0xC0
	#define RA8875_DATAWRITE        	0x00
	#define RA8875_DATAREAD         	0x40
	#define RA8875_STATREG				0x40
#define RA8875_PCSR             	  0x04//Pixel Clock Setting Register
	#define RA8875_SROC         		  0x05//Serial Flash/ROM Configuration
#define RA8875_SFCLR         		  0x06//Serial Flash/ROM CLK
	#define EXTROM_SFCLSPEED	0b00000011// /4 0b00000010 /2
#define RA8875_SYSR             	  0x10//System Configuration Register
#define RA8875_HDWR             	  0x14//LCD Horizontal Display Width Register
#define RA8875_HNDFTR           	  0x15//Horizontal Non-Display Period Fine Tuning Option Register
#define RA8875_HNDR             	  0x16//LCD Horizontal Non-Display Period Register
#define RA8875_HSTR             	  0x17//HSYNC Start Position Register
#define RA8875_HPWR             	  0x18//HSYNC Pulse Width Register
#define RA8875_VDHR0            	  0x19//LCD Vertical Display Height Register 0
//#define RA8875_VDHR1            	  0x1A//LCD Vertical Display Height Register 1
#define RA8875_VNDR0            	  0x1B//LCD Vertical Non-Display Period Register 0
//#define RA8875_VNDR1            	  0x1C//LCD Vertical Non-Display Period Register 1
#define RA8875_VSTR0            	  0x1D//VSYNC Start Position Register 0
//#define RA8875_VSTR1            	  0x1E//VSYNC Start Position Register 1
#define RA8875_VPWR             	  0x1F//VSYNC Pulse Width Register
#define RA8875_DPCR				  	  0x20//Display Configuration Register
#define RA8875_FNCR0				  0x21//Font Control Register 0
#define RA8875_FNCR1				  0x22//Font Control Register 1
#define RA8875_CGSR				      0x23//CGRAM Select Register
#define RA8875_HOFS0				  0x24//Horizontal Scroll Offset Register 0
#define RA8875_HOFS1				  0x25//Horizontal Scroll Offset Register 1
#define RA8875_VOFS0				  0x26//Vertical Scroll Offset Register 0
#define RA8875_VOFS1				  0x27//Vertical Scroll Offset Register 1
#define RA8875_FLDR				  	  0x29//Font Line Distance Setting Register
#define RA8875_F_CURXL				  0x2A//Font Write Cursor Horizontal Position Register 0
#define RA8875_F_CURXH				  0x2B//Font Write Cursor Horizontal Position Register 1
#define RA8875_F_CURYL				  0x2C//Font Write Cursor Vertical Position Register 0
#define RA8875_F_CURYH				  0x2D//Font Write Cursor Vertical Position Register 1
#define RA8875_FWTSET         		  0x2E//Font Write Type Setting Register
#define RA8875_SFRSET         		  0x2F//Serial Font ROM Setting
#define RA8875_HSAW0            	  0x30//Horizontal Start Point 0 of Active Window
//#define RA8875_HSAW1            	  0x31//Horizontal Start Point 1 of Active Window
#define RA8875_VSAW0            	  0x32//Vertical   Start Point 0 of Active Window
//#define RA8875_VSAW1            	  0x33//Vertical   Start Point 1 of Active Window
#define RA8875_HEAW0            	  0x34//Horizontal End   Point 0 of Active Window
//#define RA8875_HEAW1            	  0x35//Horizontal End   Point 1 of Active Window
#define RA8875_VEAW0           		  0x36//Vertical   End   Point of Active Window 0
//#define RA8875_VEAW1            	  0x37//Vertical   End   Point of Active Window 1
#define RA8875_HSSW0            	  0x38//Horizontal Start Point 0 of Scroll Window
//#define RA8875_HSSW1            	  0x39//Horizontal Start Point 1 of Scroll Window
#define RA8875_VSSW0            	  0x3A//Vertical 	 Start Point 0 of Scroll Window
//#define RA8875_VSSW1            	  0x3B//Vertical 	 Start Point 1 of Scroll Window
#define RA8875_HESW0            	  0x3C//Horizontal End   Point 0 of Scroll Window
//#define RA8875_HESW1            	  0x3D//Horizontal End   Point 1 of Scroll Window
#define RA8875_VESW0            	  0x3E//Vertical 	 End   Point 0 of Scroll Window
//#define RA8875_VESW1            	  0x3F//Vertical 	 End   Point 1 of Scroll Window
#define RA8875_MWCR0            	  0x40//Memory Write Control Register 0
#define RA8875_MWCR1            	  0x41//Memory Write Control Register 1
#define RA8875_BTCR            	  	  0x44//Blink Time Control Register
#define RA8875_MRCD            	  	  0x45//Memory Read Cursor Direction
#define RA8875_CURH0            	  0x46//Memory Write Cursor Horizontal Position Register 0
//#define RA8875_CURH1            	  0x47//Memory Write Cursor Horizontal Position Register 1
#define RA8875_CURV0            	  0x48//Memory Write Cursor Vertical Position Register 0
//#define RA8875_CURV1            	  0x49//Memory Write Cursor Vertical Position Register 1
//#define RA8875_RCURH0           	  0x4A//Memory Read Cursor Horizontal Position Register 0
//#define RA8875_RCURH1           	  0x4B//Memory Read Cursor Horizontal Position Register 1
//#define RA8875_RCURV0           	  0x4C//Memory Read Cursor Vertical Position Register 0
//#define RA8875_RCURV1           	  0x4D//Memory Read Cursor Vertical Position Register 1
#define RA8875_CURHS            	  0x4E//Font Write Cursor and Memory Write Cursor Horizontal Size Register
#define RA8875_CURVS            	  0x4F//Font Write Cursor Vertical Size Register
#define RA8875_BECR0            	  0x50//BTE Function Control Register 0
#define RA8875_BECR1            	  0x51//BTE Function Control Register 1
#define RA8875_LTPR0            	  0x52//Layer Transparency Register 0
#define RA8875_LTPR1            	  0x53//Layer Transparency Register 1
#define RA8875_HSBE0				  0x54//Horizontal Source Point 0 of BTE
//#define RA8875_HSBE1				  0x55//Horizontal Source Point 1 of BTE
#define RA8875_VSBE0				  0x56//Vertical Source Point 0 of BTE
//#define RA8875_VSBE1				  0x57//Vertical Source Point 1 of BTE
#define RA8875_HDBE0				  0x58//Horizontal Destination Point 0 of BTE
//#define RA8875_HDBE1				  0x59//Horizontal Destination Point 1 of BTE
#define RA8875_VDBE0				  0x5A//Vertical Destination Point 0 of BTE
//#define RA8875_VDBE1				  0x5B//Vertical Destination Point 1 of BTE
#define RA8875_BEWR0				  0x5C//BTE Width Register 0
//#define RA8875_BEWR1				  0x5D//BTE Width Register 1
#define RA8875_BEHR0				  0x5E//BTE Height Register 0
//#define RA8875_BEHR1				  0x5F//BTE Height Register 1
#define RA8875_PTNO				  	  0x66//Pattern Set No for BTE
#define RA8875_BTEROP_SOURCE	0xC0	//Overwrite dest with source (no mixing) *****THIS IS THE DEFAULT OPTION****
#define RA8875_BTEROP_BLACK		0xo0	//all black
#define RA8875_BTEROP_WHITE		0xf0	//all white
#define RA8875_BTEROP_DEST		0xA0    //destination unchanged
#define RA8875_BTEROP_ADD		0xE0    //ADD (brighter)
#define RA8875_BTEROP_SUBTRACT	0x20	//SUBTRACT (darker)
#define RA8875_BGCR0				  0x60//Background Color Register 0 (R)
//#define RA8875_BGCR1				  0x61//Background Color Register 1 (G)
//#define RA8875_BGCR2				  0x62//Background Color Register 2 (B)
#define RA8875_FGCR0				  0x63//Foreground Color Register 0 (R)
//#define RA8875_FGCR1				  0x64//Foreground Color Register 1 (G)
//#define RA8875_FGCR2				  0x65//Foreground Color Register 2 (B)
#define RA8875_BGTR0				  0x67//Background Color Register for Transparent 0 (R)
//#define RA8875_BGTR1				  0x68//Background Color Register for Transparent 1 (G)
//#define RA8875_BGTR2				  0x69//Background Color Register for Transparent 2 (B)
#define RA8875_TPCR0                  0x70//Touch Panel Control Register 0
	//#define RA8875_TPCR0_ENABLE           0x80
	//#define RA8875_TPCR0_DISABLE          0x00
	#define RA8875_TPCR0_WAIT_512CLK      0x00
	#define RA8875_TPCR0_WAIT_1024CLK     0x10
	#define RA8875_TPCR0_WAIT_2048CLK     0x20
	#define RA8875_TPCR0_WAIT_4096CLK     0x30
	#define RA8875_TPCR0_WAIT_8192CLK     0x40
	#define RA8875_TPCR0_WAIT_16384CLK    0x50
	#define RA8875_TPCR0_WAIT_32768CLK    0x60
	#define RA8875_TPCR0_WAIT_65536CLK    0x70
	#define RA8875_TPCR0_WAKEENABLE       0x08
	#define RA8875_TPCR0_WAKEDISABLE      0x00
	#define RA8875_TPCR0_ADCCLK_DIV1      0x00
	#define RA8875_TPCR0_ADCCLK_DIV2      0x01
	#define RA8875_TPCR0_ADCCLK_DIV4      0x02
	#define RA8875_TPCR0_ADCCLK_DIV8      0x03
	#define RA8875_TPCR0_ADCCLK_DIV16     0x04
	#define RA8875_TPCR0_ADCCLK_DIV32     0x05
	#define RA8875_TPCR0_ADCCLK_DIV64     0x06
	#define RA8875_TPCR0_ADCCLK_DIV128    0x07
#define RA8875_TPCR1            	  0x71//Touch Panel Control Register 1
	#define RA8875_TPCR1_AUTO       0x00
	#define RA8875_TPCR1_MANUAL     0x40
	#define RA8875_TPCR1_VREFINT    0x00
	#define RA8875_TPCR1_VREFEXT    0x20
	#define RA8875_TPCR1_DEBOUNCE   0x04
	#define RA8875_TPCR1_NODEBOUNCE 0x00
	#define RA8875_TPCR1_IDLE       0x00
	#define RA8875_TPCR1_WAIT       0x01
	#define RA8875_TPCR1_LATCHX     0x02
	#define RA8875_TPCR1_LATCHY     0x03
#define RA8875_TPXH             	  0x72//Touch Panel X High Byte Data Register
#define RA8875_TPYH             	  0x73//Touch Panel Y High Byte Data Register
#define RA8875_TPXYL            	  0x74//Touch Panel X/Y Low Byte Data Register
//#define RA8875_GCHP0            	  0x80//Graphic Cursor Horizontal Position Register 0
//#define RA8875_GCHP1            	  0x81//Graphic Cursor Horizontal Position Register 1
//#define RA8875_GCVP0            	  0x82//Graphic Cursor Vertical Position Register 0
//#define RA8875_GCVP1            	  0x83//Graphic Cursor Vertical Position Register 0
//#define RA8875_GCC0            	      0x84//Graphic Cursor Color 0
//#define RA8875_GCC1            	      0x85//Graphic Cursor Color 1
#define RA8875_PLLC1            	  0x88//PLL Control Register 1
//#define RA8875_PLLC2            	  0x89//PLL Control Register 2
#define RA8875_P1CR             	  0x8A//PWM1 Control Register
#define RA8875_P1DCR            	  0x8B//PWM1 Duty Cycle Register
#define RA8875_P2CR             	  0x8C//PWM2 Control Register
#define RA8875_P2DCR            	  0x8D//PWM2 Control Register
	#define RA8875_PxCR_ENABLE      0x80
	#define RA8875_PxCR_DISABLE     0x00
	#define RA8875_PxCR_CLKOUT      0x10
	#define RA8875_PxCR_PWMOUT      0x00
 	#define RA8875_PWM_CLK_DIV1     0x00
	#define RA8875_PWM_CLK_DIV2     0x01
	#define RA8875_PWM_CLK_DIV4     0x02
	#define RA8875_PWM_CLK_DIV8     0x03
	#define RA8875_PWM_CLK_DIV16    0x04
	#define RA8875_PWM_CLK_DIV32    0x05
	#define RA8875_PWM_CLK_DIV64    0x06
	#define RA8875_PWM_CLK_DIV128   0x07
	#define RA8875_PWM_CLK_DIV256   0x08
	#define RA8875_PWM_CLK_DIV512   0x09
	#define RA8875_PWM_CLK_DIV1024  0x0A
	#define RA8875_PWM_CLK_DIV2048  0x0B
	#define RA8875_PWM_CLK_DIV4096  0x0C
	#define RA8875_PWM_CLK_DIV8192  0x0D
	#define RA8875_PWM_CLK_DIV16384 0x0E
	#define RA8875_PWM_CLK_DIV32768 0x0F 
#define RA8875_MCLR             	  0x8E//Memory Clear Control Register
#define RA8875_DCR                    0x90//Draw Line/Circle/Square Control Register
	#define RA8875_DCR_LINESQUTRI_START   0x80
	#define RA8875_DCR_LINESQUTRI_STOP    0x00
	#define RA8875_DCR_LINESQUTRI_STATUS  0x80
	#define RA8875_DCR_CIRCLE_START       0x40
	#define RA8875_DCR_CIRCLE_STATUS      0x40
	#define RA8875_DCR_CIRCLE_STOP        0x00
	#define RA8875_DCR_FILL               0x20
	#define RA8875_DCR_NOFILL             0x00
	#define RA8875_DCR_DRAWLINE           0x00
	#define RA8875_DCR_DRAWTRIANGLE       0x01
	#define RA8875_DCR_DRAWSQUARE         0x10
#define RA8875_DLHSR0         		  0x91//Draw Line/Square Horizontal Start Address Register0
//#define RA8875_DLHSR1         		  0x92//Draw Line/Square Horizontal Start Address Register1
#define RA8875_DLVSR0         		  0x93//Draw Line/Square Vertical Start Address Register0
//#define RA8875_DLVSR1         		  0x94//Draw Line/Square Vertical Start Address Register1
#define RA8875_DLHER0         		  0x95//Draw Line/Square Horizontal End Address Register0
//#define RA8875_DLHER1         		  0x96//Draw Line/Square Horizontal End Address Register1
#define RA8875_DLVER0         		  0x97//Draw Line/Square Vertical End Address Register0
//#define RA8875_DLVER1         		  0x98//Draw Line/Square Vertical End Address Register0
#define RA8875_DCHR0         		  0x99//Draw Circle Center Horizontal Address Register0
//#define RA8875_DCHR1         		  0x9A//Draw Circle Center Horizontal Address Register1
#define RA8875_DCVR0         		  0x9B//Draw Circle Center Vertical Address Register0
//#define RA8875_DCVR1         		  0x9C//Draw Circle Center Vertical Address Register1
#define RA8875_DCRR         		  0x9D//Draw Circle Radius Register
#define RA8875_ELLIPSE                0xA0//Draw Ellipse/Ellipse Curve/Circle Square Control Register
	#define RA8875_ELLIPSE_STATUS         0x80
#define RA8875_ELL_A0         		  0xA1//Draw Ellipse/Circle Square Long axis Setting Register0
//#define RA8875_ELL_A1         		  0xA2//Draw Ellipse/Circle Square Long axis Setting Register1
#define RA8875_ELL_B0         		  0xA3//Draw Ellipse/Circle Square Short axis Setting Register0
//#define RA8875_ELL_B1         		  0xA4//Draw Ellipse/Circle Square Short axis Setting Register1
#define RA8875_DEHR0         		  0xA5//Draw Ellipse/Circle Square Center Horizontal Address Register0
//#define RA8875_DEHR1         		  0xA6//Draw Ellipse/Circle Square Center Horizontal Address Register1
#define RA8875_DEVR0         		  0xA7//Draw Ellipse/Circle Square Center Vertical Address Register0
//#define RA8875_DEVR1         		  0xA8//Draw Ellipse/Circle Square Center Vertical Address Register1
#define RA8875_DTPH0         		  0xA9//Draw Triangle Point 2 Horizontal Address Register0
//#define RA8875_DTPH1         		  0xAA//Draw Triangle Point 2 Horizontal Address Register1
#define RA8875_DTPV0         		  0xAB//Draw Triangle Point 2 Vertical Address Register0
//#define RA8875_DTPV1         		  0xAC//Draw Triangle Point 2 Vertical Address Register1
#define RA8875_SSAR0				  0xB0//Source Starting Address REG 0
//#define RA8875_SSAR1				  0xB1//Source Starting Address REG 1
//#define RA8875_SSAR2				  0xB2//Source Starting Address REG 2
//#define RA8875_????					0xB3//???????????
#define RA8875_DTNR0				  0xB4//Block Width REG 0(BWR0) / DMA Transfer Number REG 0
#define RA8875_BWR1					  0xB5//Block Width REG 1
#define RA8875_DTNR1				  0xB6//Block Height REG 0(BHR0) /DMA Transfer Number REG 1
#define RA8875_BHR1					  0xB7//Block Height REG 1
#define RA8875_DTNR2				  0xB8//Source Picture Width REG 0(SPWR0) / DMA Transfer Number REG 2
#define RA8875_SPWR1				  0xB9//Source Picture Width REG 1
#define RA8875_DMACR				  0xBF//DMA Configuration REG
#define RA8875_GPIOX            	  0xC7
#define RA8875_KSCR1            	  0xC0 //Key-Scan Control Register 1 (KSCR1)
#define RA8875_KSCR2            	  0xC1 //Key-Scan Controller Register 2 (KSCR2)
#define RA8875_KSDR0            	  0xC2 //Key-Scan Data Register (KSDR0)
#define RA8875_KSDR1            	  0xC3 //Key-Scan Data Register (KSDR1)
#define RA8875_KSDR2            	  0xC4 //Key-Scan Data Register (KSDR2)
#define RA8875_INTC1            	  0xF0//Interrupt Control Register1
#define RA8875_INTC2            	  0xF1//Interrupt Control Register2
	#define RA8875_INTCx_KEY        	  0x10
	#define RA8875_INTCx_DMA        	  0x08
	#define RA8875_INTCx_TP         	  0x04
	#define RA8875_INTCx_BTE        	  0x02
	#define RA8875_ENABLE_INT_TP        ((uint8_t)(1<<2)) 
	#define RA8875_DISABLE_INT_TP       ((uint8_t)(0<<2)) 
    #define TP_ENABLE   				((uint8_t)(1<<7))
    #define TP_DISABLE  				((uint8_t)(0<<7))
    #define TP_MODE_AUTO    			((uint8_t)(0<<6))   
    #define TP_MODE_MANUAL  			((uint8_t)(1<<6))
    #define TP_DEBOUNCE_OFF 			((uint8_t)(0<<2))
    #define TP_DEBOUNCE_ON  			((uint8_t)(1<<2))
    #define TP_ADC_CLKDIV_1             0
    #define TP_ADC_CLKDIV_2             1        
    #define TP_ADC_CLKDIV_4             2        
    #define TP_ADC_CLKDIV_8             3      
    #define TP_ADC_CLKDIV_16            4        
    #define TP_ADC_CLKDIV_32            5        
    #define TP_ADC_CLKDIV_64            6        
    #define TP_ADC_CLKDIV_128           7
    #define TP_ADC_SAMPLE_512_CLKS     ((uint8_t)(0<<4))
    #define TP_ADC_SAMPLE_1024_CLKS    ((uint8_t)(1<<4))
    #define TP_ADC_SAMPLE_2048_CLKS    ((uint8_t)(2<<4))
    #define TP_ADC_SAMPLE_4096_CLKS    ((uint8_t)(3<<4))
    #define TP_ADC_SAMPLE_8192_CLKS    ((uint8_t)(4<<4))
    #define TP_ADC_SAMPLE_16384_CLKS   ((uint8_t)(5<<4))
    #define TP_ADC_SAMPLE_32768_CLKS   ((uint8_t)(6<<4))
    #define TP_ADC_SAMPLE_65536_CLKS   ((uint8_t)(7<<4))

#endif
