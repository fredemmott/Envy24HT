#include "DriverData.h"

#ifndef _Envy24HTAudioDevice_H
#define _Envy24HTAudioDevice_H

#include <IOKit/audio/IOAudioDevice.h>

#ifdef DEBUG
	#define DBGPRINT(msg,...)    IOLog(msg, ##__VA_ARGS__)
#else
	#define DBGPRINT(msg,...)
#endif

class IOPCIDevice;
class IOMemoryMap;

#define Envy24HTAudioDevice com_audio_evolution_driver_Envy24HT


class Envy24HTAudioDevice : public IOAudioDevice
{
    friend class Envy24HTAudioEngine;
    
    OSDeclareDefaultStructors(Envy24HTAudioDevice)
    
	struct CardData *card;

    virtual bool	initHardware(IOService *provider);
    virtual bool	createAudioEngine();
    virtual void	free();
	virtual IOReturn performPowerStateChange(IOAudioDevicePowerState oldPowerState, 
											 IOAudioDevicePowerState newPowerState, 
											 UInt32 *microsecondsUntilComplete);
	
	void dumpRegisters();
    
	static IOReturn volumeChangeHandler(IOService *target, IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn volumeChanged(IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue);
    
    static IOReturn outputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn outputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);

    static IOReturn gainChangeHandler(IOService *target, IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn gainChanged(IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue);
    
    static IOReturn inputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
    virtual IOReturn inputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue);
};

#endif /* _Envy24HTAudioDevice_H */
