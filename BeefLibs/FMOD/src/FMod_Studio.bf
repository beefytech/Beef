// FMOD Studio API 2.03.14. Adapted from Firelight Technologies headers and C# wrapper.
// Copyright (c), Firelight Technologies Pty, Ltd. 2004-2023.

using System;

namespace FMOD.Studio
{
    public class STUDIO_VERSION
    {
        public const String dll = "fmodstudio.dll";
        public const int32 number = FMOD.VERSION.number;
        public const int32 LOAD_MEMORY_ALIGNMENT = 32;
    }

    public enum STOP_MODE : int32
    {
        ALLOWFADEOUT,
        IMMEDIATE,
    }

    public enum LOADING_STATE : int32
    {
        UNLOADING,
        UNLOADED,
        LOADING,
        LOADED,
        ERROR,
    }

    public enum SYSTEM_CALLBACK_TYPE : uint32
    {
        PREUPDATE = 0x00000001,
        POSTUPDATE = 0x00000002,
        BANK_UNLOAD = 0x00000004,
        LIVEUPDATE_CONNECTED = 0x00000008,
        LIVEUPDATE_DISCONNECTED = 0x00000010,
        ALL = 0xFFFFFFFF,
    }

    public enum PARAMETER_TYPE : int32
    {
        GAME_CONTROLLED,
        AUTOMATIC_DISTANCE,
        AUTOMATIC_EVENT_CONE_ANGLE,
        AUTOMATIC_EVENT_ORIENTATION,
        AUTOMATIC_DIRECTION,
        AUTOMATIC_ELEVATION,
        AUTOMATIC_LISTENER_ORIENTATION,
        AUTOMATIC_SPEED,
        AUTOMATIC_SPEED_ABSOLUTE,
        AUTOMATIC_DISTANCE_NORMALIZED,
        MAX
    }

    public enum PARAMETER_FLAGS : uint32
    {
        READONLY      = 0x00000001,
        AUTOMATIC     = 0x00000002,
        GLOBAL        = 0x00000004,
        DISCRETE      = 0x00000008,
        LABELED       = 0x00000010,
    }

    public enum LOAD_MEMORY_MODE : int32
    {
        LOAD_MEMORY,
        LOAD_MEMORY_POINT,
    }

    public enum LOAD_MEMORY_ALIGNMENT : int32
    {
        VALUE = 32
    }

    public enum USER_PROPERTY_TYPE : int32
    {
        INTEGER,
        BOOLEAN,
        FLOAT,
        STRING,
    }

    public enum INITFLAGS : uint32
    {
        NORMAL                  = 0x00000000,
        LIVEUPDATE              = 0x00000001,
        ALLOW_MISSING_PLUGINS   = 0x00000002,
        SYNCHRONOUS_UPDATE      = 0x00000004,
        DEFERRED_CALLBACKS      = 0x00000008,
        LOAD_FROM_UPDATE        = 0x00000010,
        MEMORY_TRACKING         = 0x00000020,
    }

    public enum LOAD_BANK_FLAGS : uint32
    {
        NORMAL                  = 0x00000000,
        NONBLOCKING             = 0x00000001,
        DECOMPRESS_SAMPLES      = 0x00000002,
        UNENCRYPTED             = 0x00000004,
    }

    public enum COMMANDCAPTURE_FLAGS : uint32
    {
        NORMAL                  = 0x00000000,
        FILEFLUSH               = 0x00000001,
        SKIP_INITIAL_STATE      = 0x00000002,
    }

    public enum COMMANDREPLAY_FLAGS : uint32
    {
        NORMAL                  = 0x00000000,
        SKIP_CLEANUP            = 0x00000001,
        FAST_FORWARD            = 0x00000002,
        SKIP_BANK_LOAD          = 0x00000004,
    }

    public enum PLAYBACK_STATE : int32
    {
        PLAYING,
        SUSTAINING,
        STOPPED,
        STARTING,
        STOPPING,
    }

    public enum EVENT_PROPERTY : int32
    {
        CHANNELPRIORITY,
        SCHEDULE_DELAY,
        SCHEDULE_LOOKAHEAD,
        MINIMUM_DISTANCE,
        MAXIMUM_DISTANCE,
        COOLDOWN,
        MAX
    }

    public enum EVENT_CALLBACK_TYPE : uint32
    {
        CREATED                  = 0x00000001,
        DESTROYED                = 0x00000002,
        STARTING                 = 0x00000004,
        STARTED                  = 0x00000008,
        RESTARTED                = 0x00000010,
        STOPPED                  = 0x00000020,
        START_FAILED             = 0x00000040,
        CREATE_PROGRAMMER_SOUND  = 0x00000080,
        DESTROY_PROGRAMMER_SOUND = 0x00000100,
        PLUGIN_CREATED           = 0x00000200,
        PLUGIN_DESTROYED         = 0x00000400,
        TIMELINE_MARKER          = 0x00000800,
        TIMELINE_BEAT            = 0x00001000,
        SOUND_PLAYED             = 0x00002000,
        SOUND_STOPPED            = 0x00004000,
        REAL_TO_VIRTUAL          = 0x00008000,
        VIRTUAL_TO_REAL          = 0x00010000,
        START_EVENT_COMMAND      = 0x00020000,
        NESTED_TIMELINE_BEAT     = 0x00040000,
        ALL                      = 0xFFFFFFFF,
    }

    public enum INSTANCETYPE : int32
    {
        NONE,
        SYSTEM,
        EVENTDESCRIPTION,
        EVENTINSTANCE,
        PARAMETERINSTANCE,
        BUS,
        VCA,
        BANK,
        COMMANDREPLAY,
    }

    [CRepr]
    public struct BANK_INFO
    {
        public int32 size;
        public int userdata;
        public int32 userdatalength;
        public FILE_OPEN_CALLBACK opencallback;
        public FILE_CLOSE_CALLBACK closecallback;
        public FILE_READ_CALLBACK readcallback;
        public FILE_SEEK_CALLBACK seekcallback;
    }

    [CRepr]
    public struct PARAMETER_ID
    {
        public uint32 data1;
        public uint32 data2;
    }

    [CRepr]
    public struct PARAMETER_DESCRIPTION
    {
        public char8* name;
        public PARAMETER_ID id;
        public float minimum;
        public float maximum;
        public float defaultvalue;
        public PARAMETER_TYPE type;
        public PARAMETER_FLAGS flags;
        public Guid guid;
    }

    [CRepr, Union]
    public struct USER_PROPERTY_VALUE
    {
        public int32 intvalue;
        public int32 boolvalue;
        public float floatvalue;
        public char8* stringvalue;
    }

    [CRepr]
    public struct USER_PROPERTY
    {
        public char8* name;
        public USER_PROPERTY_TYPE type;
        public USER_PROPERTY_VALUE value;

        public int32 intValue() => type == .INTEGER ? value.intvalue : -1;
        public bool boolValue() => (type == .BOOLEAN) && (value.boolvalue != 0);
        public float floatValue() => type == .FLOAT ? value.floatvalue : -1;
        public StringView stringValue() => (type == .STRING) && (value.stringvalue != null) ? StringView(value.stringvalue) : default;
    }

    [CRepr]
    public struct PROGRAMMER_SOUND_PROPERTIES
    {
        public char8* name;
        public int sound;
        public int32 subsoundIndex;
    }

    [CRepr]
    public struct PLUGIN_INSTANCE_PROPERTIES
    {
        public char8* name;
        public int dsp;
    }

    [CRepr]
    public struct TIMELINE_MARKER_PROPERTIES
    {
        public char8* name;
        public int32 position;
    }

    [CRepr]
    public struct TIMELINE_BEAT_PROPERTIES
    {
        public int32 bar;
        public int32 beat;
        public int32 position;
        public float tempo;
        public int32 timesignatureupper;
        public int32 timesignaturelower;
    }

    [CRepr]
    public struct TIMELINE_NESTED_BEAT_PROPERTIES
    {
        public Guid eventid;
        public TIMELINE_BEAT_PROPERTIES properties;
    }

    [CRepr]
    public struct ADVANCEDSETTINGS
    {
        public int32 cbsize;
        public uint32 commandqueuesize;
        public uint32 handleinitialsize;
        public int32 studioupdateperiod;
        public int32 idlesampledatapoolsize;
        public uint32 streamingscheduledelay;
        public char8* encryptionkey;
    }

    [CRepr]
    public struct CPU_USAGE
    {
        public float update;
    }

    [CRepr]
    public struct BUFFER_INFO
    {
        public int32 currentusage;
        public int32 peakusage;
        public int32 capacity;
        public int32 stallcount;
        public float stalltime;
    }

    [CRepr]
    public struct BUFFER_USAGE
    {
        public BUFFER_INFO studiocommandqueue;
        public BUFFER_INFO studiohandle;
    }

    [CRepr]
    public struct SOUND_INFO
    {
        public char8* name_or_data;
        public FMOD.MODE mode;
        public FMOD.CREATESOUNDEXINFO exinfo;
        public int32 subsoundindex;

        public StringView name => ((mode & (.OPENMEMORY | .OPENMEMORY_POint32)) == 0) && (name_or_data != null) ? StringView(name_or_data) : default;
    }

    [CRepr]
    public struct COMMAND_INFO
    {
        public char8* commandname;
        public int32 parentcommandindex;
        public int32 framenumber;
        public float frametime;
        public INSTANCETYPE instancetype;
        public INSTANCETYPE outputtype;
        public uint32 instancehandle;
        public uint32 outputhandle;
    }

    [CRepr]
    public struct MEMORY_USAGE
    {
        public int32 exclusive;
        public int32 inclusive;
        public int32 sampledata;
    }

    [CallingConvention(.Stdcall)]
    public function RESULT SYSTEM_CALLBACK(int system, FMOD.Studio.SYSTEM_CALLBACK_TYPE type, int commanddata, int userdata);

    [CallingConvention(.Stdcall)]
    public function RESULT EVENT_CALLBACK(EVENT_CALLBACK_TYPE type, int @event, int parameters);

    [CallingConvention(.Stdcall)]
    public function RESULT COMMANDREPLAY_FRAME_CALLBACK(int replay, int32 commandindex, float currenttime, int userdata);

    [CallingConvention(.Stdcall)]
    public function RESULT COMMANDREPLAY_LOAD_BANK_CALLBACK(int replay, int32 commandindex, Guid* bankguid, char8* bankfilename, LOAD_BANK_FLAGS flags, int* bank, int userdata);

    [CallingConvention(.Stdcall)]
    public function RESULT COMMANDREPLAY_CREATE_INSTANCE_CALLBACK(int replay, int32 commandindex, int eventdescription, int* instance, int userdata);

    [CallingConvention(.Stdcall)]
    public function RESULT FILE_OPEN_CALLBACK(char8* name, uint32* filesize, int* handle, int userdata);

    [CallingConvention(.Stdcall)]
    public function RESULT FILE_CLOSE_CALLBACK(int handle, int userdata);

    [CallingConvention(.Stdcall)]
    public function RESULT FILE_READ_CALLBACK(int handle, int buffer, uint32 sizebytes, uint32* bytesread, int userdata);

    [CallingConvention(.Stdcall)]
    public function RESULT FILE_SEEK_CALLBACK(int handle, uint32 pos, int userdata);

    public class Util
    {

        public static RESULT parseID(StringView idstring, out Guid id)
        {
            let idstringUTF8 = idstring.ToScopeCStr!();
            id = default;
            return FMOD_Studio_ParseID(idstringUTF8, &id);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_ParseID(char8* idstring, Guid* id);

    }

    // Deleting a wrapper does not release its native resource.
    public class System : HandleBase
    {

        public RESULT setParametersByIDs(Span<PARAMETER_ID> ids, Span<float> values, bool ignoreseekspeed = false)
        {
            if ((ids.Length != values.Length) || (ids.Length > int32.MaxValue))
                return .ERR_INVALID_PARAM;
            return setParametersByIDs(ids.Ptr, values.Ptr, (int32)ids.Length, ignoreseekspeed);
        }

        public RESULT getBankList(Span<int> array, out int32 count)
        {
            count = 0;
            if (array.Length > int32.MaxValue)
                return .ERR_INVALID_PARAM;
            return getBankList(array.Ptr, (int32)array.Length, out count);
        }

        public RESULT getParameterDescriptionList(Span<PARAMETER_DESCRIPTION> array, out int32 count)
        {
            count = 0;
            if (array.Length > int32.MaxValue)
                return .ERR_INVALID_PARAM;
            return getParameterDescriptionList(array.Ptr, (int32)array.Length, out count);
        }

        public RESULT loadBankMemory(Span<uint8> buffer, LOAD_BANK_FLAGS flags, out Bank bank)
        {
            bank = null;
            if ((buffer.Length == 0) || (buffer.Length > int32.MaxValue))
                return .ERR_INVALID_PARAM;
            return loadBankMemory((char8*)buffer.Ptr, (int32)buffer.Length, .LOAD_MEMORY, flags, out bank);
        }

        public RESULT setAdvancedSettings(FMOD.Studio.ADVANCEDSETTINGS settings, StringView encryptionKey)
        {
            var settings;
            settings.encryptionkey = encryptionKey.ToScopeCStr!();
            return setAdvancedSettings(ref settings);
        }

        public RESULT getListenerAttributes(int32 index, out FMOD._3D_ATTRIBUTES attributes)
        {
            return getListenerAttributes(index, out attributes, null);
        }

        public RESULT setListenerAttributes(int32 index, ref FMOD._3D_ATTRIBUTES attributes)
        {
            return setListenerAttributes(index, ref attributes, null);
        }

        public this(int raw) : base(raw) {}

        public static RESULT create(out FMOD.Studio.System system)
        {
            system = null;
            int systemRaw = 0;
            RESULT result = FMOD_Studio_System_Create(&systemRaw, STUDIO_VERSION.number);
            if ((result == .OK) && (systemRaw != 0))
                system = new FMOD.Studio.System(systemRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_Create(int* system, uint32 headerversion);

        public new bool isValid()
        {
            return (rawPtr != 0) && (FMOD_Studio_System_IsValid(rawPtr) != 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern int32 FMOD_Studio_System_IsValid(int system);

        public RESULT setAdvancedSettings(ref FMOD.Studio.ADVANCEDSETTINGS settings)
        {
            settings.cbsize = (int32)sizeof(FMOD.Studio.ADVANCEDSETTINGS);
            return FMOD_Studio_System_SetAdvancedSettings(rawPtr, &settings);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetAdvancedSettings(int system, FMOD.Studio.ADVANCEDSETTINGS* settings);

        public RESULT getAdvancedSettings(out FMOD.Studio.ADVANCEDSETTINGS settings)
        {
            settings = default;
            settings.cbsize = (int32)sizeof(FMOD.Studio.ADVANCEDSETTINGS);
            return FMOD_Studio_System_GetAdvancedSettings(rawPtr, &settings);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetAdvancedSettings(int system, FMOD.Studio.ADVANCEDSETTINGS* settings);

        public RESULT initialize(int32 maxchannels, FMOD.Studio.INITFLAGS studioflags, FMOD.INITFLAGS flags, int extradriverdata = 0)
        {
            return FMOD_Studio_System_Initialize(rawPtr, maxchannels, studioflags, flags, extradriverdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_Initialize(int system, int32 maxchannels, FMOD.Studio.INITFLAGS studioflags, FMOD.INITFLAGS flags, int extradriverdata);

        public RESULT release()
        {
            RESULT result = FMOD_Studio_System_Release(rawPtr);
            if (result == .OK)
                rawPtr = 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_Release(int system);

        public RESULT update()
        {
            return FMOD_Studio_System_Update(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_Update(int system);

        public RESULT getCoreSystem(out FMOD.System coresystem)
        {
            coresystem = null;
            int coresystemRaw = 0;
            RESULT result = FMOD_Studio_System_GetCoreSystem(rawPtr, &coresystemRaw);
            if ((result == .OK) && (coresystemRaw != 0))
                coresystem = new FMOD.System(coresystemRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetCoreSystem(int system, int* coresystem);

        public RESULT getEvent(StringView pathOrID, out EventDescription @event)
        {
            let pathOrIDUTF8 = pathOrID.ToScopeCStr!();
            @event = null;
            int @eventRaw = 0;
            RESULT result = FMOD_Studio_System_GetEvent(rawPtr, pathOrIDUTF8, &@eventRaw);
            if ((result == .OK) && (@eventRaw != 0))
                @event = new EventDescription(@eventRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetEvent(int system, char8* pathOrID, int* @event);

        public RESULT getBus(StringView pathOrID, out Bus bus)
        {
            let pathOrIDUTF8 = pathOrID.ToScopeCStr!();
            bus = null;
            int busRaw = 0;
            RESULT result = FMOD_Studio_System_GetBus(rawPtr, pathOrIDUTF8, &busRaw);
            if ((result == .OK) && (busRaw != 0))
                bus = new Bus(busRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetBus(int system, char8* pathOrID, int* bus);

        public RESULT getVCA(StringView pathOrID, out VCA vca)
        {
            let pathOrIDUTF8 = pathOrID.ToScopeCStr!();
            vca = null;
            int vcaRaw = 0;
            RESULT result = FMOD_Studio_System_GetVCA(rawPtr, pathOrIDUTF8, &vcaRaw);
            if ((result == .OK) && (vcaRaw != 0))
                vca = new VCA(vcaRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetVCA(int system, char8* pathOrID, int* vca);

        public RESULT getBank(StringView pathOrID, out Bank bank)
        {
            let pathOrIDUTF8 = pathOrID.ToScopeCStr!();
            bank = null;
            int bankRaw = 0;
            RESULT result = FMOD_Studio_System_GetBank(rawPtr, pathOrIDUTF8, &bankRaw);
            if ((result == .OK) && (bankRaw != 0))
                bank = new Bank(bankRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetBank(int system, char8* pathOrID, int* bank);

        public RESULT getEventByID(ref Guid id, out EventDescription @event)
        {
            @event = null;
            int @eventRaw = 0;
            RESULT result = FMOD_Studio_System_GetEventByID(rawPtr, &id, &@eventRaw);
            if ((result == .OK) && (@eventRaw != 0))
                @event = new EventDescription(@eventRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetEventByID(int system, Guid* id, int* @event);

        public RESULT getBusByID(ref Guid id, out Bus bus)
        {
            bus = null;
            int busRaw = 0;
            RESULT result = FMOD_Studio_System_GetBusByID(rawPtr, &id, &busRaw);
            if ((result == .OK) && (busRaw != 0))
                bus = new Bus(busRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetBusByID(int system, Guid* id, int* bus);

        public RESULT getVCAByID(ref Guid id, out VCA vca)
        {
            vca = null;
            int vcaRaw = 0;
            RESULT result = FMOD_Studio_System_GetVCAByID(rawPtr, &id, &vcaRaw);
            if ((result == .OK) && (vcaRaw != 0))
                vca = new VCA(vcaRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetVCAByID(int system, Guid* id, int* vca);

        public RESULT getBankByID(ref Guid id, out Bank bank)
        {
            bank = null;
            int bankRaw = 0;
            RESULT result = FMOD_Studio_System_GetBankByID(rawPtr, &id, &bankRaw);
            if ((result == .OK) && (bankRaw != 0))
                bank = new Bank(bankRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetBankByID(int system, Guid* id, int* bank);

        public RESULT getSoundInfo(StringView key, out SOUND_INFO info)
        {
            let keyUTF8 = key.ToScopeCStr!();
            info = default;
            return FMOD_Studio_System_GetSoundInfo(rawPtr, keyUTF8, &info);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetSoundInfo(int system, char8* key, SOUND_INFO* info);

        public RESULT getParameterDescriptionByName(StringView name, out PARAMETER_DESCRIPTION parameter)
        {
            let nameUTF8 = name.ToScopeCStr!();
            parameter = default;
            return FMOD_Studio_System_GetParameterDescriptionByName(rawPtr, nameUTF8, &parameter);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetParameterDescriptionByName(int system, char8* name, PARAMETER_DESCRIPTION* parameter);

        public RESULT getParameterDescriptionByID(PARAMETER_ID id, out PARAMETER_DESCRIPTION parameter)
        {
            parameter = default;
            return FMOD_Studio_System_GetParameterDescriptionByID(rawPtr, id, &parameter);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetParameterDescriptionByID(int system, PARAMETER_ID id, PARAMETER_DESCRIPTION* parameter);

        public RESULT getParameterLabelByName(StringView name, int32 labelindex, char8* label, int32 size, out int32 retrieved)
        {
            let nameUTF8 = name.ToScopeCStr!();
            retrieved = default;
            return FMOD_Studio_System_GetParameterLabelByName(rawPtr, nameUTF8, labelindex, label, size, &retrieved);
        }

        public RESULT getParameterLabelByName(StringView name, int32 labelindex, String label)
        {
            label.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getParameterLabelByName(name, labelindex, buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        label.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetParameterLabelByName(int system, char8* name, int32 labelindex, char8* label, int32 size, int32* retrieved);

        public RESULT getParameterLabelByID(PARAMETER_ID id, int32 labelindex, char8* label, int32 size, out int32 retrieved)
        {
            retrieved = default;
            return FMOD_Studio_System_GetParameterLabelByID(rawPtr, id, labelindex, label, size, &retrieved);
        }

        public RESULT getParameterLabelByID(PARAMETER_ID id, int32 labelindex, String label)
        {
            label.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getParameterLabelByID(id, labelindex, buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        label.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetParameterLabelByID(int system, PARAMETER_ID id, int32 labelindex, char8* label, int32 size, int32* retrieved);

        public RESULT getParameterByID(PARAMETER_ID id, out float value, out float finalvalue)
        {
            value = default;
            finalvalue = default;
            return FMOD_Studio_System_GetParameterByID(rawPtr, id, &value, &finalvalue);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetParameterByID(int system, PARAMETER_ID id, float* value, float* finalvalue);

        public RESULT setParameterByID(PARAMETER_ID id, float value, bool ignoreseekspeed = false)
        {
            return FMOD_Studio_System_SetParameterByID(rawPtr, id, value, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetParameterByID(int system, PARAMETER_ID id, float value, int32 ignoreseekspeed);

        public RESULT setParameterByIDWithLabel(PARAMETER_ID id, StringView label, bool ignoreseekspeed = false)
        {
            let labelUTF8 = label.ToScopeCStr!();
            return FMOD_Studio_System_SetParameterByIDWithLabel(rawPtr, id, labelUTF8, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetParameterByIDWithLabel(int system, PARAMETER_ID id, char8* label, int32 ignoreseekspeed);

        public RESULT setParametersByIDs(PARAMETER_ID* ids, float* values, int32 count, bool ignoreseekspeed = false)
        {
            return FMOD_Studio_System_SetParametersByIDs(rawPtr, ids, values, count, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetParametersByIDs(int system, PARAMETER_ID* ids, float* values, int32 count, int32 ignoreseekspeed);

        public RESULT getParameterByName(StringView name, out float value, out float finalvalue)
        {
            let nameUTF8 = name.ToScopeCStr!();
            value = default;
            finalvalue = default;
            return FMOD_Studio_System_GetParameterByName(rawPtr, nameUTF8, &value, &finalvalue);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetParameterByName(int system, char8* name, float* value, float* finalvalue);

        public RESULT setParameterByName(StringView name, float value, bool ignoreseekspeed = false)
        {
            let nameUTF8 = name.ToScopeCStr!();
            return FMOD_Studio_System_SetParameterByName(rawPtr, nameUTF8, value, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetParameterByName(int system, char8* name, float value, int32 ignoreseekspeed);

        public RESULT setParameterByNameWithLabel(StringView name, StringView label, bool ignoreseekspeed = false)
        {
            let nameUTF8 = name.ToScopeCStr!();
            let labelUTF8 = label.ToScopeCStr!();
            return FMOD_Studio_System_SetParameterByNameWithLabel(rawPtr, nameUTF8, labelUTF8, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetParameterByNameWithLabel(int system, char8* name, char8* label, int32 ignoreseekspeed);

        public RESULT lookupID(StringView path, out Guid id)
        {
            let pathUTF8 = path.ToScopeCStr!();
            id = default;
            return FMOD_Studio_System_LookupID(rawPtr, pathUTF8, &id);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_LookupID(int system, char8* path, Guid* id);

        public RESULT lookupPath(ref Guid id, char8* path, int32 size, out int32 retrieved)
        {
            retrieved = default;
            return FMOD_Studio_System_LookupPath(rawPtr, &id, path, size, &retrieved);
        }

        public RESULT lookupPath(ref Guid id, String path)
        {
            path.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = lookupPath(ref id, buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        path.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_LookupPath(int system, Guid* id, char8* path, int32 size, int32* retrieved);

        public RESULT getNumListeners(out int32 numlisteners)
        {
            numlisteners = default;
            return FMOD_Studio_System_GetNumListeners(rawPtr, &numlisteners);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetNumListeners(int system, int32* numlisteners);

        public RESULT setNumListeners(int32 numlisteners)
        {
            return FMOD_Studio_System_SetNumListeners(rawPtr, numlisteners);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetNumListeners(int system, int32 numlisteners);

        public RESULT getListenerAttributes(int32 index, out FMOD._3D_ATTRIBUTES attributes, FMOD.VECTOR* attenuationposition)
        {
            attributes = default;
            return FMOD_Studio_System_GetListenerAttributes(rawPtr, index, &attributes, attenuationposition);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetListenerAttributes(int system, int32 index, FMOD._3D_ATTRIBUTES* attributes, FMOD.VECTOR* attenuationposition);

        public RESULT setListenerAttributes(int32 index, ref FMOD._3D_ATTRIBUTES attributes, FMOD.VECTOR* attenuationposition)
        {
            return FMOD_Studio_System_SetListenerAttributes(rawPtr, index, &attributes, attenuationposition);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetListenerAttributes(int system, int32 index, FMOD._3D_ATTRIBUTES* attributes, FMOD.VECTOR* attenuationposition);

        public RESULT getListenerWeight(int32 index, out float weight)
        {
            weight = default;
            return FMOD_Studio_System_GetListenerWeight(rawPtr, index, &weight);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetListenerWeight(int system, int32 index, float* weight);

        public RESULT setListenerWeight(int32 index, float weight)
        {
            return FMOD_Studio_System_SetListenerWeight(rawPtr, index, weight);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetListenerWeight(int system, int32 index, float weight);

        public RESULT loadBankFile(StringView filename, LOAD_BANK_FLAGS flags, out Bank bank)
        {
            let filenameUTF8 = filename.ToScopeCStr!();
            bank = null;
            int bankRaw = 0;
            RESULT result = FMOD_Studio_System_LoadBankFile(rawPtr, filenameUTF8, flags, &bankRaw);
            if ((result == .OK) && (bankRaw != 0))
                bank = new Bank(bankRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_LoadBankFile(int system, char8* filename, LOAD_BANK_FLAGS flags, int* bank);

        // LOAD_MEMORY_POINT requires 32-byte alignment and storage until the bank unloads.
        public RESULT loadBankMemory(char8* buffer, int32 length, LOAD_MEMORY_MODE mode, LOAD_BANK_FLAGS flags, out Bank bank)
        {
            bank = null;
            int bankRaw = 0;
            RESULT result = FMOD_Studio_System_LoadBankMemory(rawPtr, buffer, length, mode, flags, &bankRaw);
            if ((result == .OK) && (bankRaw != 0))
                bank = new Bank(bankRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_LoadBankMemory(int system, char8* buffer, int32 length, LOAD_MEMORY_MODE mode, LOAD_BANK_FLAGS flags, int* bank);

        public RESULT loadBankCustom(BANK_INFO info, LOAD_BANK_FLAGS flags, out Bank bank)
        {
            var info;
            info.size = (int32)sizeof(BANK_INFO);
            bank = null;
            int bankRaw = 0;
            RESULT result = FMOD_Studio_System_LoadBankCustom(rawPtr, &info, flags, &bankRaw);
            if ((result == .OK) && (bankRaw != 0))
                bank = new Bank(bankRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_LoadBankCustom(int system, BANK_INFO* info, LOAD_BANK_FLAGS flags, int* bank);

        public RESULT registerPlugin(ref FMOD.DSP_DESCRIPTION description)
        {
            return FMOD_Studio_System_RegisterPlugin(rawPtr, &description);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_RegisterPlugin(int system, FMOD.DSP_DESCRIPTION* description);

        public RESULT unregisterPlugin(StringView name)
        {
            let nameUTF8 = name.ToScopeCStr!();
            return FMOD_Studio_System_UnregisterPlugin(rawPtr, nameUTF8);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_UnregisterPlugin(int system, char8* name);

        public RESULT unloadAll()
        {
            return FMOD_Studio_System_UnloadAll(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_UnloadAll(int system);

        public RESULT flushCommands()
        {
            return FMOD_Studio_System_FlushCommands(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_FlushCommands(int system);

        public RESULT flushSampleLoading()
        {
            return FMOD_Studio_System_FlushSampleLoading(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_FlushSampleLoading(int system);

        public RESULT startCommandCapture(StringView filename, COMMANDCAPTURE_FLAGS flags)
        {
            let filenameUTF8 = filename.ToScopeCStr!();
            return FMOD_Studio_System_StartCommandCapture(rawPtr, filenameUTF8, flags);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_StartCommandCapture(int system, char8* filename, COMMANDCAPTURE_FLAGS flags);

        public RESULT stopCommandCapture()
        {
            return FMOD_Studio_System_StopCommandCapture(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_StopCommandCapture(int system);

        public RESULT loadCommandReplay(StringView filename, COMMANDREPLAY_FLAGS flags, out CommandReplay replay)
        {
            let filenameUTF8 = filename.ToScopeCStr!();
            replay = null;
            int replayRaw = 0;
            RESULT result = FMOD_Studio_System_LoadCommandReplay(rawPtr, filenameUTF8, flags, &replayRaw);
            if ((result == .OK) && (replayRaw != 0))
                replay = new CommandReplay(replayRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_LoadCommandReplay(int system, char8* filename, COMMANDREPLAY_FLAGS flags, int* replay);

        public RESULT getBankCount(out int32 count)
        {
            count = default;
            return FMOD_Studio_System_GetBankCount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetBankCount(int system, int32* count);

        public RESULT getBankList(int* array, int32 capacity, out int32 count)
        {
            count = default;
            return FMOD_Studio_System_GetBankList(rawPtr, array, capacity, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetBankList(int system, int* array, int32 capacity, int32* count);

        public RESULT getParameterDescriptionCount(out int32 count)
        {
            count = default;
            return FMOD_Studio_System_GetParameterDescriptionCount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetParameterDescriptionCount(int system, int32* count);

        public RESULT getParameterDescriptionList(PARAMETER_DESCRIPTION* array, int32 capacity, out int32 count)
        {
            count = default;
            return FMOD_Studio_System_GetParameterDescriptionList(rawPtr, array, capacity, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetParameterDescriptionList(int system, PARAMETER_DESCRIPTION* array, int32 capacity, int32* count);

        public RESULT getCPUUsage(out FMOD.Studio.CPU_USAGE usage, out FMOD.CPU_USAGE usage_core)
        {
            usage = default;
            usage_core = default;
            return FMOD_Studio_System_GetCPUUsage(rawPtr, &usage, &usage_core);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetCPUUsage(int system, FMOD.Studio.CPU_USAGE* usage, FMOD.CPU_USAGE* usage_core);

        public RESULT getBufferUsage(out BUFFER_USAGE usage)
        {
            usage = default;
            return FMOD_Studio_System_GetBufferUsage(rawPtr, &usage);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetBufferUsage(int system, BUFFER_USAGE* usage);

        public RESULT resetBufferUsage()
        {
            return FMOD_Studio_System_ResetBufferUsage(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_ResetBufferUsage(int system);

        public RESULT setCallback(FMOD.Studio.SYSTEM_CALLBACK callback, FMOD.Studio.SYSTEM_CALLBACK_TYPE callbackmask = .ALL)
        {
            return FMOD_Studio_System_SetCallback(rawPtr, callback, callbackmask);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetCallback(int system, FMOD.Studio.SYSTEM_CALLBACK callback, FMOD.Studio.SYSTEM_CALLBACK_TYPE callbackmask);

        public RESULT setUserData(int userdata)
        {
            return FMOD_Studio_System_SetUserData(rawPtr, userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_SetUserData(int system, int userdata);

        public RESULT getUserData(out int userdata)
        {
            userdata = 0;
            return FMOD_Studio_System_GetUserData(rawPtr, &userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetUserData(int system, int* userdata);

        public RESULT getMemoryUsage(out MEMORY_USAGE memoryusage)
        {
            memoryusage = default;
            return FMOD_Studio_System_GetMemoryUsage(rawPtr, &memoryusage);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_System_GetMemoryUsage(int system, MEMORY_USAGE* memoryusage);

    }

    public class EventDescription : HandleBase
    {

        public RESULT getInstanceList(Span<int> array, out int32 count)
        {
            count = 0;
            if (array.Length > int32.MaxValue)
                return .ERR_INVALID_PARAM;
            return getInstanceList(array.Ptr, (int32)array.Length, out count);
        }

        public this(int raw) : base(raw) {}

        public new bool isValid()
        {
            return (rawPtr != 0) && (FMOD_Studio_EventDescription_IsValid(rawPtr) != 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern int32 FMOD_Studio_EventDescription_IsValid(int eventdescription);

        public RESULT getID(out Guid id)
        {
            id = default;
            return FMOD_Studio_EventDescription_GetID(rawPtr, &id);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetID(int eventdescription, Guid* id);

        public RESULT getPath(char8* path, int32 size, out int32 retrieved)
        {
            retrieved = default;
            return FMOD_Studio_EventDescription_GetPath(rawPtr, path, size, &retrieved);
        }

        public RESULT getPath(String path)
        {
            path.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getPath(buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        path.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetPath(int eventdescription, char8* path, int32 size, int32* retrieved);

        public RESULT getParameterDescriptionCount(out int32 count)
        {
            count = default;
            return FMOD_Studio_EventDescription_GetParameterDescriptionCount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetParameterDescriptionCount(int eventdescription, int32* count);

        public RESULT getParameterDescriptionByIndex(int32 index, out PARAMETER_DESCRIPTION parameter)
        {
            parameter = default;
            return FMOD_Studio_EventDescription_GetParameterDescriptionByIndex(rawPtr, index, &parameter);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetParameterDescriptionByIndex(int eventdescription, int32 index, PARAMETER_DESCRIPTION* parameter);

        public RESULT getParameterDescriptionByName(StringView name, out PARAMETER_DESCRIPTION parameter)
        {
            let nameUTF8 = name.ToScopeCStr!();
            parameter = default;
            return FMOD_Studio_EventDescription_GetParameterDescriptionByName(rawPtr, nameUTF8, &parameter);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetParameterDescriptionByName(int eventdescription, char8* name, PARAMETER_DESCRIPTION* parameter);

        public RESULT getParameterDescriptionByID(PARAMETER_ID id, out PARAMETER_DESCRIPTION parameter)
        {
            parameter = default;
            return FMOD_Studio_EventDescription_GetParameterDescriptionByID(rawPtr, id, &parameter);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetParameterDescriptionByID(int eventdescription, PARAMETER_ID id, PARAMETER_DESCRIPTION* parameter);

        public RESULT getParameterLabelByIndex(int32 index, int32 labelindex, char8* label, int32 size, out int32 retrieved)
        {
            retrieved = default;
            return FMOD_Studio_EventDescription_GetParameterLabelByIndex(rawPtr, index, labelindex, label, size, &retrieved);
        }

        public RESULT getParameterLabelByIndex(int32 index, int32 labelindex, String label)
        {
            label.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getParameterLabelByIndex(index, labelindex, buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        label.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetParameterLabelByIndex(int eventdescription, int32 index, int32 labelindex, char8* label, int32 size, int32* retrieved);

        public RESULT getParameterLabelByName(StringView name, int32 labelindex, char8* label, int32 size, out int32 retrieved)
        {
            let nameUTF8 = name.ToScopeCStr!();
            retrieved = default;
            return FMOD_Studio_EventDescription_GetParameterLabelByName(rawPtr, nameUTF8, labelindex, label, size, &retrieved);
        }

        public RESULT getParameterLabelByName(StringView name, int32 labelindex, String label)
        {
            label.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getParameterLabelByName(name, labelindex, buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        label.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetParameterLabelByName(int eventdescription, char8* name, int32 labelindex, char8* label, int32 size, int32* retrieved);

        public RESULT getParameterLabelByID(PARAMETER_ID id, int32 labelindex, char8* label, int32 size, out int32 retrieved)
        {
            retrieved = default;
            return FMOD_Studio_EventDescription_GetParameterLabelByID(rawPtr, id, labelindex, label, size, &retrieved);
        }

        public RESULT getParameterLabelByID(PARAMETER_ID id, int32 labelindex, String label)
        {
            label.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getParameterLabelByID(id, labelindex, buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        label.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetParameterLabelByID(int eventdescription, PARAMETER_ID id, int32 labelindex, char8* label, int32 size, int32* retrieved);

        public RESULT getUserPropertyCount(out int32 count)
        {
            count = default;
            return FMOD_Studio_EventDescription_GetUserPropertyCount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetUserPropertyCount(int eventdescription, int32* count);

        public RESULT getUserPropertyByIndex(int32 index, out USER_PROPERTY property)
        {
            property = default;
            return FMOD_Studio_EventDescription_GetUserPropertyByIndex(rawPtr, index, &property);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetUserPropertyByIndex(int eventdescription, int32 index, USER_PROPERTY* property);

        public RESULT getUserProperty(StringView name, out USER_PROPERTY property)
        {
            let nameUTF8 = name.ToScopeCStr!();
            property = default;
            return FMOD_Studio_EventDescription_GetUserProperty(rawPtr, nameUTF8, &property);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetUserProperty(int eventdescription, char8* name, USER_PROPERTY* property);

        public RESULT getLength(out int32 length)
        {
            length = default;
            return FMOD_Studio_EventDescription_GetLength(rawPtr, &length);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetLength(int eventdescription, int32* length);

        public RESULT getMinMaxDistance(out float min, out float max)
        {
            min = default;
            max = default;
            return FMOD_Studio_EventDescription_GetMinMaxDistance(rawPtr, &min, &max);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetMinMaxDistance(int eventdescription, float* min, float* max);

        public RESULT getSoundSize(out float size)
        {
            size = default;
            return FMOD_Studio_EventDescription_GetSoundSize(rawPtr, &size);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetSoundSize(int eventdescription, float* size);

        public RESULT isSnapshot(out bool snapshot)
        {
            int32 snapshotRaw = 0;
            RESULT result = FMOD_Studio_EventDescription_IsSnapshot(rawPtr, &snapshotRaw);
            snapshot = snapshotRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_IsSnapshot(int eventdescription, int32* snapshot);

        public RESULT isOneshot(out bool oneshot)
        {
            int32 oneshotRaw = 0;
            RESULT result = FMOD_Studio_EventDescription_IsOneshot(rawPtr, &oneshotRaw);
            oneshot = oneshotRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_IsOneshot(int eventdescription, int32* oneshot);

        public RESULT isStream(out bool isStream)
        {
            int32 isStreamRaw = 0;
            RESULT result = FMOD_Studio_EventDescription_IsStream(rawPtr, &isStreamRaw);
            isStream = isStreamRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_IsStream(int eventdescription, int32* isStream);

        public RESULT is3D(out bool is3D)
        {
            int32 is3DRaw = 0;
            RESULT result = FMOD_Studio_EventDescription_Is3D(rawPtr, &is3DRaw);
            is3D = is3DRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_Is3D(int eventdescription, int32* is3D);

        public RESULT isDopplerEnabled(out bool doppler)
        {
            int32 dopplerRaw = 0;
            RESULT result = FMOD_Studio_EventDescription_IsDopplerEnabled(rawPtr, &dopplerRaw);
            doppler = dopplerRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_IsDopplerEnabled(int eventdescription, int32* doppler);

        public RESULT hasSustainPoint(out bool sustainPoint)
        {
            int32 sustainPointRaw = 0;
            RESULT result = FMOD_Studio_EventDescription_HasSustainPoint(rawPtr, &sustainPointRaw);
            sustainPoint = sustainPointRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_HasSustainPoint(int eventdescription, int32* sustainPoint);

        public RESULT createInstance(out EventInstance instance)
        {
            instance = null;
            int instanceRaw = 0;
            RESULT result = FMOD_Studio_EventDescription_CreateInstance(rawPtr, &instanceRaw);
            if ((result == .OK) && (instanceRaw != 0))
                instance = new EventInstance(instanceRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_CreateInstance(int eventdescription, int* instance);

        public RESULT getInstanceCount(out int32 count)
        {
            count = default;
            return FMOD_Studio_EventDescription_GetInstanceCount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetInstanceCount(int eventdescription, int32* count);

        public RESULT getInstanceList(int* array, int32 capacity, out int32 count)
        {
            count = default;
            return FMOD_Studio_EventDescription_GetInstanceList(rawPtr, array, capacity, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetInstanceList(int eventdescription, int* array, int32 capacity, int32* count);

        public RESULT loadSampleData()
        {
            return FMOD_Studio_EventDescription_LoadSampleData(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_LoadSampleData(int eventdescription);

        public RESULT unloadSampleData()
        {
            return FMOD_Studio_EventDescription_UnloadSampleData(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_UnloadSampleData(int eventdescription);

        public RESULT getSampleLoadingState(out LOADING_STATE state)
        {
            state = default;
            return FMOD_Studio_EventDescription_GetSampleLoadingState(rawPtr, &state);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetSampleLoadingState(int eventdescription, LOADING_STATE* state);

        public RESULT releaseAllInstances()
        {
            return FMOD_Studio_EventDescription_ReleaseAllInstances(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_ReleaseAllInstances(int eventdescription);

        public RESULT setCallback(EVENT_CALLBACK callback, EVENT_CALLBACK_TYPE callbackmask = .ALL)
        {
            return FMOD_Studio_EventDescription_SetCallback(rawPtr, callback, callbackmask);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_SetCallback(int eventdescription, EVENT_CALLBACK callback, EVENT_CALLBACK_TYPE callbackmask);

        public RESULT getUserData(out int userdata)
        {
            userdata = 0;
            return FMOD_Studio_EventDescription_GetUserData(rawPtr, &userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_GetUserData(int eventdescription, int* userdata);

        public RESULT setUserData(int userdata)
        {
            return FMOD_Studio_EventDescription_SetUserData(rawPtr, userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventDescription_SetUserData(int eventdescription, int userdata);

    }

    public class EventInstance : HandleBase
    {
        public RESULT getSystem(out FMOD.Studio.System system)
        {
            system = null;
            int systemRaw = 0;
            RESULT result = FMOD_Studio_EventInstance_GetSystem(rawPtr, &systemRaw);
            if ((result == .OK) && (systemRaw != 0))
                system = new FMOD.Studio.System(systemRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetSystem(int replay, int* system);


        public RESULT setParametersByIDs(Span<PARAMETER_ID> ids, Span<float> values, bool ignoreseekspeed = false)
        {
            if ((ids.Length != values.Length) || (ids.Length > int32.MaxValue))
                return .ERR_INVALID_PARAM;
            return setParametersByIDs(ids.Ptr, values.Ptr, (int32)ids.Length, ignoreseekspeed);
        }

        public this(int raw) : base(raw) {}

        public new bool isValid()
        {
            return (rawPtr != 0) && (FMOD_Studio_EventInstance_IsValid(rawPtr) != 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern int32 FMOD_Studio_EventInstance_IsValid(int eventinstance);

        public RESULT getDescription(out EventDescription description)
        {
            description = null;
            int descriptionRaw = 0;
            RESULT result = FMOD_Studio_EventInstance_GetDescription(rawPtr, &descriptionRaw);
            if ((result == .OK) && (descriptionRaw != 0))
                description = new EventDescription(descriptionRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetDescription(int eventinstance, int* description);

        public RESULT getVolume(out float volume, out float finalvolume)
        {
            volume = default;
            finalvolume = default;
            return FMOD_Studio_EventInstance_GetVolume(rawPtr, &volume, &finalvolume);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetVolume(int eventinstance, float* volume, float* finalvolume);

        public RESULT setVolume(float volume)
        {
            return FMOD_Studio_EventInstance_SetVolume(rawPtr, volume);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetVolume(int eventinstance, float volume);

        public RESULT getPitch(out float pitch, out float finalpitch)
        {
            pitch = default;
            finalpitch = default;
            return FMOD_Studio_EventInstance_GetPitch(rawPtr, &pitch, &finalpitch);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetPitch(int eventinstance, float* pitch, float* finalpitch);

        public RESULT setPitch(float pitch)
        {
            return FMOD_Studio_EventInstance_SetPitch(rawPtr, pitch);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetPitch(int eventinstance, float pitch);

        public RESULT get3DAttributes(out FMOD._3D_ATTRIBUTES attributes)
        {
            attributes = default;
            return FMOD_Studio_EventInstance_Get3DAttributes(rawPtr, &attributes);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_Get3DAttributes(int eventinstance, FMOD._3D_ATTRIBUTES* attributes);

        public RESULT set3DAttributes(ref FMOD._3D_ATTRIBUTES attributes)
        {
            return FMOD_Studio_EventInstance_Set3DAttributes(rawPtr, &attributes);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_Set3DAttributes(int eventinstance, FMOD._3D_ATTRIBUTES* attributes);

        public RESULT getListenerMask(out uint32 mask)
        {
            mask = default;
            return FMOD_Studio_EventInstance_GetListenerMask(rawPtr, &mask);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetListenerMask(int eventinstance, uint32* mask);

        public RESULT setListenerMask(uint32 mask)
        {
            return FMOD_Studio_EventInstance_SetListenerMask(rawPtr, mask);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetListenerMask(int eventinstance, uint32 mask);

        public RESULT getProperty(EVENT_PROPERTY index, out float value)
        {
            value = default;
            return FMOD_Studio_EventInstance_GetProperty(rawPtr, index, &value);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetProperty(int eventinstance, EVENT_PROPERTY index, float* value);

        public RESULT setProperty(EVENT_PROPERTY index, float value)
        {
            return FMOD_Studio_EventInstance_SetProperty(rawPtr, index, value);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetProperty(int eventinstance, EVENT_PROPERTY index, float value);

        public RESULT getReverbLevel(int32 index, out float level)
        {
            level = default;
            return FMOD_Studio_EventInstance_GetReverbLevel(rawPtr, index, &level);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetReverbLevel(int eventinstance, int32 index, float* level);

        public RESULT setReverbLevel(int32 index, float level)
        {
            return FMOD_Studio_EventInstance_SetReverbLevel(rawPtr, index, level);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetReverbLevel(int eventinstance, int32 index, float level);

        public RESULT getPaused(out bool paused)
        {
            int32 pausedRaw = 0;
            RESULT result = FMOD_Studio_EventInstance_GetPaused(rawPtr, &pausedRaw);
            paused = pausedRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetPaused(int eventinstance, int32* paused);

        public RESULT setPaused(bool paused)
        {
            return FMOD_Studio_EventInstance_SetPaused(rawPtr, paused ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetPaused(int eventinstance, int32 paused);

        public RESULT start()
        {
            return FMOD_Studio_EventInstance_Start(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_Start(int eventinstance);

        public RESULT stop(STOP_MODE mode)
        {
            return FMOD_Studio_EventInstance_Stop(rawPtr, mode);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_Stop(int eventinstance, STOP_MODE mode);

        public RESULT getTimelinePosition(out int32 position)
        {
            position = default;
            return FMOD_Studio_EventInstance_GetTimelinePosition(rawPtr, &position);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetTimelinePosition(int eventinstance, int32* position);

        public RESULT setTimelinePosition(int32 position)
        {
            return FMOD_Studio_EventInstance_SetTimelinePosition(rawPtr, position);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetTimelinePosition(int eventinstance, int32 position);

        public RESULT getPlaybackState(out PLAYBACK_STATE state)
        {
            state = default;
            return FMOD_Studio_EventInstance_GetPlaybackState(rawPtr, &state);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetPlaybackState(int eventinstance, PLAYBACK_STATE* state);

        public RESULT getChannelGroup(out FMOD.ChannelGroup group)
        {
            group = null;
            int groupRaw = 0;
            RESULT result = FMOD_Studio_EventInstance_GetChannelGroup(rawPtr, &groupRaw);
            if ((result == .OK) && (groupRaw != 0))
                group = new FMOD.ChannelGroup(groupRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetChannelGroup(int eventinstance, int* group);

        public RESULT getMinMaxDistance(out float min, out float max)
        {
            min = default;
            max = default;
            return FMOD_Studio_EventInstance_GetMinMaxDistance(rawPtr, &min, &max);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetMinMaxDistance(int eventinstance, float* min, float* max);

        public RESULT release()
        {
            RESULT result = FMOD_Studio_EventInstance_Release(rawPtr);
            if (result == .OK)
                rawPtr = 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_Release(int eventinstance);

        public RESULT isVirtual(out bool virtualstate)
        {
            int32 virtualstateRaw = 0;
            RESULT result = FMOD_Studio_EventInstance_IsVirtual(rawPtr, &virtualstateRaw);
            virtualstate = virtualstateRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_IsVirtual(int eventinstance, int32* virtualstate);

        public RESULT getParameterByName(StringView name, out float value, out float finalvalue)
        {
            let nameUTF8 = name.ToScopeCStr!();
            value = default;
            finalvalue = default;
            return FMOD_Studio_EventInstance_GetParameterByName(rawPtr, nameUTF8, &value, &finalvalue);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetParameterByName(int eventinstance, char8* name, float* value, float* finalvalue);

        public RESULT setParameterByName(StringView name, float value, bool ignoreseekspeed = false)
        {
            let nameUTF8 = name.ToScopeCStr!();
            return FMOD_Studio_EventInstance_SetParameterByName(rawPtr, nameUTF8, value, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetParameterByName(int eventinstance, char8* name, float value, int32 ignoreseekspeed);

        public RESULT setParameterByNameWithLabel(StringView name, StringView label, bool ignoreseekspeed = false)
        {
            let nameUTF8 = name.ToScopeCStr!();
            let labelUTF8 = label.ToScopeCStr!();
            return FMOD_Studio_EventInstance_SetParameterByNameWithLabel(rawPtr, nameUTF8, labelUTF8, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetParameterByNameWithLabel(int eventinstance, char8* name, char8* label, int32 ignoreseekspeed);

        public RESULT getParameterByID(PARAMETER_ID id, out float value, out float finalvalue)
        {
            value = default;
            finalvalue = default;
            return FMOD_Studio_EventInstance_GetParameterByID(rawPtr, id, &value, &finalvalue);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetParameterByID(int eventinstance, PARAMETER_ID id, float* value, float* finalvalue);

        public RESULT setParameterByID(PARAMETER_ID id, float value, bool ignoreseekspeed = false)
        {
            return FMOD_Studio_EventInstance_SetParameterByID(rawPtr, id, value, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetParameterByID(int eventinstance, PARAMETER_ID id, float value, int32 ignoreseekspeed);

        public RESULT setParameterByIDWithLabel(PARAMETER_ID id, StringView label, bool ignoreseekspeed = false)
        {
            let labelUTF8 = label.ToScopeCStr!();
            return FMOD_Studio_EventInstance_SetParameterByIDWithLabel(rawPtr, id, labelUTF8, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetParameterByIDWithLabel(int eventinstance, PARAMETER_ID id, char8* label, int32 ignoreseekspeed);

        public RESULT setParametersByIDs(PARAMETER_ID* ids, float* values, int32 count, bool ignoreseekspeed = false)
        {
            return FMOD_Studio_EventInstance_SetParametersByIDs(rawPtr, ids, values, count, ignoreseekspeed ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetParametersByIDs(int eventinstance, PARAMETER_ID* ids, float* values, int32 count, int32 ignoreseekspeed);

        public RESULT keyOff()
        {
            return FMOD_Studio_EventInstance_KeyOff(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_KeyOff(int eventinstance);

        public RESULT setCallback(EVENT_CALLBACK callback, EVENT_CALLBACK_TYPE callbackmask = .ALL)
        {
            return FMOD_Studio_EventInstance_SetCallback(rawPtr, callback, callbackmask);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetCallback(int eventinstance, EVENT_CALLBACK callback, EVENT_CALLBACK_TYPE callbackmask);

        public RESULT getUserData(out int userdata)
        {
            userdata = 0;
            return FMOD_Studio_EventInstance_GetUserData(rawPtr, &userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetUserData(int eventinstance, int* userdata);

        public RESULT setUserData(int userdata)
        {
            return FMOD_Studio_EventInstance_SetUserData(rawPtr, userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_SetUserData(int eventinstance, int userdata);

        public RESULT getCPUUsage(out uint32 exclusive, out uint32 inclusive)
        {
            exclusive = default;
            inclusive = default;
            return FMOD_Studio_EventInstance_GetCPUUsage(rawPtr, &exclusive, &inclusive);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetCPUUsage(int eventinstance, uint32* exclusive, uint32* inclusive);

        public RESULT getMemoryUsage(out MEMORY_USAGE memoryusage)
        {
            memoryusage = default;
            return FMOD_Studio_EventInstance_GetMemoryUsage(rawPtr, &memoryusage);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_EventInstance_GetMemoryUsage(int eventinstance, MEMORY_USAGE* memoryusage);

    }

    public class Bus : HandleBase
    {

        public this(int raw) : base(raw) {}

        public new bool isValid()
        {
            return (rawPtr != 0) && (FMOD_Studio_Bus_IsValid(rawPtr) != 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern int32 FMOD_Studio_Bus_IsValid(int bus);

        public RESULT getID(out Guid id)
        {
            id = default;
            return FMOD_Studio_Bus_GetID(rawPtr, &id);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_GetID(int bus, Guid* id);

        public RESULT getPath(char8* path, int32 size, out int32 retrieved)
        {
            retrieved = default;
            return FMOD_Studio_Bus_GetPath(rawPtr, path, size, &retrieved);
        }

        public RESULT getPath(String path)
        {
            path.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getPath(buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        path.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_GetPath(int bus, char8* path, int32 size, int32* retrieved);

        public RESULT getVolume(out float volume, out float finalvolume)
        {
            volume = default;
            finalvolume = default;
            return FMOD_Studio_Bus_GetVolume(rawPtr, &volume, &finalvolume);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_GetVolume(int bus, float* volume, float* finalvolume);

        public RESULT setVolume(float volume)
        {
            return FMOD_Studio_Bus_SetVolume(rawPtr, volume);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_SetVolume(int bus, float volume);

        public RESULT getPaused(out bool paused)
        {
            int32 pausedRaw = 0;
            RESULT result = FMOD_Studio_Bus_GetPaused(rawPtr, &pausedRaw);
            paused = pausedRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_GetPaused(int bus, int32* paused);

        public RESULT setPaused(bool paused)
        {
            return FMOD_Studio_Bus_SetPaused(rawPtr, paused ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_SetPaused(int bus, int32 paused);

        public RESULT getMute(out bool mute)
        {
            int32 muteRaw = 0;
            RESULT result = FMOD_Studio_Bus_GetMute(rawPtr, &muteRaw);
            mute = muteRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_GetMute(int bus, int32* mute);

        public RESULT setMute(bool mute)
        {
            return FMOD_Studio_Bus_SetMute(rawPtr, mute ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_SetMute(int bus, int32 mute);

        public RESULT stopAllEvents(STOP_MODE mode)
        {
            return FMOD_Studio_Bus_StopAllEvents(rawPtr, mode);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_StopAllEvents(int bus, STOP_MODE mode);

        public RESULT getPortIndex(out uint64 index)
        {
            index = default;
            return FMOD_Studio_Bus_GetPortIndex(rawPtr, &index);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_GetPortIndex(int bus, uint64* index);

        public RESULT setPortIndex(uint64 index)
        {
            return FMOD_Studio_Bus_SetPortIndex(rawPtr, index);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_SetPortIndex(int bus, uint64 index);

        public RESULT lockChannelGroup()
        {
            return FMOD_Studio_Bus_LockChannelGroup(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_LockChannelGroup(int bus);

        public RESULT unlockChannelGroup()
        {
            return FMOD_Studio_Bus_UnlockChannelGroup(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_UnlockChannelGroup(int bus);

        public RESULT getChannelGroup(out FMOD.ChannelGroup group)
        {
            group = null;
            int groupRaw = 0;
            RESULT result = FMOD_Studio_Bus_GetChannelGroup(rawPtr, &groupRaw);
            if ((result == .OK) && (groupRaw != 0))
                group = new FMOD.ChannelGroup(groupRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_GetChannelGroup(int bus, int* group);

        public RESULT getCPUUsage(out uint32 exclusive, out uint32 inclusive)
        {
            exclusive = default;
            inclusive = default;
            return FMOD_Studio_Bus_GetCPUUsage(rawPtr, &exclusive, &inclusive);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_GetCPUUsage(int bus, uint32* exclusive, uint32* inclusive);

        public RESULT getMemoryUsage(out MEMORY_USAGE memoryusage)
        {
            memoryusage = default;
            return FMOD_Studio_Bus_GetMemoryUsage(rawPtr, &memoryusage);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bus_GetMemoryUsage(int bus, MEMORY_USAGE* memoryusage);

    }

    public class VCA : HandleBase
    {

        public this(int raw) : base(raw) {}

        public new bool isValid()
        {
            return (rawPtr != 0) && (FMOD_Studio_VCA_IsValid(rawPtr) != 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern int32 FMOD_Studio_VCA_IsValid(int vca);

        public RESULT getID(out Guid id)
        {
            id = default;
            return FMOD_Studio_VCA_GetID(rawPtr, &id);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_VCA_GetID(int vca, Guid* id);

        public RESULT getPath(char8* path, int32 size, out int32 retrieved)
        {
            retrieved = default;
            return FMOD_Studio_VCA_GetPath(rawPtr, path, size, &retrieved);
        }

        public RESULT getPath(String path)
        {
            path.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getPath(buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        path.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_VCA_GetPath(int vca, char8* path, int32 size, int32* retrieved);

        public RESULT getVolume(out float volume, out float finalvolume)
        {
            volume = default;
            finalvolume = default;
            return FMOD_Studio_VCA_GetVolume(rawPtr, &volume, &finalvolume);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_VCA_GetVolume(int vca, float* volume, float* finalvolume);

        public RESULT setVolume(float volume)
        {
            return FMOD_Studio_VCA_SetVolume(rawPtr, volume);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_VCA_SetVolume(int vca, float volume);

    }

    public class Bank : HandleBase
    {

        public RESULT getEventList(Span<int> array, out int32 count)
        {
            count = 0;
            if (array.Length > int32.MaxValue)
                return .ERR_INVALID_PARAM;
            return getEventList(array.Ptr, (int32)array.Length, out count);
        }

        public RESULT getBusList(Span<int> array, out int32 count)
        {
            count = 0;
            if (array.Length > int32.MaxValue)
                return .ERR_INVALID_PARAM;
            return getBusList(array.Ptr, (int32)array.Length, out count);
        }

        public RESULT getVCAList(Span<int> array, out int32 count)
        {
            count = 0;
            if (array.Length > int32.MaxValue)
                return .ERR_INVALID_PARAM;
            return getVCAList(array.Ptr, (int32)array.Length, out count);
        }

        public this(int raw) : base(raw) {}

        public new bool isValid()
        {
            return (rawPtr != 0) && (FMOD_Studio_Bank_IsValid(rawPtr) != 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern int32 FMOD_Studio_Bank_IsValid(int bank);

        public RESULT getID(out Guid id)
        {
            id = default;
            return FMOD_Studio_Bank_GetID(rawPtr, &id);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetID(int bank, Guid* id);

        public RESULT getPath(char8* path, int32 size, out int32 retrieved)
        {
            retrieved = default;
            return FMOD_Studio_Bank_GetPath(rawPtr, path, size, &retrieved);
        }

        public RESULT getPath(String path)
        {
            path.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getPath(buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        path.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetPath(int bank, char8* path, int32 size, int32* retrieved);

        public RESULT unload()
        {
            RESULT result = FMOD_Studio_Bank_Unload(rawPtr);
            if (result == .OK)
                rawPtr = 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_Unload(int bank);

        public RESULT loadSampleData()
        {
            return FMOD_Studio_Bank_LoadSampleData(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_LoadSampleData(int bank);

        public RESULT unloadSampleData()
        {
            return FMOD_Studio_Bank_UnloadSampleData(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_UnloadSampleData(int bank);

        public RESULT getLoadingState(out LOADING_STATE state)
        {
            state = default;
            return FMOD_Studio_Bank_GetLoadingState(rawPtr, &state);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetLoadingState(int bank, LOADING_STATE* state);

        public RESULT getSampleLoadingState(out LOADING_STATE state)
        {
            state = default;
            return FMOD_Studio_Bank_GetSampleLoadingState(rawPtr, &state);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetSampleLoadingState(int bank, LOADING_STATE* state);

        public RESULT getStringCount(out int32 count)
        {
            count = default;
            return FMOD_Studio_Bank_GetStringCount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetStringCount(int bank, int32* count);

        public RESULT getStringInfo(int32 index, out Guid id, char8* path, int32 size, out int32 retrieved)
        {
            id = default;
            retrieved = default;
            return FMOD_Studio_Bank_GetStringInfo(rawPtr, index, &id, path, size, &retrieved);
        }

        public RESULT getStringInfo(int32 index, out Guid id, String path)
        {
            path.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                int32 retrieved;
                let result = getStringInfo(index, out id, buffer.Ptr, capacity, out retrieved);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        path.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity = Math.Max(capacity * 2, retrieved);
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetStringInfo(int bank, int32 index, Guid* id, char8* path, int32 size, int32* retrieved);

        public RESULT getEventCount(out int32 count)
        {
            count = default;
            return FMOD_Studio_Bank_GetEventCount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetEventCount(int bank, int32* count);

        public RESULT getEventList(int* array, int32 capacity, out int32 count)
        {
            count = default;
            return FMOD_Studio_Bank_GetEventList(rawPtr, array, capacity, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetEventList(int bank, int* array, int32 capacity, int32* count);

        public RESULT getBusCount(out int32 count)
        {
            count = default;
            return FMOD_Studio_Bank_GetBusCount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetBusCount(int bank, int32* count);

        public RESULT getBusList(int* array, int32 capacity, out int32 count)
        {
            count = default;
            return FMOD_Studio_Bank_GetBusList(rawPtr, array, capacity, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetBusList(int bank, int* array, int32 capacity, int32* count);

        public RESULT getVCACount(out int32 count)
        {
            count = default;
            return FMOD_Studio_Bank_GetVCACount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetVCACount(int bank, int32* count);

        public RESULT getVCAList(int* array, int32 capacity, out int32 count)
        {
            count = default;
            return FMOD_Studio_Bank_GetVCAList(rawPtr, array, capacity, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetVCAList(int bank, int* array, int32 capacity, int32* count);

        public RESULT getUserData(out int userdata)
        {
            userdata = 0;
            return FMOD_Studio_Bank_GetUserData(rawPtr, &userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_GetUserData(int bank, int* userdata);

        public RESULT setUserData(int userdata)
        {
            return FMOD_Studio_Bank_SetUserData(rawPtr, userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_Bank_SetUserData(int bank, int userdata);

    }

    public class CommandReplay : HandleBase
    {

        public this(int raw) : base(raw) {}

        public new bool isValid()
        {
            return (rawPtr != 0) && (FMOD_Studio_CommandReplay_IsValid(rawPtr) != 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern int32 FMOD_Studio_CommandReplay_IsValid(int replay);

        public RESULT getSystem(out FMOD.Studio.System system)
        {
            system = null;
            int systemRaw = 0;
            RESULT result = FMOD_Studio_CommandReplay_GetSystem(rawPtr, &systemRaw);
            if ((result == .OK) && (systemRaw != 0))
                system = new FMOD.Studio.System(systemRaw);
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetSystem(int replay, int* system);

        public RESULT getLength(out float length)
        {
            length = default;
            return FMOD_Studio_CommandReplay_GetLength(rawPtr, &length);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetLength(int replay, float* length);

        public RESULT getCommandCount(out int32 count)
        {
            count = default;
            return FMOD_Studio_CommandReplay_GetCommandCount(rawPtr, &count);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetCommandCount(int replay, int32* count);

        public RESULT getCommandInfo(int32 commandindex, out COMMAND_INFO info)
        {
            info = default;
            return FMOD_Studio_CommandReplay_GetCommandInfo(rawPtr, commandindex, &info);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetCommandInfo(int replay, int32 commandindex, COMMAND_INFO* info);

        public RESULT getCommandString(int32 commandindex, char8* buffer, int32 length)
        {
            return FMOD_Studio_CommandReplay_GetCommandString(rawPtr, commandindex, buffer, length);
        }

        public RESULT getCommandString(int32 commandindex, String text)
        {
            text.Clear();
            int32 capacity = 256;
            while (true)
            {
                let buffer = new char8[capacity];
                defer delete buffer;
                let result = getCommandString(commandindex, buffer.Ptr, capacity);
                if (result != .ERR_TRUNCATED)
                {
                    if (result == .OK)
                        text.Append(buffer.Ptr);
                    return result;
                }
                if (capacity > int32.MaxValue / 2)
                    return .ERR_TRUNCATED;
                capacity *= 2;
            }
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetCommandString(int replay, int32 commandindex, char8* buffer, int32 length);

        public RESULT getCommandAtTime(float time, out int32 commandindex)
        {
            commandindex = default;
            return FMOD_Studio_CommandReplay_GetCommandAtTime(rawPtr, time, &commandindex);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetCommandAtTime(int replay, float time, int32* commandindex);

        public RESULT setBankPath(StringView bankPath)
        {
            let bankPathUTF8 = bankPath.ToScopeCStr!();
            return FMOD_Studio_CommandReplay_SetBankPath(rawPtr, bankPathUTF8);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_SetBankPath(int replay, char8* bankPath);

        public RESULT start()
        {
            return FMOD_Studio_CommandReplay_Start(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_Start(int replay);

        public RESULT stop()
        {
            return FMOD_Studio_CommandReplay_Stop(rawPtr);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_Stop(int replay);

        public RESULT seekToTime(float time)
        {
            return FMOD_Studio_CommandReplay_SeekToTime(rawPtr, time);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_SeekToTime(int replay, float time);

        public RESULT seekToCommand(int32 commandindex)
        {
            return FMOD_Studio_CommandReplay_SeekToCommand(rawPtr, commandindex);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_SeekToCommand(int replay, int32 commandindex);

        public RESULT getPaused(out bool paused)
        {
            int32 pausedRaw = 0;
            RESULT result = FMOD_Studio_CommandReplay_GetPaused(rawPtr, &pausedRaw);
            paused = pausedRaw != 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetPaused(int replay, int32* paused);

        public RESULT setPaused(bool paused)
        {
            return FMOD_Studio_CommandReplay_SetPaused(rawPtr, paused ? 1 : 0);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_SetPaused(int replay, int32 paused);

        public RESULT getPlaybackState(out PLAYBACK_STATE state)
        {
            state = default;
            return FMOD_Studio_CommandReplay_GetPlaybackState(rawPtr, &state);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetPlaybackState(int replay, PLAYBACK_STATE* state);

        public RESULT getCurrentCommand(out int32 commandindex, out float currenttime)
        {
            commandindex = default;
            currenttime = default;
            return FMOD_Studio_CommandReplay_GetCurrentCommand(rawPtr, &commandindex, &currenttime);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetCurrentCommand(int replay, int32* commandindex, float* currenttime);

        public RESULT release()
        {
            RESULT result = FMOD_Studio_CommandReplay_Release(rawPtr);
            if (result == .OK)
                rawPtr = 0;
            return result;
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_Release(int replay);

        public RESULT setFrameCallback(COMMANDREPLAY_FRAME_CALLBACK callback)
        {
            return FMOD_Studio_CommandReplay_SetFrameCallback(rawPtr, callback);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_SetFrameCallback(int replay, COMMANDREPLAY_FRAME_CALLBACK callback);

        public RESULT setLoadBankCallback(COMMANDREPLAY_LOAD_BANK_CALLBACK callback)
        {
            return FMOD_Studio_CommandReplay_SetLoadBankCallback(rawPtr, callback);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_SetLoadBankCallback(int replay, COMMANDREPLAY_LOAD_BANK_CALLBACK callback);

        public RESULT setCreateInstanceCallback(COMMANDREPLAY_CREATE_INSTANCE_CALLBACK callback)
        {
            return FMOD_Studio_CommandReplay_SetCreateInstanceCallback(rawPtr, callback);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_SetCreateInstanceCallback(int replay, COMMANDREPLAY_CREATE_INSTANCE_CALLBACK callback);

        public RESULT getUserData(out int userdata)
        {
            userdata = 0;
            return FMOD_Studio_CommandReplay_GetUserData(rawPtr, &userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_GetUserData(int replay, int* userdata);

        public RESULT setUserData(int userdata)
        {
            return FMOD_Studio_CommandReplay_SetUserData(rawPtr, userdata);
        }

        [Import(STUDIO_VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Studio_CommandReplay_SetUserData(int replay, int userdata);

    }

}

