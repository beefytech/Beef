using System.Collections;

namespace System
{
	class BumpAllocator : ITypedAllocator
	{
		struct DtorEntry
		{
			public uint16 mPoolIdx;
			public uint16 mPoolOfs;
		}

		struct DtorEntryEx
		{
			public uint32 mPoolIdx;
			public uint32 mPoolOfs;
		}

		public enum DestructorHandlingKind
		{
			Allow,
			Fail,
			Ignore,
		}

		List<Span<uint8>> mPools;
		List<DtorEntry> mDtorEntries;
		List<DtorEntryEx> mDtorEntriesEx;
		List<void*> mLargeRawAllocs;
		List<Object> mLargeDtorAllocs;
		int mPoolsSize;
		int mLargeAllocs;
		public DestructorHandlingKind DestructorHandling = .Allow;

		uint8* mCurAlloc;
		uint8* mCurPtr;
		uint8* mCurEnd;

		public int PoolSizeMin = 4*1024;
		public int PoolSizeMax = 64*1024;

#if BF_ENABLE_REALTIME_LEAK_CHECK
		// We will either contain only data that needs to be marked or not, based on the first
		//  data allocated. The paired allocator will contain the other type of data
		bool mMarkData;
		BumpAllocator mPairedAllocator ~ delete _;
#endif
		
		public this(DestructorHandlingKind destructorHandling = .Allow)
		{
			mCurAlloc = null;
			mCurPtr = null;
			mCurEnd = null;
		}

		public ~this()
		{
			if (mDtorEntries != null)
			{
				for (var dtorEntry in ref mDtorEntries)
				{
					uint8* ptr = mPools[dtorEntry.mPoolIdx].Ptr + dtorEntry.mPoolOfs;
					Object obj = Internal.UnsafeCastToObject(ptr);
					delete:null obj;
				}
				delete mDtorEntries;
			}

			if (mDtorEntriesEx != null)
			{
				for (var dtorEntry in ref mDtorEntriesEx)
				{
					uint8* ptr = mPools[(int)dtorEntry.mPoolIdx].Ptr + dtorEntry.mPoolOfs;
					Object obj = Internal.UnsafeCastToObject(ptr);
					delete:null obj;
				}
				delete mDtorEntriesEx;
			}

			if (mPools != null)
			{
				for (var span in mPools)
					FreePool(span);
                delete mPools;
			}

			if (mLargeDtorAllocs != null)
			{
				for (var obj in mLargeDtorAllocs)
				{
					delete:null obj;
					FreeLarge(Internal.UnsafeCastToPtr(obj));
				}
				delete mLargeDtorAllocs;
			}

			if (mLargeRawAllocs != null)
			{
				for (var ptr in mLargeRawAllocs)
					FreeLarge(ptr);
				delete mLargeRawAllocs;
			}
		}

		protected virtual void* AllocLarge(int size, int align)
		{
			return new uint8[size]* (?);
		}

		protected virtual void FreeLarge(void* ptr)
		{
			delete ptr;
		}

		protected virtual Span<uint8> AllocPool()
		{
			int poolSize = (mPools != null) ? mPools.Count : 0;
			int allocSize = Math.Clamp((int)Math.Pow(poolSize, 1.5) * PoolSizeMin, PoolSizeMin, PoolSizeMax);
			return Span<uint8>(new uint8[allocSize]* (?), allocSize);
		}

		protected virtual void FreePool(Span<uint8> span)
		{
			delete span.Ptr;
		}

		protected void GrowPool()
		{
			var span = AllocPool();
			mPoolsSize += span.Length;
			if (mPools == null)
				mPools = new List<Span<uint8>>();
			mPools.Add(span);
			mCurAlloc = span.Ptr;
			mCurPtr = mCurAlloc;
			mCurEnd = mCurAlloc + span.Length;
		}

		public void* Alloc(int size, int align)
		{
			mCurPtr = (uint8*)(void*)(((int)(void*)mCurPtr + align - 1) & ~(align - 1));

			while (mCurPtr + size >= mCurEnd)
			{
				if ((size > (mCurEnd - mCurAlloc) / 2) && (mCurAlloc != null))
				{
					mLargeAllocs += size;
					void* largeAlloc = AllocLarge(size, align);
					if (mLargeRawAllocs == null)
						mLargeRawAllocs = new List<void*>();
					mLargeRawAllocs.Add(largeAlloc);
					return largeAlloc;
				}

                GrowPool();
			}

			uint8* ptr = mCurPtr;
			mCurPtr += size;
			return ptr;
		}

		protected void* AllocWithDtor(int size, int align)
		{
			mCurPtr = (uint8*)(void*)(((int)(void*)mCurPtr + align - 1) & ~(align - 1));

			while (mCurPtr + size >= mCurEnd)
			{
				if ((size > (mCurEnd - mCurAlloc) / 2) && (mCurAlloc != null))
				{
					mLargeAllocs += size;
					void* largeAlloc = AllocLarge(size, align);
					if (mLargeDtorAllocs == null)
						mLargeDtorAllocs = new List<Object>();
					mLargeDtorAllocs.Add(Internal.UnsafeCastToObject(largeAlloc));
					return largeAlloc;
				}

		        GrowPool();
			}

			uint32 poolOfs = (.)(mCurPtr - mCurAlloc);

			if (poolOfs <= 0xFFFF)
			{
				DtorEntry dtorEntry;
				dtorEntry.mPoolIdx = (uint16)mPools.Count - 1;
				dtorEntry.mPoolOfs = (uint16)(mCurPtr - mCurAlloc);
				if (mDtorEntries == null)
					mDtorEntries = new List<DtorEntry>();
				mDtorEntries.Add(dtorEntry);
			}
			else
			{
				DtorEntryEx dtorEntry;
				dtorEntry.mPoolIdx = (uint32)mPools.Count - 1;
				dtorEntry.mPoolOfs = (uint32)(mCurPtr - mCurAlloc);
				if (mDtorEntriesEx == null)
					mDtorEntriesEx = new List<DtorEntryEx>();
				mDtorEntriesEx.Add(dtorEntry);
			}

			uint8* ptr = mCurPtr;
			mCurPtr += size;
			return ptr;
		}

#if BF_ENABLE_REALTIME_LEAK_CHECK
		public void* AllocTyped(Type type, int size, int align)
		{
			bool markData = type.WantsMark;
			if (mPools == null)
			{
				mMarkData = markData;
			}
			else
			{
				if (mMarkData != markData)
				{
					if (mPairedAllocator == null)
					{
						mPairedAllocator = new BumpAllocator();
						mPairedAllocator.mMarkData = markData;
					}
					return mPairedAllocator.Alloc(size, align);
				}
			}

			if ((DestructorHandling != .Ignore) && (type.HasDestructor))
			{
				if (DestructorHandling == .Fail)
					Runtime.FatalError("Destructor not allowed");
				return AllocWithDtor(size, align);
			}

			return Alloc(size, align);
		}

		[DisableObjectAccessChecks]
		protected override void GCMarkMembers()
		{
			GC.Mark(mPools);
			GC.Mark(mLargeRawAllocs);
			GC.Mark(mLargeDtorAllocs);
			GC.Mark(mPairedAllocator);
			GC.Mark(mDtorEntries);
			GC.Mark(mDtorEntriesEx);
			if ((mMarkData) && (mPools != null))
			{
				let arr = mPools.[Friend]mItems;
				let size = mPools.[Friend]mSize;
				if (arr != null)
				{
					for (int idx < size)
					{
						var pool = arr[idx];
						GC.Mark(pool.Ptr, pool.Length);
					}
				}
			}
		}
#else
		public void* AllocTyped(Type type, int size, int align)
		{
			if ((DestructorHandling != .Ignore) && (type.HasDestructor))
			{
				if (DestructorHandling == .Fail)
					Runtime.FatalError("Destructor not allowed");
				return AllocWithDtor(size, align);
			}
	
			return Alloc(size, align);
		}
#endif

		[SkipCall]
		public void Free(void* ptr)
		{
			// Does nothing
		}

		public int GetAllocSize()
		{
			return mPoolsSize - (mCurEnd - mCurPtr);
		}

		public int GetTotalAllocSize()
		{
			return mPoolsSize;
		}
	}
}
