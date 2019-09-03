#region License
/* SDL2# - C# Wrapper for SDL2
 *
 * Copyright (c) 2013-2016 Ethan Lee.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */
#endregion

#region Using Statements
using System;
#endregion

namespace SDL2
{
	public static class SDLMixer
	{
		/* Similar to the headers, this is the version we're expecting to be
		 * running with. You will likely want to check this somewhere in your
		 * program!
		 */
		public const int32 MIXER_MAJOR_VERSION =	2;
		public const int32 MIXER_MINOR_VERSION =	0;
		public const int32 MIXER_PATCHLEVEL =		2;

		/* In C, you can redefine this value before including SDL_mixer.h.
		 * We're not going to allow this in SDL2#, since the value of this
		 * variable is persistent and not dependent on preprocessor ordering.
		 */
		public const int32 MIX_CHANNELS = 8;

		public static readonly int32 MIX_DEFAULT_FREQUENCY = 22050;
		public static readonly uint16 MIX_DEFAULT_FORMAT =
			BitConverter.IsLittleEndian ? SDL.AUDIO_S16LSB : SDL.AUDIO_S16MSB;
		public static readonly int32 MIX_DEFAULT_CHANNELS = 2;
		public static readonly uint8 MIX_MAX_VOLUME = 128;

		public enum MIX_InitFlags
		{
			Flac =		0x00000001,
			Mod =		0x00000002,
			Mp3 =		0x00000008,
			Ogg =		0x00000010,
			Mid =		0x00000020,
		}

		public enum Fading
		{
			NoFading,
			FadingOut,
			FadingIn
		}

		public enum MusicType
		{
			None,
			Cmd,
			Wav,
			Mod,
			Mid,
			Ogg,
			Mp3,
			Mp3Mad,
			Flac,
			Modplug
		}
		
		public function void MixFuncDelegate(void* udata, uint8* stream, int32 len);
		
		public function void Mix_EffectFunc_t(int32 chan, void* stream, int32 len, void* udata);
		
		public function void Mix_EffectDone_t(int32 chan, void* udata);

		public function void MusicFinishedDelegate();

		public function void ChannelFinishedDelegate(int32 channel);

		public function int32 SoundFontDelegate(char8* a, void* b);

		public static void SDL_MIXER_VERSION(out SDL.Version X)
		{
			X.major = MIXER_MAJOR_VERSION;
			X.minor = MIXER_MINOR_VERSION;
			X.patch = MIXER_PATCHLEVEL;
		}

		[LinkName("Version ")]
		public static extern SDL.Version MIX_Linked_Version();
		

		[LinkName("Mix_Init")]
		public static extern int32 Init(MIX_InitFlags flags);

		[LinkName("Mix_Quit")]
		public static extern void Quit();

		[LinkName("Mix_OpenAudio")]
		public static extern int32 OpenAudio(
			int frequency,
			uint16 format,
			int channels,
			int chunksize
		);

		[LinkName("Mix_AllocateChannels")]
		public static extern int32 AllocateChannels(int32 numchans);

		[LinkName("Mix_QuerySpec")]
		public static extern int32 QuerySpec(
			out int32 frequency,
			out uint16 format,
			out int32 channels
		);

		/* src refers to an SDL_RWops*, IntPtr to a Mix_Chunk* */
		/* THIS IS A PUBLIC RWops FUNCTION! */
		[LinkName("Mix_LoadWAV_RW")]
		public static extern Chunk* LoadWAV_RW(
			SDL.RWOps* src,
			int freesrc
		);
		
		public static Chunk* LoadWAV(StringView file)
		{
			SDL.RWOps* rwops = SDL.RWFromFile(file.ToScopeCStr!(), "rb");
			return LoadWAV_RW(rwops, 1);
		}

		[LinkName("Mix_LoadMUS")]
		public static extern Music* LoadMUS(char8* file);

		[LinkName("Mix_QuickLoad_WAV")]
		public static extern Chunk* QuickLoad_WAV(uint8* mem);

		[LinkName("Mix_QuickLoad_RAW")]
		public static extern Chunk* QuickLoad_RAW(uint8* mem, uint len);

		[LinkName("Mix_FreeChunk")]
		public static extern void FreeChunk(Chunk* chunk);

		[LinkName("Mix_FreeMusic")]
		public static extern void FreeMusic(Music* music);

		[LinkName("Mix_GetNumChunkDecoders")]
		public static extern int32 GetNumChunkDecoders();

		[LinkName("Mix_GetChunkDecoder")]
		public static extern char8* Mix_GetChunkDecoder(int32 index);

		[LinkName("Mix_GetNumMusicDecoders")]
		public static extern int32 GetNumMusicDecoders();

		[LinkName("Mix_GetMusicDecoder")]
		public static extern char8* GetMusicDecoder(int32 index);

		[LinkName("Mix_GetMusicType")]
		public static extern MusicType GetMusicType(Music* music);

		[LinkName("Mix_SetPostMix")]
		public static extern void SetPostMix(
			MixFuncDelegate mix_func,
			void* arg
		);

		[LinkName("Mix_HookMusic")]
		public static extern void HookMusic(
			MixFuncDelegate mix_func,
			void* arg
		);

		[LinkName("Mix_HookMusicFinished")]
		public static extern void HookMusicFinished(
			MusicFinishedDelegate music_finished
		);

		/* IntPtr refers to a void* */
		[LinkName("Mix_GetMusicHookData")]
		public static extern void* GetMusicHookData();

		[LinkName("Mix_ChannelFinished")]
		public static extern void ChannelFinished(
			ChannelFinishedDelegate channel_finished
		);

		[LinkName("Mix_RegisterEffect")]
		public static extern int32 RegisterEffect(
			int chan,
			Mix_EffectFunc_t f,
			Mix_EffectDone_t d,
			void* arg // void*
		);

		[LinkName("Mix_UnregisterEffect")]
		public static extern int32 UnregisterEffect(
			int channel,
			Mix_EffectFunc_t f
		);

		[LinkName("Mix_UnregisterAllEffects")]
		public static extern int32 UnregisterAllEffects(int32 channel);

		[LinkName("Mix_SetPanning")]
		public static extern int32 SetPanning(
			int channel,
			uint8 left,
			uint8 right
		);

		[LinkName("Mix_SetPosition")]
		public static extern int32 SetPosition(
			int channel,
			int16 angle,
			uint8 distance
		);

		[LinkName("Mix_SetDistance")]
		public static extern int32 SetDistance(int32 channel, uint8 distance);

		[LinkName("Mix_SetReverseStereo")]
		public static extern int32 SetReverseStereo(int32 channel, int32 flip);

		[LinkName("Mix_ReserveChannels")]
		public static extern int32 ReserveChannels(int32 num);

		[LinkName("Mix_GroupChannel")]
		public static extern int32 GroupChannel(int32 which, int32 tag);

		[LinkName("Mix_GroupChannels")]
		public static extern int32 GroupChannels(int32 from, int32 to, int32 tag);

		[LinkName("Mix_GroupAvailable")]
		public static extern int32 GroupAvailable(int32 tag);

		[LinkName("Mix_GroupCount")]
		public static extern int32 GroupCount(int32 tag);

		[LinkName("Mix_GroupOldest")]
		public static extern int32 GroupOldest(int32 tag);

		[LinkName("Mix_GroupNewer")]
		public static extern int32 GroupNewer(int32 tag);

		public struct Music;
		public struct Chunk;

		public static int32 PlayChannel(int channel, Chunk* chunk, int loops)
		{
			return PlayChannelTimed(channel, chunk, loops, -1);
		}

		/* chunk refers to a Mix_Chunk* */
		[LinkName("Mix_PlayChannelTimed")]
		public static extern int32 PlayChannelTimed(
			int channel,
			Chunk* chunk,
			int loops,
			int ticks
		);

		[LinkName("Mix_PlayMusic")]
		public static extern int32 PlayMusic(Music* music, int32 loops);

		[LinkName("Mix_FadeInMusic")]
		public static extern int32 FadeInMusic(
			Music* music,
			int loops,
			int ms
		);

		[LinkName("Mix_FadeInMusicPos")]
		public static extern int32 FadeInMusicPos(
			Music* music,
			int loops,
			int ms,
			double position
		);

		public static int32 Mix_FadeInChannel(
			int channel,
			Chunk* chunk,
			int loops,
			int ms
		)
		{
			return FadeInChannelTimed(channel, chunk, loops, ms, -1);
		}

		[LinkName("Mix_FadeInChannelTimed")]
		public static extern int32 FadeInChannelTimed(
			int channel,
			Chunk* chunk,
			int loops,
			int ms,
			int ticks
		);

		[LinkName("Mix_Volume")]
		public static extern int32 Volume(int32 channel, int32 volume);

		/* chunk refers to a Mix_Chunk* */
		[LinkName("Mix_VolumeChunk")]
		public static extern int32 VolumeChunk(
			Chunk* chunk,
			int volume
		);

		[LinkName("Mix_VolumeMusic")]
		public static extern int32 VolumeMusic(int32 volume);

		[LinkName("Mix_HaltChannel")]
		public static extern int32 HaltChannel(int32 channel);

		[LinkName("Mix_HaltGroup")]
		public static extern int32 HaltGroup(int32 tag);

		[LinkName("Mix_HaltMusic")]
		public static extern int32 HaltMusic();

		[LinkName("Mix_ExpireChannel")]
		public static extern int32 ExpireChannel(int32 channel, int32 ticks);

		[LinkName("Mix_FadeOutChannel")]
		public static extern int32 FadeOutChannel(int32 which, int32 ms);

		[LinkName("Mix_FadeOutGroup")]
		public static extern int32 FadeOutGroup(int32 tag, int32 ms);

		[LinkName("Mix_FadeOutMusic")]
		public static extern int32 FadeOutMusic(int32 ms);

		[LinkName("Mix_FadingMusic")]
		public static extern Fading FadingMusic();

		[LinkName("Mix_FadingChannel")]
		public static extern Fading FadingChannel(int32 which);

		[LinkName("Mix_Pause")]
		public static extern void Pause(int32 channel);

		[LinkName("Mix_Resume")]
		public static extern void Resume(int32 channel);

		[LinkName("Mix_Paused")]
		public static extern int32 Paused(int32 channel);

		[LinkName("Mix_PauseMusic")]
		public static extern void PauseMusic();

		[LinkName("Mix_ResumeMusic")]
		public static extern void ResumeMusic();

		[LinkName("Mix_RewindMusic")]
		public static extern void RewindMusic();

		[LinkName("Mix_PausedMusic")]
		public static extern int32 PausedMusic();

		[LinkName("Mix_SetMusicPosition")]
		public static extern int32 SetMusicPosition(double position);

		[LinkName("Mix_Playing")]
		public static extern int32 Playing(int32 channel);

		[LinkName("Mix_PlayingMusic")]
		public static extern int32 PlayingMusic();

		[LinkName("Mix_SetMusicCMD")]
		public static extern int32 Mix_SetMusicCMD(char8* command);

		[LinkName("Mix_SetSynchroValue")]
		public static extern int32 SetSynchroValue(int32 value);

		[LinkName("Mix_GetSynchroValue")]
		public static extern int32 GetSynchroValue();

		[LinkName("Mix_SetSoundFonts")]
		public static extern int32 SetSoundFonts(char8* paths);
		
		[LinkName("Mix_GetSoundFonts")]
		public static extern char8* GetSoundFonts();

		[LinkName("Mix_EachSoundFont")]
		public static extern int32 EachSoundFont(SoundFontDelegate func, void* data);

		[LinkName("Mix_GetChunk")]
		public static extern void* GetChunk(int32 channel);

		[LinkName("Mix_CloseAudio")]
		public static extern void CloseAudio();
	}
}
