using System;

namespace Beefy2D.utils
{
	class HTTPRequest
	{
		public enum HTTPResult
		{
			NotDone = -1,
			Failed = 0,
			Success = 1
		}

		void* mNativeNetRequest;

		[CallingConvention(.Stdcall), CLink]
		static extern void* HTTP_GetFile(char8* url, char8* destPath);

		[CallingConvention(.Stdcall), CLink]
		static extern int32 HTTP_GetResult(void* netRequest, int32 waitMS);

		[CallingConvention(.Stdcall), CLink]
		static extern void HTTP_Delete(void* netRequest);

		public ~this()
		{
			if (mNativeNetRequest != null)
				HTTP_Delete(mNativeNetRequest);
		}

		public void GetFile(StringView url, StringView destPath)
		{
			mNativeNetRequest = HTTP_GetFile(url.ToScopeCStr!(), destPath.ToScopeCStr!());
		}

		public HTTPResult GetResult()
		{
			return (HTTPResult)HTTP_GetResult(mNativeNetRequest, 0);
		}
	}
}
