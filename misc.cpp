
#include "regs.h"
#include "misc.h"
#include "ak4114.h"
#include "DriverData.h"
#include "Revo51.h"
#include "prodigy_hifi.h"

#include <IOKit/IOLib.h>
#include <IOKit/audio/IOAudioDevice.h>
#include <IOKit/audio/IOAudioDefines.h>


/* Global in Card.c */
extern const UWORD InputBits[];

unsigned long Dirs[9] = {0x005FFFFF, 0x005FFFFF, 0x001EFFF7, 0x004000FA,
                         0x004000FA, 0x007FFF9F, 0x00FFFFFF, 0x00FFFFFF,
                         0x00DFFFFF};

/* Public functions in main.c */
void card_cleanup(struct CardData *card);
int aureon_ac97_init(IOPCIDevice *dev, IOMemoryMap *map);
void AddResetHandler(struct CardData *card);
unsigned char ReadI2CDelay(IOPCIDevice *dev, struct CardData *card, unsigned char addr, int delay);

static void CreateParmsForJulia(struct CardData *card);
static void CreateParmsForPhase22(struct CardData *card);
static void CreateParmsForRevo71(struct CardData *card);
static void CreateParmsForRevo51(struct CardData *card);
static void CreateParmsForAP192(struct CardData *card);
static void CreateParmsForAureonSpace(struct CardData *card);

extern void ap192_card_init (struct CardData *card);

#define BIT_DEPTH			32


static unsigned char inits_ak4358[] = {
            0x01, 0x02, /* 1: reset + soft mute */

            0x00, 0x87, // I2S + unreset (used to be 0F)

            0x01, 0x01, // unreset + soft mute off
		    0x02, 0x4F, /* 2: DA's power up, normal speed, RSTN#=0 */
	    	0x03, 0x01, /* 3: de-emphasis 44.1 */

		    0x04, 0xFF, /* 4: LOUT1 volume (PCM) */
    		0x05, 0xFF, /* 5: ROUT1 volume */

	    	0x06, 0x00, /* 6: LOUT2 volume (analogue in monitor, doesn't work) */
		    0x07, 0x00, /* 7: ROUT2 volume */

    		0x08, 0x00, /* 8: LOUT3 volume (dig out monitor, use it as analogue out) */
	    	0x09, 0x00, /* 9: ROUT3 volume */

            0x0a, 0x00, /* a: DATT speed=0, ignore DZF */

		    0x0b, 0xFF, /* b: LOUT4 volume */
    		0x0c, 0xFF, /* c: ROUT4 volume */

	    	0x0d, 0xFF, // DFZ
		    0x0E, 0x00,
            0x0F, 0x00,
    		0xff, 0xff
	    };

void revo_i2s_mclk_changed(struct CardData *card)
{
    IOPCIDevice *dev = card->pci_dev;

	/* assert PRST# to converters; MT05 bit 7 */
	dev->ioWrite8(MT_AC97_CMD_STATUS, dev->ioRead8(MT_AC97_CMD_STATUS, card->mtbase) | 0x80, card->mtbase);
	MicroDelay(5);
	/* deassert PRST# */
	dev->ioWrite8(MT_AC97_CMD_STATUS, dev->ioRead8(MT_AC97_CMD_STATUS, card->mtbase) & ~0x80, card->mtbase);
}


void WritePartialMask8(IOPCIDevice *dev, IOMemoryMap *map, unsigned char reg, unsigned char shift, unsigned char mask, unsigned char val)
{
    UInt8 tmp;
    
    tmp = dev->ioRead8( reg, map);
    tmp &= ~(mask << shift);
    tmp |= val << shift;
    dev->ioWrite8(reg, tmp, map);
}


void ClearMask8(IOPCIDevice *dev, IOMemoryMap *map, unsigned char reg, unsigned char mask)
{
    UBYTE tmp;
    
    tmp = dev->ioRead8(reg, map);
    tmp &= ~mask;
    dev->ioWrite8(reg, tmp, map);
}


void WriteMask8(IOPCIDevice *dev, IOMemoryMap *map, unsigned char reg, unsigned char mask)
{
    UBYTE tmp;
    
    tmp = dev->ioRead8(reg, map);
    tmp |= mask;
    dev->ioWrite8(reg, tmp, map);
}


void WritePartialMask(IOPCIDevice *dev, IOMemoryMap *map, unsigned char reg, unsigned long shift, unsigned long mask, unsigned long val)
{
    ULONG tmp;
    
    tmp = dev->ioRead32(reg, map);
    tmp &= ~(mask << shift);
    tmp |= val << shift;
    dev->ioWrite32(reg, tmp, map);
}


void ClearMask(IOPCIDevice *dev, IOMemoryMap *map, unsigned long reg, unsigned long mask)
{
    ULONG tmp;
    
    tmp = dev->ioRead32(reg, map);
    tmp &= ~mask;
    dev->ioWrite32(reg, tmp, map);
}


void WriteMask(IOPCIDevice *dev, IOMemoryMap *map, unsigned long reg, unsigned long mask)
{
    ULONG tmp;
    
    tmp = dev->ioRead32(reg, map);
    tmp |= mask;
    dev->ioWrite32(reg, tmp, map);
}

void SetGPIOData(IOPCIDevice *dev, IOMemoryMap *map, unsigned long data)
{
    dev->ioWrite16(CCS_GPIO_DATA, data & 0xFFFF, map);
    dev->ioWrite8(CCS_GPIO_DATA2, (data & (0xFF0000)) >> 16, map);
    dev->ioRead16(CCS_GPIO_DATA, map); /* dummy read for pci-posting */
}

void SetGPIOMask(IOPCIDevice *dev, IOMemoryMap *map, unsigned long data)
{
    dev->ioWrite16(CCS_GPIO_MASK, data & 0xFFF, map);
    dev->ioWrite8(CCS_GPIO_MASK2, (data & (0xFF0000)) >> 16, map);
    dev->ioRead16(CCS_GPIO_MASK, map); /* dummy read for pci-posting */
}


void SaveGPIO(IOPCIDevice *dev, struct CardData* card)
{
    card->SavedDir = dev->ioRead32(CCS_GPIO_DIR, card->iobase) & 0x7FFFFF;
    card->SavedMask = dev->ioRead16(CCS_GPIO_MASK, card->iobase);
}


void RestoreGPIO(IOPCIDevice *dev, struct CardData* card)
{
    dev->ioWrite32(CCS_GPIO_DIR, card->SavedDir, card->iobase);
    dev->ioWrite16(CCS_GPIO_MASK, card->SavedMask, card->iobase);
}


unsigned long GetGPIOData(IOPCIDevice *dev, IOMemoryMap *map)
{
    unsigned long data;
    
    data = (unsigned long) dev->ioRead8(CCS_GPIO_DATA2, map);
    data = (data << 16) | dev->ioRead16(CCS_GPIO_DATA, map);
    return data;
}


void SetGPIODir(IOPCIDevice *dev, struct CardData* card, unsigned long data)
{
    dev->ioWrite32(CCS_GPIO_DIR, data, card->iobase);
    dev->ioRead16(CCS_GPIO_DIR, card->iobase);
}


void WaitForI2C(IOPCIDevice *dev, struct CardData *card)
{
    int Counter = 0;
    
	for (Counter = 0; Counter < 10000; Counter++)
	{
		UInt8 status = dev->ioRead8(CCS_I2C_STATUS, card->iobase);
	    
		if ((status & CCS_I2C_BUSY) == 0)
        {
		    //IOLog("Counter was %d\n", Counter);
            return;
	    }
		
		MicroDelay(32);
	}

	IOLog("WaitForI2C() failed!\n");
	IOSleep(5000);
}


unsigned char ReadI2C(IOPCIDevice *dev, struct CardData *card, unsigned char addr)
{
    UInt8 val;

	WaitForI2C(dev, card);
    dev->ioWrite8(CCS_I2C_ADDR, addr, card->iobase);
	dev->ioWrite8(CCS_I2C_DEV_ADDRESS, 0xA0, card->iobase);
	WaitForI2C(dev, card);
    val = dev->ioRead8(CCS_I2C_DATA, card->iobase);
    
    return val;
}


unsigned char ReadI2CDelay(IOPCIDevice *dev, struct CardData *card, unsigned char addr, int delay)
{
    UInt8 val;
    
	WaitForI2C(dev, card);
    
    for (int i = 0; i < delay; i++)
    {
         dev->ioRead8(CCS_I2C_STATUS, card->iobase);
    }
    dev->ioWrite8(CCS_I2C_ADDR, addr, card->iobase);
	dev->ioWrite8(CCS_I2C_DEV_ADDRESS, 0xA0, card->iobase);
	WaitForI2C(dev, card);
    val = dev->ioRead8(CCS_I2C_DATA, card->iobase);
    
    return val;
}


void WriteI2C(IOPCIDevice *dev, struct CardData *card, unsigned chip_address, unsigned char reg, unsigned char data)
{
    WaitForI2C(dev, card);    
    dev->ioWrite8(CCS_I2C_ADDR, reg, card->iobase);
    dev->ioWrite8(CCS_I2C_DATA, data, card->iobase);
    
    WaitForI2C(dev, card);
	dev->ioWrite8(CCS_I2C_DEV_ADDRESS, chip_address | CCS_ADDRESS_WRITE, card->iobase);
}


void update_spdif_bits(struct CardData *card, unsigned short val)
{
	unsigned char cbit, disabled;
    IOPCIDevice *dev = card->pci_dev;

	cbit = dev->ioRead8(CCS_SPDIF_CONFIG, card->iobase); // get S/PDIF status
	disabled = cbit & ~CCS_SPDIF_INTEGRATED; // status without enabled bit set
    
	if (cbit != disabled) // it was enabled
		dev->ioWrite8(CCS_SPDIF_CONFIG, disabled, card->iobase); // so, disable it
        
	dev->ioWrite16(MT_SPDIF_TRANSMIT, val, card->mtbase); // now we can safely write to the SPDIF control reg
    
	if (cbit != disabled)
		dev->ioWrite8(CCS_SPDIF_CONFIG, cbit, card->mtbase); // restore
    
	dev->ioWrite16(MT_SPDIF_TRANSMIT, val, card->mtbase); // twice???
}


void update_spdif_rate(struct CardData *card, unsigned short rate)
{
	unsigned short val, nval;
    IOPCIDevice *dev = card->pci_dev;

	nval = val = dev->ioRead16(MT_SPDIF_TRANSMIT, card->mtbase);
	nval &= ~(7 << 12);
	switch (rate) {
	case 44100: break;
	case 48000: nval |= 2 << 12; break;
	case 32000: nval |= 3 << 12; break;
	}
	if (val != nval)
		update_spdif_bits(card, nval);
}


static void aureon_spi_write(struct CardData *card, IOMemoryMap *base, unsigned int cs, unsigned int data, int bits)
{
    IOPCIDevice *dev = card->pci_dev;
	unsigned int tmp;
	int i;

	tmp = GetGPIOData(dev, base);

    if (card->SubType == PHASE28)
    	SetGPIOMask(dev, base, ~(AUREON_WM_RW|AUREON_WM_DATA|AUREON_WM_CLK|AUREON_WM_CS));
    else
        SetGPIOMask(dev, base, ~(AUREON_WM_RW|AUREON_WM_DATA|AUREON_WM_CLK|AUREON_WM_CS | AUREON_CS8415_CS));
    
    SetGPIOMask(dev, base, 0);
    
	tmp |= AUREON_WM_RW;
	tmp &= ~cs; 
	SetGPIOData(dev, base, tmp); // set CS low
	MicroDelay(1);

   
	for (i = bits - 1; i >= 0; i--) {
		tmp &= ~AUREON_WM_CLK;
		SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
		if (data & (1 << i))
			tmp |= AUREON_WM_DATA;
		else
			tmp &= ~AUREON_WM_DATA;
		SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
		tmp |= AUREON_WM_CLK;
        SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
	}

	tmp &= ~AUREON_WM_CLK;
	tmp |= cs;
   SetGPIOData(dev, base, tmp);
	MicroDelay(1);
	tmp |= AUREON_WM_CLK;
   SetGPIOData(dev, base, tmp);
	MicroDelay(1);
}


unsigned char CS8415_read(IOPCIDevice *dev, IOMemoryMap *base, unsigned char reg)
{
	unsigned int tmp;
	int i, bits;
   unsigned long gpio;
   unsigned char ret = 0, data;
   unsigned int cs = AUREON_CS8415_CS;

	tmp = GetGPIOData(dev, base);

	SetGPIOMask(dev, base, ~(AUREON_WM_RW|AUREON_WM_DATA|AUREON_WM_CLK|AUREON_WM_CS|AUREON_CS8415_CS | AUREON_CS8415_CDOUT));
   tmp |= AUREON_WM_RW;
	tmp &= ~cs;
	SetGPIOData(dev, base, tmp); // set CS low
	MicroDelay(1);

   data = 0x20;
   bits = 8;
   for (i = bits - 1; i >= 0; i--) { // first write chip address
		tmp &= ~AUREON_WM_CLK;
		SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
		if (data & (1 << i))
			tmp |= AUREON_WM_DATA;
		else
			tmp &= ~AUREON_WM_DATA;
		SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
		tmp |= AUREON_WM_CLK;
      SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
	}
   
   data = reg;
   bits = 8;
   for (i = bits - 1; i >= 0; i--) { // then write MAP/reg
		tmp &= ~AUREON_WM_CLK;
		SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
		if (data & (1 << i))
			tmp |= AUREON_WM_DATA;
		else
			tmp &= ~AUREON_WM_DATA;
		SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
		tmp |= AUREON_WM_CLK;
      SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
	}
   
   // finish CS high
   tmp &= ~AUREON_WM_CLK;
   tmp |= cs;
   SetGPIOData(dev, base, tmp);
	MicroDelay(1);
   
   // bring CS low again
   tmp &= ~cs;
   tmp |= AUREON_WM_CLK;
	SetGPIOData(dev, base, tmp); // set CS low
	MicroDelay(1);

   data = 0x21; // chip address 0010000 and r/w bit high
   bits = 8;
	for (i = bits - 1; i >= 0; i--) { // send chip address and r/w bit
		tmp &= ~AUREON_WM_CLK;
		SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
		if (data & (1 << i))
			tmp |= AUREON_WM_DATA;
		else
			tmp &= ~AUREON_WM_DATA;
		SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
		tmp |= AUREON_WM_CLK;
      SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
	}
   
   tmp &= ~AUREON_WM_CLK;
	SetGPIOData(dev, base, tmp);
   MicroDelay(1);
   
   tmp |= AUREON_WM_CLK;
   SetGPIOData(dev, base, tmp);
   MicroDelay(1);
   
   // read
   
   bits = 8;
   for (i = bits - 1; i >= 0; i--) {
		tmp &= ~AUREON_WM_CLK;
		SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
      
      gpio = GetGPIOData(dev, base);
      MicroDelay(1);
      ret |= ((gpio >> 21) & 1) << i;
    	MicroDelay(1);
		
      tmp |= AUREON_WM_CLK;
      SetGPIOData(dev, base, tmp);
    	MicroDelay(1);
	}

	tmp &= ~AUREON_WM_CLK;
	tmp |= cs;
   SetGPIOData(dev, base, tmp);
	MicroDelay(1);
	tmp |= AUREON_WM_CLK;
   SetGPIOData(dev, base, tmp);
	MicroDelay(1);
   
   return ret;
}


static void aureon_ac97_write(IOPCIDevice *dev, IOMemoryMap *base, unsigned short reg, unsigned short val)
{
	unsigned int tmp;

	/* Send address to XILINX chip */
	tmp = (GetGPIOData(dev, base) & ~0xFF) | (reg & 0x7F);
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);
	tmp |= AUREON_AC97_ADDR;
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);
	tmp &= ~AUREON_AC97_ADDR;
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);	

	/* Send low-order byte to XILINX chip */
	tmp &= ~AUREON_AC97_DATA_MASK;
	tmp |= val & AUREON_AC97_DATA_MASK;
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);
	tmp |= AUREON_AC97_DATA_LOW;
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);
	tmp &= ~AUREON_AC97_DATA_LOW;
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);
	
	/* Send high-order byte to XILINX chip */
	tmp &= ~AUREON_AC97_DATA_MASK;
	tmp |= (val >> 8) & AUREON_AC97_DATA_MASK;

	SetGPIOData(dev, base, tmp);
	MicroDelay(10);
	tmp |= AUREON_AC97_DATA_HIGH;
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);
	tmp &= ~AUREON_AC97_DATA_HIGH;
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);
	
	/* Instruct XILINX chip to parse the data to the STAC9744 chip */
	tmp |= AUREON_AC97_COMMIT;
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);
	tmp &= ~AUREON_AC97_COMMIT;
	SetGPIOData(dev, base, tmp);
	MicroDelay(10);	
}


int aureon_ac97_init(IOPCIDevice *dev, IOMemoryMap *base)
{
	int i;
	static unsigned short ac97_defaults[] = {
		AC97_RESET, 0x6940,
		AC97_MASTER_VOL_STEREO, 0x0101, // 0dB atten., no mute, may not exceed 0101!!!
		AC97_AUXOUT_VOL, 0x8808,
		AC97_MASTER_VOL_MONO, 0x8000,
		AC97_PHONE_VOL, 0x8008, // mute
		AC97_MIC_VOL, 0x8008,
		AC97_LINEIN_VOL, 0x8808,
		AC97_CD_VOL, 0x0808,
		AC97_VIDEO_VOL, 0x8808,
		AC97_AUX_VOL, 0x8808,
		AC97_PCMOUT_VOL, 0x8808,
        //0x1C, 0x8000,
		//0x26, 0x000F,
		//0x28, 0x0201,
		0x2C, 0xAC44,
		0x32, 0xAC44,
		//0x7C, 0x8384,
		//0x7E, 0x7644,
		(unsigned short)-1
	};
	unsigned int tmp;

	/* Cold reset */
	tmp = (GetGPIOData(dev, base) | AUREON_AC97_RESET) & ~AUREON_AC97_DATA_MASK;
    
	SetGPIOData(dev, base, tmp);
	MicroDelay(3);
	
	tmp &= ~AUREON_AC97_RESET;
	SetGPIOData(dev, base, tmp);
	MicroDelay(3);
	
	tmp |= AUREON_AC97_RESET;
	SetGPIOData(dev, base, tmp);
	MicroDelay(3);
	
	for (i=0; ac97_defaults[i] != (unsigned short)-1; i+=2)
		aureon_ac97_write(dev, base, ac97_defaults[i], ac97_defaults[i+1]);

	return 0;
}

void wm_put(struct CardData *card, IOMemoryMap *map, unsigned short reg, unsigned short val)
{
	aureon_spi_write(card, map, AUREON_WM_CS, (reg << 9) | (val & 0x1ff), 16);
}


/******************************************************************************
** DriverData allocation ******************************************************
******************************************************************************/

// This code used to be in _AHIsub_AllocAudio(), but since we're now
// handling CAMD support too, it needs to be done at driver loading
// time.

struct CardData*
AllocDriverData( IOPCIDevice *    dev, struct CardData *card )
{
  card->SavedMask = 0;
  card->RevoFrontCodec = NULL;
  card->RevoSurroundCodec = NULL;
  card->RevoRecCodec = NULL;
  card->SPDIF_RateSupported = true;
  card->ParmList = NULL;


  /* Initialize chip */
  if( card_init( card ) < 0 )
  {
    return NULL;
  }

  card->card_initialized = TRUE;

  if (card->SubType == REVO51)
  {
     card->input = 1; // line in
  }
  else
  {
     card->input          = 0;
  }
  card->output         = 0;
 
  return card;
}


/******************************************************************************
** DriverData deallocation ****************************************************
******************************************************************************/

// And this code used to be in _AHIsub_FreeAudio().

void
FreeDriverData( struct CardData* card )
{
  if( card != NULL )
  {
	if (card->RevoFrontCodec)
	{
	   delete card->RevoFrontCodec;
	   card->RevoFrontCodec = NULL; 
	}
	
	if (card->RevoSurroundCodec)
	{
	   delete card->RevoSurroundCodec;
	   card->RevoSurroundCodec = NULL; 
	}

	if (card->RevoRecCodec)
	{
	   delete card->RevoRecCodec;
	   card->RevoRecCodec = NULL; 
	}

  }
}


static unsigned short wm_inits[] = {
		
        0x18, 0x000,		/* All power-up */
        
		0x1b, 0x022,		/* ADC Mux (AIN1 = CD-in) */
		0x1c, 0x00B,  		/* Output1 = DAC + Aux (= ac'97 mix), output2 = DAC */
		0x1d, 0x009,		/* Output3+4 = DAC */
        
        0x16, 0x122,		/* I2S, normal polarity, 24bit */
		0x17, 0x022,		/* 256fs, slave mode */
        
		0x00, 0x17F,		/* DAC1 analog mute */
		0x01, 0x17F,		/* DAC2 analog mute */
		0x02, 0x17F,		/* DAC3 analog mute */
		0x03, 0x17F,		/* DAC4 analog mute */
		0x04, 0x7F,		/* DAC5 analog mute */
		0x05, 0x7F,		/* DAC6 analog mute */
		0x06, 0x7F,		/* DAC7 analog mute */
		0x07, 0x7F,		/* DAC8 analog mute */
		0x08, 0x17F,	/* master analog mute */
		0x09, 0x1ff,		/* DAC1 digital full */
		0x0a, 0x1ff,		/* DAC2 digital full */
		0x0b, 0xff,		/* DAC3 digital full */
		0x0c, 0xff,		/* DAC4 digital full */
		0x0d, 0xff,		/* DAC5 digital full */
		0x0e, 0xff,		/* DAC6 digital full */
		0x0f, 0xff,		/* DAC7 digital full */
		0x10, 0xff,		/* DAC8 digital full */
		0x11, 0x1ff,		/* master digital full */
		0x12, 0x000,		/* phase normal */
		0x13, 0x090,		/* unmute DAC L/R */
		0x14, 0x000,		/* all unmute (bit 5 is rec enable) */
		0x15, 0x000,		/* no deemphasis, no ZFLG */
		0x19, 0x0C,		/* 0dB gain ADC/L */
		0x1a, 0x0C		/* 0dB gain ADC/R */
	};

static unsigned short wm_inits_Phase28[] = {
		
        0x18, 0x000,		/* All power-up */
        
        0x1b, 0x000, 		/* ADC Mux (AIN1 = Line-in, no other inputs are present) */
		0x1c, 0x009,  		/* Output1 = DAC , Output2 = DAC */
        0x1d, 0x009,		/* Output3+4 = DAC */
        
        0x16, 0x122,		/* I2S, normal polarity, 24bit */
		0x17, 0x022,		/* 256fs, slave mode */
        
		0x00, 0x000,		/* DAC1 analog mute */
		0x01, 0x000,		/* DAC2 analog mute */
		0x02, 0x7F,		/* DAC3 analog mute */
		0x03, 0x7F,		/* DAC4 analog mute */
		0x04, 0x7F,		/* DAC5 analog mute */
		0x05, 0x7F,		/* DAC6 analog mute */
		0x06, 0x7F,		/* DAC7 analog mute */
		0x07, 0x7F,		/* DAC8 analog mute */
		0x08, 0x17F,	/* master analog mute */
		0x09, 0xff,		/* DAC1 digital full */
		0x0a, 0xff,		/* DAC2 digital full */
		0x0b, 0xff,		/* DAC3 digital full */
		0x0c, 0xff,		/* DAC4 digital full */
		0x0d, 0xff,		/* DAC5 digital full */
		0x0e, 0xff,		/* DAC6 digital full */
		0x0f, 0xff,		/* DAC7 digital full */
		0x10, 0xff,		/* DAC8 digital full */
		0x11, 0x1ff,		/* master digital full */
		0x12, 0x000,		/* phase normal */
		0x13, 0x090,		/* unmute DAC L/R */
		0x14, 0x000,		/* all unmute (bit 5 is rec enable) */
		0x15, 0x000,		/* no deemphasis, no ZFLG */
		0x19, 0x0C,		/* 0dB gain ADC/L */
		0x1a, 0x0C		/* 0dB gain ADC/R */
	};
    
	static unsigned short cs_inits[] = {
		0x0441, /* RUN */
		0x0180, /* no mute */
		0x0201, /* */
		0x0605, /* master, 16-bit slave, 24bit */
	};


int card_init(struct CardData *card)
{
    IOPCIDevice *dev = (IOPCIDevice *) card->pci_dev;
    //unsigned short cod;
    int i;
    unsigned int tmp;
	
	dev->ioWrite8(MT_AC97_CMD_STATUS, MT_AC97_RESET, card->mtbase);

	MicroDelay(5);
    dev->ioWrite8(MT_AC97_CMD_STATUS, 0x00, card->mtbase);
    
    dev->ioWrite8(CCS_POWER_DOWN, 0, card->iobase); // power up the whole thing
    
    // reset
    dev->ioWrite8(CCS_CTRL, CCS_RESET_ALL, card->iobase);
    MicroDelay(100);
    ClearMask8(dev, card->iobase, CCS_CTRL, CCS_RESET_ALL);
    MicroDelay(500);
    
    if ((dev->ioRead8(CCS_I2C_STATUS, card->iobase) & CCS_I2C_EPROM) != 0)
    {
	    UInt32 subvendor = 0;
		UInt8 a,b,c,d;
		
		a = ReadI2C(dev, card, 0x00);
		b = ReadI2C(dev, card, 0x01);
		c = ReadI2C(dev, card, 0x02);
		d = ReadI2C(dev, card, 0x03);
		// for some reason we need to do this twice, leave alone!!
			
		a = ReadI2C(dev, card, 0x00);
		b = ReadI2C(dev, card, 0x01);
		c = ReadI2C(dev, card, 0x02);
		d = ReadI2C(dev, card, 0x03);
		
		subvendor =
        		(a << 0) |
				(b << 8) | 
				(c << 16) | 
				(d << 24);
        
        switch (subvendor)
        {
            case SUBVENDOR_AUREON_SKY: card->SubType = AUREON_SKY;
									IOLog("Found Aureon Sky!\n");
									card->Specific.NumChannels = 6;
									card->Specific.HasSPDIF = true;
                                    break;
            
            case SUBVENDOR_PRODIGY71:
            case SUBVENDOR_AUREON_SPACE: card->SubType = AUREON_SPACE;
			                        IOLog("Found Aureon Space!\n");
									card->Specific.NumChannels = 8;
									card->Specific.HasSPDIF = true;
                                    break;
                                    
            case SUBVENDOR_PHASE28:
			{
			    card->SubType = PHASE28;
				card->Specific.NumChannels = 8;
				card->Specific.HasSPDIF = true;
			    IOLog("Found Phase28!\n");
                break;
		    }

            case SUBVENDOR_MAUDIO_REVOLUTION51: card->SubType = REVO51;
									card->Specific.NumChannels = 6;
									card->Specific.HasSPDIF = true;
                                    IOLog("Found M-Audio Revolution 5.1!\n");
                                    break;

            case SUBVENDOR_MAUDIO_REVOLUTION71: card->SubType = REVO71;
									card->Specific.NumChannels = 8;
									card->Specific.HasSPDIF = true;
                                    IOLog("Found M-Audio Revolution 7.1!\n");
									break;
            
            case SUBVENDOR_JULIA: card->SubType = JULIA;
									card->Specific.NumChannels = 2;
									card->Specific.HasSPDIF = true;
									IOLog("Found ESI Juli@!\n");
                                    break;
            
            case SUBVENDOR_PHASE22:
            case SUBVENDOR_FAME22:
			{
				card->SubType = PHASE22;
				card->Specific.NumChannels = 2;
				card->Specific.HasSPDIF = true;
				IOLog("Found Phase22!\n");
                break;
		    }
                
            case SUBVENDOR_MAUDIO_AP192:
            {
                card->SubType = AP192;
				card->Specific.NumChannels = 2;
				card->Specific.HasSPDIF = true;
				IOLog("Found Audiophile 192!\n");
                break;
            }
                
            case VT1724_SUBDEVICE_PRODIGY_HD2:
            {
                card->SubType = PRODIGY_HD2;
				card->Specific.NumChannels = 2;
				card->Specific.HasSPDIF = true;
				IOLog("Found AudioTrak Prodigy HD2!\n");
                break;
            }
                
            case SUBVENDOR_CANTATIS:
            {
                card->SubType = CANTATIS;
				card->Specific.NumChannels = 2;
				card->Specific.HasSPDIF = true;
				IOLog("Found Cantatis card!\n");
                break;
                
            }
            
            default:
			{
				IOLog("This specific Envy24HT card with subvendor id %x is not supported!\n", subvendor);
				IOLog("This Envy24HT driver only supports Terratec Aureon Sky, Space, Phase 22 and 28, M-Audio Revolution 5.1/7.1 and ESI Juli@\n");
                return -1;
			}
        }
        
        IOLog("subvendor = %x\n", subvendor);
    }
	
	card->Specific.BufferSize = NUM_SAMPLE_FRAMES * card->Specific.NumChannels * (BIT_DEPTH / 8);
	card->Specific.BufferSizeRec = NUM_SAMPLE_FRAMES * 2 * (BIT_DEPTH / 8);
	
    
    if (card->SubType == PHASE22)
    {
        dev->ioWrite8(CCS_SYSTEM_CONFIG, 0x28, card->iobase);
        dev->ioWrite8(CCS_ACLINK_CONFIG, 0x80, card->iobase); // AC-link
        dev->ioWrite8(CCS_I2S_FEATURES, 0x70, card->iobase); // I2S
        dev->ioWrite8(CCS_SPDIF_CONFIG, 0xC3, card->iobase); // S/PDIF
    }
    else
    {
       if (card->SubType == PHASE28)
           dev->ioWrite8(CCS_SYSTEM_CONFIG, 0x2B, card->iobase); // MIDI, ADC+SPDIF IN, 4 DACS
       else if (card->SubType == AUREON_SPACE)
           dev->ioWrite8(CCS_SYSTEM_CONFIG, 0x0B, card->iobase); // ADC+SPDIF IN, 4 DACS
       else if (card->SubType == REVO71)
           dev->ioWrite8(CCS_SYSTEM_CONFIG, 0x43, card->iobase); // XIN1 + ADC + 4 DACS
       else if (card->SubType == REVO51)
           dev->ioWrite8(CCS_SYSTEM_CONFIG, 0x42, card->iobase);
       else if (card->SubType == AUREON_SKY)
           dev->ioWrite8(CCS_SYSTEM_CONFIG, 0x0A, card->iobase); // ADC+SPDIF IN, 3 DACS
           
       dev->ioWrite8(CCS_ACLINK_CONFIG, CCS_ACLINK_I2S, card->iobase); // I2S in split mode
       
       if (card->SubType != JULIA)
           dev->ioWrite8(CCS_I2S_FEATURES, CCS_I2S_VOLMUTE | CCS_I2S_96KHZ | CCS_I2S_24BIT | CCS_I2S_192KHZ, card->iobase);
       
       if (card->SubType == REVO71 || card->SubType == REVO51)
           dev->ioWrite8(CCS_SPDIF_CONFIG, CCS_SPDIF_INTEGRATED | CCS_SPDIF_INTERNAL_OUT | CCS_SPDIF_EXTERNAL_OUT, card->iobase);
       else
           dev->ioWrite8(CCS_SPDIF_CONFIG, CCS_SPDIF_INTEGRATED | CCS_SPDIF_INTERNAL_OUT | CCS_SPDIF_IN_PRESENT | CCS_SPDIF_EXTERNAL_OUT, card->iobase);
    }
    
    card->SavedDir = Dirs[card->SubType];
	dev->ioWrite8(MT_INTR_MASK, 0xFF /*MT_DMA_FIFO_MASK*/, card->mtbase);
	
    if (card->SubType == REVO71)
       SetGPIOMask(dev, card->iobase, 0x00BFFF85);
    else if (card->SubType == REVO51)
       SetGPIOMask(dev, card->iobase, 0x00BFFF05);
    else
       SetGPIOMask(dev, card->iobase, 0x00000000);

    dev->ioWrite32(CCS_GPIO_DIR, Dirs[card->SubType], card->iobase); // input/output
    dev->ioRead16(CCS_GPIO_DIR, card->iobase);
       
    if (card->SubType == REVO71 || card->SubType == REVO51) {
       dev->ioWrite16(CCS_GPIO_DATA, 0x0072, card->iobase);
       dev->ioWrite8(CCS_GPIO_DATA2, 0x00, card->iobase);
       }
    else if (card->SubType == JULIA) {
       dev->ioWrite16(CCS_GPIO_DATA, 0x3819, card->iobase);
       }
    else if (card->SubType == AP192) {
        dev->ioWrite16(CCS_GPIO_DATA, 0xFFFF, card->iobase);   
    }
    else {
       dev->ioWrite16(CCS_GPIO_DATA, 0x0000, card->iobase);
       if (card->SubType != PHASE22)
           dev->ioWrite8(CCS_GPIO_DATA2, 0x0, card->iobase);
       }
    dev->ioRead16(CCS_GPIO_DATA, card->iobase);
    
    //SaveGPIO(dev, card);
    
    if (card->SubType == REVO71 || card->SubType == REVO51)
       dev->ioWrite8(MT_I2S_FORMAT, 0 /*0x08*/, card->mtbase); // tbd
    else
       dev->ioWrite8(MT_I2S_FORMAT, 0, card->mtbase);
    
    if (card->SubType == AUREON_SKY || card->SubType == AUREON_SPACE || card->SubType == PHASE28)
    {
        if (card->SubType == AUREON_SKY || card->SubType == AUREON_SPACE)
        {
            aureon_ac97_init(dev, card->iobase);
            SetGPIOMask(dev, card->iobase, ~(AUREON_WM_RESET | AUREON_WM_CS | AUREON_CS8415_CS));
        }
        else if (card->SubType == PHASE28)
            SetGPIOMask(dev, card->iobase, ~(AUREON_WM_RESET | AUREON_WM_CS));
       
   
        tmp = GetGPIOData(dev, card->iobase);
        tmp &= ~AUREON_WM_RESET;
        SetGPIOData(dev, card->iobase, tmp);
        MicroDelay(1);
       
        if (card->SubType != PHASE28)
            tmp |= AUREON_WM_CS | AUREON_CS8415_CS;
        else
            tmp |= AUREON_WM_CS;
   	
        SetGPIOData(dev, card->iobase, tmp);
       	MicroDelay(1);
   	    tmp |= AUREON_WM_RESET;
       	SetGPIOData(dev, card->iobase, tmp);
        MicroDelay(1);
       
        if (card->SubType != PHASE28)
        {
            /* initialize WM8770 codec */
       	    for (i = 0; i < 60; i += 2)
            {
       		    wm_put(card, card->iobase, wm_inits[i], wm_inits[i+1]);
            }
            
            /* initialize CS8415A codec */
    	    for (i = 0; i < 4; i++)
      		    aureon_spi_write(card, card->iobase, AUREON_CS8415_CS, cs_inits[i] | 0x200000, 24);
        }
        else
        {
            /* initialize WM8770 codec */
   	        for (i = 0; i < 60; i += 2)
            {
   	    	    wm_put(card, card->iobase, wm_inits_Phase28[i], wm_inits_Phase28[i+1]);
            }
        }
        
        if (card->SubType == AUREON_SPACE || card->SubType == AUREON_SKY)
        {
            CreateParmsForAureonSpace(card);
        }
    }
    else if (card->SubType == REVO51)
    {
        card->RevoFrontCodec = new akm_codec;
        card->RevoFrontCodec->caddr = 2;
        card->RevoFrontCodec->cif = 0;
        card->RevoFrontCodec->datamask = REVO_CDOUT;
        card->RevoFrontCodec->clockmask = REVO_CCLK;
        card->RevoFrontCodec->csmask = REVO_CS0 | REVO_CS1;
        card->RevoFrontCodec->csaddr = REVO_CS1;
        card->RevoFrontCodec->csnone = REVO_CS0 | REVO_CS1;
        card->RevoFrontCodec->addflags = REVO_CCLK;
        card->RevoFrontCodec->type = AKM4358;
        card->RevoFrontCodec->totalmask = 0;
        card->RevoFrontCodec->newflag = 1;
        
        
        card->RevoSurroundCodec = NULL;

        card->RevoRecCodec = new akm_codec;
        card->RevoRecCodec->caddr = 2;
        card->RevoRecCodec->csmask = REVO_CS0 | REVO_CS1;
        card->RevoRecCodec->clockmask = REVO_CCLK;
        card->RevoRecCodec->datamask = REVO_CDOUT;
        card->RevoRecCodec->type = AKM5365;
        card->RevoRecCodec->cif = 0;
        card->RevoRecCodec->addflags = REVO_CCLK;
        card->RevoRecCodec->csaddr = REVO_CS0;
        card->RevoRecCodec->csnone = REVO_CS0 | REVO_CS1;
        card->RevoRecCodec->totalmask = 0;
        card->RevoRecCodec->newflag = 1;

        dev->ioWrite8(MT_SAMPLERATE, 8, card->mtbase);

        {
         unsigned int tmp = GetGPIOData(dev, card->iobase);
         tmp &= ~REVO_MUTE; // mute
         SetGPIOData(dev, card->iobase, tmp);
        }

        Init_akm4xxx(card, card->RevoFrontCodec);
		Init_akm4xxx(card, card->RevoRecCodec);

        {
         unsigned int tmp = GetGPIOData(dev, card->iobase);
         tmp |= REVO_MUTE; // unmute
         SetGPIOData(dev, card->iobase, tmp);
        }

        // Has to be after mute, otherwise the mask is changed in Revo51_Init() which enables the mute mask bit...
        Revo51_Init(card); // I2C
        CreateParmsForRevo51(card);
    }
    else if (card->SubType == REVO71)
    {
        card->RevoFrontCodec = new akm_codec;
        card->RevoFrontCodec->caddr = 1;
        card->RevoFrontCodec->csmask = REVO_CS1;
        card->RevoFrontCodec->clockmask = REVO_CCLK;
        card->RevoFrontCodec->datamask = REVO_CDOUT;
        card->RevoFrontCodec->type = AKM4381;
        card->RevoFrontCodec->cif = 0;
        card->RevoFrontCodec->addflags = 0; //REVO_CCLK;?
        
        card->RevoSurroundCodec = new akm_codec;
        card->RevoSurroundCodec->caddr = 3;
        card->RevoSurroundCodec->csmask = REVO_CS2;
        card->RevoSurroundCodec->clockmask = REVO_CCLK;
        card->RevoSurroundCodec->datamask = REVO_CDOUT;
        card->RevoSurroundCodec->type = AKM4355;
        card->RevoSurroundCodec->cif = 0;
        card->RevoSurroundCodec->addflags = 0; //REVO_CCLK;?
        
        dev->ioWrite8(MT_SAMPLERATE, 8, card->mtbase);
        
        {
         unsigned int tmp = GetGPIOData(dev, card->iobase);
         tmp &= ~REVO_MUTE; // mute
         SetGPIOData(dev, card->iobase, tmp);
        }
        
        Init_akm4xxx(card, card->RevoFrontCodec);
        Init_akm4xxx(card, card->RevoSurroundCodec);
        //revo_i2s_mclk_changed(card);
        
        {
         unsigned int tmp = GetGPIOData(dev, card->iobase);
         tmp |= REVO_MUTE; // unmute
         SetGPIOData(dev, card->iobase, tmp);
        }
        
        CreateParmsForRevo71(card);
    }

    else if (card->SubType == JULIA)
    {
        dev->ioWrite8(CCS_SYSTEM_CONFIG, 0x78, card->iobase);
        dev->ioWrite8(CCS_ACLINK_CONFIG, CCS_ACLINK_I2S, card->iobase); // I2S in split mode
        dev->ioWrite8(CCS_I2S_FEATURES, CCS_I2S_96KHZ | CCS_I2S_24BIT | CCS_I2S_192KHZ, card->iobase);
        dev->ioWrite8(CCS_SPDIF_CONFIG, CCS_SPDIF_INTEGRATED | CCS_SPDIF_INTERNAL_OUT | CCS_SPDIF_IN_PRESENT | CCS_SPDIF_EXTERNAL_OUT, card->iobase);
       
        
        unsigned char *ptr, reg, data;
        
        static unsigned char inits_ak4114[] = {
            0x00, 0x00, // power down & reset
    		0x00, 0x0F, // power on
	    	0x01, 0x70, // I2S
		    0x02, 0x80, // TX1 output enable
	    	0x03, 0x49, // 1024 LRCK + transmit data
		    0x04, 0x00, // no mask
    		0x05, 0x00, // no mask
	    	0x0D, 0x41, // 
		    0x0E, 0x02,
    		0x0F, 0x2C,
	    	0x10, 0x00,
		    0x11, 0x00,
    		0xff, 0xff
	    };
        
        ptr = inits_ak4358;
		while (*ptr != 0xff) {
			reg = *ptr++;
			data = *ptr++;
			WriteI2C(dev, card, AK4358_ADDR, reg, data);
            MicroDelay(5);
		}
        
        ptr = inits_ak4114;
		while (*ptr != 0xff) {
			reg = *ptr++;
			data = *ptr++;
			WriteI2C(dev, card, AK4114_ADDR, reg, data);
            MicroDelay(100);
            }
        
        dev->ioWrite8(MT_SAMPLERATE, 8, card->mtbase);
        //dev->ioWrite32(0x2C, 0x300200, card->mtbase); // routes analogue in to analogue out
        
        CreateParmsForJulia(card);
    }
    else if (card->SubType == PHASE22)
    {
        
        card->RevoFrontCodec = new akm_codec;
        card->RevoFrontCodec->caddr = 2;
        card->RevoFrontCodec->csmask = 1 << 10;
        card->RevoFrontCodec->clockmask = 1 << 5;
        card->RevoFrontCodec->datamask = 1 << 4;
        card->RevoFrontCodec->type = AKM4524;
        card->RevoFrontCodec->cif = 1;
        card->RevoFrontCodec->addflags = 1 << 3;
        card->RevoFrontCodec->newflag = false;

        Init_akm4xxx(card, card->RevoFrontCodec);
        CreateParmsForPhase22(card);
    }
    else if (card->SubType == AP192)
    {
        dev->ioWrite8(CCS_SYSTEM_CONFIG, 0x68, card->iobase);
        dev->ioWrite8(CCS_ACLINK_CONFIG, 0x80, card->iobase); // AC-link
        dev->ioWrite8(CCS_I2S_FEATURES, 0xF8, card->iobase); // I2S
        dev->ioWrite8(CCS_SPDIF_CONFIG, 0xC3, card->iobase); // S/PDIF
		
        ap192_card_init(card);
        CreateParmsForAP192(card);
    }
    else if (card->SubType == PRODIGY_HD2)
    {
        dev->ioWrite8(CCS_SYSTEM_CONFIG, 0x28, card->iobase);
        dev->ioWrite8(CCS_ACLINK_CONFIG, 0x80, card->iobase); // AC-link
        dev->ioWrite8(CCS_I2S_FEATURES, 0x78, card->iobase); // I2S
        dev->ioWrite8(CCS_SPDIF_CONFIG, 0xC3, card->iobase); // S/PDIF
        
        ProdigyHD2_Init(card);
    }

    //RestoreGPIO(dev, card);
    
    ClearMask8(dev, card->iobase, CCS_INTR_MASK, CCS_INTR_PLAYREC); // enable

    // Enter SPI mode for CS8415A digital receiver
    /*SetGPIOMask(dev, card->iobase, ~(AUREON_CS8415_CS));
	 tmp |= AUREON_CS8415_CS;
	 SetGPIOData(dev, card->iobase, tmp); // set CS high
	 MicroDelay(1);
    
	 tmp &= ~AUREON_CS8415_CS;
	 SetGPIOData(dev, card->iobase, tmp); // set CS low
	 MicroDelay(1);*/

    // WritePartialMask(dev, card->mtbase, 0x2C, 8, 7, 6); // this line is to route the s/pdif input to the left
    // analogue output for testing purposes

    //dev->ioWrite32(0x2C, 0x000200, card->mtbase); // route

    IOLog("Card init done!\n");
	//IOSleep(1000);

   return 0;
}


void card_cleanup(struct CardData *card)
{
}



/******************************************************************************
** Misc. **********************************************************************
******************************************************************************/

void
SaveMixerState( struct CardData* card )
{
}


void
RestoreMixerState( struct CardData* card )
{
}

void
UpdateMonitorMixer( struct CardData* card )
{
}


ULONG
SamplerateToLinearPitch( ULONG samplingrate )
{
  samplingrate = (samplingrate << 8) / 375;
  return (samplingrate >> 1) + (samplingrate & 1);
}


void MicroDelay(unsigned int micros)
{
  IODelay(micros);
}


static void CreateParmsForJulia(struct CardData *card)
{
    Parm* p = new Parm;
    Parm *prev = NULL;
    card->ParmList = p;
    
    // left output
    p->InitialValue = 0x7F;
    p->MinValue = 14;
    p->MaxValue = 0x7F;
    p->MindB = (-49 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft; 
    p->Name = kIOAudioControlChannelNameLeft;
    p->ControlID = 0;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x4;
    p->reverse = false;  
    p->I2C = true;
    p->I2C_codec_addr = AK4358_ADDR;
    p->HasMute = true;
    p->MuteReg =0x1;
    p->MuteOnVal = 0x3;
    p->MuteOffVal = 0x1;
    p->codec = NULL;
    
    
    // right output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x7F;
    p->MinValue = 14;
    p->MaxValue = 0x7F;
    p->MindB = (-49 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight; 
    p->Name = kIOAudioControlChannelNameRight;
    p->ControlID = 1;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x5;
    p->reverse = false; 
    p->I2C = true;
    p->I2C_codec_addr = AK4358_ADDR;
    p->codec = NULL;
    p->HasMute = false;
    p->Next = NULL;
}    


static void CreateParmsForPhase22(struct CardData *card)
{
    Parm* p = new Parm;
    Parm *prev = NULL;
    card->ParmList = p;
    
    // left output
    p->InitialValue = 0x7E;
    p->MinValue = 14;
    p->MaxValue = 0x7E;
    p->MindB = (-49 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft; 
    p->Name = kIOAudioControlChannelNameLeft;
    p->ControlID = 0;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x6;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = true;
    p->MuteReg =0x3;
    p->MuteOnVal = 0x99;
    p->MuteOffVal = 0x19;
    p->codec = card->RevoFrontCodec;
    
    
    // right output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x7E;
    p->MinValue = 14;
    p->MaxValue = 0x7E;
    p->MindB = (-49 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight; 
    p->Name = kIOAudioControlChannelNameRight;
    p->ControlID = 1;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x7;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoFrontCodec;
    
    
    // left input
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 128;
    p->MinValue = 128;
    p->MaxValue = 164;
    p->MindB = (0 << 16) + 32768;
    p->MaxdB = (18 << 16) + 32768;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft; 
    p->Name = kIOAudioControlChannelNameLeft;
    p->ControlID = 2;
    p->Usage = kIOAudioControlUsageInput;
    p->reg = 0x4;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoFrontCodec;
    
    
    // right input
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 128;
    p->MinValue = 128;
    p->MaxValue = 164;
    p->MindB = (0 << 16) + 32768;
    p->MaxdB = (18 << 16) + 32768;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight; 
    p->Name = kIOAudioControlChannelNameRight;
    p->ControlID = 3;
    p->Usage = kIOAudioControlUsageInput;
    p->reg = 0x5;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->codec = card->RevoFrontCodec;
    p->HasMute = false;
    p->Next = NULL;
}


static void CreateParmsForRevo71(struct CardData *card)
{
    Parm* p = new Parm;
    Parm *prev = NULL;
    card->ParmList = p;
    
    // left output
    p->InitialValue = 0xFF;
    p->MinValue = 1;
    p->MaxValue = 0xFF;
    p->MindB = (-48 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft; 
    p->Name = kIOAudioControlChannelNameLeft;
    p->ControlID = 0;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x3;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = true;
    p->MuteReg =0x1;
    p->MuteOnVal = 0xB;
    p->MuteOffVal = 0xA;
    p->codec = card->RevoFrontCodec;
    
    
    // right output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xFF;
    p->MinValue = 1;
    p->MaxValue = 0xFF;
    p->MindB = (-48 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight; 
    p->Name = kIOAudioControlChannelNameRight;
    p->ControlID = 1;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x4;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoFrontCodec;
    
    // third output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xFF;
    p->MinValue = 159;
    p->MaxValue = 0xFF;
    p->MindB = (-48 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft + 2; 
    p->Name = "Output 3";
    p->ControlID = 2;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x4;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoSurroundCodec;
    
    
    // fourth output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xFF;
    p->MinValue = 159;
    p->MaxValue = 0xFF;
    p->MindB = (-48 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight + 2; 
    p->Name = "Output 4";
    p->ControlID = 3;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x4;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoSurroundCodec;
    
    // fifth output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xFF;
    p->MinValue = 159;
    p->MaxValue = 0xFF;
    p->MindB = (-48 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft + 4; 
    p->Name = "Output 5";
    p->ControlID = 4;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x6;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoSurroundCodec;
    
    
    // sixth output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xFF;
    p->MinValue = 159;
    p->MaxValue = 0xFF;
    p->MindB = (-48 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight + 5; 
    p->Name = "Output 6";
    p->ControlID = 5;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x7;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoSurroundCodec;
    
    // seventh output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xFF;
    p->MinValue = 159;
    p->MaxValue = 0xFF;
    p->MindB = (-48 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft + 6; 
    p->Name = "Output 6";
    p->ControlID = 6;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x8;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoSurroundCodec;
    
    
    // eigth output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xFF;
    p->MinValue = 159;
    p->MaxValue = 0xFF;
    p->MindB = (-48 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight + 7; 
    p->Name = "Output 7";
    p->ControlID = 7;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x9;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoSurroundCodec;
    
    p->Next = NULL;
}


static void CreateParmsForRevo51(struct CardData *card)
{
    Parm* p = new Parm;
    Parm *prev = NULL;
    card->ParmList = p;
    
    // left output
    p->InitialValue = 0x7F;
    p->MinValue = 0x43; // -30 dB
    p->MaxValue = 0x7F; // 0 dB
    p->MindB = (-30 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft; 
    p->Name = kIOAudioControlChannelNameLeft;
    p->ControlID = 0;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x4;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = true;
    p->MuteReg =0x1;
    p->MuteOnVal = 0x3;
    p->MuteOffVal = 0x1;
    p->codec = card->RevoFrontCodec;
    
    
    // right output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x7F;
    p->MinValue = 0x43;
    p->MaxValue = 0x7F;
    p->MindB = (-30 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight; 
    p->Name = kIOAudioControlChannelNameRight;
    p->ControlID = 1;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x5;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoFrontCodec;

    // third output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x7F;
    p->MinValue = 0x43;
    p->MaxValue = 0x7F;
    p->MindB = (-30 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft + 2; 
    p->Name = "Output 3";
    p->ControlID = 2;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x6;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoFrontCodec;
    
    
    // fourth output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x7F;
    p->MinValue = 0x43;
    p->MaxValue = 0x7F;
    p->MindB = (-30 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight + 2; 
    p->Name = "Output 4";
    p->ControlID = 3;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x7;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoFrontCodec;
    
    // fifth output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x7F;
    p->MinValue = 0x43;
    p->MaxValue = 0x7F;
    p->MindB = (-30 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft + 4; 
    p->Name = "Output 5";
    p->ControlID = 4;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x8;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoFrontCodec;
    
    
    // sixth output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x7F;
    p->MinValue = 0x43;
    p->MaxValue = 0x7F;
    p->MindB = (-30 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight + 4; 
    p->Name = "Output 6";
    p->ControlID = 5;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x9;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->codec = card->RevoFrontCodec;
    p->HasMute = false;

    // left input
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x80;
    p->MinValue = 0x80;
    p->MaxValue = 0x98;
    p->MindB = (0 << 16) + 32768;
    p->MaxdB = (12 << 16) + 32768;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft; 
    p->Name = kIOAudioControlChannelNameLeft;
    p->ControlID = 6;
    p->Usage = kIOAudioControlUsageInput;
    p->reg = 0x4;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = card->RevoRecCodec;
    
    
    // right input
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x80;
    p->MinValue = 0x80;
    p->MaxValue = 0x98;
    p->MindB = (0 << 16) + 32768;
    p->MaxdB = (12 << 16) + 32768;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight; 
    p->Name = kIOAudioControlChannelNameRight;
    p->ControlID = 7;
    p->Usage = kIOAudioControlUsageInput;
    p->reg = 0x5;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->codec = card->RevoRecCodec;
    p->HasMute = false;
    p->Next = NULL;
}


// needs set_dac() call
static void CreateParmsForAP192(struct CardData *card)
{
    Parm* p = new Parm;
    Parm *prev = NULL;
    card->ParmList = p;
    
    // left output
    p->InitialValue = 0x7F;
    p->MinValue = 0;
    p->MaxValue = 0x7F;
    p->MindB = (-64 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft; 
    p->Name = kIOAudioControlChannelNameLeft;
    p->ControlID = 0;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x4;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = AK4358_ADDR;
    p->HasMute = false;
    p->MuteReg =0x4;
    p->MuteOnVal = 0x80;
    p->MuteOffVal = 0x0;
    p->codec = NULL;
    
    
    // right output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0x7F;
    p->MinValue = 0;
    p->MaxValue = 0x7F;
    p->MindB = (-64 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight; 
    p->Name = kIOAudioControlChannelNameRight;
    p->ControlID = 1;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x5;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = AK4358_ADDR;
    p->codec = NULL;
    p->HasMute = false;
    p->Next = NULL;
}    


static void CreateParmsForAureonSpace(struct CardData *card)
{
    Parm* p = new Parm;
    Parm *prev = NULL;
    card->ParmList = p;
    
    IOLog("CreateParmsForAureonSpace\n");
    
    // left output
    p->InitialValue = 0xFF;
    p->MinValue = 0xB9;
    p->MaxValue = 0xFF;
    p->MindB = (-35 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft; 
    p->Name = kIOAudioControlChannelNameLeft;
    p->ControlID = 0;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0x9;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = true;
    p->MuteReg =0x14;
    p->MuteOnVal = 0x1;
    p->MuteOffVal = 0x0;
    p->codec = NULL;
    
    
    // right output
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xFF;
    p->MinValue = 0xB9;
    p->MaxValue = 0xFF;
    p->MindB = (-35 << 16) + 32768;
    p->MaxdB = 0;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight; 
    p->Name = kIOAudioControlChannelNameRight;
    p->ControlID = 1;
    p->Usage = kIOAudioControlUsageOutput;
    p->reg = 0xA;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = true;
    p->MuteReg =0x14;
    p->MuteOnVal = 0x1;
    p->MuteOffVal = 0x0;
    p->codec = NULL;
    
    
    // left input
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xC;
    p->MinValue = 0;
    p->MaxValue = 0x1F;
    p->MindB = (-12 << 16) + 32768;
    p->MaxdB = (19 << 16) + 32768;
    p->ChannelID = kIOAudioControlChannelIDDefaultLeft; 
    p->Name = kIOAudioControlChannelNameLeft;
    p->ControlID = 2;
    p->Usage = kIOAudioControlUsageInput;
    p->reg = 0x19;
    p->reverse = false;  
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->HasMute = false;
    p->codec = NULL;
    
    
    // right input
    prev = p;
    p = new Parm;
    prev->Next = p;
    p->InitialValue = 0xC;
    p->MinValue = 0;
    p->MaxValue = 0x1F;
    p->MindB = (-12 << 16) + 32768;
    p->MaxdB = (19 << 16) + 32768;
    p->ChannelID = kIOAudioControlChannelIDDefaultRight; 
    p->Name = kIOAudioControlChannelNameRight;
    p->ControlID = 3;
    p->Usage = kIOAudioControlUsageInput;
    p->reg = 0x1A;
    p->reverse = false; 
    p->I2C = false;
    p->I2C_codec_addr = 0;
    p->codec = NULL;
    p->HasMute = false;
    p->Next = NULL;
}
