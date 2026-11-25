/****************************************************************
 *		TLCS-900 C Compiler Header File			*
 *--------------------------------------------------------------*
 *	[For]		{TMP95C061F}				*
 *	[File name]	{IO900H.H}				*
 *	[Subject]	{Special Function Registers}		*
 *  Copyright(C) 1994-96 TOSHIBA CORPORATION All rights reserved   *
 ****************************************************************/

/***[0x00]*********************************************************************/
/*---undefined---*/			/* 0x00: */
#pragma io	IOP1		0x01	/* 0x01: Port1 */
/*---undefined---*/			/* 0x02: */
/*---undefined---*/			/* 0x03: */
#pragma io	P1CR		0x04	/* 0x04: Port1 control */
#pragma io	P1FC		0x05	/* 0x05: Port1 function */
#pragma io	IOP2		0x06	/* 0x06: Port2 */
/*---undefined---*/			/* 0x07: */
/*---undefined---*/			/* 0x08: */
#pragma io	P2FC		0x09	/* 0x09: Port2 function */
/*---undefined---*/			/* 0x0a: */
/*---undefined---*/			/* 0x0b: */
/*---undefined---*/			/* 0x0c: */
#pragma io	IOP5		0x0d	/* 0x0d: Port5 */
/*---undefined---*/			/* 0x0e: */
/*---undefined---*/			/* 0x0f: */

/***[0x10]*********************************************************************/
#pragma io	P5CR		0x10	/* 0x10: Port5 control */
#pragma io	P5FC		0x11	/* 0x11: Port5 function */
#pragma io	IOP6		0x12	/* 0x12: Port6 */
#pragma io	IOP7		0x13	/* 0x13: Port7 */
/*---undefined---*/			/* 0x14: */
#pragma io	P6FC		0x15	/* 0x15: Port6 function */
#pragma io	P7CR		0x16	/* 0x16: Port7 control */
#pragma io	P7FC		0x17	/* 0x17: Port7 function */
#pragma io	IOP8		0x18	/* 0x18: Port8 */
#pragma io	IOP9		0x19	/* 0x19: Port9 */
#pragma io	P8CR		0x1a	/* 0x1a: Port8 control */
#pragma io	P8FC		0x1b	/* 0x1b: Port8 function */
/*---undefined---*/			/* 0x1c: */
/*---undefined---*/			/* 0x1d: */
#pragma io	IOPA		0x1e	/* 0x1e: PortA */
#pragma io	IOPB		0x1f	/* 0x1f: PortB */

/***[0x20]*********************************************************************/
#pragma io	TRUN		0x20	/* 0x20: Timer run */
/*---undefined---*/			/* 0x21: */
#pragma io	TREG0		0x22	/* 0x22: 8bit timer register 0 */
#pragma io	TREG1		0x23	/* 0x23: 8bit timer register 1 */
#pragma io	T01MOD		0x24	/* 0x24: Timer 0,1 mode control */
#pragma io	TFFCR		0x25	/* 0x25: Timer F/F control */
#pragma io	TREG2		0x26	/* 0x26: PWM0 timer register */
#pragma io	TREG3		0x27	/* 0x27: PWM1 timer register */
#pragma io	T23MOD		0x28	/* 0x28: Timer 2,3 mode control */
#pragma io	TRDC		0x29	/* 0x29: Timer Double Buffer control */
/*---undefined---*/			/* 0x2a: */
/*---undefined---*/			/* 0x2b: */
#pragma io	PACR		0x2c	/* 0x2c: PortA control */
#pragma io	PAFC		0x2d	/* 0x2d: PortA function */
#pragma io	PBCR		0x2e	/* 0x2e: PortB control */
#pragma io	PBFC		0x2f	/* 0x2f: PortB function */

/***[0x30]*********************************************************************/
#pragma io	TREG4		0x30	/* 0x30: 16bit timer register 4 */
/*#pragma io	TREG4L		0x30	   0x30: 16bit timer register 4[low] */
/*#pragma io	TREG4H		0x31	   0x31: 16nit timer register 4[high] */
#pragma io	TREG5		0x32	/* 0x32: 16bit timer register 5 */
/*#pragma io	TREG5L		0x32	   0x32: 16bit timer register 5[low] */
/*#pragma io	TREG5H		0x33	   0x33: 16bit timer register 5[high] */
#pragma io	CAP1		0x34	/* 0x34: Capture register 1 */
/*#pragma io	CAP1L		0x34	   0x34: Capture register 1[low] */
/*#pragma io	CAP1H		0x35	   0x35: Capture register 1[high] */
#pragma io	CAP2		0x36	/* 0x36: Capture register 2 */
/*#pragma io	CAP2L		0x36	   0x36: Capture register 2[low] */
/*#pragma io	CAP2H		0x37	   0x37: Capture register 2[high] */
#pragma io	T4MOD		0x38	/* 0x38: 16bit timer mode register 4 */
#pragma io	T4FFCR		0x39	/* 0x39: 16bit timer F/F4 control */
#pragma io	T45CR		0x3a	/* 0x3a: 16bit timer control */
/*---undefined---*/			/* 0x3b: */
#pragma io	MSAR0		0x3c	/* 0x3c: Memory start address 0 */
#pragma io	MAMR0		0x3d	/* 0x3d: Memory start address mask 0 */
#pragma io	MSAR1		0x3e	/* 0x3e: Memory start address 1 */
#pragma io	MAMR1		0x3f	/* 0x3f: Memory start address mask 1 */

/***[0x40]*********************************************************************/
#pragma io	TREG6		0x40	/* 0x40: 16bit timer register 6 */
/*#pragma io	TREG6L		0x40	   0x40: 16bit timer register 6[low] */
/*#pragma io	TREG6H		0x41	   0x41: 16bit timer register 6[high] */
#pragma io	TREG7		0x42	/* 0x42: 16bit timer register 7 */
/*#pragma io	TREG7L		0x42	   0x42: 16bit timer register 7[low] */
/*#pragma io	TREG7H		0x43	   0x43: 16bit timer register 7[high] */
#pragma io	CAP3		0x44	/* 0x44: Capture register 3 */
/*#pragma io	CAP3L		0x44	   0x44: Capture register 3[low] */
/*#pragma io	CAP3H		0x45	   0x45: Capture register 3[high] */
#pragma io	CAP4		0x46	/* 0x46: Capture register 4 */
/*#pragma io	CAP4L		0x46	   0x46: Capture register 4[low] */
/*#pragma io	CAP4H		0x47	   0x47: Capture register 4[high] */
#pragma io	T5MOD		0x48	/* 0x48: 16bit timer mode register 5 */
#pragma io	T5FFCR		0x49	/* 0x49: 16bit timer F/F5 control */
/*---undefined---*/			/* 0x4a: */
/*---undefined---*/			/* 0x4b: */
#pragma io	PG0REG		0x4c	/* 0x4c: Pattern gen0 register */
#pragma io	PG1REG		0x4d	/* 0x4d: Pattern gen1 register */
#pragma io	PG01CR		0x4e	/* 0x4e: Pattern gen01 control */
/*---undefined---*/			/* 0x4f: */

/***[0x50]*********************************************************************/
#pragma io	SC0BUF		0x50	/* 0x50: Serial channel0 buffer*/
#pragma io	SC0CR		0x51	/* 0x51: Serial channel0 control */
#pragma io	SC0MOD		0x52	/* 0x52: Serial channel0 mode */
#pragma io	BR0CR		0x53	/* 0x53: Serial channel0 baud rate */
#pragma io	SC1BUF		0x54	/* 0x54: Serial channel1 buffer */
#pragma io	SC1CR		0x55	/* 0x55: Serial channel1 control */
#pragma io	SC1MOD		0x56	/* 0x56: Serial channel1 mode */
#pragma io	BR1CR		0x57	/* 0x57: Serial channel1 baud rate */
#pragma io	ODE		0x58	/* 0x58: Serial out control */
/*---undefined---*/			/* 0x59: */
#pragma io	DREFCR		0x5a	/* 0x5a: Refresh control register */
#pragma io	DMEMCR		0x5b	/* 0x5b: Memory control register */
#pragma io	MSAR2		0x5c	/* 0x5c: Memory start address 2 */
#pragma io	MAMR2		0x5d	/* 0x5d: Memory start address mask 2 */
#pragma io	MSAR3		0x5e	/* 0x5e: Memory start address 3 */
#pragma io	MAMR3		0x5f	/* 0x5f: Memory start address mask 3 */

/***[0x60]*********************************************************************/
#pragma io	ADREG0		0x60	/* 0x60: A/D result register 0 */
/*#pragma io	ADREG0L		0x60	   0x60: A/D result register 0[low] */
/*#pragma io	ADREG0H		0x61	   0x61: A/D result register 0[high] */
#pragma io	ADREG1		0x62	/* 0x62: A/D result register 1 */
/*#pragma io	ADREG1L		0x62	   0x62: A/D result register 1[low] */
/*#pragma io	ADREG1H		0x63	   0x63: A/D result register 1[high] */
#pragma io	ADREG2		0x64	/* 0x64: A/D result register 2 */
/*#pragma io	ADREG2L		0x64	   0x64: A/D result register 2[low] */
/*#pragma io	ADREG2H		0x65	   0x65: A/D result register 2[high] */
#pragma io	ADREG3		0x66	/* 0x66: A/D result register 3 */
/*#pragma io	ADREG3L		0x66	   0x66: A/D result register 3[low] */
/*#pragma io	ADREG3H		0x67	   0x67: A/D result register 3[high] */
#pragma io	B0CS		0x68	/* 0x68: Programable CS0 control */
#pragma io	B1CS		0x69	/* 0x69: Programable CS1 control */
#pragma io	B2CS		0x6a	/* 0x6a: Programable CS2 control */
#pragma io	B3CS		0x6b	/* 0x6b: Programable CS3 control */
#pragma io	BEXCS		0x6c	/* 0x6c: External CS control */
#pragma io	ADMOD		0x6d	/* 0x6d: A/D converter mode */
#pragma io	WDMOD		0x6e	/* 0x6e: Watch dog mode register */
#pragma io	WDCR		0x6f	/* 0x6f: Watch dog control */

/***[0x70]*********************************************************************/
#pragma io	INTE0AD		0x70	/* 0x70: Interrupt enable 0 & A/D */
#pragma io	INTE45		0x71	/* 0x71: Interrupt enable 5/4 */
#pragma io	INTE67		0x72	/* 0x72: Interrupt enable 7/6 */
#pragma io	INTET01		0x73	/* 0x73: Interrupt enable timer 1/0 */
#pragma io	INTET23		0x74	/* 0x74: Interrupt enable timer 3/2 */
#pragma io	INTET45		0x75	/* 0x75: Interrupt enable timer 5/4 */
#pragma io	INTET67		0x76	/* 0x76: Interrupt enable timer 7/6 */
#pragma io	INTES0		0x77	/* 0x77: Interrupt enable serial 0 */
#pragma io	INTES1		0x78	/* 0x78: Interrupt enable serial 1 */
#pragma io	INTETC01	0x79	/* 0x79: Interrupt enable TC 0/1 */
#pragma io	INTETC23	0x7a	/* 0x7a: Interrupt enable TC 2/3 */
#pragma io	IIMC		0x7b	/* 0x7b: INT input mode control */
#pragma io	DMA0V		0x7c	/* 0x7c: DMA0 request vector */
#pragma io	DMA1V		0x7d	/* 0x7d: DMA1 request vector */
#pragma io	DMA2V		0x7e	/* 0x7e: DMA2 request vector */
#pragma io	DMA3V		0x7f	/* 0x7f: DMA3 request vector */

/***[0x00]*********************************************************************/
/*---undefined---*/			/* 0x00: */
extern  unsigned char	IOP1;		/* 0x01: Port1 */
/*---undefined---*/			/* 0x02: */
/*---undefined---*/			/* 0x03: */
extern  unsigned char	P1CR;		/* 0x04: Port1 control */
extern  unsigned char	P1FC;		/* 0x05: Port1 function */
extern  unsigned char	IOP2;		/* 0x06: Port2 */
/*---undefined---*/			/* 0x07: */
/*---undefined---*/			/* 0x08: */
extern  unsigned char	P2FC;		/* 0x09: Port2 function */
/*---undefined---*/			/* 0x0a: */
/*---undefined---*/			/* 0x0b: */
/*---undefined---*/			/* 0x0c: */
extern  unsigned char	IOP5;		/* 0x0d: Port5 */
/*---undefined---*/			/* 0x0e: */
/*---undefined---*/			/* 0x0f: */

/***[0x10]*********************************************************************/
extern  unsigned char	P5CR;		/* 0x10: Port5 control */
extern  unsigned char	P5FC;		/* 0x11: Port5 function */
extern  unsigned char	IOP6;		/* 0x12: Port6 */
extern  unsigned char	IOP7;		/* 0x13: Port7 */
/*---undefined---*/			/* 0x14: */
extern  unsigned char	P6FC;		/* 0x15: Port6 function */
extern  unsigned char	P7CR;		/* 0x16: Port7 control */
extern  unsigned char	P7FC;		/* 0x17: Port7 function */
extern  unsigned char	IOP8;		/* 0x18: Port8 */
extern  unsigned char	IOP9;		/* 0x19: Port9 */
extern  unsigned char	P8CR;		/* 0x1a: Port8 control */
extern  unsigned char	P8FC;		/* 0x1b: Port8 function */
/*---undefined---*/			/* 0x1c: */
/*---undefined---*/			/* 0x1d: */
extern  unsigned char	IOPA;		/* 0x1e: PortA */
extern  unsigned char	IOPB;		/* 0x1f: PortB */

/***[0x20]*********************************************************************/
extern  unsigned char	TRUN;		/* 0x20: Timer run */
/*---undefined---*/			/* 0x21: */
extern  unsigned char	TREG0;		/* 0x22: 8bit timer register 0 */
extern  unsigned char	TREG1;		/* 0x23: 8bit timer register 1 */
extern  unsigned char	T01MOD;		/* 0x24: Timer 0,1 mode control */
extern  unsigned char	TFFCR;		/* 0x25: Timer F/F control */
extern  unsigned char	TREG2;		/* 0x26: PWM0 timer register */
extern  unsigned char	TREG3;		/* 0x27: PWM1 timer register */
extern  unsigned char	T23MOD;		/* 0x28: Timer 2,3 mode control */
extern  unsigned char	TRDC;		/* 0x29: Timer Double Buffer control */
/*---undefined---*/			/* 0x2a: */
/*---undefined---*/			/* 0x2b: */
extern  unsigned char	PACR;		/* 0x2c: PortA control */
extern  unsigned char	PAFC;		/* 0x2d: PortA function */
extern  unsigned char	PBCR;		/* 0x2e: PortB control */
extern  unsigned char	PBFC;		/* 0x2f: PortB function */

/***[0x30]*********************************************************************/
extern  unsigned int	TREG4;		/* 0x30: 16bit timer register 4 */
/*extern  unsigned char	TREG4L;		   0x30: 16bit timer register 4[low] */
/*extern  unsigned char	TREG4H;		   0x31: 16nit timer register 4[high] */
extern  unsigned int	TREG5;		/* 0x32: 16bit timer register 5 */
/*extern  unsigned char	TREG5L;		   0x32: 16bit timer register 5[low] */
/*extern  unsigned char	TREG5H;		   0x33: 16bit timer register 5[high] */
extern  unsigned int	CAP1;		/* 0x34: Capture register 1 */
/*extern  unsigned char	CAP1L;		   0x34: Capture register 1[low] */
/*extern  unsigned char	CAP1H;		   0x35: Capture register 1[high] */
extern  unsigned int	CAP2;		/* 0x36: Capture register 2 */
/*extern  unsigned char	CAP2L;		   0x36: Capture register 2[low] */
/*extern  unsigned char	CAP2H;		   0x37: Capture register 2[high] */
extern  unsigned char	T4MOD;		/* 0x38: 16bit timer mode register 4 */
extern  unsigned char	T4FFCR;		/* 0x39: 16bit timer F/F4 control */
extern  unsigned char	T45CR;		/* 0x3a: 16bit timer control */
/*---undefined---*/			/* 0x3b: */
extern  unsigned char	MSAR0;		/* 0x3c: Memory start address 0 */
extern  unsigned char	MAMR0;		/* 0x3d: Memory start address mask 0 */
extern  unsigned char	MSAR1;		/* 0x3e: Memory start address 1 */
extern  unsigned char	MAMR1;		/* 0x3f: Memory start address mask 1 */

/***[0x40]*********************************************************************/
extern  unsigned int	TREG6;		/* 0x40: 16bit timer register 6 */
/*extern  unsigned char	TREG6L;		   0x40: 16bit timer register 6[low] */
/*extern  unsigned char	TREG6H;		   0x41: 16bit timer register 6[high] */
extern  unsigned int	TREG7;		/* 0x42: 16bit timer register 7 */
/*extern  unsigned char	TREG7L;		   0x42: 16bit timer register 7[low] */
/*extern  unsigned char	TREG7H;		   0x43: 16bit timer register 7[high] */
extern  unsigned int	CAP3;		/* 0x44: Capture register 3 */
/*extern  unsigned char	CAP3L;		   0x44: Capture register 3[low] */
/*extern  unsigned char	CAP3H;		   0x45: Capture register 3[high] */
extern  unsigned int	CAP4;		/* 0x46: Capture register 4 */
/*extern  unsigned char	CAP4L;		   0x46: Capture register 4[low] */
/*extern  unsigned char	CAP4H;		   0x47: Capture register 4[high] */
extern  unsigned char	T5MOD;		/* 0x48: 16bit timer mode register 5 */
extern  unsigned char	T5FFCR;		/* 0x49: 16bit timer F/F5 control */
/*---undefined---*/			/* 0x4a: */
/*---undefined---*/			/* 0x4b: */
extern  unsigned char	PG0REG;		/* 0x4c: Pattern gen0 register */
extern  unsigned char	PG1REG;		/* 0x4d: Pattern gen1 register */
extern  unsigned char	PG01CR;		/* 0x4e: Pattern gen01 control */
/*---undefined---*/			/* 0x4f: */

/***[0x50]*********************************************************************/
extern  unsigned char	SC0BUF;		/* 0x50: Serial channel0 buffer*/
extern  unsigned char	SC0CR;		/* 0x51: Serial channel0 control */
extern  unsigned char	SC0MOD;		/* 0x52: Serial channel0 mode */
extern  unsigned char	BR0CR;		/* 0x53: Serial channel0 baud rate */
extern  unsigned char	SC1BUF;		/* 0x54: Serial channel1 buffer */
extern  unsigned char	SC1CR;		/* 0x55: Serial channel1 control */
extern  unsigned char	SC1MOD;		/* 0x56: Serial channel1 mode */
extern  unsigned char	BR1CR;		/* 0x57: Serial channel1 baud rate */
extern  unsigned char	ODE;		/* 0x58: Serial out control */
/*---undefined---*/			/* 0x59: */
extern  unsigned char	DREFCR;		/* 0x5a: Refresh control register */
extern  unsigned char	DMEMCR;		/* 0x5b: Memory control register */
extern  unsigned char	MSAR2;		/* 0x5c: Memory start address 2 */
extern  unsigned char	MAMR2;		/* 0x5d: Memory start address mask 2 */
extern  unsigned char	MSAR3;		/* 0x5e: Memory start address 3 */
extern  unsigned char	MAMR3;		/* 0x5f: Memory start address mask 3 */

/***[0x60]*********************************************************************/
extern  unsigned int	ADREG0;		/* 0x60: A/D result register 0 */
/*extern  unsigned char	ADREG0L;	   0x60: A/D result register 0[low] */
/*extern  unsigned char	ADREG0H;	   0x61: A/D result register 0[high] */
extern  unsigned int	ADREG1;		/* 0x62: A/D result register 1 */
/*extern  unsigned char	ADREG1L;	   0x62: A/D result register 1[low] */
/*extern  unsigned char	ADREG1H;	   0x63: A/D result register 1[high] */
extern  unsigned int	ADREG2;		/* 0x64: A/D result register 2 */
/*extern  unsigned char	ADREG2L;	   0x64: A/D result register 2[low] */
/*extern  unsigned char	ADREG2H;	   0x65: A/D result register 2[high] */
extern  unsigned int	ADREG3;		/* 0x66: A/D result register 3 */
/*extern  unsigned char	ADREG3L;	   0x66: A/D result register 3[low] */
/*extern  unsigned char	ADREG3H;	   0x67: A/D result register 3[high] */
extern  unsigned char	B0CS;		/* 0x68: Programable CS0 control */
extern  unsigned char	B1CS;		/* 0x69: Programable CS1 control */
extern  unsigned char	B2CS;		/* 0x6a: Programable CS2 control */
extern  unsigned char	B3CS;		/* 0x6b: Programable CS3 control */
extern  unsigned char	BEXCS;		/* 0x6c: External CS control */
extern  unsigned char	ADMOD;		/* 0x6d: A/D converter mode */
extern  unsigned char	WDMOD;		/* 0x6e: Watch dog mode register */
extern  unsigned char	WDCR;		/* 0x6f: Watch dog control */

/***[0x70]*********************************************************************/
extern  unsigned char	INTE0AD;	/* 0x70: Interrupt enable 0 & A/D */
extern  unsigned char	INTE45;		/* 0x71: Interrupt enable 5/4 */
extern  unsigned char	INTE67;		/* 0x72: Interrupt enable 7/6 */
extern  unsigned char	INTET01;	/* 0x73: Interrupt enable timer 1/0 */
extern  unsigned char	INTET23;	/* 0x74: Interrupt enable timer 3/2 */
extern  unsigned char	INTET45;	/* 0x75: Interrupt enable timer 5/4 */
extern  unsigned char	INTET67;	/* 0x76: Interrupt enable timer 7/6 */
extern  unsigned char	INTES0;		/* 0x77: Interrupt enable serial 0 */
extern  unsigned char	INTES1;		/* 0x78: Interrupt enable serial 1 */
extern  unsigned char	INTETC01;	/* 0x79: Interrupt enable TC 0/1 */
extern  unsigned char	INTETC23;	/* 0x7a: Interrupt enable TC 2/3 */
extern  unsigned char	IIMC;		/* 0x7b: INT input mode control */
extern  unsigned char	DMA0V;		/* 0x7c: DMA0 request vector */
extern  unsigned char	DMA1V;		/* 0x7d: DMA1 request vector */
extern  unsigned char	DMA2V;		/* 0x7e: DMA2 request vector */
extern  unsigned char	DMA3V;		/* 0x7f: DMA3 request vector */

/*=={EOF}==*/
