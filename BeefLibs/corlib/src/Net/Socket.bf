using System;
using System.Diagnostics;
using System.Interop;

namespace System.Net
{
	public enum SocketType : int
	{
		Stream = 1,
		Datagram = 2
	}

	public enum Protocol : int
	{
		TCP = 6,
		UDP = 17
	}

	public enum AddressFamily : int
	{
		IPv4 = 2,
		IPv6 = 23
	}

	public enum MessageFlags : int32
	{
		OutOfBounds = 0x1,
		Peek        = 0x2,
		DontRoute   = 0x4,
		WaitAll     = 0x8
	}

	class Socket
	{

		public enum SocketError : int32
		{
#if BF_PLATFORM_WINDOWS
			Interrupted              = 10008,
			InvalidHandle            = 10009,
			PermissionDenied         = 10013,
			InvalidArgument          = 10022,
			TooManyOpenInSystem      = 10023,
			TooManyOpen              = 10024,
			WouldBlock               = 10035,
			InProgress               = 10036,
			Already                  = 10037,
			NotASocket               = 10038,
			DestAddressRequired      = 10039,
			MessageTooLong           = 10040,
			WrongProtocolType        = 10041,
			ProtocolUnavailable      = 10042,
			ProtocolUnsupported      = 10043,
			SocketTypeUnsupported    = 10044,
			OperationUnsupported     = 10045,
			ProtFamilyUnsupported    = 10046,
			AddressFamilyUnsupported = 10047,
			AddressInUse             = 10048,
			AddressUnavailable       = 10049,
			NetworkDown              = 10050,
			NetworkUnreachable       = 10051,
			NetworkReset             = 10052,
			ConnectionAborted        = 10053,
			ConnectionReset          = 10054,
			NoBufferSpace            = 10055,
			AlreadyConnected         = 10056,
			NotConnected             = 10057,
			SocketShutdown           = 10058,
			TooManyReferences        = 10059,
			TimedOut                 = 10060,
			ConnectionRefused        = 10061,
			Loop                     = 10062,
			NameTooLong              = 10063,
			HostDown                 = 10064,
			HostUnreachable          = 10065,
			NotEmpty                 = 10066,
			TooManyProcesses         = 10067,
#else
			Interrupted              = 4,
			InvalidHandle            = 9,
			PermissionDenied         = 13,
			InvalidArgument          = 22,
			TooManyOpenInSystem      = 23,
			TooManyOpen              = 24,
			WouldBlock               = 11,
			InProgress               = 115,
			Already                  = 114,
			NotASocket               = 88,
			DestAddressRequired      = 89,
			MessageTooLong           = 90,
			WrongProtocolType        = 91,
			ProtocolUnavailable      = 92,
			ProtocolUnsupported      = 93,
			SocketTypeUnsupported    = 94,
			OperationUnsupported     = 95,
			ProtFamilyUnsupported    = 96,
			AddressFamilyUnsupported = 97,
			AddressInUse             = 98,
			AddressUnavailable       = 99,
			NetworkDown              = 100,
			NetworkUnreachable       = 101,
			NetworkReset             = 102,
			ConnectionAborted        = 103,
			ConnectionReset          = 104,
			NoBufferSpace            = 105,
			AlreadyConnected         = 106,
			NotConnected             = 107,
			SocketShutdown           = 108,
			TooManyReferences        = 109,
			TimedOut                 = 110,
			ConnectionRefused        = 111,
			Loop                     = 40,
			NameTooLong              = 36,
			HostDown                 = 112,
			HostUnreachable          = 113,
			NotEmpty                 = 39,
			TooManyProcesses         = 127,
#endif
			ConnectionClosed = -1
		}

		const uint16 WINSOCK_VERSION = 0x0202;

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
			public int16 sa_family;
		}

		[CRepr]
		public struct SockAddr_in : SockAddr
        {
	        public int16 sin_family { get => sa_family; set mut => sa_family = value; }
	        public uint16 sin_port;
	        public IPv4Address sin_addr;
	        public char8[8] sin_zero;
		}

		[CRepr]
		public struct SockAddr_in6 : SockAddr
		{
			public int16 sin6_family { get => sa_family; set mut => sa_family = value; }
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

		public struct SockOpt : IDisposable
		{
			bool ownsMemory = false;

			public int32 Level;
			public int32 Name;
			public void* Value;
			public int32 Size;

			public this(int32 level, int32 name, void* value, int32 size)
			{
				Level = level;
				Name = name;
				Value = value;
				Size = size;
			}

			public this<T>(int32 level, int32 name, T* value) where T : struct : this(level, name, value, sizeof(T)) {}

			public this<T>(int32 level, int32 name, T value) where T : struct
			{
				Level = level;
				Name = name;
				Size = sizeof(T);
				ownsMemory = true;

				let ptr = new T[1]*;
				ptr[0] = value;
				Value = ptr;
			}

			public void Dispose()
			{
				if(ownsMemory)
					delete Value;
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
		[Import("wsock32.lib"), CLink, CallingConvention(.Stdcall)]
		static extern int32 WSAStartup(uint16 versionRequired, WSAData* wsaData);
		
		[Import("wsock32.lib"), CLink, CallingConvention(.Stdcall)]
		static extern int32 WSACleanup();

		[Import("wsock32.lib"), CLink, CallingConvention(.Stdcall)]
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
		[Import("wsock32.lib"), CLink, CallingConvention(.Stdcall)]
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

		static SocketError GetLastError()
		{
#if BF_PLATFORM_WINDOWS
			return (.)WSAGetLastError();
#else
			return (.)*_errno();
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

		public static Result<void, SocketError> GetAddrInfo(StringView addr, AddrInfo hints, AddrInfo** res) => GetAddrInfo(addr, (StringView)default, hints, res);
		public static Result<void, SocketError> GetAddrInfo(StringView addr, uint16 port, AddrInfo hints, AddrInfo** res) => GetAddrInfo(addr, port.ToString(.. scope .()), hints, res);
		public static Result<void, SocketError> GetAddrInfo(StringView addr, StringView service, AddrInfo hints, AddrInfo** res)
		{
			var hints;
			return (getaddrinfo(addr.Ptr, service.Ptr, &hints, res) != 0) ? .Err(GetLastError()) : .Ok;
		}

		public static Result<SockAddrInfo, SocketError> GetAddrInfo(StringView addr, AddrInfo hints = default) => GetAddrInfo(addr, (StringView)default, hints);
		public static Result<SockAddrInfo, SocketError> GetAddrInfo(StringView addr, uint16 port, AddrInfo hints = default) => GetAddrInfo(addr, port.ToString(.. scope .()), hints);
		public static Result<SockAddrInfo, SocketError> GetAddrInfo(StringView addr, StringView service, AddrInfo hints = default)
		{
			AddrInfo* addrInfo = null;
			defer
			{
				if (addrInfo != null)
					freeaddrinfo(addrInfo);
			}

			Try!(GetAddrInfo(addr, service, hints, &addrInfo));

			let sockAddrInfo = SockAddrInfo{ addrInfo = addrInfo };
			addrInfo = null;

			return sockAddrInfo;
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

        public Result<void, SocketError> Listen(int32 port, int32 backlog = 5) => Listen(.(0, 0, 0, 0), port, backlog);
		public Result<void, SocketError> ListenLocal(int32 port, int32 backlog = 5) => Listen(.(127, 0, 0, 1), port, backlog);
		public Result<void, SocketError> Listen(IPv4Address address, int32 port, int32 backlog = 5) => OpenEx(address, port, .Stream, .TCP, backlog);
		public Result<void, SocketError> Listen(IPv6Address address, int32 port, int32 backlog = 5, bool v6Only = false)
			=> OpenEx(
				address,
				port,
				.Stream,
				.TCP,
				backlog,
				.(IPPROTO_IPV6, IPV6_V6ONLY, (int32)(v6Only ? 1 : 0))
			);

		public Result<void, SocketError> Connect(StringView addr, int32 port) => Connect(addr, port, ?);
		public Result<void, SocketError> Connect(StringView addr, int32 port, out SockAddr_in sockAddr)
		{
			sockAddr = default;

			var hostEnt = gethostbyname(scope String(addr));
			if (hostEnt == null)
				return .Err(GetLastError());

			sockAddr.sin_family = AF_INET;
			Internal.MemCpy(&sockAddr.sin_addr, hostEnt.h_addr_list[0], sizeof(IPv4Address));
			sockAddr.sin_port = (uint16)htons((int16)port);

			return ConnectEx(&sockAddr, sizeof(SockAddr_in), .Stream, .TCP);
		}

		public Result<void, SocketError> ConnectEx(StringView addr, int32 port, out SockAddrInfo sockAddrInfo)
		{
			sockAddrInfo = default;

			AddrInfo* addrInfo = null;
			defer
			{
				if (addrInfo != null)
					freeaddrinfo(addrInfo);
			}

			Try!(GetAddrInfo(addr, (.)port, .(){ ai_socktype = SOCK_STREAM, ai_protocol = IPPROTO_TCP }, &addrInfo));
			Try!(ConnectEx(addrInfo.ai_addr, (.)addrInfo.ai_addrlen, .Stream, .TCP));

			sockAddrInfo.addrInfo = addrInfo;
			addrInfo = null;

			return .Ok;
		}

		public Result<void, SocketError> ConnectEx(SockAddr* addr, int32 addrLen, SocketType type, Protocol protocol, params SockOpt[] opts)
		{
			int32 addrFamily = addr.sa_family;
			mHandle = socket(addrFamily, (.)type, (.)protocol);
			if (mHandle == INVALID_SOCKET)
				return .Err(GetLastError());

			for (let opt in opts)
			{
				if (setsockopt(mHandle, opt.Level, opt.Name, opt.Value, opt.Size) == SOCKET_ERROR)
				{
					let err = GetLastError();
					Close();
					return .Err(err);
				}
				opt.Dispose();
			}

			if (connect(mHandle, addr, addrLen) == SOCKET_ERROR)
			{
#unwarn
				let err = GetLastError();
				Close();
				return .Err(err);
			}

			mIsConnected = true;
			RehupSettings();

			return .Ok;
		}

		public Result<void, SocketError> AcceptFrom(Socket listenSocket, SockAddr* from, int32* fromLen)
		{
			mHandle = accept(listenSocket.mHandle, from, fromLen);
			if (mHandle == INVALID_SOCKET)
			{
#unwarn
				let lastErr = GetLastError();
				return .Err(lastErr);
			}

			RehupSettings();
			mIsConnected = true;
			return .Ok;
		}

		public Result<void, SocketError> AcceptFrom(Socket listenSocket, out SockAddr_in clientAddr)
		{
			clientAddr = default;
			int32 clientAddrLen = sizeof(SockAddr_in);
			return AcceptFrom(listenSocket, &clientAddr, &clientAddrLen);
		}

		public Result<void, SocketError> AcceptFrom(Socket listenSocket) => AcceptFrom(listenSocket, null, null);

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
			let lastErr = GetLastError();
			switch (lastErr)
			{
			case .NetworkReset,
				 .ConnectionAborted,
				 .ConnectionReset:
				mIsConnected = false;
			case .WouldBlock:
			default:
				NOP!();
			}
		}

		public Result<int, SocketError> Recv(void* ptr, int size, MessageFlags flags = 0)
		{
			int32 result = recv(mHandle, ptr, (int32)size, (.)flags);
			if (result == 0)
			{
				mIsConnected = false;
				return .Err(.ConnectionClosed);
			}
			if (result == -1)
			{
				CheckDisconnected();
				return .Err(GetLastError());
			}
			return result;
		}

		public Result<int, SocketError> RecvFrom(void* ptr, int size, out SockAddr_in from, MessageFlags flags = 0)
		{
			from = default;
			//from.sin_family = AF_INET;
			int32 fromLen = sizeof(SockAddr_in);
			return RecvFrom(ptr, size, &from, ref fromLen, (.)flags);
		}

		public Result<int, SocketError> RecvFrom(void* ptr, int size, SockAddr* from, ref int32 fromLen, MessageFlags flags = 0)
		{
			int32 result = recvfrom(mHandle, ptr, (int32)size, (.)flags, from, &fromLen);
			if (result == 0)
			{
				mIsConnected = false;
				return .Err(.ConnectionClosed);
			}
			if (result == -1)
			{
				CheckDisconnected();
				return .Err(GetLastError());
			}
			return result;
		}

		public Result<int, SocketError> Send(void* ptr, int size, MessageFlags flags = 0)
		{
			int32 result = send(mHandle, ptr, (int32)size, (.)flags);
			if (result < 0)
			{
				CheckDisconnected();
				return .Err(GetLastError());
			}
			return result;
		}

		public Result<int, SocketError> SendTo(void* ptr, int size, SockAddr* to, int toLen, MessageFlags flags = 0)
		{
			int32 result = sendto(mHandle, ptr, (int32)size, (.)flags, to, (.)toLen);
			if (result < 0)
			{
				CheckDisconnected();
				return .Err(GetLastError());
			}
			return result;
		}

#unwarn
		public Result<int, SocketError> SendTo(void* ptr, int size, SockAddr_in to, MessageFlags flags = 0) => SendTo(ptr, size, &to, sizeof(SockAddr_in), flags);
#unwarn
		public Result<int, SocketError> SendTo(void* ptr, int size, SockAddr_in6 to, MessageFlags flags = 0) => SendTo(ptr, size, &to, sizeof(SockAddr_in6), flags);
		
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

		
		public Result<void, SocketError> OpenUDP(int32 port = -1) => OpenUDP(INADDR_ANY, port);
		public Result<void, SocketError> OpenUDP(IPv4Address addr, int32 port = -1)
			=> OpenEx(
				addr,
				port,
				.Datagram,
				.UDP,
				-1,
				.(SOL_SOCKET, SO_BROADCAST, (int32)1)
			);

		public Result<void, SocketError> OpenUDPIPv6(int32 port = -1, bool v6Only = false) => OpenUDPIPv6(IN6ADDR_ANY, port, v6Only);
		public Result<void, SocketError> OpenUDPIPv6(IPv6Address addr, int32 port = -1, bool v6Only = false)
			=> OpenEx(
				addr,
				port,
				.Datagram,
				.UDP,
				-1,
				.(SOL_SOCKET, SO_BROADCAST, (int32)1),
				.(IPPROTO_IPV6, IPV6_V6ONLY, (int32)(v6Only ? 1 : 0))
			);
		
		public Result<void, SocketError> OpenEx(IPv4Address addr, int32 port, SocketType type, Protocol protocol, int32 backlog, params SockOpt[] opts)
		{
			SockAddr_in bindAddr = default;
			bindAddr.sin_addr = addr;
			bindAddr.sin_port = (.)htons((int16)port);
			bindAddr.sin_family = AF_INET;

			return OpenEx(&bindAddr, sizeof(SockAddr_in), type, protocol, backlog, params opts);
		}

		public Result<void, SocketError> OpenEx(IPv6Address addr, int32 port, SocketType type, Protocol protocol, int32 backlog, params SockOpt[] opts)
		{
			SockAddr_in6 bindAddr = default;
			bindAddr.sin6_addr = addr;
			bindAddr.sin6_port = (.)htons((int16)port);
			bindAddr.sin6_family = AF_INET6;

			return OpenEx(&bindAddr, sizeof(SockAddr_in6), type, protocol, backlog, params opts);
		}

		public Result<void, SocketError> OpenEx(SockAddr* addr, int32 addrLength, SocketType type, Protocol protocol, int32 backlog, params SockOpt[] opts)
		{
			Debug.Assert(mHandle == INVALID_SOCKET);

			int32 addrFamily = addr.sa_family;
			mHandle = socket(addrFamily, (.)type, (.)protocol);
			if (mHandle == INVALID_SOCKET)
			{
				return .Err(GetLastError());
			}

			RehupSettings();

			for (let opt in opts)
			{
				if (setsockopt(mHandle, opt.Level, opt.Name, opt.Value, opt.Size) == SOCKET_ERROR)
				{
					let err = GetLastError();
					Close();
					return .Err(err);
				}
				opt.Dispose();
			}

			if (bind(mHandle, addr, addrLength) == SOCKET_ERROR)
			{
				let err = GetLastError();
				Close();
				return .Err(err);
			}

			if(type == .Stream)
			{
				if (listen(mHandle, backlog) == SOCKET_ERROR)
				{
					let err = GetLastError();
					Close();
					return .Err(err);
				}
			}

			return .Ok;
		}
	}
}

namespace System
{
	extension Result<T, TErr> where TErr : System.Net.Socket.SocketError
	{
		public static implicit operator Result<T>(Self res)
		{
			switch(res)
			{
			case .Ok(let val):
				return .Ok(val);
			case .Err:
				return .Err;
			}
		}
	}
}
