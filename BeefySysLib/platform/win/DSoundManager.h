#ifndef __DSOUNDMANAGER_H__
#define __DSOUNDMANAGER_H__

#include <dsound.h>
#include "BFSound.h"

namespace Beefy
{

class DSoundInstance;

class DSoundManager : public BFSoundManager
{
	friend class DSoundInstance;
	friend class DSoundMusicInterface;

protected:
	LPDIRECTSOUNDBUFFER		mSourceSounds[MAX_SOURCE_SOUNDS];
	String					mSourceFileNames[MAX_SOURCE_SOUNDS];
	LPDIRECTSOUNDBUFFER		mPrimaryBuffer;
	int32					mSourceDataSizes[MAX_SOURCE_SOUNDS];
	float					mBaseVolumes[MAX_SOURCE_SOUNDS];
	int						mBasePans[MAX_SOURCE_SOUNDS];
	DSoundInstance*			mPlayingSounds[MAX_CHANNELS];	
	float					mMasterVolume;
	DWORD					mLastReleaseTick;

protected:
	int						FindFreeChannel();
	int						VolumeToDB(float theVolume);
	bool					LoadWAVSound(unsigned int theSfxID, const StringImpl& theFilename);			
	void					ReleaseFreeChannels();

public:
	LPDIRECTSOUND			mDirectSound;	

	DSoundManager(HWND theHWnd);
	virtual ~DSoundManager();

	virtual bool			Initialized() override;
	
	virtual bool			LoadSound(unsigned int theSfxID, const StringImpl& theFilename) override;
	virtual int				LoadSound(const StringImpl& theFilename) override;
	virtual void			ReleaseSound(unsigned int theSfxID) override;

	virtual void			SetVolume(float theVolume) override;
	virtual bool			SetBaseVolume(unsigned int theSfxID, float theBaseVolume) override;
	virtual bool			SetBasePan(unsigned int theSfxID, int theBasePan) override;

	virtual BFSoundInstance* GetSoundInstance(unsigned int theSfxID);

	virtual void			ReleaseSounds() override;
	virtual void			ReleaseChannels() override;		

	virtual float			GetMasterVolume() override;
	virtual void			SetMasterVolume(float theVolume) override;

	virtual void			Flush() override;

	virtual void			SetCooperativeWindow(HWND theHWnd, bool isWindowed);
	virtual void			StopAllSounds() override;
	virtual int				GetFreeSoundId() override;
	virtual int				GetNumSounds() override;
};

}

#endif //__DSOUNDMANAGER_H__