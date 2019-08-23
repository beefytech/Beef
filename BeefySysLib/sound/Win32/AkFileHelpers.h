//////////////////////////////////////////////////////////////////////
//
// AkFileHelpers.h
//
// Platform-specific helpers for files.
//
// Copyright (c) 2006 Audiokinetic Inc. / All Rights Reserved
//
//////////////////////////////////////////////////////////////////////

#ifndef _AK_FILE_HELPERS_H_
#define _AK_FILE_HELPERS_H_

#include <AK/Tools/Common/AkAssert.h>
#include <windows.h>
#include <AK/SoundEngine/Common/IAkStreamMgr.h>

class CAkFileHelpers
{
public:

	// Wrapper for Win32 CreateFile().
	static AKRESULT OpenFile( 
        const AkOSChar* in_pszFilename,     // File name.
        AkOpenMode      in_eOpenMode,       // Open mode.
        bool            in_bOverlappedIO,	// Use FILE_FLAG_OVERLAPPED flag.
        bool            in_bUnbufferedIO,   // Use FILE_FLAG_NO_BUFFERING flag.
        AkFileHandle &  out_hFile           // Returned file identifier/handle.
        )
	{
		// Check parameters.
		if ( !in_pszFilename )
		{
			AKASSERT( !"NULL file name" );
			return AK_InvalidParameter;
		}

		// Open mode
		DWORD dwShareMode;
		DWORD dwAccessMode;
		DWORD dwCreationDisposition;
		switch ( in_eOpenMode )
		{
			case AK_OpenModeRead:
					dwShareMode = FILE_SHARE_READ;
					dwAccessMode = GENERIC_READ;
					dwCreationDisposition = OPEN_EXISTING;
				break;
			case AK_OpenModeWrite:
					dwShareMode = FILE_SHARE_WRITE;
					dwAccessMode = GENERIC_WRITE;
					dwCreationDisposition = OPEN_ALWAYS;
				break;
			case AK_OpenModeWriteOvrwr:
					dwShareMode = FILE_SHARE_WRITE;
					dwAccessMode = GENERIC_WRITE;
					dwCreationDisposition = CREATE_ALWAYS;
				break;
			case AK_OpenModeReadWrite:
					dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;
					dwAccessMode = GENERIC_READ | GENERIC_WRITE;
					dwCreationDisposition = OPEN_ALWAYS;
				break;
			default:
					AKASSERT( !"Invalid open mode" );
					out_hFile = NULL;
					return AK_InvalidParameter;
				break;
		}

		// Flags
		DWORD dwFlags = FILE_FLAG_SEQUENTIAL_SCAN;
		if ( in_bUnbufferedIO && in_eOpenMode == AK_OpenModeRead )
			dwFlags |= FILE_FLAG_NO_BUFFERING;
		if ( in_bOverlappedIO )
			dwFlags |= FILE_FLAG_OVERLAPPED;

		// Create the file handle.
#ifdef AK_USE_METRO_API
		out_hFile = ::CreateFile2( 
			in_pszFilename,
			dwAccessMode,
			dwShareMode, 
			dwCreationDisposition,
			NULL );
#else
		out_hFile = ::CreateFileW( 
			in_pszFilename,
			dwAccessMode,
			dwShareMode, 
			NULL,
			dwCreationDisposition,
			dwFlags,
			NULL );
#endif
		if( out_hFile == INVALID_HANDLE_VALUE )
		{
			DWORD dwAllocError = ::GetLastError();
			if ( ERROR_FILE_NOT_FOUND == dwAllocError ||
				 ERROR_PATH_NOT_FOUND == dwAllocError )
				return AK_FileNotFound;

			return AK_Fail;
		}

		return AK_Success;
	}

	// Wrapper for system file handle closing.
	static AKRESULT CloseFile( AkFileHandle in_hFile )
	{
		if ( ::CloseHandle( in_hFile ) )
			return AK_Success;
		
		AKASSERT( !"Failed to close file handle" );
		return AK_Fail;
	}

	//
	// Simple platform-independent API to open and read files using AkFileHandles, 
	// with blocking calls and minimal constraints.
	// ---------------------------------------------------------------------------

	// Open file to use with ReadBlocking().
	static AKRESULT OpenBlocking(
        const AkOSChar* in_pszFilename,     // File name.
        AkFileHandle &  out_hFile           // Returned file handle.
		)
	{
		return OpenFile( 
			in_pszFilename,
			AK_OpenModeRead,
			false,
			false, 
			out_hFile );
	}

	// Required block size for reads (used by ReadBlocking() below).
	static const AkUInt32 s_uRequiredBlockSize = 1;

	// Simple blocking read method.
	static AKRESULT ReadBlocking(
        AkFileHandle &	in_hFile,			// Returned file identifier/handle.
		void *			in_pBuffer,			// Buffer. Must be aligned on CAkFileHelpers::s_uRequiredBlockSize boundary.
		AkUInt32		in_uPosition,		// Position from which to start reading.
		AkUInt32		in_uSizeToRead,		// Size to read. Must be a multiple of CAkFileHelpers::s_uRequiredBlockSize.
		AkUInt32 &		out_uSizeRead		// Returned size read.        
		)
	{
		AKASSERT( in_uSizeToRead % s_uRequiredBlockSize == 0 
			&& in_uPosition % s_uRequiredBlockSize == 0 );

#ifdef AK_USE_METRO_API
		LARGE_INTEGER uPosition;
		uPosition.QuadPart = in_uPosition;
		if ( SetFilePointerEx( in_hFile, uPosition, NULL, FILE_BEGIN ) == FALSE )
			return AK_Fail;
#else
		if ( SetFilePointer( in_hFile, in_uPosition, NULL, FILE_BEGIN ) != in_uPosition )
			return AK_Fail;
#endif
		if ( ::ReadFile( in_hFile, in_pBuffer, in_uSizeToRead, &out_uSizeRead, NULL ) )
			return AK_Success;
		return AK_Fail;		
	}
};

#endif //_AK_FILE_HELPERS_H_
