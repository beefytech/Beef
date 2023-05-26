using System;
using System.IO;
using System.Threading;
using System.Diagnostics;
using System.Collections;

namespace CURL
{
	class Transfer
	{
		CURL.Easy mCurl;
		CURL.Easy.SList* mHeaderList;
		bool mOwns;
		bool mCancelling = false;
		List<uint8> mData = new List<uint8>() ~ delete _;
		Stopwatch mStatsTimer = new Stopwatch() ~ delete _;
		WaitEvent mDoneEvent ~ delete _;
		Result<Span<uint8>> mResult;

		int mTotalBytes = -1;
		int mBytesReceived = 0;

		int mLastBytesPerSecond = -1;
		int mBytesAtLastPeriod = 0;
		bool mRunning;
		
		public int BytesPerSecond
		{
			get
			{
				UpdateBytesPerSecond();
				if (mLastBytesPerSecond != -1)
					return mLastBytesPerSecond;
				return GetCurBytesPerSecond();
			}
		}

		public int TotalBytes
		{
			get
			{
				return mTotalBytes;
			}
		}

		public int BytesReceived
		{
			get
			{
				return mBytesReceived;
			}
		}
		
		public bool IsRunning
		{
			get
			{
				return mRunning;
			}
		}

		public this()
		{
			mCurl = new CURL.Easy();
			mOwns = true;
		}

		public this(CURL.Easy curl)
		{
			mCurl = curl;
		}

		public ~this()
		{
			mCancelling = true;
			if (mRunning)
				mDoneEvent.WaitFor();

			if (mOwns)
				delete mCurl;

			if (mHeaderList != null)
				mCurl.Free(mHeaderList);
		}

		int GetCurBytesPerSecond()
		{
			int elapsedTime = mStatsTimer.ElapsedMilliseconds;
			if (elapsedTime == 0)
				return 0;

			return (int)((int64)(mBytesReceived - mBytesAtLastPeriod) * 1000 / elapsedTime);
		}

		void UpdateBytesPerSecond()
		{
			if (mStatsTimer.ElapsedMilliseconds >= 500)
			{
				mLastBytesPerSecond = GetCurBytesPerSecond();
				mBytesAtLastPeriod = mBytesReceived;
				mStatsTimer.Restart();
			}
		}

		void Update(int dltotal, int dlnow)
		{
			if (dltotal > mTotalBytes)
				mTotalBytes = dltotal;
			mBytesReceived = dlnow;
			UpdateBytesPerSecond();
		}

		[CallingConvention(.Stdcall)]
		static int Callback(void *p, int dltotal, int dlnow, int ultotal, int ulnow)
		{
			Transfer transfer = (Transfer)Internal.UnsafeCastToObject(p);
			if (transfer.mCancelling)
				return 1;

			transfer.Update(dltotal, dlnow);
			return 0;
		}

		[CallingConvention(.Stdcall)]
		static int Write(void* dataPtr, int size, int count, void* ctx)
		{
			Transfer transfer = (Transfer)Internal.UnsafeCastToObject(ctx);
			int byteCount = size * count;
			if (byteCount > 0)
			{
	            Internal.MemCpy(transfer.mData.GrowUnitialized(byteCount), dataPtr, byteCount);
			}
			return count;
		}

		public void Init(StringView url)
		{
			function int(void *p, int dltotal, int dlnow, int ultotal, int ulnow) callback = => Callback;
			mCurl.SetOptFunc(.XferInfoFunction, (void*)callback);
			mCurl.SetOpt(.XferInfoData, Internal.UnsafeCastToPtr(this));

			function int(void* ptr, int size, int count, void* ctx) writeFunc = => Write;
			mCurl.SetOptFunc(.WriteFunction, (void*)writeFunc);
			mCurl.SetOpt(.WriteData, Internal.UnsafeCastToPtr(this));
			
			mCurl.SetOpt(.FollowLocation, true);
			mCurl.SetOpt(.URL, url);
			mCurl.SetOpt(.NoProgress, false);
			mCurl.SetOpt(.IPResolve, (int)CURL.Easy.IPResolve.V4);
			mCurl.SetOpt(.HTTPGet, true);
		}

		public void AddHeader(StringView header)
		{
			mHeaderList = mCurl.Add(mHeaderList, header);
		}

		public void InitPost(String url, String param)
		{
			Init(url);
			mCurl.SetOpt(.Postfields, param);
		}

		public void Escape(StringView str, String outStr)
		{
			mCurl.Escape(str, outStr);
		}

		public void Unescape(StringView str, String outStr)
		{
			int startPos = outStr.Length;

			mCurl.Unescape(str, outStr);

			for (int i = startPos; i < outStr.Length; i++)
				if (outStr[i] == '+')
					outStr[i] = ' ';
		}

		public Result<Span<uint8>> Perform()
		{
			if (mHeaderList != null)
				mCurl.SetOpt(.HTTPHeader, mHeaderList);

			mStatsTimer.Start();
			var result = mCurl.Perform();
			mStatsTimer.Stop();

			switch (result)
			{
			case .Err:
				return .Err;
			default:
				if (mData.Count > 0)
					return .Ok(.(&mData[0], mData.Count));
				else
					return .Ok(.());
			}
		}

		void DoBackground()
		{
			mResult = Perform();
			mRunning = false;
			mDoneEvent.Set(true);
		}

		public Result<void> PerformBackground()
		{
			// This is a one-use object
			if (mDoneEvent != null)
				return .Err; 
			mDoneEvent = new WaitEvent();
			mRunning = true;
			ThreadPool.QueueUserWorkItem(new => DoBackground);
			return .Ok;
		}

		public Result<Span<uint8>> GetResult()
		{
			if (mDoneEvent != null)
				mDoneEvent.WaitFor();
			return mResult;
		}

		public void GetContentType(String outContentType)
		{
			mCurl.GetInfo(.ContentType, outContentType);
		}

		public void Cancel(bool wait = false)
		{
			mCancelling = true;
			if ((wait) && (mRunning))
				mDoneEvent.WaitFor();
		}
	}
}
