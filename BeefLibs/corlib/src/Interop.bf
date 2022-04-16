namespace System
{
	namespace Interop
	{
		typealias c_bool = bool;
		typealias c_short = int16;
		typealias c_ushort = uint16;
		typealias c_int = int32;
		typealias c_uint = uint32;
		typealias c_longlong = int64;
		typealias c_ulonglong = uint64;
		typealias c_intptr = int;
		typealias c_uintptr = uint;
		typealias c_size = uint;
		typealias c_char = char8;
		typealias c_uchar = uint8;

#if BF_PLATFORM_WINDOWS
		typealias c_wchar = char16;
#else
		typealias c_wchar = char32;
#endif

#if BF_PLATFORM_WINDOWS || BF_32_BIT
		typealias c_long = int32;
		typealias c_ulong = uint32;
#else
		typealias c_long = int64;
		typealias c_ulong = uint64;
		
#endif
	}
}
