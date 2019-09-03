#include "DSoundInstance.h"
#include "DSoundManager.h"

using namespace Beefy;

DSoundInstance::DSoundInstance(DSoundManager* theSoundManager, LPDIRECTSOUNDBUFFER theSourceSound)
{
	mSoundManagerP = theSoundManager;
	mReleased = false;
	mAutoRelease = false;
	mHasPlayed = false;
	mSourceSoundBuffer = theSourceSound;
	mSoundBuffer = NULL;

	mBaseVolume = 1.0;
	mBasePan = 0;

	mVolume = 1.0;
	mPan = 0;

	mDefaultFrequency = 44100;

	HRESULT hr;

	if (mSourceSoundBuffer != NULL)
	{
		hr=mSoundManagerP->mDirectSound->DuplicateSoundBuffer(mSourceSoundBuffer, &mSoundBuffer);
		if (hr!=DS_OK)
		{
			switch (hr)
			{
			case DSERR_ALLOCATED: MessageBoxA(0,"DSERR_ALLOCATED","Hey",MB_OK);break;
			case DSERR_INVALIDCALL: MessageBoxA(0,"DSERR_INVALIDCALL","Hey",MB_OK);break;
			case DSERR_INVALIDPARAM: MessageBoxA(0,"DSERR_INVALIDPARAM","Hey",MB_OK);break;
			case DSERR_OUTOFMEMORY: MessageBoxA(0,"DSERR_OUTOFMEMORY","Hey",MB_OK);break;
			case DSERR_UNINITIALIZED: MessageBoxA(0,"DSERR_UNINITIALIZED","Hey",MB_OK);break;
			}
			exit(0);
		}

		mSoundBuffer->GetFrequency(&mDefaultFrequency);
	}

	RehupVolume();
}

DSoundInstance::~DSoundInstance()
{
	if (mSoundBuffer != NULL)
		mSoundBuffer->Release();
}

void DSoundInstance::RehupVolume()
{
	if (mSoundBuffer != NULL)
		mSoundBuffer->SetVolume(mSoundManagerP->VolumeToDB(mBaseVolume * mVolume * mSoundManagerP->mMasterVolume));
}

void DSoundInstance::RehupPan()
{
	if (mSoundBuffer != NULL)
		mSoundBuffer->SetPan(mBasePan + mPan);
}

void DSoundInstance::Release()
{
	Stop();
	mReleased = true;			
}

void DSoundInstance::SetVolume(float theVolume) // 0 = max
{
	mVolume = theVolume;
	RehupVolume();	
}

void DSoundInstance::SetPan(int thePosition) //-db to =db = left to right
{
	mPan = thePosition;
	RehupPan();	
}

void DSoundInstance::SetBaseVolume(float theBaseVolume)
{
	mBaseVolume = theBaseVolume;
	RehupVolume();
}

void DSoundInstance::SetBasePan(int theBasePan)
{
	mBasePan = theBasePan;
	RehupPan();
}

bool DSoundInstance::Play(bool looping, bool autoRelease)
{
	Stop();

	mHasPlayed = true;	
	mAutoRelease = autoRelease;	

	if (mSoundBuffer == NULL)
	{
		return false;
	}

	if (looping)
	{
		if (mSoundBuffer->Play(0, 0, DSBPLAY_LOOPING) != DS_OK)
			return false;
	}
	else
	{
		if (mSoundBuffer->Play(0, 0, 0) != DS_OK)
		{
			return false;
		}
	}

	return true;
}

void DSoundInstance::Stop()
{
	if (mSoundBuffer != NULL)
	{
		mSoundBuffer->Stop();
		mSoundBuffer->SetCurrentPosition(0);
		mAutoRelease = false;
	}
}

//#include "DirectXErrorString.h"
void DSoundInstance::AdjustPitch(float theNumSteps)
{
	if (mSoundBuffer != NULL)
	{
		float aFrequencyMult = powf(1.0594630943592952645618252949463f, theNumSteps);
		float aNewFrequency = mDefaultFrequency*aFrequencyMult;
		if (aNewFrequency < DSBFREQUENCY_MIN)
			aNewFrequency = DSBFREQUENCY_MIN;
		if (aNewFrequency > DSBFREQUENCY_MAX)
			aNewFrequency = DSBFREQUENCY_MAX;

		mSoundBuffer->SetFrequency((DWORD)aNewFrequency);
	}
}

bool DSoundInstance::IsPlaying()
{
	if (!mHasPlayed)
		return false;

	if (mSoundBuffer == NULL)
		return false;

	DWORD aStatus;
	if (mSoundBuffer->GetStatus(&aStatus) == DS_OK)
		// Has the sound stopped?
		return ((aStatus & DSBSTATUS_PLAYING) != 0);
	else
		return false;
}

bool DSoundInstance::IsReleased()
{
	if ((!mReleased) && (mAutoRelease) && (mHasPlayed) && (!IsPlaying()))	
		Release();	

	return mReleased;
}

float DSoundInstance::GetVolume()
{
	return mVolume; 
}

