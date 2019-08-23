//////////////////////////////////////////////////////////////////////
//
// AkFilePackageLowLevelIODeferred.h
//
// Extends the CAkDefaultIOHookDeferred low level I/O hook with File 
// Package handling functionality. 
//
// See AkDefaultIOHookBlocking.h for details on using the deferred 
// low level I/O hook. 
// 
// See AkFilePackageLowLevelIO.h for details on using file packages.
// 
// Copyright (c) 2006 Audiokinetic Inc. / All Rights Reserved
//
//////////////////////////////////////////////////////////////////////

#ifndef _AK_FILE_PACKAGE_LOW_LEVEL_IO_DEFERRED_H_
#define _AK_FILE_PACKAGE_LOW_LEVEL_IO_DEFERRED_H_

#include "../Common/AkFilePackageLowLevelIO.h"
#include "AkDefaultIOHookDeferred.h"

class CAkFilePackageLowLevelIODeferred
	: public CAkFilePackageLowLevelIO<CAkDefaultIOHookDeferred>
{
public:
	CAkFilePackageLowLevelIODeferred() {}
	virtual ~CAkFilePackageLowLevelIODeferred() {}

	// Override Cancel: The Windows platform SDK only permits cancellations of all transfers 
	// for a given file handle. Since the packaged files share the same handle, we cannot do this.
	void Cancel(
		AkFileDesc &			in_fileDesc,		// File descriptor.
		AkAsyncIOTransferInfo & io_transferInfo,	// Transfer info to cancel.
		bool & io_bCancelAllTransfersForThisFile	// Flag indicating whether all transfers should be cancelled for this file (see notes in function description).
		)
	{
		if ( !IsInPackage( in_fileDesc ) )
		{
			CAkDefaultIOHookDeferred::Cancel(
				in_fileDesc,		// File descriptor.
				io_transferInfo,	// Transfer info to cancel.
				io_bCancelAllTransfersForThisFile	// Flag indicating whether all transfers should be cancelled for this file (see notes in function description).
				);
		}
	}
};

#endif //_AK_FILE_PACKAGE_LOW_LEVEL_IO_DEFERRED_H_
