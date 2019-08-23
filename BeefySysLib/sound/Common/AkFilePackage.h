//////////////////////////////////////////////////////////////////////
// 
// AkFilePackage.h
//
// This class represents a file package that was created with the 
// AkFilePackager utility app (located in ($WWISESDK)/samples/FilePackager/). 
// It holds a system file handle and a look-up table (CAkFilePackageLUT).
//
// CAkFilePackage objects can be chained together using the ListFilePackages
// typedef defined below.
// 
// Copyright (c) 2007-2009 Audiokinetic Inc. / All Rights Reserved
//
//////////////////////////////////////////////////////////////////////

#ifndef _AK_FILE_PACKAGE_H_
#define _AK_FILE_PACKAGE_H_

#include "AkFilePackageLUT.h"
#include <AK/SoundEngine/Common/AkSoundEngine.h>
#include <AK/Tools/Common/AkObject.h>
#include <AK/Tools/Common/AkListBare.h>

//-----------------------------------------------------------------------------
// Name: Base class for items that can be chained in AkListBareLight lists.
//-----------------------------------------------------------------------------
template<class T>
class CAkListAware
{
public:
	CAkListAware()
		: pNextItem( NULL ) {}

	struct AkListNextItem
	{
		static AkForceInline T *& Get( T * in_pItem ) 
		{
			return in_pItem->pNextItem;
		}
	};

	T * pNextItem;
};

//-----------------------------------------------------------------------------
// Name: CAkFilePackage 
// Desc: Base class representing a file package (incomplete implementation). 
// It holds a look-up table (CAkFilePackageLUT) and manages memory for the LUT and
// for itself. 
//-----------------------------------------------------------------------------
class CAkFilePackage : public CAkListAware<CAkFilePackage>
{
public:
	// Package factory.
	// Creates a memory pool to contain the header of the file package and this object. 
	// Returns its address.
	template<class T_PACKAGE>
	static T_PACKAGE * Create( 
		const AkOSChar*		in_pszPackageName,	// Name of the file package (for memory monitoring and ID generation).
		AkMemPoolId			in_memPoolID,		// Memory pool in which the package is created with its lookup table.
		AkUInt32 			in_uHeaderSize,		// File package header size, including the size of the header chunk AKPK_HEADER_CHUNK_DEF_SIZE.
		AkUInt32			in_uBlockAlign,		// Alignment of memory block.
		AkUInt32 &			out_uReservedHeaderSize, // Size reserved for header, taking mem align into account.
		AkUInt8 *&			out_pHeaderBuffer	// Returned address of memory for header.
		)
	{
		AKASSERT( in_uHeaderSize > 0 );

		out_pHeaderBuffer = NULL;
		
		// Create memory pool and copy header.
		// The pool must be big enough to hold both the buffer for the LUT's header
		// and a CAkFilePackage object.
		bool bIsInternalPool;
		AkUInt8 * pToRelease = NULL;
		out_uReservedHeaderSize = ( ( in_uHeaderSize + in_uBlockAlign - 1 ) / in_uBlockAlign ) * in_uBlockAlign;
		AkUInt32 uMemSize = out_uReservedHeaderSize + sizeof( T_PACKAGE );
		if ( in_memPoolID == AK_DEFAULT_POOL_ID )
		{
			in_memPoolID = AK::MemoryMgr::CreatePool( NULL, uMemSize, uMemSize, AkMalloc | AkFixedSizeBlocksMode, in_uBlockAlign );
			if ( in_memPoolID == AK_INVALID_POOL_ID )
				return NULL;
			AK_SETPOOLNAME( in_memPoolID, in_pszPackageName );
			bIsInternalPool = true;
			pToRelease = (AkUInt8*)AK::MemoryMgr::GetBlock( in_memPoolID );
			AKASSERT( pToRelease );
		}
		else
		{
			// Shared pool.
			bIsInternalPool = false;
			AKRESULT eResult = AK::MemoryMgr::CheckPoolId( in_memPoolID );
			if ( eResult == AK_Success )
			{
				if ( AK::MemoryMgr::GetPoolAttributes( in_memPoolID ) & AkBlockMgmtMask )
				{
					if ( AK::MemoryMgr::GetBlockSize( in_memPoolID ) >= uMemSize )
						pToRelease = (AkUInt8*)AK::MemoryMgr::GetBlock( in_memPoolID );
				}
				else
					pToRelease = (AkUInt8*)AkAlloc( in_memPoolID, uMemSize );
			}
		}

		if ( !pToRelease )
			return NULL;

		// Generate an ID.
		AkUInt32 uPackageID = AK::SoundEngine::GetIDFromString( in_pszPackageName );
		
		// Construct a CAkFilePackage at the end of this memory region.
		T_PACKAGE * pFilePackage = AkPlacementNew( pToRelease + out_uReservedHeaderSize ) T_PACKAGE( uPackageID, in_uHeaderSize, in_memPoolID, pToRelease, bIsInternalPool );
		AKASSERT( pFilePackage );	// Must succeed.

		out_pHeaderBuffer = pToRelease;

		return pFilePackage;
	}

	// Destroy file package and free memory / destroy pool.
	virtual void Destroy();

	// Getters.
	inline AkUInt32 ID() { return m_uPackageID; }
	inline AkUInt32 HeaderSize() { return m_uHeaderSize; }
	inline AkUInt32 ExternalPool() { return ( !m_bIsInternalPool ) ? m_poolID : AK_DEFAULT_POOL_ID; }

	// Members.
	// ------------------------------
	CAkFilePackageLUT	lut;		// Package look-up table.

protected:
	AkUInt32			m_uPackageID;
	AkUInt32			m_uHeaderSize;
	// ------------------------------

protected:
	// Private constructors: users should use Create().
	CAkFilePackage();
	CAkFilePackage(CAkFilePackage&);
	CAkFilePackage( AkUInt32 in_uPackageID, AkUInt32 in_uHeaderSize, AkMemPoolId in_poolID, void * in_pToRelease, bool in_bIsInternalPool )
		: m_uPackageID( in_uPackageID )
		, m_uHeaderSize( in_uHeaderSize )
		, m_poolID( in_poolID )
		, m_pToRelease( in_pToRelease )
		, m_bIsInternalPool( in_bIsInternalPool )
	{
	}
	virtual ~CAkFilePackage() {}
	
	// Helper.
	static void ClearMemory(
		AkMemPoolId in_poolID,			// Pool to destroy.
		void *		in_pMemToRelease,	// Memory block to free before destroying pool.
		bool		in_bIsInternalPool	// Pool was created internally (and needs to be destroyed).
		);

protected:
	// Memory management.
	AkMemPoolId			m_poolID;		// Memory pool for LUT.
	void *				m_pToRelease;	// LUT storage (only keep this pointer to release memory).
	bool				m_bIsInternalPool;	// True if pool was created by package, false if shared.
};

//-----------------------------------------------------------------------------
// Name: ListFilePackages
// Desc: AkListBare of CAkFilePackage items.
//-----------------------------------------------------------------------------
typedef AkListBare<CAkFilePackage,CAkListAware<CAkFilePackage>::AkListNextItem,AkCountPolicyWithCount> ListFilePackages;

#endif //_AK_FILE_PACKAGE_H_
