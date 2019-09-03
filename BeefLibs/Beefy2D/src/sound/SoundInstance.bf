using System;

namespace Beefy.sound
{
	struct SoundInstance
	{
		[StdCall, CLink]
		public static extern void* BFSoundInstance_Play(void* nativeSoundInstance, bool looping, bool autoRelease);

		[StdCall, CLink]
		public static extern void BFSoundInstance_Release(void* nativeSoundInstance);

		[StdCall, CLink]
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
