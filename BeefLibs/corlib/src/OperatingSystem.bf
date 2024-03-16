namespace System
{
	class OperatingSystem
	{
#if BF_PLATFORM_WINDOWS
		const String Arch32 = "32-bit Edition";
		const String Arch64 = "64-bit Edition";

		const uint8 VER_EQUAL = 1;
		const uint8 VER_PRODUCT_TYPE = 0x00000080;
		const uint8 VER_NT_WORKSTATION = 0x00000001;
		const uint8 PROCESSOR_ARCHITECTURE_AMD64 = 9;
		const uint8 SM_SERVERR2 = 89;

		[CRepr]
		struct OSVersionInfoA
		{
			public uint32 dwOSVersionInfoSize;
			public uint32 dwMajorVersion;
			public uint32 dwMinorVersion;
			public uint32 dwBuildNumber;
			public uint32 dwPlatformId;
			public uint8[128] szCSDVersion; // Maintenance UnicodeString for PSS usage

		}
		[CRepr]
		struct OSVersionInfoExA : OSVersionInfoA
		{
			public uint16 wServicePackMajor;
			public uint16 wServicePackMinor;
			public uint16 wSuiteMask;
			public uint8 wProductType;
			public uint8 wReserved;
		}

		[CRepr]
		struct SystemInfo
		{
			public uint16 wProcessorArchitecture;
			public uint16 wReserved;
			public uint32 dwPageSize;
			public void* lpMinimumApplicationAddress;
			public void* lpMaximumApplicationAddress;
			public uint32* dwActiveProcessorMask;
			public uint32 dwNumberOfProcessors;
			public uint32 dwProcessorType;
			public uint32 dwAllocationGranularity;
			public int16 wProcessorLevel;
			public int16 wProcessorRevision;
		}

		[CRepr]
		struct WKSTA_INFO_100
		{
			public uint32 wki100_platform_id;
			public uint32 wki100_computername;
			public uint32 wki100_langroup;
			public uint32 wki100_ver_major;
			public uint32 wki100_ver_minor;
		}
		typealias LPWKSTA_INFO_100 = WKSTA_INFO_100*;

		[CRepr]
		struct VSFixedFileInfo
		{
			public uint32 dwSignature;        // e.g. $feef04bd
			public uint32 dwStrucVersion;     // e.g. $00000042 = "0.42"
			public uint32 dwFileVersionMS;    // e.g. $00030075 = "3.75"
			public uint32 dwFileVersionLS;    // e.g. $00000031 = "0.31"
			public uint32 dwProductVersionMS; // e.g. $00030010 = "3.10"
			public uint32 dwProductVersionLS; // e.g. $00000031 = "0.31"
			public uint32 dwFileFlagsMask;    // = $3F for version "0.42"
			public uint32 dwFileFlags;        // e.g. VFF_DEBUG | VFF_PRERELEASE
			public uint32 dwFileOS;           // e.g. VOS_DOS_WINDOWS16
			public uint32 dwFileType;         // e.g. VFT_DRIVER
			public uint32 dwFileSubtype;      // e.g. VFT2_DRV_KEYBOARD
			public uint32 dwFileDateMS;       // e.g. 0
			public uint32 dwFileDateLS;       // e.g. 0
		}

		[Import("kernel32.lib"), CLink, CallingConvention(.Stdcall)]
		extern static bool GetVersionExA(OSVersionInfoExA* lpVersionInformation);

		[CLink, CallingConvention(.Stdcall)]
		extern static bool VerifyVersionInfoA(OSVersionInfoExA* lpVersionInformation, uint32 dwTypeMask, uint64 dwlConditionMask);

		[CLink, CallingConvention(.Stdcall)]
		extern static uint64 VerSetConditionMask(uint64 dwlConditionMask, uint32 dwTypeBitMask, uint8 dwConditionMask);

		[CLink, CallingConvention(.Stdcall)]
		extern static void GetNativeSystemInfo(SystemInfo* lpSystemInformation);
		[CLink, CallingConvention(.Stdcall)]
		extern static void GetSystemInfo(SystemInfo* lpSystemInfo);

		[Import("netapi32.lib"), CLink, CallingConvention(.Stdcall)]
		extern static uint32 NetWkstaGetInfo(char16* ServerName, uint32 Level, LPWKSTA_INFO_100* BufPtr);
		[Import("netapi32.lib"), CLink, CallingConvention(.Stdcall)]
		extern static int32 NetApiBufferFree(LPWKSTA_INFO_100 BufPtr);

		[CLink, CallingConvention(.Stdcall)]
		extern static uint32 GetFileVersionInfoSizeA(char8* lptstrFilename, uint32* lpdwHandle);

		[Import("version.lib"), CLink, CallingConvention(.Stdcall)]
		extern static bool GetFileVersionInfoA(char8* lptstrFilename, uint32* dwHandle, uint32 dwLen, void* lpData);
		[Import("version.lib"), CLink, CallingConvention(.Stdcall)]
		extern static bool VerQueryValueA(void* pBlock, char8* lpSubBlock, void** lplpBuffer, uint32* puLen);

		[Import("user32.lib"), CLink, CallingConvention(.Stdcall)]
		extern static int GetSystemMetrics(int nIndex);
		#endif

		public Version Version;
		public PlatformID Platform;
		public String Name = new .() ~ delete _;
#if BF_PLATFORM_LINUX
		public String PrettyName = new .() ~ delete _;
#endif

		public this()
		{
			if (Compiler.IsComptime)
				return;

#if BF_PLATFORM_WINDOWS
			bool isWinSrv()
			{
				OSVersionInfoExA osvi = .();
				uint64 dwlCondMask;

				osvi.wProductType = VER_NT_WORKSTATION;

				dwlCondMask = VerSetConditionMask(0, VER_PRODUCT_TYPE, VER_EQUAL);

				return VerifyVersionInfoA(&osvi, VER_PRODUCT_TYPE, dwlCondMask) == false;
			}

			bool GetProductVersion(out uint32 major, out uint32 minor, out uint32 build)
			{
				bool result = false;
				major = 0;
				minor = 0;
				build = 0;
				uint32 verSize, wnd;
				VSFixedFileInfo* FI = &VSFixedFileInfo();

				uint32 infoSize = GetFileVersionInfoSizeA("kernel32.dll", &wnd);

				if (infoSize != 0)
				{
					void* verBuf = new uint8[infoSize]*;
					defer delete verBuf;

					if (GetFileVersionInfoA("kernel32.dll", &wnd, infoSize, verBuf))
					{
						if (VerQueryValueA(verBuf, "\\", (void**)(&FI), &verSize))
						{
							major = FI.dwProductVersionMS >> 16;
							minor = (uint16)FI.dwProductVersionMS;
							build = FI.dwProductVersionLS >> 16;
							result = true;
						}
					}
				}

				return result;
			}

			bool GetNetWkstaMajorMinor(out uint32 major, out uint32 minor)
			{
				LPWKSTA_INFO_100 LBuf = null;
				bool result = NetWkstaGetInfo(null, 100, &LBuf) == 0;

				if (result)
				{
					major = LBuf.wki100_ver_major;
					minor = LBuf.wki100_ver_minor;
				}
				else
				{
					major = 0;
					minor = 0;
				}

				if (LBuf != null)
					NetApiBufferFree(LBuf);

				return result;
			}

			SystemInfo SysInfo = .();
			OSVersionInfoExA VerInfo = .();
			uint32 MajorNum, MinorNum, BuildNum;

			VerInfo.dwOSVersionInfoSize = sizeof(OSVersionInfoExA);
			GetVersionExA(&VerInfo);

			Platform = .Win32NT;
			Version.Major = VerInfo.dwMajorVersion;
			Version.Minor = VerInfo.dwMinorVersion;
			Version.Build = VerInfo.dwBuildNumber;
			Version.Revision = VerInfo.wServicePackMajor;

			if (Version.Check(5, 1)) // GetNativeSystemInfo not supported on Windows 2000
				GetNativeSystemInfo(&SysInfo);

			if ((Version.Major > 6) || ((Version.Major == 6) && (Version.Minor > 1)))
			{
				if (GetProductVersion(out MajorNum, out MinorNum, out BuildNum))
				{
					Version.Major = MajorNum;
					Version.Minor = MinorNum;
					Version.Build = BuildNum;
				}
				else if (GetNetWkstaMajorMinor(out MajorNum, out MinorNum))
				{
					Version.Major = MajorNum;
					Version.Minor = MinorNum;
				}
			}

			Name.Append("Windows");

			switch(Version.Major)
			{
			case 10:
				switch(Version.Minor)
				{
				case 0: Name.Append(!isWinSrv() ? " 10" : " Server 2016");
					// Server 2019 is also 10.0
				}
				break;
			case 6:
				switch(Version.Minor)
				{
				case 0: Name.Append(VerInfo.wProductType == VER_NT_WORKSTATION ? " Vista" : " Server 2008");
				case 1: Name.Append(VerInfo.wProductType == VER_NT_WORKSTATION ? " 7" : " Server 2008 R2");
				case 2: Name.Append(VerInfo.wProductType == VER_NT_WORKSTATION ? " 8" : " Server 2012");
				case 3: Name.Append(!isWinSrv() ? " 8.1" : " Server 2012 R2");
				}
				break;
			case 5:
				switch(Version.Minor)
				{
				case 0: Name.Append(" 2000");
				case 1: Name.Append(" XP");
				case 2:
					{
						if ((VerInfo.wProductType == VER_NT_WORKSTATION) &&
							(SysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)) {
							Name.Append(" XP");
						} else {
							Name.Append(GetSystemMetrics(SM_SERVERR2) == 0 ? " Server 2003" : " Server 2003 R2");
						}
					}
				}
				break;
			}
#elif BF_PLATFORM_LINUX
			Version.Major = 5; //TODO:
			Platform = PlatformID.Unix;
#else // MACOS and ANDROID
			Version.Major = 5; //TODO:
			Platform = PlatformID.MacOSX;
#endif
		}

		public override void ToString(String outVar)
		{
#if BF_PLATFORM_WINDOWS

#if BF_64_BIT
			String arch = Arch64;
#else
			String arch = Arch32;
#endif
			if (Version.Revision == 0)
				outVar.AppendF("{} (Version {}.{}, Build {}, {})", Name, (UInt.Simple)Version.Major, (UInt.Simple)Version.Minor, (UInt.Simple)Version.Build, arch);
			else
				outVar.AppendF("{} Service Pack {} (Version {}.{}, Build {}, {})", Name, (UInt.Simple)Version.Revision, (UInt.Simple)Version.Major, (UInt.Simple)Version.Minor, (UInt.Simple)Version.Build, arch);
#elif BF_PLATFORM_LINUX
			outVar.AppendF("{} {} (Version {}.{}.{})", PrettyName, Name, Version.Major, Version.Minor, Version.Revision);
#else // MACOS and ANDROID
			outVar.AppendF("{} (Version {}.{}.{})", Name, Version.Major, Version.Minor, Version.Revision);
#endif
		}
	}
}
