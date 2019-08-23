//////////////////////////////////////////////////////////////////////
//
// AkFilePackageLowLevelIOBlocking.h
//
// Extends the CAkDefaultIOHookBlocking low level I/O hook with File 
// Package handling functionality. 
//
// See AkDefaultIOHookBlocking.h for details on using the blocking 
// low level I/O hook. 
// 
// See AkFilePackageLowLevelIO.h for details on using file packages.
// 
// Copyright (c) 2006 Audiokinetic Inc. / All Rights Reserved
//
//////////////////////////////////////////////////////////////////////

#ifndef _AK_FILE_PACKAGE_LOW_LEVEL_IO_BLOCKING_H_
#define _AK_FILE_PACKAGE_LOW_LEVEL_IO_BLOCKING_H_

#include "../Common/AkFilePackageLowLevelIO.h"
#include "AkDefaultIOHookBlocking.h"

class CAkFilePackageLowLevelIOBlocking 
	: public CAkFilePackageLowLevelIO<CAkDefaultIOHookBlocking>
{
public:
	CAkFilePackageLowLevelIOBlocking() {}
	virtual ~CAkFilePackageLowLevelIOBlocking() {}
};

#endif //_AK_FILE_PACKAGE_LOW_LEVEL_IO_BLOCKING_H_
