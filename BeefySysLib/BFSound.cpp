#include "BFSound.h"

using namespace Beefy;

//////////////////////////////////////////////////////////////////////////

BF_EXPORT int32 BF_CALLTYPE BFSoundManager_LoadSound(BFSoundManager* manager, const char* fileName)
{
	return manager->LoadSound(fileName);
}

BF_EXPORT BFSoundInstance* BF_CALLTYPE BFSoundManager_GetSoundInstance(BFSoundManager* manager, int32 sfxId)
{
	return manager->GetSoundInstance(sfxId);
}

//////////////////////////////////////////////////////////////////////////

BF_EXPORT void BF_CALLTYPE BFSoundInstance_Release(BFSoundInstance* instance)
{
	instance->Release();
}

BF_EXPORT void BF_CALLTYPE BFSoundInstance_SetBaseVolume(BFSoundInstance* instance, float theBaseVolume)
{
	instance->SetBaseVolume(theBaseVolume);
}

BF_EXPORT void BF_CALLTYPE BFSoundInstance_SetBasePan(BFSoundInstance* instance, int theBasePan)
{
	instance->SetBasePan(theBasePan);
}

BF_EXPORT void BF_CALLTYPE BFSoundInstance_SetVolume(BFSoundInstance* instance, float theVolume)
{
	instance->SetVolume(theVolume);
}

BF_EXPORT void BF_CALLTYPE BFSoundInstance_SetPan(BFSoundInstance* instance, int thePosition)
{
	instance->SetPan(thePosition);
}

BF_EXPORT void BF_CALLTYPE BFSoundInstance_AdjustPitch(BFSoundInstance* instance, float theNumSteps)
{
	instance->AdjustPitch(theNumSteps);
}

BF_EXPORT bool BF_CALLTYPE BFSoundInstance_Play(BFSoundInstance* instance, bool looping, bool autoRelease)
{
	return instance->Play(looping, autoRelease);
}

BF_EXPORT void BF_CALLTYPE BFSoundInstance_Stop(BFSoundInstance* instance)
{
	instance->Stop();
}

BF_EXPORT bool BF_CALLTYPE BFSoundInstance_IsPlaying(BFSoundInstance* instance)
{
	return instance->IsPlaying();
}

BF_EXPORT bool BF_CALLTYPE BFSoundInstance_IsReleased(BFSoundInstance* instance)
{
	return instance->IsReleased();
}

BF_EXPORT float BF_CALLTYPE BFSoundInstance_GetVolume(BFSoundInstance* instance)
{
	return (float)instance->GetVolume();
}