#if BF_PLATFORM_LINUX
using System.Interop;
namespace System;

class Linux
{
	public struct DBus;
	public struct DBusMsg;
	public struct DBusSlot;

	public enum DBusType : char8
	{
		Invalid        = 0,
		Byte           = 'y',
		Bool           = 'b',
		Int16          = 'n',
		UInt16         = 'q',
		Int32          = 'i',
		UInt32         = 'u',
		Int64          = 'x',
		UInt64         = 't',
		Double         = 'd',
		String         = 's',
		ObjectPath     = 'o',
		Signature      = 'g',
		UnixFD         = 'h',
		Array          = 'a',
		Variant        = 'v',
		Struct         = 'r', /* not actually used in signatures */
		StructBegin    = '(',
		StructEnd      = ')',
		DictEntry      = 'e', /* not actually used in signatures */
		DictEntryBegin = '{',
		DictEntryEnd   = '}'
	}

	[CRepr]
	public struct DBusErr
	{
		public char8* name;
		public char8* message;
		public c_int _need_free;
	}

	public typealias DBusMsgHandler = function int32(DBusMsg *m, void *userdata, DBusErr *ret_error);

	[Import("libsystemd.so"), LinkName("sd_bus_open_user")]
	public static extern c_int SdBusOpenUser(DBus **ret);
	[Import("libsystemd.so"), LinkName("sd_bus_open_system")]
	public static extern c_int SdBusOpenSystem(DBus **ret);
	[Import("libsystemd.so"), LinkName("sd_bus_unref")]
	public static extern DBus* SdBusUnref(DBus *bus);
	[Import("libsystemd.so"), LinkName("sd_bus_call")]
	public static extern c_int SdBusCall(DBus *bus, DBusMsg *m, uint64 usec, DBusErr *ret_error, DBusMsg **reply);
	[Import("libsystemd.so"), LinkName("sd_bus_process")]
	public static extern c_int SdBusProcess(DBus *bus, DBusMsg **r);
	[Import("libsystemd.so"), LinkName("sd_bus_wait")]
	public static extern c_int SdBusWait(DBus *bus, uint64 timeout_usec);

	[Import("libsystemd.so"), LinkName("sd_bus_message_new_method_call")]
	public static extern c_int SdBusNewMethodCall(DBus *bus, DBusMsg **m, char8 *destination, char8 *path, char8 *iface, char8 *member);
	[Import("libsystemd.so"), LinkName("sd_bus_message_unref")]
	public static extern DBusMsg* SdBusMessageUnref(DBusMsg *m);
	[Import("libsystemd.so"), LinkName("sd_bus_message_append")]
	public static extern c_int SdBusMessageAppend(DBusMsg *m, char8 *types, ...);
	[Import("libsystemd.so"), LinkName("sd_bus_message_append_basic")]
	public static extern c_int SdBusMessageAppendBasic(DBusMsg *m, DBusType type,  void *p);
	[Import("libsystemd.so"), LinkName("sd_bus_message_append_array")]
	public static extern c_int SdBusMessageAppendArray(DBusMsg *m, DBusType type,  void *ptr, c_size size);
	[Import("libsystemd.so"), LinkName("sd_bus_message_open_container")]
	public static extern c_int SdBusMessageOpenContainer(DBusMsg *m, DBusType type, char8 *contents);
	[Import("libsystemd.so"), LinkName("sd_bus_message_close_container")]
	public static extern c_int SdBusMessageCloseContainer(DBusMsg *m);

	[Import("libsystemd.so"), LinkName("sd_bus_message_read")]
	public static extern c_int SdBusMessageRead(DBusMsg *m, char8 *types, ...);
	[Import("libsystemd.so"), LinkName("sd_bus_message_read_basic")]
	public static extern c_int SdBusMessageReadBasic(DBusMsg *m, DBusType type, void *p);
	[Import("libsystemd.so"), LinkName("sd_bus_message_read_array")]
	public static extern c_int SdBusMessageReadArray(DBusMsg *m, DBusType type,  void **ptr, c_size *size);
	[Import("libsystemd.so"), LinkName("sd_bus_message_skip")]
	public static extern c_int SdBusMessageSkip(DBusMsg *m, char8 *types);
	[Import("libsystemd.so"), LinkName("sd_bus_message_enter_container")]
	public static extern c_int SdBusMessageEnterContainer(DBusMsg *m, DBusType type, char8 *contents);
	[Import("libsystemd.so"), LinkName("sd_bus_message_exit_container")]
	public static extern c_int SdBusMessageExitContainer(DBusMsg *m);
	[Import("libsystemd.so"), LinkName("sd_bus_message_peek_type")]
	public static extern c_int SdBusMessagePeekType(DBusMsg *m, char8 *type, char8 **contents);

	[Import("libsystemd.so"), LinkName("sd_bus_call_method")]
	public static extern c_int SdBusCallMethod(DBus *bus, char8 *destination, char8 *path, char8 *iface, char8 *member, DBusErr *ret_error, DBusMsg **reply, char8 *types, ...);
	[Import("libsystemd.so"), LinkName("sd_bus_match_signal")]
	public static extern c_int SdBusMatchSignal(DBus *bus, DBusSlot **ret, char8 *sender, char8 *path, char8 *iface, char8 *member, DBusMsgHandler callback, void *userdata);

	[Import("libsystemd.so"), LinkName("sd_bus_error_free")]
	public static extern void SdBusErrorFree(DBusErr *e);
}

static
{
	public static mixin TryC(int result) { if(result < 0) return .Err; }
}
#endif