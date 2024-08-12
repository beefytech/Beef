using System;

namespace Beefy.sound
{
	struct SoundSource : int32
	{
		public bool IsInvalid
		{
			get
			{
				return this <= 0;
			}
		}
	}
	
	class SoundManager
	{
		void* mNativeSoundManager;

		[CallingConvention(.Stdcall), CLink]
		public static extern int32 BFSoundManager_LoadSound(void* nativeSoundManager, char8* fileName);

		[CallingConvention(.Stdcall), CLink]
		public static extern void* BFSoundManager_GetSoundInstance(void* nativeSoundManager, int32 sfxId);

		public SoundSource LoadSound(StringView fileName)
		{
			return (.)BFSoundManager_LoadSound(mNativeSoundManager, fileName.ToScopeCStr!());
		}

		public SoundInstance GetSoundInstance(SoundSource soundSource)
		{
			void* nativeSoundInstance = BFSoundManager_GetSoundInstance(mNativeSoundManager, (.)soundSource);
			return .(nativeSoundInstance);
		}

		public void PlaySound(SoundSource soundSource)
		{
			let soundInstance = GetSoundInstance(soundSource);
			soundInstance.Play(false, true);
		}
	}
}
