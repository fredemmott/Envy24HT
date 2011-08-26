#ifndef _Envy24HTAudioEngine_H
#define _Envy24HTAudioEngine_H

#include <IOKit/audio/IOAudioEngine.h>

#include "AudioDevice.h"

#define Envy24HTAudioEngine com_Envy24HTAudioEngine

class IOFilterInterruptEventSource;
class IOInterruptEventSource;

class Envy24HTAudioEngine : public IOAudioEngine
{
    OSDeclareDefaultStructors(Envy24HTAudioEngine)
    
public:

    virtual bool	init(struct CardData* i_card);
    virtual void	free();
    
    virtual bool	initHardware(IOService *provider);
    virtual void	stop(IOService *provider);
	
	UInt32 lookUpFrequencyBits(UInt32 Frequency, const UInt32* FreqList, const UInt32* FreqBitList, UInt32 ListSize, UInt32 Default);
    virtual void	dumpRegisters();

	virtual IOAudioStream *createNewAudioStream(IOAudioStreamDirection direction, void *sampleBuffer, UInt32 sampleBufferSize, UInt32 channel, UInt32 channels);

    virtual IOReturn performAudioEngineStart();
    virtual IOReturn performAudioEngineStop();
    
    virtual UInt32 getCurrentSampleFrame();
    
    virtual IOReturn performFormatChange(IOAudioStream *audioStream, const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate);

    virtual IOReturn clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream);
    virtual IOReturn convertInputSamples(const void *sampleBuf, void *destBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream);
    
    static void interruptHandler(OSObject *owner, IOInterruptEventSource *source, int count);
    static bool interruptFilter(OSObject *owner, IOFilterInterruptEventSource *source);
    virtual void filterInterrupt(int index);
	
	virtual IOReturn eraseOutputSamples(const void *mixBuf,
										void *sampleBuf,
									    UInt32 firstSampleFrame,
									    UInt32 numSampleFrames,
									    const IOAudioStreamFormat *streamFormat,
										IOAudioStream *audioStream);
	
private:
	struct CardData				   *card;
	UInt32							currentSampleRate;
    
	SInt32							*inputBuffer;
    SInt32							*outputBuffer;
	SInt32							*outputBufferSPDIF;
    
	IOPhysicalAddress               physicalAddressInput;
	IOPhysicalAddress               physicalAddressOutput;
	IOPhysicalAddress               physicalAddressOutputSPDIF;
    
    IOFilterInterruptEventSource	*interruptEventSource;
};

#endif /* _Envy24HTAudioEngine_H */
