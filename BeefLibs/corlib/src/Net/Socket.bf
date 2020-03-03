using System;
using System.Diagnostics;

namespace System.Net
{
	class Socket
	{
		const int32 WSAENETRESET    = 10052;
		const int32 WSAECONNABORTED = 10053;
		const int32 WSAECONNRESET   = 10054;

		public struct HSocket : uint
		{

		}

		[CRepr]
		public struct TimeVal
		{
			public int32 mSec;
			public int32 mUSec;
		}

		[CRepr]
		public struct FDSet
		{
			const int cMaxCount = 64;

			int32 mCount;
			HSocket[64] mSockets;

			public void Add(HSocket s) mut
			{
				Debug.Assert(mCount < cMaxCount);
				if (mCount < cMaxCount)
					mSockets[mCount++] = s;
			}

			public bool IsSet(HSocket s)
			{
				for (int i < mCount)
					if (s == mSockets[i])
						return true;
				return false;
			}
		}

#if BF_PLATFORM_WINDOWS
		[CRepr]
		struct WSAData
        {
	        public uint16 wVersion;
	        public uint16 wHighVersion;
#if BF_64_BIT
	        public uint16 iMaxSockets;
	        public uint16 iMaxUdpDg;
	        public char8* lpVendorInfo;
	        public char8[256+1] szDescription;
	        public char8[128+1] szSystemStatus;
#else
	        char8[256+1] szDescription;
	        char8[128+1] szSystemStatus;
	        uint16 iMaxSockets;
	        uint16 iMaxUdpDg;
	        char8* lpVendorInfo;
#endif
		}
#endif

		[CRepr]
		struct in_addr
		{
			public uint8 b1;
			public uint8 b2;
			public uint8 b3;
			public uint8 b4;

			public this(uint8 b1, uint8 b2, uint8 b3, uint8 b4)
			{
				this.b1 = b1;
				this.b2 = b2;
				this.b3 = b3;
				this.b4 = b4;
			}
		}

		[CRepr]
		struct SockAddr
		{

		}

		[CRepr]
		struct SockAddr_in : SockAddr
        {
	        public int16 sin_family;
	        public uint16 sin_port;
	        public in_addr sin_addr;
	        public char8[8] sin_zero;
		}

		[CRepr]
		public struct HostEnt
		{
			public char8* h_name;           /* official name of host */
			public char8** h_aliases;  /* alias list */
			public int16 h_addrtype;             /* host address type */
			public int16 h_length;               /* length of address */
			public char8** h_addr_list; /* list of addresses */
		}

		const HSocket INVALID_SOCKET = (HSocket)-1;
		const int32 SOCKET_ERROR = -1;
		const int AF_INET = 2;
		const int SOCK_STREAM = 1;
		const int IPPROTO_TCP = 6;
		const int FIONBIO = (int)0x8004667e;

		HSocket mHandle = INVALID_SOCKET;
		bool mIsConnected = true;

		public bool IsOpen
		{
			get
			{
				return mHandle != INVALID_SOCKET;
			}
		}

#if BF_PLATFORM_WINDOWS
		[Import("wsock32.lib"), CLink, StdCall]
		internal static extern int32 WSAStartup(uint16 versionRequired, WSAData* wsaData);

		[Import("wsock32.lib"), CLink, StdCall]
		internal static extern int32 WSAGetLastError();
#else
		[CLink]
		internal static int32 errno;
#endif

		[CLink, StdCall]
		internal static extern HostEnt* gethostbyname(char8* name);

		[CLink, StdCall]
		internal static extern HSocket socket(int32 af, int32 type, int32 protocol);

		[CLink, StdCall]
		internal static extern int32 connect(HSocket s, SockAddr* name, int32 nameLen);
#if BF_PLATFORM_WINDOWS
		[CLink, StdCall]
		internal static extern int32 closesocket(HSocket s);
#else
		[CLink, StdCall]
		internal static extern int32 close(HSocket s);
#endif
		[CLink, StdCall]
		internal static extern int32 bind(HSocket s, SockAddr* name, int32 nameLen);

		[CLink, StdCall]
		internal static extern int32 listen(HSocket s, int32 backlog);

		[CLink, StdCall]
		internal static extern HSocket accept(HSocket s, SockAddr* addr, int32* addrLen);

		[CLink, StdCall]
		internal static extern int32 ioctlsocket(HSocket s, int cmd, int* argp);

		[CLink, StdCall]
		internal static extern int32 select(int nfds, FDSet* readFDS, FDSet* writeFDS, FDSet* exceptFDS, TimeVal* timeVal);

		[CLink, StdCall]
		internal static extern int32 recv(HSocket s, void* ptr, int32 len, int32 flags);

		[CLink, StdCall]
		internal static extern int32 send(HSocket s, void* ptr, int32 len, int32 flags);

		public ~this()
		{
			if (mHandle != INVALID_SOCKET)
#if BF_PLATFORM_WINDOWS
				closesocket(mHandle);
#else
				close(mHandle);
#endif
		}

		public static void Init()
		{
#if BF_PLATFORM_WINDOWS
			WSAData wsaData = default;
			WSAStartup(0x202, &wsaData);
#endif
		}

		int32 GetLastError()
		{
#if BF_PLATFORM_WINDOWS
			return WSAGetLastError();
#else
			return errno;
#endif
		}

		int32 htons(int32 val)
		{
			return ((val & 0x000000FF) << 24) | ((val & 0x0000FF00) <<  8) |
				((val & 0x00FF0000) >>  8) | ((val & (int32)0xFF000000) >> 24);
		}

		int16 htons(int16 val)
		{
			return (int16)(((val & 0x00FF) << 8) |
				((val & 0xFF00) >> 8));
		}

		public Result<void> Listen(int32 port, int32 backlog = 5)
		{
			Debug.Assert(mHandle == INVALID_SOCKET);

			mHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			int param = 1;
			ioctlsocket(mHandle, FIONBIO, &param);
			if (mHandle == INVALID_SOCKET)
			{
#unwarn
				int32 err = GetLastError();
				return .Err;
			}

			SockAddr_in service;
			service.sin_family = AF_INET;
			service.sin_addr = in_addr(127, 0, 0, 1);
			service.sin_port = (uint16)htons((int16)port);

			if (bind(mHandle, &service, sizeof(SockAddr_in)) == SOCKET_ERROR)
			{
				Close();
				return .Err;
			}

			if (listen(mHandle, backlog) == SOCKET_ERROR)
			{
#unwarn
				int err = GetLastError();
				Close();
				return .Err;
			}

			return .Ok;
		}

		public Result<void> Connect(StringView addr, int32 port)
		{
			mHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (mHandle == INVALID_SOCKET)
				return .Err;

			var hostEnt = gethostbyname(scope String(addr));
			if (hostEnt == null)
				return .Err;

			SockAddr_in sockAddr;
			sockAddr.sin_family = AF_INET;
			Internal.MemCpy(&sockAddr.sin_addr, hostEnt.h_addr_list[0], sizeof(in_addr));
			sockAddr.sin_port = (uint16)htons((int16)port);

			if (connect(mHandle, &sockAddr, sizeof(SockAddr_in)) == SOCKET_ERROR)
				return .Err;

			int param = 1;
			ioctlsocket(mHandle, FIONBIO, &param);
			if (mHandle == INVALID_SOCKET)
			{
#unwarn
				int32 err = GetLastError();
				return .Err;
			}

			return .Ok;
		}

		public Result<void> AcceptFrom(Socket listenSocket)
		{
			SockAddr_in clientAddr;
			int32 clientAddrLen = sizeof(SockAddr_in);
			mHandle = accept(listenSocket.mHandle, &clientAddr, &clientAddrLen);
			if (mHandle == INVALID_SOCKET)
			{
#unwarn
				int lastErr = GetLastError();
			}
			return (mHandle != INVALID_SOCKET) ? .Ok : .Err;
		}

		public static int32 Select(FDSet* readFDS, FDSet* writeFDS, FDSet* exceptFDS, int waitTimeUS)
		{
			TimeVal timeVal;
			timeVal.mSec = (.)(waitTimeUS / (1000*1000));
			timeVal.mUSec = (.)(waitTimeUS % (1000*1000));
			return select(0, readFDS, writeFDS, exceptFDS, &timeVal);
		}

		public int32 DbgRecv(void* ptr, int32 size)
		{
			int32 result = recv(mHandle, ptr, size, 0);

			String str = scope String("\"");
			for (int i = 0; i < result; i++)
			{
				str.AppendF(@"\x{0:X}", ((uint8*)ptr)[i]);
			}
			str.Append("\"");
			Debug.WriteLine(str);

			return result;
		}

		void CheckDisconnected()
		{
			int32 lastErr = GetLastError();
			switch (lastErr)
			{
			case WSAENETRESET,
				 WSAECONNABORTED,
				 WSAECONNRESET:
				mIsConnected = false;
			}
		}

		public Result<int> Recv(void* ptr, int size)
		{
			int32 result = recv(mHandle, ptr, (int32)size, 0);
			if (result == 0)
			{
				mIsConnected = false;
				return .Err;
			}
			if (result == -1)
			{
				CheckDisconnected();
				if (!mIsConnected)
					return .Err;
			}
			return result;
		}

		public Result<int> Send(void* ptr, int size)
		{
			int32 result = send(mHandle, ptr, (int32)size, 0);
			if (result < 0)
			{
				CheckDisconnected();
				if (!mIsConnected)
					return .Err;
			}
			return result;
		}
		
		public void Close()
		{
			mIsConnected = false;
#if BF_PLATFORM_WINDOWS
			closesocket(mHandle);
#else
			close(mHandle);
#endif
			mHandle = INVALID_SOCKET;
		}

		public bool IsConnected
		{
			get
			{
				return mIsConnected;
			}
		}
	}
}

