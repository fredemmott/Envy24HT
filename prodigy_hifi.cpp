#include "prodigy_hifi.h"
#include "misc.h"

/* I2C addresses */
#define WM_DEV		0x34

/* WM8776 registers */
#define WM_HP_ATTEN_L		0x00	/* headphone left attenuation */
#define WM_HP_ATTEN_R		0x01	/* headphone left attenuation */
#define WM_HP_MASTER		0x02	/* headphone master (both channels),
						override LLR */
#define WM_DAC_ATTEN_L		0x03	/* digital left attenuation */
#define WM_DAC_ATTEN_R		0x04
#define WM_DAC_MASTER		0x05
#define WM_PHASE_SWAP		0x06	/* DAC phase swap */
#define WM_DAC_CTRL1		0x07
#define WM_DAC_MUTE		0x08
#define WM_DAC_CTRL2		0x09
#define WM_DAC_INT		0x0a
#define WM_ADC_INT		0x0b
#define WM_MASTER_CTRL		0x0c
#define WM_POWERDOWN		0x0d
#define WM_ADC_ATTEN_L		0x0e
#define WM_ADC_ATTEN_R		0x0f
#define WM_ALC_CTRL1		0x10
#define WM_ALC_CTRL2		0x11
#define WM_ALC_CTRL3		0x12
#define WM_NOISE_GATE		0x13
#define WM_LIMITER		0x14
#define WM_ADC_MUX		0x15
#define WM_OUT_MUX		0x16
#define WM_RESET		0x17

/* Analog Recording Source :- Mic, LineIn, CD/Video, */

/* implement capture source select control for WM8776 */

#define WM_AIN1 "AIN1"
#define WM_AIN2 "AIN2"
#define WM_AIN3 "AIN3"
#define WM_AIN4 "AIN4"
#define WM_AIN5 "AIN5"

/* GPIO pins of envy24ht connected to wm8766 */
#define WM8766_SPI_CLK	 (1<<17) /* CLK, Pin97 on ICE1724 */
#define WM8766_SPI_MD	  (1<<16) /* DATA VT1724 -> WM8766, Pin96 */
#define WM8766_SPI_ML	  (1<<18) /* Latch, Pin98 */

/* WM8766 registers */
#define WM8766_DAC_CTRL	 0x02   /* DAC Control */
#define WM8766_INT_CTRL	 0x03   /* Interface Control */
#define WM8766_DAC_CTRL2	0x09
#define WM8766_DAC_CTRL3	0x0a
#define WM8766_RESET	    0x1f
#define WM8766_LDA1	     0x00
#define WM8766_LDA2	     0x04
#define WM8766_LDA3	     0x06
#define WM8766_RDA1	     0x01
#define WM8766_RDA2	     0x05
#define WM8766_RDA3	     0x07
#define WM8766_MUTE1	    0x0C
#define WM8766_MUTE2	    0x0F


/*
 * Prodigy HD2
 */
#define AK4396_ADDR    0x00
#define AK4396_CSN    (1 << 8)    /* CSN->GPIO8, pin 75 */
#define AK4396_CCLK   (1 << 9)    /* CCLK->GPIO9, pin 76 */
#define AK4396_CDTI   (1 << 10)   /* CDTI->GPIO10, pin 77 */

/* ak4396 registers */
#define AK4396_CTRL1	    0x00
#define AK4396_CTRL2	    0x01
#define AK4396_CTRL3	    0x02
#define AK4396_LCH_ATT	  0x03
#define AK4396_RCH_ATT	  0x04

/*
 * write data in the SPI mode
 */

static void SetGPIOBits(struct CardData *card, unsigned int bit, int val)
{
	unsigned int tmp = GetGPIOData(card->pci_dev, card->iobase);
	if (val)
		tmp |= bit;
	else
		tmp &= ~bit;
	SetGPIOData(card->pci_dev, card->iobase, tmp);
}

/*
 * serial interface for ak4396 - only writing supported, no readback
 */

static void ak4396_write_word(struct CardData *card, unsigned int data)
{
	int i;
	for (i = 0; i < 16; i++) {
		SetGPIOBits(card, AK4396_CCLK, 0);
		MicroDelay(1);
		SetGPIOBits(card, AK4396_CDTI, data & 0x8000);
		MicroDelay(1);
		SetGPIOBits(card, AK4396_CCLK, 1);
		MicroDelay(1);
		data <<= 1;
	}
}

static void ak4396_write(struct CardData *card, unsigned int reg,
			 unsigned int data)
{
	unsigned int block;

    SaveGPIO(card->pci_dev, card);
	SetGPIODir(card->pci_dev, card, AK4396_CSN|AK4396_CCLK|AK4396_CDTI);
	SetGPIOMask(card->pci_dev, card->iobase, ~(AK4396_CSN|AK4396_CCLK|AK4396_CDTI));
	/* latch must be low when writing */
	SetGPIOBits(card, AK4396_CSN, 0); 
	block =  ((AK4396_ADDR & 0x03) << 14) | (1 << 13) |
			((reg & 0x1f) << 8) | (data & 0xff);
	ak4396_write_word(card, block); /* REGISTER ADDRESS */
	/* release latch */
	SetGPIOBits(card, AK4396_CSN, 1);
	MicroDelay(1);
	/* restore */
    RestoreGPIO(card->pci_dev, card);
}


/*
 * initialize the chip
 */
void ProdigyHD2_Init(struct CardData *card)
{
	static unsigned short ak4396_inits[] = {
		AK4396_CTRL1,	   0x87,   /* I2S Normal Mode, 24 bit */
		AK4396_CTRL2,	   0x02,
		AK4396_CTRL3,	   0x00, 
		AK4396_LCH_ATT,	 0xFF,
		AK4396_RCH_ATT,	 0xFF,
	};
    int i;

	/* initialize ak4396 codec */
	/* reset codec */
	ak4396_write(card, AK4396_CTRL1, 0x86);
	MicroDelay(100);
	ak4396_write(card, AK4396_CTRL1, 0x87);
			
	for (i = 0; i < 10; i += 2)
		ak4396_write(card, ak4396_inits[i], ak4396_inits[i+1]);
}

