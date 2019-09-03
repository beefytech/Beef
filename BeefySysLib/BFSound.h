#pragma once

#include "Common.h"

NS_BF_BEGIN;

#define MAX_SOURCE_SOUNDS 1024
#define MAX_CHANNELS 32

class BFSoundInstance
{
public:
	virtual ~BFSoundInstance() {}

	virtual void			Release() = 0;

	virtual void			SetBaseVolume(float theBaseVolume) = 0;
	virtual void			SetBasePan(int theBasePan) = 0;

	virtual void			SetVolume(float theVolume) = 0;
	virtual void			SetPan(int thePosition) = 0; //-hundredth db to +hundredth db = left to right	
	virtual void			AdjustPitch(float theNumSteps) = 0;

	virtual bool			Play(bool looping, bool autoRelease) = 0;
	virtual void			Stop() = 0;
	virtual bool			IsPlaying() = 0;
	virtual bool			IsReleased() = 0;
	virtual float			GetVolume() = 0;
};

class BFSoundManager
{
public:
	virtual ~BFSoundManager() {}

	virtual bool			Initialized() = 0;

	virtual bool			LoadSound(unsigned int theSfxID, const StringImpl& theFilename) = 0;
	virtual int				LoadSound(const StringImpl& theFilename) = 0;
	virtual void			ReleaseSound(unsigned int theSfxID) = 0;

	virtual void			SetVolume(float theVolume) = 0;
	virtual bool			SetBaseVolume(unsigned int theSfxID, float theBaseVolume) = 0;
	virtual bool			SetBasePan(unsigned int theSfxID, int theBasePan) = 0;

	virtual BFSoundInstance* GetSoundInstance(unsigned int theSfxID) = 0;

	virtual void			ReleaseSounds() = 0;
	virtual void			ReleaseChannels() = 0;

	virtual float			GetMasterVolume() = 0;
	virtual void			SetMasterVolume(float theVolume) = 0;

	virtual void			Flush() = 0;
	
	virtual void			StopAllSounds() = 0;
	virtual int				GetFreeSoundId() = 0;
	virtual int				GetNumSounds() = 0;
};

NS_BF_END;