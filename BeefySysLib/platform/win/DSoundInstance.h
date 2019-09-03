#ifndef __DSOUNDINSTANCE_H__
#define __DSOUNDINSTANCE_H__

#include <dsound.h>
#include "BFSound.h"

//#define _LPCWAVEFORMATEX_DEFINED

namespace Beefy
{

class DSoundManager;

class DSoundInstance : public BFSoundInstance
{
	friend class DSoundManager;

protected:
	DSoundManager*			mSoundManagerP;
	LPDIRECTSOUNDBUFFER	mSourceSoundBuffer;
	LPDIRECTSOUNDBUFFER	mSoundBuffer;
	bool					mAutoRelease;
	bool					mHasPlayed;
	bool					mReleased;

	int						mBasePan;
	float					mBaseVolume;

	int						mPan;
	float					mVolume;

	DWORD					mDefaultFrequency;

protected:
	void					RehupVolume();
	void					RehupPan();

public:
	DSoundInstance(DSoundManager* theSoundManager, LPDIRECTSOUNDBUFFER theSourceSound);
	virtual ~DSoundInstance();	
	virtual void			Release() override;
	
	virtual void			SetBaseVolume(float theBaseVolume) override;
	virtual void			SetBasePan(int theBasePan) override;

	virtual void			SetVolume(float theVolume) override;
	virtual void			SetPan(int thePosition) override; //-hundredth db to +hundredth db = left to right	
	virtual void			AdjustPitch(float theNumSteps) override;

	virtual bool			Play(bool looping, bool autoRelease) override;
	virtual void			Stop() override;
	virtual bool			IsPlaying() override;
	virtual bool			IsReleased() override;
	virtual float			GetVolume() override;

};

}

#endif //__DSOUNDINSTANCE_H__