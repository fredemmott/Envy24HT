#include "AudioEngine.h"
#include <IOKit/IOLib.h>

#define INT_MIN 2147483648.0
#define INT_MAX 2147483647.0
#define INT_MINDIV (1.0 / INT_MIN)
#define INT_MAXDIV (1.0 / INT_MAX)

// The function clipOutputSamples() is called to clip and convert samples from the float mix buffer into the actual
// hardware sample buffer.  The samples to be clipped, are guaranteed not to wrap from the end of the buffer to the
// beginning.
// This implementation is very inefficient, but illustrates the clip and conversion process that must take place.
// Each floating-point sample must be clipped to a range of -1.0 to 1.0 and then converted to the hardware buffer
// format

// The parameters are as follows:
//		mixBuf - a pointer to the beginning of the float mix buffer - its size is based on the number of sample frames
// 					times the number of channels for the stream
//		sampleBuf - a pointer to the beginning of the hardware formatted sample buffer - this is the same buffer passed
//					to the IOAudioStream using setSampleBuffer()
//		firstSampleFrame - this is the index of the first sample frame to perform the clipping and conversion on
//		numSampleFrames - the total number of sample frames to clip and convert
//		streamFormat - the current format of the IOAudioStream this function is operating on
//		audioStream - the audio stream this function is operating on
IOReturn Envy24HTAudioEngine::clipOutputSamples(const void *mixBuf, void *sampleBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream)
{
    UInt32 sampleIndex, maxSampleIndex, spdifIndex;
    float *floatMixBuf;
	float inSample;
    SInt32 *outputSInt32Buf = (SInt32 *)sampleBuf;
    // Start by casting the void * mix and sample buffers to the appropriate types - float * for the mix buffer
    // and SInt32 * for the sample buffer (because our sample hardware uses signed 32-bit samples)
    floatMixBuf = (float *)mixBuf;

    // We calculate the maximum sample index we are going to clip and convert
    // This is an index into the entire sample and mix buffers
    maxSampleIndex = (firstSampleFrame + numSampleFrames) * streamFormat->fNumChannels;
    //IOLog("clip: firstFrame = %lu, numSampleFrames = %lu, channels = %lu, maxSampleIndex = %lu\n", firstSampleFrame, numSampleFrames, streamFormat->fNumChannels, maxSampleIndex);
    
    // Loop through the mix/sample buffers one sample at a time and perform the clip and conversion operations
    for (sampleIndex = (firstSampleFrame * streamFormat->fNumChannels); sampleIndex < maxSampleIndex; sampleIndex++) {
        // Fetch the floating point mix sample
        inSample = floatMixBuf[sampleIndex];
        
        // Clip that sample to a range of -1.0 to 1.0
        // A softer clipping operation could be done here
        if (inSample > 1.0)
		{
            inSample = 1.0;
        }
		else if (inSample < -1.0)
		{
            inSample = -1.0;
        }
        
        // Scale the -1.0 to 1.0 range to the appropriate scale for signed 32-bit samples and then
        // convert to SInt32 and store in the hardware sample buffer
		if (inSample >= 0)
		{
			outputSInt32Buf[sampleIndex] = (SInt32) (inSample * INT_MAX);
		}
		else
		{
			outputSInt32Buf[sampleIndex] = (SInt32) (inSample * INT_MIN);
		}
    }
	
	// Fill SPDIF buffer with first stereo pair mixed sound
	UInt32 skip = (streamFormat->fNumChannels - 2) + 1;
	spdifIndex = firstSampleFrame * 2;
	for (sampleIndex = (firstSampleFrame * streamFormat->fNumChannels); sampleIndex < maxSampleIndex; sampleIndex+=skip)
	{
        outputBufferSPDIF[spdifIndex++] = outputSInt32Buf[sampleIndex++];
		outputBufferSPDIF[spdifIndex++] = outputSInt32Buf[sampleIndex];
    }
	
    
    return kIOReturnSuccess;
}

// The function convertInputSamples() is responsible for converting from the hardware format 
// in the input sample buffer to float samples in the destination buffer and scale the samples 
// to a range of -1.0 to 1.0.  This function is guaranteed not to have the samples wrapped
// from the end of the buffer to the beginning.
// This function only needs to be implemented if the device has any input IOAudioStreams

// This implementation is very inefficient, but illustrates the conversion and scaling that needs to take place.

// The parameters are as follows:
//		sampleBuf - a pointer to the beginning of the hardware formatted sample buffer - this is the same buffer passed
//					to the IOAudioStream using setSampleBuffer()
//		destBuf - a pointer to the float destination buffer - this is the buffer that the CoreAudio.framework uses
//					its size is numSampleFrames * numChannels * sizeof(float)
//		firstSampleFrame - this is the index of the first sample frame to the input conversion on
//		numSampleFrames - the total number of sample frames to convert and scale
//		streamFormat - the current format of the IOAudioStream this function is operating on
//		audioStream - the audio stream this function is operating on
IOReturn Envy24HTAudioEngine::convertInputSamples(const void *sampleBuf, void *destBuf, UInt32 firstSampleFrame, UInt32 numSampleFrames, const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream)
{
    UInt32 numSamplesLeft;
    float *floatDestBuf;
    SInt32 *inputBuf;
	SInt32 inputSample;
	UInt32 i;
    
    // Start by casting the destination buffer to a float *
    floatDestBuf = (float *)destBuf;
    // Determine the starting point for our input conversion 
    inputBuf = &(((SInt32 *)sampleBuf)[firstSampleFrame * streamFormat->fNumChannels]);
    
    // Calculate the number of actual samples to convert
    numSamplesLeft = numSampleFrames * streamFormat->fNumChannels;
    
	//IOLog("convert: %lu %lu %ld\n", numSampleFrames, numSamplesLeft, *inputBuf);
	
    // Loop through each sample and scale and convert them
    for (i = 0; i < numSamplesLeft; i++) {
        // Fetch the SInt32 input sample
        inputSample = *inputBuf;
    
        // Scale that sample to a range of -1.0 to 1.0, convert to float and store in the destination buffer
        // at the proper location
        if (inputSample >= 0) {
            *floatDestBuf = inputSample * INT_MAXDIV;
        } else {
            *floatDestBuf = inputSample * INT_MINDIV;
        }
        
        // Move on to the next sample
        ++inputBuf;
        ++floatDestBuf;
    }

    return kIOReturnSuccess;
}
