#include "WwiseSound.h"
#include "BFApp.h"

#if (_MSC_VER != 1800)
#undef BF_WWISE_ENABLED
#endif

#ifdef BF_WWISE_ENABLED

#if _DEBUG
#define BF_IS_WWISE_COMM_ENABLED 1
#endif

#include "AK/SoundEngine/Common/AkTypes.h"
#include <AK/SoundEngine/Common/AkMemoryMgr.h>      // Memory Manager
#include <AK/SoundEngine/Common/AkModule.h>			// Default memory and stream managers
#include <AK/SoundEngine/Common/IAkStreamMgr.h>		// Streaming Manager
#include <AK/SoundEngine/Common/AkSoundEngine.h>    // Sound engine
#include <AK/MusicEngine/Common/AkMusicEngine.h>	// Music Engine
#include <AK/SoundEngine/Common/AkStreamMgrModule.h>	// AkStreamMgrModule
#include <AK/Plugin/AllPluginsFactories.h>
#include "Win32/AkDefaultIOHookBlocking.h"

#ifndef AK_OPTIMIZED
#include <AK/Comm/AkCommunication.h>	// Communication between Wwise and the game (excluded in release build)
#endif

/*
AkMusicEngine.lib
AkHarmonizerFX.lib 
AkPitchShifterFX.lib 
AkStereoDelayFX.lib 
AkTimeStretchFX.lib 
AkMeterFX.lib 
AkConvolutionReverbFX.lib 
AkAudioInputSource.lib 
AkSilenceSource.lib 
AkPeakLimiterFX.lib 
AkRoomVerbFX.lib 
AkParametricEQFX.lib 
AkGainFX.lib 
AkDelayFX.lib 
AkMatrixReverbFX.lib 
AkExpanderFX.lib 
AkCompressorFX.lib 
AkSineSource.lib 
AkGuitarDistortionFX.lib 
AkTremoloFX.lib 
AkFlangerFX.lib 
AkToneSource.lib 
AkSoundSeedImpactFX.lib 
 McDSPLimiterFX.lib McDSPFutzBoxFX.lib AkSoundSeedWoosh.lib AkSoundSeedWind.lib AkStreamMgr.lib AkVorbisDecoder.lib ;%(AdditionalDependencies)*/

//iZHybridReverbFX.lib iZTrashBoxModelerFX.lib iZTrashDelayFX.lib iZTrashDistortionFX.lib iZTrashDynamicsFX.lib iZTrashFiltersFX.lib iZTrashMultibandDistortionFX.lib McDSPLimiterFX.lib

#pragma comment(lib, "AkSoundEngine.lib")
#pragma comment(lib, "AkMemoryMgr.lib")
#pragma comment(lib, "AkStreamMgr.lib")
#pragma comment(lib, "AkMusicEngine.lib")
#pragma comment(lib, "AkVorbisDecoder.lib")
#pragma comment(lib, "AkAudioInputSource.lib")
#pragma comment(lib, "AkToneSource.lib")
#pragma comment(lib, "AkSilenceSource.lib")
#pragma comment(lib, "AkSineSource.lib")
//#pragma comment(lib, "AkMP3Source.lib")
#pragma comment(lib, "AkMatrixReverbFX.lib")
#pragma comment(lib, "AkMeterFX.lib")
#pragma comment(lib, "AkParametricEQFX.lib")
#pragma comment(lib, "AkGainFX.lib")
#pragma comment(lib, "AkDelayFX.lib")
#pragma comment(lib, "AkCompressorFX.lib")
#pragma comment(lib, "AkExpanderFX.lib")
#pragma comment(lib, "AkPeakLimiterFX.lib")
#pragma comment(lib, "AkRoomVerbFX.lib")
#pragma comment(lib, "AkGuitarDistortionFX.lib")
#pragma comment(lib, "AkFlangerFX.lib")
#pragma comment(lib, "AkStereoDelayFX.lib")
#pragma comment(lib, "AkTimeStretchFX.lib")
#pragma comment(lib, "AkHarmonizerFX.lib")
#pragma comment(lib, "AkPitchShifterFX.lib")
#pragma comment(lib, "AkConvolutionReverbFX.lib")
#pragma comment(lib, "AkTremoloFX.lib")
#pragma comment(lib, "AkRumble.lib")
#pragma comment(lib, "AkMotionGenerator.lib")
#pragma comment(lib, "AkSoundSeedWind.lib")
#pragma comment(lib, "AkSoundSeedWoosh.lib")
#pragma comment(lib, "AkSoundSeedImpactFX.lib")
#pragma comment(lib, "McDSPFutzBoxFX.lib")
#pragma comment(lib, "dxguid.lib")
#ifdef BF_IS_WWISE_COMM_ENABLED
#pragma comment(lib, "CommunicationCentral.lib")
#endif
#pragma comment(lib, "wsock32.lib")

static bool gWwiseInitialized = false;

USING_NS_BF;

#define DEMO_DEFAULT_POOL_SIZE 2*1024*1024
#define DEMO_LENGINE_DEFAULT_POOL_SIZE 1*1024*1024

CAkDefaultIOHookBlocking* m_pLowLevelIO = NULL;

namespace AK
{
    void * AllocHook( size_t in_size )
    {
        return malloc( in_size );
    }
    void FreeHook( void * in_ptr )
    {
        free( in_ptr );
    }
#ifdef _WINDOWS
    // Note: VirtualAllocHook() may be used by I/O pools of the default implementation
    // of the Stream Manager, to allow "true" unbuffered I/O (using FILE_FLAG_NO_BUFFERING
    // - refer to the Windows SDK documentation for more details). This is NOT mandatory;
    // you may implement it with a simple malloc().
    void * VirtualAllocHook(
        void * in_pMemAddress,
        size_t in_size,
        DWORD in_dwAllocationType,
        DWORD in_dwProtect
        )
    {
        return VirtualAlloc( in_pMemAddress, in_size, in_dwAllocationType, in_dwProtect );
    }
    void VirtualFreeHook( 
        void * in_pMemAddress,
        size_t in_size,
        DWORD in_dwFreeType
        )
    {
        VirtualFree( in_pMemAddress, in_size, in_dwFreeType );
    }
#endif
}

static bool WWise_Init()
{	
	m_pLowLevelIO = new CAkDefaultIOHookBlocking();

	AkMemSettings memSettings;
	AkStreamMgrSettings stmSettings;
	AkDeviceSettings deviceSettings;
	AkInitSettings initSettings;
	AkPlatformInitSettings platformInitSettings;
	AkMusicSettings musicInit;
    
    // Get default settings
	memSettings.uMaxNumPools = 20;
	AK::StreamMgr::GetDefaultSettings( stmSettings );
	
	AK::StreamMgr::GetDefaultDeviceSettings( deviceSettings );
	
	AK::SoundEngine::GetDefaultInitSettings( initSettings );
	initSettings.uDefaultPoolSize = DEMO_DEFAULT_POOL_SIZE;

#if defined( INTEGRATIONDEMO_ASSERT_HOOK )
	initSettings.pfnAssertHook = INTEGRATIONDEMO_ASSERT_HOOK;
#endif // defined( INTEGRATIONDEMO_ASSERT_HOOK )
	
	AK::SoundEngine::GetDefaultPlatformInitSettings( platformInitSettings );
	platformInitSettings.uLEngineDefaultPoolSize = DEMO_LENGINE_DEFAULT_POOL_SIZE;
    
	AK::MusicEngine::GetDefaultInitSettings( musicInit );
    
    //
    // Create and initialize an instance of the default memory manager. Note
    // that you can override the default memory manager with your own. Refer
    // to the SDK documentation for more information.
    //
    
	AKRESULT res = AK::MemoryMgr::Init( &memSettings );
    if ( res != AK_Success )
    {
        return false;
    }
    
	//
    // Create and initialize an instance of the default streaming manager. Note
    // that you can override the default streaming manager with your own. Refer
    // to the SDK documentation for more information.
    //
    
    // Customize the Stream Manager settings here.
    
    if ( !AK::StreamMgr::Create( stmSettings ) )
    {
        return false;
    }

	// 
    // Create a streaming device with blocking low-level I/O handshaking.
    // Init() creates a streaming device in the Stream Manager, and registers itself as the File Location Resolver.

    deviceSettings.uSchedulerTypeFlags = AK_SCHEDULER_BLOCKING;
    res = m_pLowLevelIO->Init( deviceSettings );
	if ( res != AK_Success )
	{
        return false;
    }

	// Set the path to the SoundBank Files.
/*#ifdef HOST_WINDOWS
	std::wstring wFullPath = StringToWString(m_filePath);
	m_pLowLevelIO->SetBasePath(  wFullPath.c_str() );
#else
	m_pLowLevelIO->SetBasePath( m_filePath.c_str() );
#endif	*/

    //
    // Create the Sound Engine
    // Using default initialization parameters
    //
    
	res = AK::SoundEngine::Init( &initSettings, &platformInitSettings );
    if ( res != AK_Success )
    {
        return false;
    }
    
    //
    // Initialize the music engine
    // Using default initialization parameters
    //
    
	res = AK::MusicEngine::Init( &musicInit );
    if ( res != AK_Success )
    {
        return false;
    }
    
#if defined HOST_MACOSX || defined HOST_IPHONEOS
	// Register the AAC codec for Mac/iOS.
	AK::SoundEngine::RegisterCodec(
		AKCOMPANYID_AUDIOKINETIC,
		AKCODECID_AAC,
		CreateAACFilePlugin,
		CreateAACBankPlugin );
#endif

#if BF_IS_WWISE_COMM_ENABLED
    //
    // Initialize communications (not in release build!)
    //
	AkCommSettings commSettings;
	AK::Comm::GetDefaultInitSettings( commSettings );
	res = AK::Comm::Init( commSettings );
	if ( res != AK_Success )
	{
	}
#endif // BF_IS_WWISE_COMM_ENABLED


	gWwiseInitialized = true;
    return true;
}

void Beefy::WWiseUpdate()
{
	if (gWwiseInitialized)
		AK::SoundEngine::RenderAudio();
}

BF_EXPORT void BF_CALLTYPE Wwise_Shutdown()
{
	if (!gWwiseInitialized)
		return;

#if BF_IS_WWISE_COMM_ENABLED
    //
    // Terminate Communication Services (not in release build!)
    //
    AK::Comm::Term();
#endif // BF_IS_WWISE_COMM_ENABLED

    //
    // Terminate the music engine
    //
    AK::MusicEngine::Term();

    //
    // Terminate the sound engine
    //
    AK::SoundEngine::Term();

    // Terminate the streaming device and streaming manager
    
    // SexyIOHookBlocking::Term() destroys its associated streaming device 
    // that lives in the Stream Manager, and unregisters itself as the File Location Resolver.
    m_pLowLevelIO->Term();
	delete m_pLowLevelIO;
    
    if ( AK::IAkStreamMgr::Get() )
        AK::IAkStreamMgr::Get()->Destroy();

    // Terminate the Memory Manager
    AK::MemoryMgr::Term();

	gWwiseInitialized = false;
}

//

BF_EXPORT void* BF_CALLTYPE Wwise_LoadBankByName(const uint16* name)
{
	if (!gWwiseInitialized)
		WWise_Init();	

	std::wstring bankFileName = gBFApp->mInstallDir + UTF16Decode(name) + L".bnk";

	AkBankID bankID;
	AKRESULT result = AK::SoundEngine::LoadBank(bankFileName.c_str(), AK_DEFAULT_POOL_ID, bankID);
	if (result != AK_Success)
		return NULL;

	return (void*) bankID;
}

BF_EXPORT uint32 BF_CALLTYPE Wwise_SendEvent(uint32 eventId, void* gameObject)
{
	uint32 playingSound;	
	playingSound = AK::SoundEngine::PostEvent(eventId, (AkGameObjectID) gameObject);
	return playingSound;
}

BF_EXPORT int BF_CALLTYPE Wwise_SetRTPCValue(uint32 paramId, float value, void* gameObject)
{	
	if (gameObject == 0)		
	{
		if (AK::SoundEngine::SetRTPCValue(paramId, value, (AkGameObjectID) AK_INVALID_GAME_OBJECT) == AK_Success)		
			return true;		
	}
	else
	{
		if (AK::SoundEngine::SetRTPCValue(paramId, value, (AkGameObjectID) gameObject) == AK_Success)		
			return true;		
	}

    return false;
}

#else
void Beefy::WWiseUpdate()
{
}

BF_EXPORT void* BF_CALLTYPE Wwise_LoadBankByName(const uint16* name)
{
	return NULL;
}

BF_EXPORT uint32 BF_CALLTYPE Wwise_SendEvent(uint32 eventId, void* gameObject)
{
	return 0;
}

BF_EXPORT int BF_CALLTYPE Wwise_SetRTPCValue(uint32 paramId, float value, void* gameObject)
{
	return false;
}

BF_EXPORT void BF_CALLTYPE Wwise_Shutdown()
{
}

#endif
