using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using Beefy.utils;
using System.Diagnostics;
using System.Threading;

namespace IDE.Compiler
{
    public class BfPassInstance
    {
        [StdCall, CLink]
        static extern void BfPassInstance_Delete(void* bfSystem);

        [StdCall, CLink]
        static extern char8* BfPassInstance_PopOutString(void* bfSystem);

        [StdCall, CLink]
        static extern void BfPassInstance_SetClassifierPassId(void* bfResolvePassData, uint8 classifierPassId);

        [StdCall, CLink]
        static extern int32 BfPassInstance_GetErrorCount(void* mNativeResolvePassData);        

        [StdCall, CLink]
        static extern char8* BfPassInstance_GetErrorData(void* mNativeResolvePassData, int32 errorIdx, out bool isWarning,
            out bool isAfter, out bool isDeferred, out bool isWhileSpecializing,
            out bool isPersistent, out int32 srcStart, out int32 srcEnd, out int32 moreInfoCount);

		[StdCall, CLink]
		static extern char8* BfPassInstance_Error_GetMoreInfoData(void* mNativeResolvePassData, int32 errorIdx, int32 moreInfoIdx, out char8* fileName, out int32 srcStart, out int32 srcEnd);

        [StdCall, CLink]
        static extern bool BfPassInstance_HadSignatureChanges(void* mNativeResolvePassData);

        public class BfError
        {
            public bool mIsWarning;
            public bool mIsAfter;
            public bool mIsDeferred;
            public bool mIsWhileSpecializing;
            public bool mIsPersistent;
            public String mError ~ delete _;
            public int32 mSrcStart;
            public int32 mSrcEnd;
			public int32 mMoreInfoCount;
			public bool mOwnsSpan;
            public IdSpan mIdSpan ~ { if (mOwnsSpan) _.Dispose(); };
			public int32 mErrorIdx = -1;
			public String mFileName ~ delete _;
			public List<BfError> mMoreInfo ~ DeleteContainerAndItems!(_);
        }

        static public List<BfPassInstance> sPassInstances = new List<BfPassInstance>() ~ delete _;
		static Monitor sMonitor = new Monitor() ~ delete _;
        static int32 sCurId;

        public void* mNativeBfPassInstance;
        public bool mDidCompile;
        public bool mCompileSucceeded;
        public bool mIsDisposed;
		public bool mFailed;

        public int32 mId = sCurId++;
        internal String mDbgStr ~ delete _;

        public this(void* nativePassInstance)
        {
            mNativeBfPassInstance = nativePassInstance;

            using (sMonitor.Enter())
            {
                sPassInstances.Add(this);
            }

            //Debug.Assert(sPassInstances.Count <= 5);
            Debug.Assert(sPassInstances.Count <= 4);
        }

        public ~this()
        {
            Debug.Assert(!mIsDisposed);

            using (sMonitor.Enter())
            {                
                sPassInstances.Remove(this);
            }

            mIsDisposed = true;
            BfPassInstance_Delete(mNativeBfPassInstance);
            mNativeBfPassInstance = null;

			if (mFailed)
			{
				NOP!();
			}
        }

        public bool PopOutString(String outStr)
        {
            char8* strVal = BfPassInstance_PopOutString(mNativeBfPassInstance);
			if (strVal == null)
				return false;
			outStr.Append(strVal);
            return true;
        }

        public void SetClassifierPassId(uint8 classifierPassId)
        {
            BfPassInstance_SetClassifierPassId(mNativeBfPassInstance, classifierPassId);
        }

        public int32 GetErrorCount()
        {
            return BfPassInstance_GetErrorCount(mNativeBfPassInstance);
        }
        
        public void GetErrorData(int32 errorIdx, BfError bfError)
        {
			Debug.Assert(bfError.mError == null);
			bfError.mErrorIdx = errorIdx;
            bfError.mError = new String(BfPassInstance_GetErrorData(mNativeBfPassInstance, errorIdx, out bfError.mIsWarning, out bfError.mIsAfter, out bfError.mIsDeferred, 
                out bfError.mIsWhileSpecializing, out bfError.mIsPersistent, out bfError.mSrcStart, out bfError.mSrcEnd, out bfError.mMoreInfoCount));
        }

		public void GetMoreInfoErrorData(int32 errorIdx, int32 moreInfoIdx, BfError bfError)
		{
			char8* fileName = null;
			char8* errorStr = BfPassInstance_Error_GetMoreInfoData(mNativeBfPassInstance, errorIdx, moreInfoIdx, out fileName, out bfError.mSrcStart, out bfError.mSrcEnd);
			Debug.Assert(bfError.mFileName == null);
			if (fileName != null)
				bfError.mFileName = new String(fileName);
			if (bfError.mError == null)
				bfError.mError = new String(errorStr);
			else
				bfError.mError.Append(errorStr);
		}

        public bool HadSignatureChanges()
        {
            return BfPassInstance_HadSignatureChanges(mNativeBfPassInstance);
        }
    }
}
