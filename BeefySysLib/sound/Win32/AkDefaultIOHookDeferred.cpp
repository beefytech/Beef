#include "BFPlatform.h"

#ifdef BF_WWISE_ENABLED

//////////////////////////////////////////////////////////////////////
//
// AkDefaultIOHookDeferred.cpp
//
// Default deferred low level IO hook (AK::StreamMgr::IAkIOHookDeferred) 
// and file system (AK::StreamMgr::IAkFileLocationResolver) implementation 
// on Windows.
// 
// AK::StreamMgr::IAkFileLocationResolver: 
// Resolves file location using simple path concatenation logic 
// (implemented in ../Common/CAkFileLocationBase). It can be used as a 
// standalone Low-Level IO system, or as part of a multi device system. 
// In the latter case, you should manage multiple devices by implementing 
// AK::StreamMgr::IAkFileLocationResolver elsewhere (you may take a look 
// at class CAkDefaultLowLevelIODispatcher).
//
// AK::StreamMgr::IAkIOHookDeferred: 
// Uses platform API for I/O. Calls to ::ReadFile() and ::WriteFile() 
// do not block because files are opened with the FILE_FLAG_OVERLAPPED flag. 
// Transfer completion is handled by internal FileIOCompletionRoutine function,
// which then calls the AkAIOCallback.
// The AK::StreamMgr::IAkIOHookDeferred interface is meant to be used with
// AK_SCHEDULER_DEFERRED_LINED_UP streaming devices. 
//
// Init() creates a streaming device (by calling AK::StreamMgr::CreateDevice()).
// AkDeviceSettings::uSchedulerTypeFlags is set inside to AK_SCHEDULER_DEFERRED_LINED_UP.
// If there was no AK::StreamMgr::IAkFileLocationResolver previously registered 
// to the Stream Manager, this object registers itself as the File Location Resolver.
//
// Copyright (c) 2006 Audiokinetic Inc. / All Rights Reserved
//
//////////////////////////////////////////////////////////////////////

#include "Common.h"
#include "AkDefaultIOHookDeferred.h"
#include <AK/SoundEngine/Common/AkMemoryMgr.h>
#include "AkFileHelpers.h"


// Device info.
#define WIN32_DEFERRED_DEVICE_NAME		(L"Win32 Deferred")	// Default deferred device name.

// With files opened with FILE_FLAG_NO_BUFFERING flag, accesses should be made in multiple of the
// sector size. We don't know what it is, so we choose a safe value of 2 KB.
// On Windows, real sector size can be queried with ::GetDiskFreeSpace() (but you need to know the drive letter).
#define WIN32_NO_BUFFERING_BLOCK_SIZE	(2048)	


AkMemPoolId CAkDefaultIOHookDeferred::m_poolID = AK_INVALID_POOL_ID;

CAkDefaultIOHookDeferred::CAkDefaultIOHookDeferred()
: m_deviceID( AK_INVALID_DEVICE_ID )
, m_bAsyncOpen( false )
{
}

CAkDefaultIOHookDeferred::~CAkDefaultIOHookDeferred()
{
}

// Initialization/termination. Init() registers this object as the one and 
// only File Location Resolver if none were registered before. Then 
// it creates a streaming device with scheduler type AK_SCHEDULER_DEFERRED_LINED_UP.
AKRESULT CAkDefaultIOHookDeferred::Init(
	const AkDeviceSettings &	in_deviceSettings,		// Device settings.
	bool						in_bAsyncOpen/*=false*/	// If true, files are opened asynchronously when possible.
	)
{
	if ( in_deviceSettings.uSchedulerTypeFlags != AK_SCHEDULER_DEFERRED_LINED_UP )
	{
		AKASSERT( !"CAkDefaultIOHookDeferred I/O hook only works with AK_SCHEDULER_DEFERRED_LINED_UP devices" );
		return AK_Fail;
	}
	
	m_bAsyncOpen = in_bAsyncOpen;
	
	// If the Stream Manager's File Location Resolver was not set yet, set this object as the 
	// File Location Resolver (this I/O hook is also able to resolve file location).
	if ( !AK::StreamMgr::GetFileLocationResolver() )
		AK::StreamMgr::SetFileLocationResolver( this );

	// Create a device in the Stream Manager, specifying this as the hook.
	m_deviceID = AK::StreamMgr::CreateDevice( in_deviceSettings, this );
	if ( m_deviceID != AK_INVALID_DEVICE_ID )
	{
		// Initialize structures needed to perform deferred transfers.
		AkUInt32 uPoolSize = (AkUInt32)( in_deviceSettings.uMaxConcurrentIO * sizeof( OVERLAPPED ) );
		m_poolID = AK::MemoryMgr::CreatePool( NULL, uPoolSize, sizeof( OVERLAPPED ), AkMalloc | AkFixedSizeBlocksMode );
		if ( m_poolID == AK_INVALID_POOL_ID )
		{
			AKASSERT( !"Failed creating pool for asynchronous device" );
			return AK_Fail;
		}
		AK_SETPOOLNAME( m_poolID, L"Deferred I/O hook" );
		
		return AK_Success;
	}

	return AK_Fail;
}

void CAkDefaultIOHookDeferred::Term()
{
	if ( AK::StreamMgr::GetFileLocationResolver() == this )
		AK::StreamMgr::SetFileLocationResolver( NULL );

	AK::StreamMgr::DestroyDevice( m_deviceID );

	if ( m_poolID != AK_INVALID_POOL_ID )
	{
		AK::MemoryMgr::DestroyPool( m_poolID );
		m_poolID = AK_INVALID_POOL_ID;
	}
}

//
// IAkFileLocationAware implementation.
//-----------------------------------------------------------------------------

// Returns a file descriptor for a given file name (string).
AKRESULT CAkDefaultIOHookDeferred::Open( 
    const AkOSChar* in_pszFileName,     // File name.
    AkOpenMode      in_eOpenMode,       // Open mode.
    AkFileSystemFlags * in_pFlags,      // Special flags. Can pass NULL.
	bool &			io_bSyncOpen,		// If true, the file must be opened synchronously. Otherwise it is left at the File Location Resolver's discretion. Return false if Open needs to be deferred.
    AkFileDesc &    out_fileDesc        // Returned file descriptor.
    )
{
	// We normally consider that calls to ::CreateFile() on a hard drive are fast enough to execute in the
	// client thread. If you want files to be opened asynchronously when it is possible, this device should 
	// be initialized with the flag in_bAsyncOpen set to true.
	if ( io_bSyncOpen || !m_bAsyncOpen )
	{
		io_bSyncOpen = true;
	
	    // Get the full file path, using path concatenation logic.
	    AkOSChar szFullFilePath[AK_MAX_PATH];
	    if ( GetFullFilePath( in_pszFileName, in_pFlags, in_eOpenMode, szFullFilePath ) == AK_Success )
		{
			// Open the file with FILE_FLAG_OVERLAPPED and FILE_FLAG_NO_BUFFERING flags.
			AKRESULT eResult = CAkFileHelpers::OpenFile( 
				szFullFilePath,
				in_eOpenMode,
				true,
				true,
				out_fileDesc.hFile );
			if ( eResult == AK_Success )
			{
				ULARGE_INTEGER Temp;
				Temp.LowPart = ::GetFileSize( out_fileDesc.hFile,(LPDWORD)&Temp.HighPart );
	
				out_fileDesc.iFileSize			= Temp.QuadPart;
				out_fileDesc.uSector			= 0;
				out_fileDesc.deviceID			= m_deviceID;
				out_fileDesc.pCustomParam		= ( in_eOpenMode == AK_OpenModeRead ) ? NULL : (void*)1;
				out_fileDesc.uCustomParamSize	= 0;
			}
			return eResult;
		}
	
		return AK_Fail;
	}
	else
	{
		// The client allows us to perform asynchronous opening.
		// We only need to specify the deviceID, and leave the boolean to false.
		out_fileDesc.iFileSize			= 0;
		out_fileDesc.uSector			= 0;
		out_fileDesc.deviceID			= m_deviceID;
		out_fileDesc.pCustomParam		= NULL;
		out_fileDesc.uCustomParamSize	= 0;
		return AK_Success;
	}
}

// Returns a file descriptor for a given file ID.
AKRESULT CAkDefaultIOHookDeferred::Open( 
    AkFileID        in_fileID,          // File ID.
    AkOpenMode      in_eOpenMode,       // Open mode.
    AkFileSystemFlags * in_pFlags,      // Special flags. Can pass NULL.
	bool &			io_bSyncOpen,		// If true, the file must be opened synchronously. Otherwise it is left at the File Location Resolver's discretion. Return false if Open needs to be deferred.
    AkFileDesc &    out_fileDesc        // Returned file descriptor.
    )
{
	// We normally consider that calls to ::CreateFile() on a hard drive are fast enough to execute in the
	// client thread. If you want files to be opened asynchronously when it is possible, this device should 
	// be initialized with the flag in_bAsyncOpen set to true.
	if ( io_bSyncOpen || !m_bAsyncOpen )
	{
		io_bSyncOpen = true;
	
	    // Get the full file path, using path concatenation logic.
	    AkOSChar szFullFilePath[AK_MAX_PATH];
	    if ( GetFullFilePath( in_fileID, in_pFlags, in_eOpenMode, szFullFilePath ) == AK_Success )
		{
			// Open the file with FILE_FLAG_OVERLAPPED and FILE_FLAG_NO_BUFFERING flags.
			AKRESULT eResult = CAkFileHelpers::OpenFile( 
				szFullFilePath,
				in_eOpenMode,
				true,
				true,
				out_fileDesc.hFile );
			if ( eResult == AK_Success )
			{
				ULARGE_INTEGER Temp;
				Temp.LowPart = ::GetFileSize( out_fileDesc.hFile,(LPDWORD)&Temp.HighPart );
	
				out_fileDesc.iFileSize			= Temp.QuadPart;
				out_fileDesc.uSector			= 0;
				out_fileDesc.deviceID			= m_deviceID;
				out_fileDesc.pCustomParam		= NULL;
				out_fileDesc.uCustomParamSize	= 0;
			}
			return eResult;
		}
	
		return AK_Fail;
	}
	else
	{
		// The client allows us to perform asynchronous opening.
		// We only need to specify the deviceID, and leave the boolean to false.
		out_fileDesc.iFileSize			= 0;
		out_fileDesc.uSector			= 0;
		out_fileDesc.deviceID			= m_deviceID;
		out_fileDesc.pCustomParam		= NULL;
		out_fileDesc.uCustomParamSize	= 0;
		return AK_Success;
	}
}

//
// IAkIOHookDeferred implementation.
//-----------------------------------------------------------------------------

// Local callback for overlapped I/O.
VOID CALLBACK CAkDefaultIOHookDeferred::FileIOCompletionRoutine(
  DWORD dwErrorCode,
  DWORD 
#ifdef _DEBUG
  dwNumberOfBytesTransfered
#endif
  ,
  LPOVERLAPPED lpOverlapped
)
{
	AkAsyncIOTransferInfo * pXferInfo = (AkAsyncIOTransferInfo*)(lpOverlapped->hEvent);

	ReleaseOverlapped( lpOverlapped );

	AKRESULT eResult = AK_Fail;
	if ( ERROR_SUCCESS == dwErrorCode )
	{
		eResult = AK_Success;
		AKASSERT( dwNumberOfBytesTransfered >= pXferInfo->uRequestedSize && dwNumberOfBytesTransfered <= pXferInfo->uBufferSize );
	}

	pXferInfo->pCallback( pXferInfo, eResult );
}

// Reads data from a file (asynchronous overload).
AKRESULT CAkDefaultIOHookDeferred::Read(
	AkFileDesc &			in_fileDesc,        // File descriptor.
	const AkIoHeuristics & /*in_heuristics*/,	// Heuristics for this data transfer (not used in this implementation).
	AkAsyncIOTransferInfo & io_transferInfo		// Asynchronous data transfer info.
	)
{
	AKASSERT( in_fileDesc.hFile != INVALID_HANDLE_VALUE 
			&& io_transferInfo.uRequestedSize > 0 
			&& io_transferInfo.uBufferSize >= io_transferInfo.uRequestedSize );

	// If this assert comes up, it might be beacause this hook's GetBlockSize() return value is incompatible 
	// with the system's handling of file reading for this specific file handle.
	// If you are using the File Package extension, did you create your package with a compatible
	// block size? It should be a multiple of WIN32_NO_BUFFERING_BLOCK_SIZE. (check -blocksize argument in the File Packager command line)
	AKASSERT( ( io_transferInfo.uFilePosition % WIN32_NO_BUFFERING_BLOCK_SIZE ) == 0
			|| !"Requested file position for I/O transfer is inconsistent with block size" );

	OVERLAPPED * pOverlapped = GetFreeOverlapped( &io_transferInfo );
	
	// Set file offset in OVERLAPPED structure.
	pOverlapped->Offset = (DWORD)( io_transferInfo.uFilePosition & 0xFFFFFFFF );
	pOverlapped->OffsetHigh = (DWORD)( ( io_transferInfo.uFilePosition >> 32 ) & 0xFFFFFFFF );

	// File was open with asynchronous flag. 
    // Read overlapped. 
	// Note: With a file handle opened with FILE_FLAG_NO_BUFFERING, ::ReadFileEx() supports read sizes that go beyond the end
	// of file. However, it does not support read sizes that are not a multiple of the drive's sector size.
	// Since the buffer size is always a multiple of the block size, let's use io_transferInfo.uBufferSize
	// instead of io_transferInfo.uRequestedSize.
    if ( ::ReadFileEx( in_fileDesc.hFile,
                      io_transferInfo.pBuffer,
                      io_transferInfo.uBufferSize,
                      pOverlapped,
					  CAkDefaultIOHookDeferred::FileIOCompletionRoutine ) )
	{
		return AK_Success;
	}
	ReleaseOverlapped( pOverlapped );
    return AK_Fail;
}

// Writes data to a file (asynchronous overload).
AKRESULT CAkDefaultIOHookDeferred::Write(
	AkFileDesc &			in_fileDesc,        // File descriptor.
	const AkIoHeuristics & /*in_heuristics*/,	// Heuristics for this data transfer (not used in this implementation).
	AkAsyncIOTransferInfo & io_transferInfo		// Platform-specific asynchronous IO operation info.
	)
{
	AKASSERT( in_fileDesc.hFile != INVALID_HANDLE_VALUE 
			&& io_transferInfo.uRequestedSize > 0 );

	// If this assert comes up, it might be beacause this hook's GetBlockSize() return value is incompatible 
	// with the system's handling of file reading for this specific file handle.
	// Are you using the File Package Low-Level I/O with incompatible block size? (check -blocksize argument in the File Packager command line)
	AKASSERT( io_transferInfo.uFilePosition % GetBlockSize( in_fileDesc ) == 0
			|| !"Requested file position for I/O transfer is inconsistent with block size" );

	OVERLAPPED * pOverlapped = GetFreeOverlapped( &io_transferInfo );
	
	// Set file offset in OVERLAPPED structure.
	pOverlapped->Offset = (DWORD)( io_transferInfo.uFilePosition & 0xFFFFFFFF );
	pOverlapped->OffsetHigh = (DWORD)( ( io_transferInfo.uFilePosition >> 32 ) & 0xFFFFFFFF );

    // File was open with asynchronous flag. 
    // Read overlapped. 
    if ( ::WriteFileEx( in_fileDesc.hFile,
                      io_transferInfo.pBuffer,
                      io_transferInfo.uRequestedSize,
                      pOverlapped,
					  CAkDefaultIOHookDeferred::FileIOCompletionRoutine ) )
	{
		return AK_Success;
	}
	ReleaseOverlapped( pOverlapped );
    return AK_Fail;
}

// Cancel transfer(s).
void CAkDefaultIOHookDeferred::Cancel(
	AkFileDesc &			in_fileDesc,		// File descriptor.
	AkAsyncIOTransferInfo & /*io_transferInfo*/,// Transfer info to cancel (this implementation only handles "cancel all").
	bool & io_bCancelAllTransfersForThisFile	// Flag indicating whether all transfers should be cancelled for this file (see notes in function description).
	)
{
	if ( io_bCancelAllTransfersForThisFile )
	{
		CancelIo( in_fileDesc.hFile );
		// Leave io_bCancelAllTransfersForThisFile to true: all transfers were cancelled for this file,
		// so we don't need to be called again.
	}
}

// Close a file.
AKRESULT CAkDefaultIOHookDeferred::Close(
    AkFileDesc & in_fileDesc      // File descriptor.
    )
{
    return CAkFileHelpers::CloseFile( in_fileDesc.hFile );
}

// Returns the block size for the file or its storage device. 
AkUInt32 CAkDefaultIOHookDeferred::GetBlockSize(
    AkFileDesc &  in_fileDesc     // File descriptor.
    )
{
	if ( in_fileDesc.pCustomParam == 0 )
	    return WIN32_NO_BUFFERING_BLOCK_SIZE;
	return 1;
}

// Returns a description for the streaming device above this low-level hook.
void CAkDefaultIOHookDeferred::GetDeviceDesc(
    AkDeviceDesc &  
#ifndef AK_OPTIMIZED
	out_deviceDesc      // Description of associated low-level I/O device.
#endif
    )
{
#ifndef AK_OPTIMIZED
	AKASSERT( m_deviceID != AK_INVALID_DEVICE_ID || !"Low-Level device was not initialized" );

	// Deferred scheduler.
	out_deviceDesc.deviceID       = m_deviceID;
	out_deviceDesc.bCanRead       = true;
	out_deviceDesc.bCanWrite      = true;
	AKPLATFORM::SafeStrCpy( out_deviceDesc.szDeviceName, WIN32_DEFERRED_DEVICE_NAME, AK_MONITOR_DEVICENAME_MAXLENGTH );
	out_deviceDesc.uStringSize   = (AkUInt32)wcslen( out_deviceDesc.szDeviceName ) + 1;
#endif
}

// Returns custom profiling data: 1 if file opens are asynchronous, 0 otherwise.
// Tip: An interesting application for custom profiling data in deferred devices is to display
// the number of requests currently pending in the low-level IO.
AkUInt32 CAkDefaultIOHookDeferred::GetDeviceData()
{
	return ( m_bAsyncOpen ) ? 1 : 0;
}

#endif