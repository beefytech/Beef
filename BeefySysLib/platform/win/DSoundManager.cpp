#pragma warning(disable:4996)

#include "DSoundManager.h"
#include <io.h>
#include <fcntl.h>
#include "DSoundInstance.h"
#include <math.h>

using namespace Beefy;

static HMODULE gDSoundDLL;

#pragma comment(lib, "dsound.lib")

#define SOUND_FLAGS (DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME |  DSBCAPS_STATIC | DSBCAPS_LOCSOFTWARE | DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLFREQUENCY)
DSoundManager::DSoundManager(HWND theHWnd)
{	
	mLastReleaseTick = 0;
	mPrimaryBuffer = NULL;

	int i;

	for (i = 0; i < MAX_SOURCE_SOUNDS; i++)
	{
		mSourceSounds[i] = NULL;
		mBaseVolumes[i] = 1;
		mBasePans[i] = 0;
	}

	for (i = 0; i < MAX_CHANNELS; i++)
		mPlayingSounds[i] = NULL;

	mDirectSound = NULL;

	mMasterVolume = 1.0;
	
	//typedef HRESULT (WINAPI *DirectSoundCreateFunc)(LPCGUID lpcGuid, LPDIRECTSOUND * ppDS, LPUNKNOWN  pUnkOuter);
	//DirectSoundCreateFunc aDirectSoundCreateFunc = (DirectSoundCreateFunc)GetProcAddress(gDSoundDLL,"DirectSoundCreate");	

	// Seems crazy but this was even suggested in MSDN docs for windowless applications
	if (theHWnd == NULL)
		theHWnd = ::GetDesktopWindow();

	//if (aDirectSoundCreateFunc != NULL && aDirectSoundCreateFunc(NULL, &mDirectSound, NULL) == DS_OK)

	if (DirectSoundCreate(NULL, &mDirectSound, NULL) == DS_OK)
	{	
		bool handled = false;

		if (theHWnd != NULL)
		{
			HRESULT aResult = mDirectSound->SetCooperativeLevel(theHWnd, DSSCL_PRIORITY);
			if (SUCCEEDED(aResult))
			{
				// Set primary buffer to 16-bit 44.1Khz
				WAVEFORMATEX aWaveFormat;
				DSBUFFERDESC aBufferDesc;

				// Set up wave format structure.
				int aBitCount = 16;
				int aChannelCount = 2;
				int aSampleRate = 44100;

				// Set up wave format structure.
				memset(&aWaveFormat, 0, sizeof(WAVEFORMATEX));
				aWaveFormat.cbSize = sizeof(WAVEFORMATEX);
				aWaveFormat.wFormatTag = WAVE_FORMAT_PCM;
				aWaveFormat.nChannels = aChannelCount;
				aWaveFormat.nSamplesPerSec = aSampleRate;
				aWaveFormat.nBlockAlign = aChannelCount * aBitCount / 8;
				aWaveFormat.nAvgBytesPerSec =
					aWaveFormat.nSamplesPerSec * aWaveFormat.nBlockAlign;
				aWaveFormat.wBitsPerSample = aBitCount;

				// Set up DSBUFFERDESC structure.
				memset(&aBufferDesc, 0, sizeof(DSBUFFERDESC)); // Zero it out.
				aBufferDesc.dwSize = sizeof(DSBUFFERDESC1);
				aBufferDesc.dwFlags = DSBCAPS_PRIMARYBUFFER;//| DSBCAPS_CTRL3D; // Need default controls (pan, volume, frequency).
				aBufferDesc.dwBufferBytes = 0;
				aBufferDesc.lpwfxFormat = NULL;//(LPWAVEFORMATEX)&aWaveFormat;

				HRESULT aResult = mDirectSound->CreateSoundBuffer(&aBufferDesc, &mPrimaryBuffer, NULL);
				if (aResult == DS_OK)
				{
					aResult = mPrimaryBuffer->SetFormat(&aWaveFormat);
				}
				handled = true;
			}
		}
		
		if (!handled)
		{
			HRESULT aResult = mDirectSound->SetCooperativeLevel(theHWnd,DSSCL_NORMAL);
		}
	}	
}

DSoundManager::~DSoundManager()
{
	ReleaseChannels();
	ReleaseSounds();

	if (mPrimaryBuffer)
		mPrimaryBuffer->Release();

	if (mDirectSound != NULL)
	{	
		mDirectSound->Release();
	}
}

int	DSoundManager::FindFreeChannel()
{
	DWORD aTick = GetTickCount();
	if (aTick-mLastReleaseTick > 1000)
	{
		ReleaseFreeChannels();
		mLastReleaseTick = aTick;
	}

	for (int i = 0; i < MAX_CHANNELS; i++)
	{		
		if (mPlayingSounds[i] == NULL)
			return i;
		
		if (mPlayingSounds[i]->IsReleased())
		{
			delete mPlayingSounds[i];
			mPlayingSounds[i] = NULL;
			return i;
		}
	}
	
	return -1;
}

bool DSoundManager::Initialized()
{
/*
	if (mDirectSound!=NULL)
	{
		mDirectSound->SetCooperativeLevel(theHWnd,DSSCL_NORMAL);
	}
*/

	return (mDirectSound != NULL);
}

int DSoundManager::VolumeToDB(float theVolume)
{
	int aVol = (int) ((log10(1 + theVolume*9) - 1.0) * 2333);
	if (aVol < -2000)
		aVol = -10000;

	return aVol;
}

void DSoundManager::SetVolume(float theVolume)
{
	mMasterVolume = theVolume;

	for (int i = 0; i < MAX_CHANNELS; i++)
		if (mPlayingSounds[i] != NULL)
			mPlayingSounds[i]->RehupVolume();
}

bool DSoundManager::LoadWAVSound(unsigned int theSfxID, const StringImpl& theFilename)
{		
	int aDataSize;

	FILE* fp;

	fp = fopen(theFilename.c_str(), "rb");

	if (fp <= 0)
		return false;	

	char aChunkType[5];	
	aChunkType[4] = '\0';
	uint32 aChunkSize;

	fread(aChunkType, 1, 4, fp);	
	if (!strcmp(aChunkType, "RIFF") == 0)
		return false;
	fread(&aChunkSize, 4, 1, fp);

	fread(aChunkType, 1, 4, fp);	
	if (!strcmp(aChunkType, "WAVE") == 0)
		return false;

	uint16 aBitCount = 16;
	uint16 aChannelCount = 1;
	uint32 aSampleRate = 22050;
	uint8 anXor = 0;

	while (!feof(fp))
	{
		fread(aChunkType, 1, 4, fp);		
		if (fread(&aChunkSize, 4, 1, fp) == 0)
			return false;

		int aCurPos = ftell(fp);

		if (strcmp(aChunkType, "fmt ") == 0)
		{
			uint16 aFormatTag;
			uint32 aBytesPerSec;
			uint16 aBlockAlign;			

			fread(&aFormatTag, 2, 1, fp);
			fread(&aChannelCount, 2, 1, fp);
			fread(&aSampleRate, 4, 1, fp);
			fread(&aBytesPerSec, 4, 1, fp);
			fread(&aBlockAlign, 2, 1, fp);
			fread(&aBitCount, 2, 1, fp);

			if (aFormatTag != 1)
				return false;
		}		
		else if (strcmp(aChunkType, "data") == 0)
		{
			aDataSize = aChunkSize;

			mSourceDataSizes[theSfxID] = aChunkSize;

			PCMWAVEFORMAT aWaveFormat;
			DSBUFFERDESC aBufferDesc;    			

			// Set up wave format structure.
			memset(&aWaveFormat, 0, sizeof(PCMWAVEFORMAT));
			aWaveFormat.wf.wFormatTag = WAVE_FORMAT_PCM;
			aWaveFormat.wf.nChannels = aChannelCount;
			aWaveFormat.wf.nSamplesPerSec = aSampleRate;
			aWaveFormat.wf.nBlockAlign = aChannelCount*aBitCount/8;
			aWaveFormat.wf.nAvgBytesPerSec = 
				aWaveFormat.wf.nSamplesPerSec * aWaveFormat.wf.nBlockAlign;
			aWaveFormat.wBitsPerSample = aBitCount;
			// Set up DSBUFFERDESC structure.
			memset(&aBufferDesc, 0, sizeof(DSBUFFERDESC)); // Zero it out.
			aBufferDesc.dwSize = sizeof(DSBUFFERDESC);
			//aBufferDesc.dwFlags = DSBCAPS_CTRL3D; 
			aBufferDesc.dwFlags = SOUND_FLAGS; //DSBCAPS_CTRLDEFAULT;

			//aBufferDesc.dwFlags = 0;

			aBufferDesc.dwBufferBytes = aDataSize;                                                             
			aBufferDesc.lpwfxFormat = (LPWAVEFORMATEX)&aWaveFormat;

			if (mDirectSound->CreateSoundBuffer(&aBufferDesc, &mSourceSounds[theSfxID], NULL) != DS_OK)
			{				
				fclose(fp);
				return false;
			}


			void* lpvPtr;
			DWORD dwBytes;
			if (mSourceSounds[theSfxID]->Lock(0, aDataSize, &lpvPtr, &dwBytes, NULL, NULL, 0) != DS_OK)
			{
				fclose(fp);
				return false;
			}

			int aReadSize = (int)fread(lpvPtr, 1, aDataSize, fp);
			fclose(fp);

			for (int i = 0; i < aDataSize; i++)
				((uint8*) lpvPtr)[i] ^= anXor;

			if (mSourceSounds[theSfxID]->Unlock(lpvPtr, dwBytes, NULL, NULL) != DS_OK)
				return false;

			if (aReadSize != aDataSize)
				return false;

			return true;
		}

		fseek(fp, aCurPos+aChunkSize, SEEK_SET);
	}
	
	return false;
}

bool DSoundManager::LoadSound(unsigned int theSfxID, const StringImpl& theFilename)
{
	if ((theSfxID < 0) || (theSfxID >= MAX_SOURCE_SOUNDS))
		return false;

	ReleaseSound(theSfxID);

	if (!mDirectSound)
		return true; // sounds just	won't play, but this is not treated as a failure condition

	mSourceFileNames[theSfxID] = theFilename;

	StringImpl aFilename = theFilename;
	
	if (aFilename.EndsWith(".wav", StringImpl::CompareKind_OrdinalIgnoreCase))
	{
		if (LoadWAVSound(theSfxID, aFilename))
			return true;
	}

	return false;
}

int DSoundManager::LoadSound(const StringImpl& theFilename)
{
	int i;
	for (i = 0; i < MAX_SOURCE_SOUNDS; i++)
		if (mSourceFileNames[i] == theFilename)
			return i;

	for (i = MAX_SOURCE_SOUNDS-1; i >= 0; i--)
	{		
		if (mSourceSounds[i] == NULL)
		{
			if (!LoadSound(i, theFilename))
				return -1;
			else
				return i;
		}
	}	

	return -1;
}

void DSoundManager::ReleaseSound(unsigned int theSfxID)
{
	if (mSourceSounds[theSfxID] != NULL)
	{
		mSourceSounds[theSfxID]->Release();
		mSourceSounds[theSfxID] = NULL;
		mSourceFileNames[theSfxID] = "";
	}
}

int DSoundManager::GetFreeSoundId()
{
	for (int i=0; i<MAX_SOURCE_SOUNDS; i++)
	{
		if (mSourceSounds[i]==NULL)
			return i;
	}

	return -1;
}

int DSoundManager::GetNumSounds()
{
	int aCount = 0;
	for (int i=0; i<MAX_SOURCE_SOUNDS; i++)
	{
		if (mSourceSounds[i]!=NULL)
			aCount++;
	}

	return aCount;
}

bool DSoundManager::SetBaseVolume(unsigned int theSfxID, float theBaseVolume)
{
	if ((theSfxID < 0) || (theSfxID >= MAX_SOURCE_SOUNDS))
		return false;

	mBaseVolumes[theSfxID] = theBaseVolume;
	return true;
}

bool DSoundManager::SetBasePan(unsigned int theSfxID, int theBasePan)
{
	if ((theSfxID < 0) || (theSfxID >= MAX_SOURCE_SOUNDS))
		return false;

	mBasePans[theSfxID] = theBasePan;
	return true;
}

BFSoundInstance* DSoundManager::GetSoundInstance(unsigned int theSfxID)
{
	if (theSfxID > MAX_SOURCE_SOUNDS)
		return NULL;

	int aFreeChannel = FindFreeChannel();
	if (aFreeChannel < 0)
		return NULL;

	if (mDirectSound==NULL)
	{
		mPlayingSounds[aFreeChannel] = new DSoundInstance(this, NULL);
	}
	else
	{
		if (mSourceSounds[theSfxID] == NULL)
			return NULL;

		mPlayingSounds[aFreeChannel] = new DSoundInstance(this, mSourceSounds[theSfxID]);
	}

	mPlayingSounds[aFreeChannel]->SetBasePan(mBasePans[theSfxID]);
	mPlayingSounds[aFreeChannel]->SetBaseVolume(mBaseVolumes[theSfxID]);

	return mPlayingSounds[aFreeChannel];
}

void DSoundManager::ReleaseSounds()
{
	for (int i = 0; i < MAX_SOURCE_SOUNDS; i++)
		if (mSourceSounds[i] != NULL)
		{
			mSourceSounds[i]->Release();
			mSourceSounds[i] = NULL;
		}
}

void DSoundManager::ReleaseChannels()
{
	for (int i = 0; i < MAX_CHANNELS; i++)
		if (mPlayingSounds[i] != NULL)
		{
			delete mPlayingSounds[i];
			mPlayingSounds[i] = NULL;
		}
}

void DSoundManager::ReleaseFreeChannels()
{
	for (int i = 0; i < MAX_CHANNELS; i++)
		if (mPlayingSounds[i] != NULL && mPlayingSounds[i]->IsReleased())
		{
			delete mPlayingSounds[i];
			mPlayingSounds[i] = NULL;
		}
}

void DSoundManager::StopAllSounds()
{
	for (int i = 0; i < MAX_CHANNELS; i++)
		if (mPlayingSounds[i] != NULL)
		{
			bool isAutoRelease = mPlayingSounds[i]->mAutoRelease;
			mPlayingSounds[i]->Stop();
			mPlayingSounds[i]->mAutoRelease = isAutoRelease;
		}
}


float DSoundManager::GetMasterVolume()
{
	MIXERCONTROLDETAILS mcd;
	MIXERCONTROLDETAILS_UNSIGNED mxcd_u;
	MIXERLINECONTROLS mxlc;
	MIXERCONTROL mlct;
	MIXERLINE mixerLine;
	HMIXER hmx;
	MIXERCAPS pmxcaps;	

	mixerOpen((HMIXER*) &hmx, 0, 0, 0, MIXER_OBJECTF_MIXER);
	mixerGetDevCaps(0, &pmxcaps, sizeof(pmxcaps));

	mxlc.cbStruct = sizeof(mxlc);	
	mxlc.cbmxctrl = sizeof(mlct);
	mxlc.pamxctrl = &mlct;
	mxlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
	mixerLine.cbStruct = sizeof(mixerLine);
	mixerLine.dwComponentType = MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT;
	mixerGetLineInfo((HMIXEROBJ) hmx, &mixerLine, MIXER_GETLINEINFOF_COMPONENTTYPE);
	mxlc.dwLineID = mixerLine.dwLineID;
	mixerGetLineControls((HMIXEROBJ) hmx, &mxlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);	

	mcd.cbStruct = sizeof(mcd);
	mcd.dwControlID = mlct.dwControlID;
	mcd.cChannels = 1;
	mcd.cMultipleItems = 0;
	mcd.cbDetails = sizeof(mxcd_u);
	mcd.paDetails = &mxcd_u;
		
	mixerGetControlDetails((HMIXEROBJ) hmx, &mcd, 0L);	

	mixerClose(hmx);

	return mxcd_u.dwValue / (float) 0xFFFF;
}

void DSoundManager::SetMasterVolume(float theVolume)
{
	MIXERCONTROLDETAILS mcd;
	MIXERCONTROLDETAILS_UNSIGNED mxcd_u;
	MIXERLINECONTROLS mxlc;
	MIXERCONTROL mlct;
	MIXERLINE mixerLine;
	HMIXER hmx;
	MIXERCAPS pmxcaps;	

	mixerOpen((HMIXER*) &hmx, 0, 0, 0, MIXER_OBJECTF_MIXER);
	mixerGetDevCaps(0, &pmxcaps, sizeof(pmxcaps));

	mxlc.cbStruct = sizeof(mxlc);	
	mxlc.cbmxctrl = sizeof(mlct);
	mxlc.pamxctrl = &mlct;
	mxlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
	mixerLine.cbStruct = sizeof(mixerLine);
	mixerLine.dwComponentType = MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT;
	mixerGetLineInfo((HMIXEROBJ) hmx, &mixerLine, MIXER_GETLINEINFOF_COMPONENTTYPE);
	mxlc.dwLineID = mixerLine.dwLineID;
	mixerGetLineControls((HMIXEROBJ) hmx, &mxlc, MIXER_GETLINECONTROLSF_ONEBYTYPE);	

	mcd.cbStruct = sizeof(mcd);
	mcd.dwControlID = mlct.dwControlID;
	mcd.cChannels = 1;
	mcd.cMultipleItems = 0;
	mcd.cbDetails = sizeof(mxcd_u);
	mcd.paDetails = &mxcd_u;
	
	mxcd_u.dwValue = (int) (0xFFFF * theVolume);
	mixerSetControlDetails((HMIXEROBJ) hmx, &mcd, 0L);

	mixerClose(hmx);
}

void DSoundManager::Flush()
{
}

void DSoundManager::SetCooperativeWindow(HWND theHWnd, bool isWindowed)
{
	if (mDirectSound != NULL)
		mDirectSound->SetCooperativeLevel(theHWnd,DSSCL_NORMAL);
/*
	if (isWindowed==true) mDirectSound->SetCooperativeLevel(theHWnd,DSSCL_NORMAL);
	else mDirectSound->SetCooperativeLevel(theHWnd,DSSCL_EXCLUSIVE);
	*/
}
#undef SOUND_FLAGS
