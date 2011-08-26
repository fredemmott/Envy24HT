#include "AudioDevice.h"

#include "AudioEngine.h"

#include <IOKit/audio/IOAudioControl.h>
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>
#include <IOKit/audio/IOAudioDefines.h>

#include <IOKit/IOLib.h>

#include <IOKit/pci/IOPCIDevice.h>
#include "misc.h"

#define super IOAudioDevice
extern int set_dac(struct CardData *card, int reg, int level);

OSDefineMetaClassAndStructors(Envy24HTAudioDevice, IOAudioDevice)

bool Envy24HTAudioDevice::initHardware(IOService *provider)
{
    bool result = false;
    
	DBGPRINT("Envy24HTAudioDevice[%p]::initHardware(%p)\n", this, provider);
    
    if (!super::initHardware(provider)) {
        goto Done;
    }
	
	card = new CardData;
	if (!card)
	{
	  IOLog("Couldn't allocate memory for CardData!\n");
	  IOSleep(3000);
	  goto Done;
	}
	
	card->pci_dev = OSDynamicCast(IOPCIDevice, provider);
	if (!card->pci_dev)
	{
	  IOLog("Couldn't get pci_dev!\n");
	  IOSleep(3000);
	  goto Done;
	}
	
	card->pci_dev->setIOEnable(true);
    card->pci_dev->setBusMasterEnable(true);
    
	// NAMES CHANGED
    setDeviceName("Envy24HT");
    setDeviceShortName("Envy24HT");
    setManufacturerName("VIA/ICE");

    // Config a map for the PCI config base registers
    // We need to keep this map around until we're done accessing the registers
    card->iobase = card->pci_dev->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!card->iobase) {
        goto Done;
		}
	
	card->mtbase = card->pci_dev->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress1);
    if (!card->mtbase) {
        goto Done;
    }
	
	if (AllocDriverData(card->pci_dev, card) == NULL)
	{
		goto Done;
	}

    if (!createAudioEngine()) {
        goto Done;
    }
    
    result = true;
    
Done:

    if (!result) {
	    IOLog("Something failed!\n");
		IOSleep(3000);
        if (card && card->iobase) {
            card->iobase->release();
            card->iobase = NULL;
        }
		if (card && card->mtbase) {
            card->mtbase->release();
            card->mtbase = NULL;
        }

		if (card)
		{
			FreeDriverData(card);
			delete card;
	    }
    }

	//IOLog("Envy24HTAudioDevice::initHardware returns %d\n", result);
	
    return result;
}

void Envy24HTAudioDevice::free()
{
    DBGPRINT("Envy24HTAudioDevice[%p]::free()\n", this);
    
	if (card)
	{
      if (card->iobase) {
          card->iobase->release();
          card->iobase = NULL;
      }
	
	  if (card->mtbase) {
          card->mtbase->release();
          card->mtbase = NULL;
      }
	  
	  FreeDriverData(card);
	
	  delete card;
	}
	
    
    super::free();
}


bool Envy24HTAudioDevice::createAudioEngine()
{
    bool result = false;
    Envy24HTAudioEngine *audioEngine = NULL;
    IOAudioControl *control;
	struct Parm *p = card->ParmList;
    
    DBGPRINT("Envy24HTAudioDevice[%p]::createAudioEngine()\n", this);
    
    audioEngine = new Envy24HTAudioEngine;
    if (!audioEngine) {
        goto Done;
    }
    
    // Init the new audio engine with the device registers so it can access them if necessary
    // The audio engine subclass could be defined to take any number of parameters for its
    // initialization - use it like a constructor
    if (!audioEngine->init(card)) {
        goto Done;
    }
    
    while (p != NULL)
    {
        control = IOAudioLevelControl::createVolumeControl(p->InitialValue,     // Initial value
                                                           p->MinValue,         // min value
                                                           p->MaxValue,         // max value
                                                           p->MindB,    // -144 in IOFixed (16.16)
                                                           p->MaxdB,            // max 0.0 in IOFixed
                                                           p->ChannelID,
                                                           p->Name,
                                                           p->ControlID,                // control ID - driver-defined,
                                                           p->Usage);
        if (!control) {
            IOLog("Failed to create control!\n");
            goto Done;
        }
        
        control->setValueChangeHandler((IOAudioControl::IntValueChangeHandler)volumeChangeHandler, this);
        audioEngine->addDefaultAudioControl(control);
        control->release();
        
        if (p->HasMute)
        {
            // Create an output mute control
            control = IOAudioToggleControl::createMuteControl(false,	// initial state - unmuted
                                                              kIOAudioControlChannelIDAll,	// Affects all channels
                                                              kIOAudioControlChannelNameAll,
                                                              p->ControlID,		// control ID - driver-defined
                                                              kIOAudioControlUsageOutput);
            
            control->setValueChangeHandler((IOAudioControl::IntValueChangeHandler)outputMuteChangeHandler, this);
            audioEngine->addDefaultAudioControl(control);
            control->release();
        }
        
        p = p->Next;
    }
    
#if 0
	
    // Create an output mute control
    control = IOAudioToggleControl::createMuteControl(false,	// initial state - unmuted
                                                        kIOAudioControlChannelIDAll,	// Affects all channels
                                                        kIOAudioControlChannelNameAll,
                                                        0,		// control ID - driver-defined
                                                        kIOAudioControlUsageOutput);
                                
    if (!control) {
        goto Done;
    }
        
    control->setValueChangeHandler((IOAudioControl::IntValueChangeHandler)outputMuteChangeHandler, this);
    audioEngine->addDefaultAudioControl(control);
    control->release();

           
    control->setValueChangeHandler((IOAudioControl::IntValueChangeHandler)gainChangeHandler, this);
    audioEngine->addDefaultAudioControl(control);
    control->release();

    // Create an input mute control
    control = IOAudioToggleControl::createMuteControl(false,	// initial state - unmuted
                                                        kIOAudioControlChannelIDAll,	// Affects all channels
                                                        kIOAudioControlChannelNameAll,
                                                        0,		// control ID - driver-defined
                                                        kIOAudioControlUsageInput);
                                
    if (!control) {
        goto Done;
    }
        
    control->setValueChangeHandler((IOAudioControl::IntValueChangeHandler)inputMuteChangeHandler, this);
    audioEngine->addDefaultAudioControl(control);
    control->release();
#endif

    // Active the audio engine - this will cause the audio engine to have start() and initHardware() called on it
    // After this function returns, that audio engine should be ready to begin vending audio services to the system
    activateAudioEngine(audioEngine);
    // Once the audio engine has been activated, release it so that when the driver gets terminated,
    // it gets freed

    audioEngine->release();
    
    result = true;
    
Done:

    if (!result && (audioEngine != NULL)) {
        audioEngine->release();
    }

    return result;
}

IOReturn Envy24HTAudioDevice::volumeChangeHandler(IOService *target, IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue)
{
    IOReturn result = kIOReturnBadArgument;
    Envy24HTAudioDevice *audioDevice;
    
    audioDevice = (Envy24HTAudioDevice *)target;
    if (audioDevice) {
        result = audioDevice->volumeChanged(volumeControl, oldValue, newValue);
    }
  
    return result;
}

IOReturn Envy24HTAudioDevice::volumeChanged(IOAudioControl *volumeControl, SInt32 oldValue, SInt32 newValue)
{
    //IOLog("Envy24AudioDevice[%p]::volumeChanged(%p, %ld, %ld)\n", this, volumeControl, oldValue, newValue);
    
    // Add hardware volume code change 
    if (oldValue != newValue) {
        struct Parm *p = card->ParmList;
        
        while (p != NULL) // look up parm
        {
            if (volumeControl->getControlID() == p->ControlID) 
            {
                unsigned char val = newValue;
                //val = val | (val << 8);
                
                //IOLog("write reg %d, val %d\n", p->reg, val);
                if (p->I2C)
                {
                    WriteI2C(card->pci_dev, card, p->I2C_codec_addr, p->reg, val | 0x80);
                }
                else
                {
                    if (val <= p->MinValue && p->codec)
                    {
                        if (p->codec->type == AKM4528 ||
                            p->codec->type == AKM4524 ||
                            p->codec->type == AKM4355 ||
                            p->codec->type == AKM4381)
                        {
                            val = 0;
                        }
                    }
                    
                    if (p->codec && p->codec->type == AKM4358)
                    {
                        val |= 0x80; // enable
                    }
                    //IOLog("AKM write reg %d, val %d\n", p->reg, val);

                    if (card->SubType == AP192)
                    {
                        set_dac(card, p->reg, val);
                    }
                    else if (card->SubType == AUREON_SPACE || card->SubType == AUREON_SKY)
                    {
                        wm_put(card, card->iobase, p->reg, val | 0x100);
                    }
                    else
                    {
                        akm4xxx_write(card, p->codec, 0, p->reg, val);
                    }

                }
                break;
            }
            
            p = p->Next;
        }
    }
    
    return kIOReturnSuccess;
}
    
IOReturn Envy24HTAudioDevice::outputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue)
{
    IOReturn result = kIOReturnBadArgument;
    Envy24HTAudioDevice *audioDevice;
    
    audioDevice = (Envy24HTAudioDevice *)target;
    if (audioDevice) {
        result = audioDevice->outputMuteChanged(muteControl, oldValue, newValue);
    }

	return result;
}

IOReturn Envy24HTAudioDevice::outputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue)
{
    //DBGPRINT("Envy24HTAudioDevice[%p]::outputMuteChanged(%p, %ld, %ld)\n", this, muteControl, oldValue, newValue);
    
	// Add output mute code here
	// Add hardware volume code change 
    if (oldValue != newValue) {
        struct Parm *p = card->ParmList;
        
        while (p != NULL) // look up parm
        {
            if (muteControl->getControlID() == p->ControlID) 
            {
                if (p->HasMute)
                {
                    if (p->I2C)
                    {
                        WriteI2C(card->pci_dev, card, p->I2C_codec_addr, p->MuteReg, newValue > 0 ? p->MuteOnVal : p->MuteOffVal);
                    }
                    else if (p->codec)
                    {
                        akm4xxx_write(card, p->codec, 0, p->MuteReg, newValue > 0 ? p->MuteOnVal : p->MuteOffVal);
                    }
                    else if (card->SubType == AUREON_SPACE || card->SubType == AUREON_SKY)
                    {
                        wm_put(card, card->iobase, p->MuteReg, newValue > 0 ? p->MuteOnVal : p->MuteOffVal);
                    }
                    
                }
                break;
            }
            
            p = p->Next;
        }
    }
    

	return kIOReturnSuccess;
}

IOReturn Envy24HTAudioDevice::gainChangeHandler(IOService *target, IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue)
{
    IOReturn result = kIOReturnBadArgument;
    Envy24HTAudioDevice *audioDevice;
    
    audioDevice = (Envy24HTAudioDevice *)target;
    if (audioDevice) {
        result = audioDevice->gainChanged(gainControl, oldValue, newValue);
    }
    
    return result;
}

IOReturn Envy24HTAudioDevice::gainChanged(IOAudioControl *gainControl, SInt32 oldValue, SInt32 newValue)
{
    DBGPRINT("Envy24HTAudioDevice[%p]::gainChanged(%p, %ld, %ld)\n", this, gainControl, oldValue, newValue);
    
    if (gainControl) {
        DBGPRINT("\t-> Channel %ld\n", gainControl->getChannelID());
    }
    
    // Add hardware gain change code here 
//#warning TODO gainChanged()   
    return kIOReturnSuccess;
}
    
IOReturn Envy24HTAudioDevice::inputMuteChangeHandler(IOService *target, IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue)
{
    IOReturn result = kIOReturnBadArgument;
    Envy24HTAudioDevice *audioDevice;
    
    audioDevice = (Envy24HTAudioDevice *)target;
    if (audioDevice) {
        result = audioDevice->inputMuteChanged(muteControl, oldValue, newValue);
    }
    
    return result;
}

IOReturn Envy24HTAudioDevice::inputMuteChanged(IOAudioControl *muteControl, SInt32 oldValue, SInt32 newValue)
{
    DBGPRINT("Envy24HTAudioDevice[%p]::inputMuteChanged(%p, %ld, %ld)\n", this, muteControl, oldValue, newValue);
    
    // Add input mute change code here
//#warning TODO inputMuteChanged()    
    return kIOReturnSuccess;
}


/*
 typedef enum _IOAudioDevicePowerState { 
 kIOAudioDeviceSleep = 0, // When sleeping 
 kIOAudioDeviceIdle = 1, // When no audio engines running 
 kIOAudioDeviceActive = 2 // audio engines running 
 } IOAudioDevicePowerState;  
 */

IOReturn Envy24HTAudioDevice::performPowerStateChange(IOAudioDevicePowerState oldPowerState, 
													  IOAudioDevicePowerState newPowerState, 
								 					  UInt32 *microsecondsUntilComplete)
{
	IOLog("Envy24HTAudioDevice::performPowerStateChange!, old = %d, new = %d\n", oldPowerState, newPowerState);
	
	if (newPowerState == kIOAudioDeviceSleep) // go to sleep, power down and save settings
	{
		IOLog("Envy24HTAudioDevice::performPowerStateChange -> entering sleep\n");
		deactivateAllAudioEngines();
        }
	else if (newPowerState != kIOAudioDeviceSleep &&
			 oldPowerState == kIOAudioDeviceSleep) // wake from sleep, power up and restore settings
	{
		IOLog("Envy24HTAudioDevice::performPowerStateChange -> waking up!\n");
		card_init(card);
		createAudioEngine();
	}
	
	return kIOReturnSuccess;
}


void Envy24HTAudioDevice::dumpRegisters()
{
    DBGPRINT("Envy24HTAudioDevice[%p]::dumpRegisters()\n", this);
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
	for (i = 0; i <= 0x44; i+=4)
	{
	  DBGPRINT("MT %02d: %lx\n", i, card->pci_dev->ioRead32(i, card->mtbase));
	}
    IOSleep(3000);
}
