using System;

namespace Beefy.sound
{
	struct SoundInstance
	{
		[CallingConvention(.Stdcall), CLink]
		public static extern void* BFSoundInstance_Play(void* nativeSoundInstance, bool looping, bool autoRelease);

		[CallingConvention(.Stdcall), CLink]
		public static extern void BFSoundInstance_Release(void* nativeSoundInstance);

		[CallingConvention(.Stdcall), CLink]
		public static extern bool BFSoundInstance_IsPlaying(void* nativeSoundInstance);

		void* mNativeSoundInstance;

		public this(void* nativeSoundInstance)
		{
			mNativeSoundInstance = nativeSoundInstance;
		}

		public void Dispose() mut
		{
			BFSoundInstance_Release(mNativeSoundInstance);
			mNativeSoundInstance = null;
		}

		public void Play(bool looping = false, bool autoRelease = false)
		{
			if (mNativeSoundInstance == null)
				return;
			BFSoundInstance_Play(mNativeSoundInstance, looping, autoRelease);
		}

		public bool IsPlaying()
		{
			if (mNativeSoundInstance == null)
				return false;
			return BFSoundInstance_IsPlaying(mNativeSoundInstance);
		}
	}
}
