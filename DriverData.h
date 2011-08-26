#ifndef AHI_Drivers_Card_DriverData_h
#define AHI_Drivers_Card_DriverData_h

#include <IOKit/PCI/IOPCIDevice.h>
#include <IOKit/audio/IOAudioDevice.h>

#define UWORD unsigned short
#define ULONG UInt32
#define BOOL bool
#define APTR void *
#define UBYTE UInt8

#include "ak_codec.h"
#include "I2C.h"

#define DATA_PORT               CCS_UART_DATA
#define COMMAND_PORT            CCS_UART_COMMAND
#define STATUS_PORT             CCS_UART_COMMAND

#define STATUSF_OUTPUT          0x40
#define STATUSF_INPUT           0x80

#define COMMAND_RESET           0xff
#define DATA_ACKNOWLEDGE        0xfe
#define COMMAND_UART_MODE       0x3f

#define MPU401_OUTPUT_READY()  ((dev->InByte(card->iobase + STATUS_PORT) & STATUSF_OUTPUT) == 0)
#define MPU401_INPUT_READY()   ((dev->InByte(card->iobase + STATUS_PORT) & STATUSF_INPUT) == 0)

#define MPU401_CMD(c)         dev->OutByte(card->iobase + COMMAND_PORT, c)
#define MPU401_STATUS()       dev->InByte(card->iobase + STATUS_PORT)
#define MPU401_READ()         dev->InByte(card->iobase + DATA_PORT )
#define MPU401_WRITE(v)       dev->OutByte(card->iobase + DATA_PORT,v )

enum Model {AUREON_SKY, AUREON_SPACE, PHASE28, REVO51, REVO71, JULIA, PHASE22, AP192, PRODIGY_HD2, CANTATIS};
extern unsigned long Dirs[];


struct CardData;


#define NUM_SAMPLE_FRAMES	16384

struct Parm
{
    SInt32 InitialValue;
    SInt32 MinValue;
    SInt32 MaxValue;
    IOFixed MindB;
    IOFixed MaxdB;
    UInt32 ChannelID; // enum
    const char *Name;
    UInt32 ControlID;
    UInt32 Usage;
    unsigned char reg; // register
    bool reverse;
    bool I2C;
    int I2C_codec_addr;
    struct akm_codec *codec;
    bool HasMute;
    unsigned char MuteReg;
    unsigned char MuteOnVal;
    unsigned char MuteOffVal;
    struct Parm *Next;
};

struct CardSpecific
{
	UInt32 NumChannels;
	bool HasSPDIF;
	UInt32 BufferSize;
	UInt32 BufferSizeRec;
};

struct CardData
{
    /*** PCI/Card initialization progress *********************************/

   IOPCIDevice  *pci_dev;
   IOMemoryMap*     iobase;
   IOMemoryMap*     mtbase;
   unsigned short		model;
   unsigned char     chiprev;
   unsigned int      irq;
   unsigned long    SavedDir;
   unsigned short   SavedMask;
   
   struct CardSpecific Specific;

    /** TRUE if the Card chip has been initialized */
    BOOL                card_initialized;
    enum Model      SubType;

    struct I2C_bit_ops  *bit_ops;
    unsigned int       gpio_dir;
    unsigned int       gpio_data;
    struct I2C        *i2c;
    
    struct Parm        *ParmList;


    /*** Playback/recording interrupts ***************************************/
    
    /** TRUE when playback is enabled */
    BOOL                is_playing;

    /** TRUE when recording is enabled */
    BOOL                is_recording;

    /** Analog mixer variables ***********************************************/

    /** The currently selected input */
    UWORD               input;

    /** The currently selected output */
    UWORD               output;

    /** The hardware register value corresponding to monitor_volume */
    UWORD               monitor_volume_bits;

    /** The hardware register value corresponding to input_gain */
    UWORD               input_gain_bits;

    /** The hardware register value corresponding to output_volume */
    UWORD               output_volume_bits;

    /** Saved state for AC97 mike */
    UWORD               ac97_mic;

    /** Saved state for AC97 cd */
    UWORD               ac97_cd;
    
    /** Saved state for AC97 vide */
    UWORD               ac97_video;
    
    /** Saved state for AC97 aux */
    UWORD               ac97_aux;
    
    /** Saved state for AC97 line in */
    UWORD               ac97_linein;
    
    /** Saved state for AC97 phone */
    unsigned short               ac97_phone;
    
    // For revo71
    struct akm_codec    *RevoFrontCodec;
    struct akm_codec    *RevoSurroundCodec;
    struct akm_codec    *RevoRecCodec;
    
    struct akm_codec    *JuliaDAC;
    struct akm_codec    *JuliaRCV; // digital receiver
	
	bool				 SPDIF_RateSupported;
};

#endif /* AHI_Drivers_Card_DriverData_h */
