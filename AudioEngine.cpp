#include "AudioEngine.h"

#include <IOKit/IOLib.h>

#include <IOKit/IOFilterInterruptEventSource.h>

#include <IOKit/pci/IOPCIDevice.h>
#include "regs.h"
#include "misc.h"

#define INITIAL_SAMPLE_RATE	44100

#define FREQUENCIES 15


static const UInt32 Frequencies[ FREQUENCIES ] =
{
    8000,
    9600,
    11025,
    12000,
    16000,
    22050,
    24000,
	32000,
    44100, // CD
    48000,
    64000,
    88200,
    96000,
	176400,
    192000
};

static const UInt32 FrequencyBits[ FREQUENCIES ] =
{
    6,
    3,
    10,
    2,
    5,
    9,
    1,
    4,
    8,
    0,
    15,
    11,
    7,
    12,
    14 // 176.4 KHz: only when CCS_SYSTEM_CONFIG:6 = 1 or (MT_I2S_FORMAT:MT_CLOCK_128x = 1 & CCS_SYSTEM_CONFIG:6 = 0)
};


#define SPDIF_FREQUENCIES 7

static const ULONG SPDIF_Frequencies[ SPDIF_FREQUENCIES ] =
{
	32000,
    44100, // CD
    48000,
    88200,
    96000,
	176400,
    192000
};

static const ULONG SPDIF_FrequencyBits[ SPDIF_FREQUENCIES ] =
{
    3,
    0,
    2,
    4,
    5,
    7,
    6
};



#define super IOAudioEngine

OSDefineMetaClassAndStructors(Envy24HTAudioEngine, IOAudioEngine)

bool Envy24HTAudioEngine::init(struct CardData* i_card)
{
    bool result = false;
    
    DBGPRINT("Envy24HTAudioEngine[%p]::init(%p)\n", this, i_card);

    if (!super::init(NULL)) {
        goto Done;
    }

	if (!i_card) {
		goto Done;
	}
	card = i_card;
	
    result = true;
    
Done:

    return result;
}

bool Envy24HTAudioEngine::initHardware(IOService *provider)
{
    bool result = false;
    IOAudioSampleRate initialSampleRate;
    IOAudioStream *audioStream;
    IOWorkLoop *workLoop;
    
    DBGPRINT("Envy24HTAudioEngine[%p]::initHardware(%p)\n", this, provider);
    
    if (!super::initHardware(provider)) {
        goto Done;
    }
	
    // Setup the initial sample rate for the audio engine
    initialSampleRate.whole = INITIAL_SAMPLE_RATE;
    initialSampleRate.fraction = 0;
    
    //setDescription("Envy24HT Audio Engine");
    
    setSampleRate(&initialSampleRate);
    
    // Set the number of sample frames in each buffer
    setNumSampleFramesPerBuffer(NUM_SAMPLE_FRAMES);
	setSampleOffset(32);
	
	
    workLoop = getWorkLoop();
    if (!workLoop) {
        goto Done;
    }
    
    // Create an interrupt event source through which to receive interrupt callbacks
    // In this case, we only want to do work at primary interrupt time, so
    // we create an IOFilterInterruptEventSource which makes a filtering call
    // from the primary interrupt who's purpose is to determine if
    // our secondary interrupt handler is to be called.  In our case, we
    // can do the work in the filter routine and then return false to
    // indicate that we do not want our secondary handler called
	
	    
    // Allocate our input and output buffers
	outputBuffer = (SInt32 *)IOMallocContiguous(card->Specific.BufferSize, 512, &physicalAddressOutput);
	if (!outputBuffer) {
        goto Done;
    }
	
	outputBufferSPDIF = (SInt32 *)IOMallocContiguous(card->Specific.BufferSizeRec, 512, &physicalAddressOutputSPDIF);
	if (!outputBufferSPDIF) {
        goto Done;
    }

	
	inputBuffer = (SInt32 *)IOMallocContiguous(card->Specific.BufferSizeRec, 512, &physicalAddressInput);
    if (!inputBuffer) {
        goto Done;
    }
	
	card->pci_dev->ioWrite32(MT_DMAI_PB_ADDRESS, physicalAddressOutput, card->mtbase);
	card->pci_dev->ioWrite32(MT_RDMA0_ADDRESS, physicalAddressInput, card->mtbase);
	card->pci_dev->ioWrite32(MT_PDMA4_ADDRESS, physicalAddressOutputSPDIF, card->mtbase); // SPDIF
	card->pci_dev->ioWrite8(MT_SAMPLERATE, 8, card->mtbase); // initialize to 44100 Hz
	card->pci_dev->ioWrite8(MT_DMAI_BURSTSIZE, (8 - card->Specific.NumChannels) / 2, card->mtbase);
	
    // Create an IOAudioStream for each buffer and add it to this audio engine
    audioStream = createNewAudioStream(kIOAudioStreamDirectionOutput, outputBuffer, card->Specific.BufferSize, 0, card->Specific.NumChannels);
    if (!audioStream) {
        goto Done;
    }
	
	addAudioStream(audioStream);
    audioStream->release();

	
    audioStream = createNewAudioStream(kIOAudioStreamDirectionInput, inputBuffer, card->Specific.BufferSizeRec, 1, 2);
    if (!audioStream) {
        goto Done;
    }
    
    addAudioStream(audioStream);
    audioStream->release();
	
	// the interruptEventSource needs to be enabled here, else IRQ sharing doesn't work
	
    // In order to allow the interrupts to be received, the interrupt event source must be
    // added to the IOWorkLoop
    // Additionally, interrupts will not be firing until the interrupt event source is 
    // enabled by calling interruptEventSource->enable() - this probably doesn't need to
    // be done until performAudioEngineStart() is called, and can probably be disabled
    // when performAudioEngineStop() is called and the audio engine is no longer running
    // Although this really depends on the specific hardware
	
	interruptEventSource = IOFilterInterruptEventSource::filterInterruptEventSource(this, 
                                    Envy24HTAudioEngine::interruptHandler, 
                                    Envy24HTAudioEngine::interruptFilter,
                                    audioDevice->getProvider());
    if (!interruptEventSource) {
        goto Done;
    }
	
	interruptEventSource->enable();

    workLoop->addEventSource(interruptEventSource);
		
    result = true;
    
Done:

    return result;
}

void Envy24HTAudioEngine::free()
{
    DBGPRINT("Envy24HTAudioEngine[%p]::free()\n", this);
    
    // We need to free our resources when we're going away
    
    if (interruptEventSource) {
	    interruptEventSource->disable();
        interruptEventSource->release();
        interruptEventSource = NULL;
    }
    
    if (outputBuffer) {
        IOFreeContiguous(outputBuffer, card->Specific.BufferSize);
        outputBuffer = NULL;
    }
	
	if (outputBufferSPDIF) {
        IOFreeContiguous(outputBufferSPDIF, card->Specific.BufferSizeRec);
        outputBufferSPDIF = NULL;
    }
    
    if (inputBuffer) {
		IOFreeContiguous(inputBuffer, card->Specific.BufferSizeRec);
        inputBuffer = NULL;
    }
    
    super::free();
}

IOAudioStream *Envy24HTAudioEngine::createNewAudioStream(IOAudioStreamDirection direction,
														 void *sampleBuffer,
														 UInt32 sampleBufferSize,
														 UInt32 channel,
														 UInt32 channels)
{
    IOAudioStream *audioStream;
	
    // For this sample device, we are only creating a single format and allowing 44.1KHz and 48KHz
    audioStream = new IOAudioStream;

	if (audioStream) {
        if (!audioStream->initWithAudioEngine(this, direction, 1)) {
		    IOLog("initWithAudioEngine failed\n");
			IOSleep(3000);
            audioStream->release();
        } else {
            IOAudioSampleRate rate;
            IOAudioStreamFormat format = {
                channels,										// num channels
                kIOAudioStreamSampleFormatLinearPCM,			// sample format
                kIOAudioStreamNumericRepresentationSignedInt,	// numeric format
                32,												// bit depth
                32,												// bit width
                kIOAudioStreamAlignmentHighByte,				// high byte aligned - unused because bit depth == bit width
                kIOAudioStreamByteOrderBigEndian,    			// little endian
                true,											// format is mixable
                channel											// number of channel
            };

            // As part of creating a new IOAudioStream, its sample buffer needs to be set
            // It will automatically create a mix buffer should it be needed
            audioStream->setSampleBuffer(sampleBuffer, sampleBufferSize);
			
			// This device only allows a single format and a choice of 2 different sample rates
            rate.fraction = 0;

			for (int i = 0; i < FREQUENCIES; i++)
			{
				rate.whole = Frequencies[i];
				audioStream->addAvailableFormat(&format, &rate, &rate, NULL, 0);
			}

						
			// Finally, the IOAudioStream's current format needs to be indicated
            audioStream->setFormat(&format, false);
        }
    }
	else
	{
		IOLog("Couldn't allocate IOAudioStream\n");
		IOSleep(3000);
    }

    return audioStream;
}

void Envy24HTAudioEngine::stop(IOService *provider)
{
    DBGPRINT("Envy24HTAudioEngine[%p]::stop(%p)\n", this, provider);
    
    // When our device is being stopped and torn down, we should go ahead and remove
    // the interrupt event source from the IOWorkLoop
    // Additionally, we'll go ahead and release the interrupt event source since it isn't
    // needed any more
    if (interruptEventSource) {
        IOWorkLoop *wl;
        
        wl = getWorkLoop();
        if (wl) {
            wl->removeEventSource(interruptEventSource);
        }
        
        interruptEventSource->release();
        interruptEventSource = NULL;
    }
    
    // Add code to shut down hardware (beyond what is needed to simply stop the audio engine)
    // There may be nothing needed here

    super::stop(provider);
}
    
IOReturn Envy24HTAudioEngine::performAudioEngineStart()
{
    //DBGPRINT("Envy24HTAudioEngine[%p]::performAudioEngineStart()\n", this);
	
    ClearMask8(card->pci_dev, card->mtbase, MT_DMA_CONTROL, MT_PDMA0_START | MT_PDMA4_START |
			   MT_RDMA0_START | MT_RDMA1_START); // stop
    ClearMask8(card->pci_dev, card->mtbase, MT_INTR_MASK, MT_PDMA0_MASK); // | MT_RDMA0_MASK); // enable irqs
	WriteMask8(card->pci_dev, card->mtbase, MT_INTR_STATUS, MT_DMA_FIFO | MT_PDMA0 | MT_PDMA4 |
			   MT_RDMA0 | MT_RDMA1); // clear possibly pending interrupts


	// Play
	memset(outputBufferSPDIF, 0, card->Specific.BufferSizeRec);
	clearAllSampleBuffers();
    UInt32 BufferSize32 = (card->Specific.BufferSize / 4) - 1;
	UInt16 BufferSize16 = BufferSize32 & 0xFFFF;
	UInt8 BufferSize8 = BufferSize32 >> 16;
	
    card->pci_dev->ioWrite16(MT_DMAI_PB_LENGTH, BufferSize16, card->mtbase);
    card->pci_dev->ioWrite8(MT_DMAI_PB_LENGTH + 2, BufferSize8, card->mtbase);
    card->pci_dev->ioWrite16(MT_DMAI_INTLEN, BufferSize16, card->mtbase);
	card->pci_dev->ioWrite8(MT_DMAI_INTLEN + 2, BufferSize8, card->mtbase);
	//IOLog("Buffer size = %d (%x), BufferSize16 = %u, BufferSize8 = %u\n", card->Specific.BufferSize, card->Specific.BufferSize, BufferSize16, BufferSize8); 
    
	
	// REC
	BufferSize16 = (card->Specific.BufferSizeRec / 4) - 1;
	card->pci_dev->ioWrite16(MT_RDMA0_LENGTH, BufferSize16, card->mtbase);
	card->pci_dev->ioWrite16(MT_RDMA0_INTLEN, BufferSize16, card->mtbase);


    // SPDIF
	unsigned char start = MT_PDMA0_START | MT_RDMA0_START;
      
	if (card->SPDIF_RateSupported && card->Specific.HasSPDIF)
    {
		start |= MT_PDMA4_START;
		IOLog("SPDIF started\n");
		
		card->pci_dev->ioWrite16(MT_PDMA4_LENGTH, BufferSize16, card->mtbase);
		//card->pci_dev->ioWrite16(MT_PDMA4_INTLEN, BufferSize16, card->mtbase);
	}

    //IOLog("START\n");

    // When performAudioEngineStart() gets called, the audio engine should be started from the beginning
    // of the sample buffer.  Because it is starting on the first sample, a new timestamp is needed
    // to indicate when that sample is being read from/written to.  The function takeTimeStamp() 
    // is provided to do that automatically with the current time.
    // By default takeTimeStamp() will increment the current loop count in addition to taking the current
    // timestamp.  Since we are starting a new audio engine run, and not looping, we don't want the loop count
    // to be incremented.  To accomplish that, false is passed to takeTimeStamp(). 
    takeTimeStamp(false);
	
    // Add audio - I/O start code here
	WriteMask8(card->pci_dev, card->mtbase, MT_DMA_CONTROL, start);

    return kIOReturnSuccess;
}

IOReturn Envy24HTAudioEngine::performAudioEngineStop()
{
    unsigned char RMASK = MT_RDMA0_MASK | MT_RDMA1_MASK;
    
	DBGPRINT("Envy24HTAudioEngine[%p]::performAudioEngineStop()\n", this);

    // Add audio - I/O stop code here
    ClearMask8(card->pci_dev, card->mtbase, MT_DMA_CONTROL, MT_PDMA0_START | MT_PDMA4_START);
    WriteMask8(card->pci_dev, card->mtbase, MT_INTR_MASK, MT_DMA_FIFO_MASK | MT_PDMA0_MASK);
	
	ClearMask8(card->pci_dev, card->mtbase, MT_DMA_CONTROL, RMASK);
    WriteMask8(card->pci_dev, card->mtbase, MT_INTR_MASK, RMASK);
	//interruptEventSource->disable();
	
    return kIOReturnSuccess;
}
    
UInt32 Envy24HTAudioEngine::getCurrentSampleFrame()
{
    
    // In order for the erase process to run properly, this function must return the current location of
    // the audio engine - basically a sample counter
    // It doesn't need to be exact, but if it is inexact, it should err towards being before the current location
    // rather than after the current location.  The erase head will erase up to, but not including the sample
    // frame returned by this function.  If it is too large a value, sound data that hasn't been played will be 
    // erased.

    // Change to return the real value
	const UInt32 div = card->Specific.NumChannels * (32 / 8);
	UInt32 current_address = card->pci_dev->ioRead32(MT_DMAI_PB_ADDRESS, card->mtbase);
	UInt32 diff = (current_address - ((UInt32) physicalAddressOutput)) / div;

	return diff;
}
    
IOReturn Envy24HTAudioEngine::performFormatChange(IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate)
{
    DBGPRINT("Envy24HTAudioEngine[%p]::peformFormatChange(%p, %p, %p)\n", this, audioStream, newFormat, newSampleRate);
    
	if (newSampleRate)
	{
		currentSampleRate = newSampleRate->whole;
	}
	else
	{
		currentSampleRate = 44100;
	}
	
	UInt32 FreqBits = lookUpFrequencyBits(currentSampleRate, Frequencies, FrequencyBits, FREQUENCIES, 0x08);
	card->pci_dev->ioWrite8(MT_SAMPLERATE, FreqBits, card->mtbase);
	//IOLog("Freq = %x\n", (unsigned int) FreqBits);

	UInt32 SPDIFBits = lookUpFrequencyBits(currentSampleRate, SPDIF_Frequencies, SPDIF_FrequencyBits, SPDIF_FREQUENCIES, 1000);
	ClearMask8(card->pci_dev, card->iobase, CCS_SPDIF_CONFIG, CCS_SPDIF_INTEGRATED);
	if (SPDIFBits != 1000)
	{
		card->pci_dev->ioWrite16(MT_SPDIF_TRANSMIT, 0x04 | 1 << 5 | (SPDIFBits << 12), card->mtbase);
		WriteMask8(card->pci_dev, card->iobase, CCS_SPDIF_CONFIG, CCS_SPDIF_INTEGRATED);
		//IOLog("Enabled SPDIF %u\n", (unsigned int) SPDIFBits);
	}
	card->SPDIF_RateSupported = (SPDIFBits != 1000);
	
	//IOLog("Rate sup = %d\n", card->SPDIF_RateSupported);
	
    return kIOReturnSuccess;
}


void Envy24HTAudioEngine::interruptHandler(OSObject * owner, IOInterruptEventSource* source, int /*count*/)
{
	//Envy24HTAudioEngine *audioEngine = OSDynamicCast(Envy24HTAudioEngine, owner);

    // We've cast the audio engine from the owner which we passed in when we created the interrupt
    // event source
    //if (audioEngine) {
		//IOLog("RecC = %lu\n", audioEngine->recCounter);
	//}
    // Since our interrupt filter always returns false, this function will never be called
    // If the filter returned true, this function would be called on the IOWorkLoop
}

bool Envy24HTAudioEngine::interruptFilter(OSObject *owner, IOFilterInterruptEventSource *source)
{
    Envy24HTAudioEngine *audioEngine = OSDynamicCast(Envy24HTAudioEngine, owner);

    // We've cast the audio engine from the owner which we passed in when we created the interrupt
    // event source
    if (audioEngine) {
        // Then, filterInterrupt() is called on the specified audio engine
		audioEngine->filterInterrupt(source->getIntIndex());
    }
    
    return false;
}

void Envy24HTAudioEngine::filterInterrupt(int index)
{
    // In the case of our simple device, we only get interrupts when the audio engine loops to the
    // beginning of the buffer.  When that happens, we need to take a timestamp and increment
    // the loop count.  The function takeTimeStamp() does both of those for us.  Additionally,
    // if a different timestamp is to be used (other than the current time), it can be passed
    // in to takeTimeStamp()

	UInt8 intreq;
	
	if ( ( intreq = card->pci_dev->ioRead8(CCS_INTR_STATUS, card->iobase) ) != 0 )
	{
	    card->pci_dev->ioWrite8(CCS_INTR_STATUS, intreq, card->iobase); // clear it
		
	    if (intreq & CCS_INTR_PLAYREC)
        {
		   unsigned char mtstatus = card->pci_dev->ioRead8(MT_INTR_STATUS, card->mtbase);
		   	   
		   if(mtstatus & MT_DMA_FIFO)
           {
		       unsigned char status = card->pci_dev->ioRead8(MT_DMA_UNDERRUN, card->mtbase);
            
			   WriteMask8(card->pci_dev, card->mtbase, MT_INTR_STATUS, MT_DMA_FIFO); // clear it
            
               card->pci_dev->ioWrite8(MT_DMA_UNDERRUN, status, card->mtbase);
               WriteMask8(card->pci_dev, card->mtbase, MT_INTR_MASK, MT_DMA_FIFO);
           }
		
		   card->pci_dev->ioWrite8(MT_INTR_STATUS, mtstatus, card->mtbase); // clear interrupt
		   
		   if(mtstatus & MT_PDMA0)
           {
	           takeTimeStamp();
		   }
        }
    }
			   
	return;
}


UInt32 Envy24HTAudioEngine::lookUpFrequencyBits(UInt32 Frequency,
												const UInt32* FreqList,
												const UInt32* FreqBitList,
												UInt32 ListSize,
												UInt32 Default)
{
	UInt32 FreqBit = Default;

	for (UInt32 i = 0; i < ListSize; i++)
	{
		if (FreqList[i] == Frequency)
		{
			return FreqBitList[i];
		}
	}

	return FreqBit;
}


IOReturn Envy24HTAudioEngine::eraseOutputSamples(
									const void *mixBuf,
									void *sampleBuf,
									UInt32 firstSampleFrame,
									UInt32 numSampleFrames,
									const IOAudioStreamFormat *streamFormat,
									IOAudioStream *audioStream)
{
	IOAudioEngine::eraseOutputSamples(mixBuf, sampleBuf, firstSampleFrame, numSampleFrames, streamFormat, audioStream);
	
	UInt32 skip = (streamFormat->fNumChannels - 2) + 1;
	UInt32 spdifIndex = firstSampleFrame * 2;
    UInt32 maxSampleIndex = (firstSampleFrame + numSampleFrames) * streamFormat->fNumChannels;
    
	for (UInt32 sampleIndex = (firstSampleFrame * streamFormat->fNumChannels); sampleIndex < maxSampleIndex; sampleIndex+=skip)
	{
        outputBufferSPDIF[spdifIndex++] = 0; sampleIndex++;
		outputBufferSPDIF[spdifIndex++] = 0;
    }
    
	return kIOReturnSuccess;
}
 

void Envy24HTAudioEngine::dumpRegisters()
{
    DBGPRINT("Envy24HTAudioEngine[%p]::dumpRegisters()\n", this);
	int i;
	
	DBGPRINT("iobase = %lx, mtbase = %lx\n", card->iobase->getPhysicalAddress(), card->mtbase->getPhysicalAddress());
	// config
	DBGPRINT("Vendor id = %x\n", card->pci_dev->configRead16(0));
	DBGPRINT("Device id = %x\n", card->pci_dev->configRead16(2));
	DBGPRINT("PCI command id = %x\n", card->pci_dev->configRead16(4));
	DBGPRINT("iobase = %lx\n", card->pci_dev->configRead32(0x10));
	DBGPRINT("mtbase = %lx\n", card->pci_dev->configRead32(0x14));
	
	DBGPRINT("---\n");
	for (i = 0; i <= 0x1F; i++)
	{
	  DBGPRINT("CCS %02d: %x\n", i, card->pci_dev->ioRead8(i, card->iobase));
	}
    
	DBGPRINT("---\n");
	for (i = 0; i <= 0x74; i+=4)
	{
	  DBGPRINT("MT %02d (%02x): %lx\n", i, i, card->pci_dev->ioRead32(i, card->mtbase));
  	}
    DBGPRINT("---\n");
	for (i = 0; i <= 0x77; i++)
	{
	  DBGPRINT("MT %02d (%02x): %x\n", i, i, card->pci_dev->ioRead8(i, card->mtbase));
	}

    IOSleep(4000);
}