using System;
using System.Diagnostics;
using System.Interop;

namespace System.Net
{
	class Socket
	{
		const uint16 WINSOCK_VERSION = 0x101;
		const int32 WSAENETRESET    = 10052;
		const int32 WSAECONNABORTED = 10053;
		const int32 WSAECONNRESET   = 10054;
		const int32 WSAEWOULDBLOCK  = 10035;

#if BF_PLATFORM_WINDOWS
		public struct HSocket : uint
		{
		}

		[CRepr]
		public struct FDSet
		{
			const int cMaxCount = 64;

			int32 mCount;
			HSocket[64] mSockets;

			public bool Add(HSocket s) mut
			{
				if (mCount >= cMaxCount)
					return false;
				mSockets[mCount++] = s;
				return true;
			}

			public bool IsSet(HSocket s)
			{
				for (int i < mCount)
					if (s == mSockets[i])
						return true;
				return false;
			}
		}

        [CRepr]
        public struct TimeVal
        {
        	public int32 mSec;
        	public int32 mUSec;
        }

#else
		public struct HSocket : uint32
		{
		}

		[CRepr]
		public struct FDSet
		{
            const uint BITS_PER_MASK = sizeof(uint) * 8;
            const uint MASK_COUNT = 4096 / sizeof(uint);
            const uint MAX_ALLOWED_FD = MASK_COUNT * BITS_PER_MASK;

            uint[MASK_COUNT] mSocketBitMasks;
            uint32 afterLastBit;

            public bool Add(HSocket s) mut
		    {
                let fd = (uint32)s;

                if (fd > MAX_ALLOWED_FD)
                   return false;

                if (fd >= afterLastBit)
                   afterLastBit = fd + 1;

				mSocketBitMasks[fd / BITS_PER_MASK] |= 1U << (fd & (BITS_PER_MASK - 1));
				return true;
			}

			public bool IsSet(HSocket s)
			{
                let fd = (uint32)s;
                if (fd > MAX_ALLOWED_FD)
                    return false;
				return (mSocketBitMasks[fd / BITS_PER_MASK] & (1U << (fd & (BITS_PER_MASK - 1)))) != 0;
			}
		}

        [CRepr]
        public struct TimeVal
        {
        	public int64 mSec;
        	public int32 mUSec;
        }
#endif


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
		public struct IPv4Address
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

		[CRepr, Union]
		public struct IPv6Address
		{
			public uint8[16] byte;
			public uint16[8] word;

			public this(params uint16[8] addr)
			{
				for(let i < 8)
				{
					this.word[i] = (.)htons((int16)addr[i]);
				}
			}

			public this(params uint8[16] addr)
			{
				this.byte = addr;
			}
		}

		[CRepr]
		public struct SockAddr
		{

		}

		[CRepr]
		public struct SockAddr_in : SockAddr
        {
	        public int16 sin_family;
	        public uint16 sin_port;
	        public IPv4Address sin_addr;
	        public char8[8] sin_zero;
		}

		[CRepr]
		public struct SockAddr_in6 : SockAddr
		{
			public int16 sin6_family;
			public uint16 sin6_port;
			public uint32 sin6_flowinfo;
			public IPv6Address sin6_addr;
			public uint32 sin6_scope_id;
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

		[CRepr]
		public struct AddrInfo
		{
			public int32 ai_flags;
			public int32 ai_family;
			public int32 ai_socktype;
			public int32 ai_protocol;
			public c_size ai_addrlen;
#if BF_PLATFORM_WINDOWS
			public char8* ai_canonname;
			public SockAddr* ai_addr;
#else
			public SockAddr* ai_addr;
			public char8* ai_canonname;
#endif
			public SockAddr* ai_next;
		}

		public struct SockAddrInfo : IDisposable
		{
			public AddrInfo* addrInfo;

			public int32 AddressFamily => addrInfo.ai_family;
			public uint16 Port => ((SockAddr_in*)addrInfo.ai_addr).sin_port;
			public IPv4Address IPv4
			{
				get
				{
					Debug.Assert(AddressFamily == AF_INET);
					return ((SockAddr_in*)addrInfo.ai_addr).sin_addr;
				}
				
			}
			public IPv6Address IPv6
			{
				get
				{
					Debug.Assert(AddressFamily == AF_INET6);
					return ((SockAddr_in6*)addrInfo.ai_addr).sin6_addr;
				}
			}

			public void Dispose() mut
			{
				if (addrInfo != null)
					freeaddrinfo(addrInfo);
				addrInfo = null;
			}
		}

		public const HSocket INVALID_SOCKET = (HSocket)-1;
		public const int32 SOCKET_ERROR = -1;
		public const int AF_INET = 2;
		public const int AF_INET6 = 23;
		public const int SOCK_STREAM = 1;
		public const int SOCK_DGRAM = 2;
		public const int IPPROTO_TCP = 6;
		public const int IPPROTO_UDP = 17;
		public const int IPPROTO_IPV6 = 41;

		public const int TCP_NODELAY = 1;
		public const int TCP_MAXSEG = 2;
		public const int TCP_CORK = 3;
		public const int TCP_KEEPIDLE = 4;

#if BF_PLATFORM_WINDOWS
		public const int SOL_SOCKET = 0xffff;
		public const int SO_REUSEADDR = 0x0004;
 		public const int SO_BROADCAST = 0x0020;
		public const int IPV6_V6ONLY = 27;
#else
		public const int SOL_SOCKET = 1;
		public const int SO_REUSEADDR = 2;
		public const int SO_BROADCAST = 6;
		public const int IPV6_V6ONLY = 26;
#endif

		public const IPv4Address INADDR_ANY = default;
		public const IPv6Address IN6ADDR_ANY = default;

#if BF_PLATFORM_WINDOWS
		const int FIONBIO = (int)0x8004667e;
#else
		const int FIONBIO = (int)0x00005421;
#endif

		HSocket mHandle = INVALID_SOCKET;
		bool mIsConnected;
		bool mIsBlocking = false;

		public bool IsOpen
		{
			get
			{
				return mHandle != INVALID_SOCKET;
			}
		}

		public bool IsConnected
		{
			get
			{
				return mIsConnected;
			}
		}

		public HSocket NativeSocket
		{
			get
			{
				return mHandle;
			}
		}

		public bool Blocking
		{
			get => mIsBlocking;
			set
			{
				mIsBlocking = value;
				if (mHandle != INVALID_SOCKET)
					SetBlocking(mIsBlocking);
			}
		}

#if BF_PLATFORM_WINDOWS
		[Import("Ws2_32.lib"), CLink, CallingConvention(.Stdcall)]
		static extern int32 WSAStartup(uint16 versionRequired, WSAData* wsaData);
		
		[Import("Ws2_32.lib"), CLink, CallingConvention(.Stdcall)]
		static extern int32 WSACleanup();

		[Import("Ws2_32.lib"), CLink, CallingConvention(.Stdcall)]
		static extern int32 WSAGetLastError();
#elif BF_PLATFORM_LINUX
		[LinkName("__errno_location")]
		static extern int32* _errno();
#elif BF_PLATFORM_MACOS
		[LinkName("__error")]
		static extern int32* _errno();
#else
		[CLink]
		static int32 errno;
		static int32* _errno() => &errno;
#endif

		[CLink, CallingConvention(.Stdcall)]
		static extern HostEnt* gethostbyname(char8* name);

#if BF_PLATFORM_WINDOWS
		[Import("Ws2_32.lib"), CLink, CallingConvention(.Stdcall)]
		static extern int32 getaddrinfo(char8* pNodeName, char8* pServiceName, AddrInfo* pHints, AddrInfo** ppResult);

		[Import("Ws2_32.lib"), CLink, CallingConvention(.Stdcall)]
		static extern void freeaddrinfo(AddrInfo* pAddrInfo);
#else
		[CLink, CallingConvention(.Stdcall)]
		static extern int32 getaddrinfo(char8* pNodeName, char8* pServiceName, AddrInfo* pHints, AddrInfo** ppResult);

		[CLink, CallingConvention(.Stdcall)]
		static extern void freeaddrinfo(AddrInfo* pAddrInfo);
#endif

		[CLink, CallingConvention(.Stdcall)]
		static extern HSocket socket(int32 af, int32 type, int32 protocol);

		[CLink, CallingConvention(.Stdcall)]
		static extern int32 connect(HSocket s, SockAddr* name, int32 nameLen);
    
#if BF_PLATFORM_WINDOWS
		[Import("Ws2_32.lib"), CLink, CallingConvention(.Stdcall)]
		static extern int32 closesocket(HSocket s);
#else
		[CLink, CallingConvention(.Stdcall)]
		static extern int32 close(HSocket s);
#endif

		[CLink, CallingConvention(.Stdcall)]
		static extern int32 bind(HSocket s, SockAddr* name, int32 nameLen);

		[CLink, CallingConvention(.Stdcall)]
		static extern int32 listen(HSocket s, int32 backlog);

		[CLink, CallingConvention(.Stdcall)]
		static extern HSocket accept(HSocket s, SockAddr* addr, int32* addrLen);

#if BF_PLATFORM_WINDOWS
		[CLink, CallingConvention(.Stdcall)]
		static extern int32 ioctlsocket(HSocket s, int cmd, int* argp);
#else
		[CLink, CallingConvention(.Stdcall)]
		static extern int32 ioctl(HSocket s, int cmd, int* argp);
#endif

		[CLink, CallingConvention(.Stdcall)]
		static extern int32 setsockopt(HSocket s, int32 level, int32 optname, void* optval, int32 optlen);

		[CLink, CallingConvention(.Stdcall)]
		static extern int32 select(int32 nfds, FDSet* readFDS, FDSet* writeFDS, FDSet* exceptFDS, TimeVal* timeVal);

		[CLink, CallingConvention(.Stdcall)]
		static extern int32 recv(HSocket s, void* ptr, int32 len, int32 flags);

		[CLink, CallingConvention(.Stdcall)]
		static extern int32 recvfrom(HSocket s, void* ptr, int32 len, int32 flags, SockAddr* from, int32* fromLen);

		[CLink, CallingConvention(.Stdcall)]
		static extern int32 send(HSocket s, void* ptr, int32 len, int32 flags);

		[CLink, CallingConvention(.Stdcall)]
		static extern int32 sendto(HSocket s, void* ptr, int32 len, int32 flags, SockAddr* to, int32 toLen);

		public ~this()
		{
			if (mHandle != INVALID_SOCKET)
#if BF_PLATFORM_WINDOWS
				closesocket(mHandle);
#else
				close(mHandle);
#endif
		}

		public static int32 Init(uint16 versionRequired = WINSOCK_VERSION)
		{
#if BF_PLATFORM_WINDOWS
			WSAData wsaData = default;
			return WSAStartup(versionRequired, &wsaData);
#else
			return 0;
#endif
		}
		
		public static int32 Uninit()
		{
#if BF_PLATFORM_WINDOWS
			return WSACleanup();
#else
			return 0;
#endif
		}

		int32 GetLastError()
		{
#if BF_PLATFORM_WINDOWS
			return WSAGetLastError();
#else
			return *_errno();
#endif
		}

		public static int32 htons(int32 val)
		{
			return ((val & 0x000000FF) << 24) | ((val & 0x0000FF00) <<  8) |
				((val & 0x00FF0000) >>  8) | ((val & (int32)0xFF000000) >> 24);
		}

		public static int16 htons(int16 val)
		{
			return (int16)(((val & 0x00FF) << 8) |
				((val & 0xFF00) >> 8));
		}

		void SetBlocking(bool blocking)
		{
			int param = blocking ? 0 : 1;

#if BF_PLATFORM_WINDOWS
			ioctlsocket(mHandle, FIONBIO, &param);
#else
			ioctl(mHandle, FIONBIO, &param);
#endif
		}

		void RehupSettings()
		{
			SetBlocking(mIsBlocking);
		}

        public Result<void> Listen(int32 port, int32 backlog = 5)
        {
            return Listen(.(127, 0, 0, 1), port, backlog);
        }

		public Result<void> Listen(IPv4Address address, int32 port, int32 backlog = 5)
		{
			Debug.Assert(mHandle == INVALID_SOCKET);

			mHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			
			if (mHandle == INVALID_SOCKET)
			{
#unwarn
				int32 err = GetLastError();
				return .Err;
			}

			RehupSettings();

			SockAddr_in service;
			service.sin_family = AF_INET;
			service.sin_addr = address;
			service.sin_port = (uint16)htons((int16)port);

			int32 size = sizeof(SockAddr_in);
			if (bind(mHandle, &service, size) == SOCKET_ERROR)
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

		public Result<void> Listen(IPv6Address address, int32 port, int32 backlog = 5, bool v6Only = false)
		{
			Debug.Assert(mHandle == INVALID_SOCKET);

			mHandle = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
			
			if (mHandle == INVALID_SOCKET)
			{
#unwarn
				int32 err = GetLastError();
				return .Err;
			}

			int32 ipv6Opt = v6Only ? 1 : 0;
			setsockopt(mHandle, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6Opt, 4);

			RehupSettings();

			SockAddr_in6 service;
			service.sin6_family = AF_INET6;
			service.sin6_addr = address;
			service.sin6_port = (uint16)htons((int16)port);

			int32 size = sizeof(SockAddr_in6);
			if (bind(mHandle, &service, size) == SOCKET_ERROR)
			{
#unwarn
				int err = GetLastError();
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

		public Result<void> Connect(StringView addr, int32 port, out SockAddr_in sockAddr)
		{
			sockAddr = default;
			mHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if (mHandle == INVALID_SOCKET)
				return .Err;

			var hostEnt = gethostbyname(scope String(addr));
			if (hostEnt == null)
				return .Err;

			sockAddr.sin_family = AF_INET;
			Internal.MemCpy(&sockAddr.sin_addr, hostEnt.h_addr_list[0], sizeof(IPv4Address));
			sockAddr.sin_port = (uint16)htons((int16)port);

			if (connect(mHandle, &sockAddr, sizeof(SockAddr_in)) == SOCKET_ERROR)
				return .Err;

			if (mHandle == INVALID_SOCKET)
			{
#unwarn
				int32 err = GetLastError();
				return .Err;
			}

			mIsConnected = true;
			RehupSettings();

			return .Ok;
		}

		public Result<void> ConnectEx(StringView addr, int32 port, out SockAddrInfo sockAddrInfo)
		{
			sockAddrInfo = default;

			AddrInfo hints = default;
			hints.ai_socktype = SOCK_STREAM;
			hints.ai_protocol = IPPROTO_TCP;

			AddrInfo* addrInfo = null;
			defer
			{
				if (addrInfo != null)
					freeaddrinfo(addrInfo);
			}

			if (getaddrinfo(addr.Ptr, null, &hints, &addrInfo) < 0)
				return .Err;

			mHandle = socket(addrInfo.ai_family, SOCK_STREAM, IPPROTO_TCP);
			if (mHandle == INVALID_SOCKET)
				return .Err;

			if (addrInfo.ai_family == AF_INET)
			{
				SockAddr_in* sockAddrIn = (.)addrInfo.ai_addr;
				sockAddrIn.sin_port = (uint16)htons((int16)port);
			}
			else if (addrInfo.ai_family == AF_INET6)
			{
				SockAddr_in6* sockAddrIn = (.)addrInfo.ai_addr;
				sockAddrIn.sin6_port = (uint16)htons((int16)port);
			}

			if (connect(mHandle, addrInfo.ai_addr, (.)addrInfo.ai_addrlen) == SOCKET_ERROR)
			{
#unwarn
				int32 err = GetLastError();
				return .Err;
			}

			if (mHandle == INVALID_SOCKET)
			{
#unwarn
				int32 err = GetLastError();
				return .Err;
			}

			mIsConnected = true;
			RehupSettings();

			sockAddrInfo.[Friend]addrInfo = addrInfo;
			addrInfo = null;

			return .Ok;
		}

		public Result<void> Connect(StringView addr, int32 port) => Connect(addr, port, ?);

		public Result<void> AcceptFrom(Socket listenSocket, SockAddr* from, int32* fromLen)
		{
			mHandle = accept(listenSocket.mHandle, from, fromLen);
			if (mHandle == INVALID_SOCKET)
			{
#unwarn
				int lastErr = GetLastError();
				return .Err;
			}

			RehupSettings();
			mIsConnected = true;
			return .Ok;
		}

		public Result<void> AcceptFrom(Socket listenSocket, out SockAddr_in clientAddr)
		{
			clientAddr = default;
			int32 clientAddrLen = sizeof(SockAddr_in);
			return AcceptFrom(listenSocket, &clientAddr, &clientAddrLen);
		}

		public Result<void> AcceptFrom(Socket listenSocket) => AcceptFrom(listenSocket, null, null);

		public static int32 Select(FDSet* readFDS, FDSet* writeFDS, FDSet* exceptFDS, int waitTimeMS)
		{
			TimeVal timeVal;
			timeVal.mSec = (.)(waitTimeMS / 1000);
			timeVal.mUSec = (.)((waitTimeMS % 1000) * 1000);

			return Select(readFDS, writeFDS, exceptFDS, &timeVal);
		}

		public static int32 Select(FDSet* readFDS, FDSet* writeFDS, FDSet* exceptFDS, float waitTimeMS)
		{
			int waitTimeUS = (int)(waitTimeMS * 1000);
			TimeVal timeVal;
			timeVal.mSec = (.)(waitTimeUS / (1000*1000));
			timeVal.mUSec = (.)(waitTimeUS % (1000*1000));

			return Select(readFDS, writeFDS, exceptFDS, &timeVal);
		}

		private static int32 Select(FDSet* readFDS, FDSet* writeFDS, FDSet* exceptFDS, TimeVal* timeVal)
		{
#if BF_PLATFORM_WINDOWS
            const int32 nfds = 0; // Ignored
#else
            int32 nfds = 0;
            if (readFDS != null)
                nfds = (.)readFDS.[Friend]afterLastBit;
            if (writeFDS != null)
                nfds = Math.Max(nfds, (.)writeFDS.[Friend]afterLastBit);
            if (exceptFDS != null)
                nfds = Math.Max(nfds, (.)exceptFDS.[Friend]afterLastBit);
#endif
			return select(nfds, readFDS, writeFDS, exceptFDS, timeVal);
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
			case WSAEWOULDBLOCK:
			default:
				NOP!();
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

		public Result<int> RecvFrom(void* ptr, int size, SockAddr* from, ref int32 fromLen)
		{
			int32 result = recvfrom(mHandle, ptr, (int32)size, 0, from, &fromLen);
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

		public Result<int> RecvFrom(void* ptr, int size, out SockAddr_in from)
		{
			from = default;
			//from.sin_family = AF_INET;
			int32 fromLen = sizeof(SockAddr_in);
			return RecvFrom(ptr, size, &from, ref fromLen);
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

		public Result<int> SendTo(void* ptr, int size, SockAddr* to, int toLen)
		{
			int32 result = sendto(mHandle, ptr, (int32)size, 0, to, (.)toLen);
			if (result < 0)
			{
				CheckDisconnected();
				if (!mIsConnected)
					return .Err;
			}
			return result;
		}

#unwarn
		public Result<int> SendTo(void* ptr, int size, SockAddr_in to) => SendTo(ptr, size, &to, sizeof(SockAddr_in));
#unwarn
		public Result<int> SendTo(void* ptr, int size, SockAddr_in6 to) => SendTo(ptr, size, &to, sizeof(SockAddr_in6));
		
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

		public Result<void> OpenUDP(int32 port = -1)
		{
			SockAddr_in bindAddr = default;

			mHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (mHandle == INVALID_SOCKET)
			{
				return .Err;
			}

			RehupSettings();

			int32 yes = 1;
			//setsockopt(mHandle, SOL_SOCKET, SO_REUSEADDR, &yes, 4);
			int32 status = setsockopt(mHandle, SOL_SOCKET, SO_BROADCAST, &yes, 4);

			bindAddr.sin_addr = INADDR_ANY;
			bindAddr.sin_port = (.)htons((int16)port);
			bindAddr.sin_family = AF_INET;

			status = bind(mHandle, &bindAddr, sizeof(SockAddr_in));
			return .Ok;
		}

		public Result<void> OpenUDPIPv6(int32 port = -1, bool v6Only = false)
		{
			SockAddr_in6 bindAddr = default;

			mHandle = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
			if (mHandle == INVALID_SOCKET)
			{
				return .Err;
			}

			RehupSettings();

			int32 yes = 1;
			//setsockopt(mHandle, SOL_SOCKET, SO_REUSEADDR, &yes, 4);
			int32 status = setsockopt(mHandle, SOL_SOCKET, SO_BROADCAST, &yes, 4);

			int32 ipv6Opt = v6Only ? 1 : 0;
			setsockopt(mHandle, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6Opt, 4);

			bindAddr.sin6_addr = IN6ADDR_ANY;
			bindAddr.sin6_port = (.)htons((int16)port);
			bindAddr.sin6_family = AF_INET6;

			status = bind(mHandle, &bindAddr, sizeof(SockAddr_in6));
			return .Ok;
		}
	}
}
