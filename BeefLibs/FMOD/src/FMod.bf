/* ========================================================================================== */
/*                                                                                            */
/* FMOD Studio - C# Wrapper . Copyright (c), Firelight Technologies Pty, Ltd. 2004-2016.      */
/*                                                                                            */
/* ========================================================================================== */

using System;
using System.Text;

namespace FMOD
{
    /*
        FMOD version number.  Check this against FMOD::System::getVersion / System_GetVersion
        0xaaaabbcc -> aaaa = major version number.  bb = minor version number.  cc = development version number.
    */
    public class VERSION
    {
        public const int32 number = 0x00020220;
        public const String dll = "fmod.dll";
    }

    public class CONSTANTS
    {
        public const int32 MAX_CHANNEL_WIDTH = 32;
        public const int32 MAX_LISTENERS = 8;
    }

    /*
        FMOD types
    */

    /*
    [ENUM]
    [
        [DESCRIPTION]
        error codes.  Returned from every function.

        [REMARKS]

        [SEE_ALSO]
    ]
    */
    public enum RESULT : int32
    {
        OK,                        /* No errors. */
        ERR_BADCOMMAND,            /* Tried to call a function on a data type that does not allow this type of functionality (ie calling Sound::lock on a streaming sound). */
        ERR_CHANNEL_ALLOC,         /* Error trying to allocate a channel. */
        ERR_CHANNEL_STOLEN,        /* The specified channel has been reused to play another sound. */
        ERR_DMA,                   /* DMA Failure.  See debug output for more information. */
        ERR_DSP_CONNECTION,        /* DSP connection error.  Connection possibly caused a cyclic dependency or connected dsps with incompatible buffer counts. */
        ERR_DSP_DONTPROCESS,       /* DSP return code from a DSP process query callback.  Tells mixer not to call the process callback and therefore not consume CPU.  Use this to optimize the DSP graph. */
        ERR_DSP_FORMAT,            /* DSP Format error.  A DSP unit may have attempted to connect to this network with the wrong format, or a matrix may have been set with the wrong size if the target unit has a specified channel map. */
        ERR_DSP_INUSE,             /* DSP is already in the mixer's DSP network. It must be removed before being reinserted or released. */
        ERR_DSP_NOTFOUND,          /* DSP connection error.  Couldn't find the DSP unit specified. */
        ERR_DSP_RESERVED,          /* DSP operation error.  Cannot perform operation on this DSP as it is reserved by the system. */
        ERR_DSP_SILENCE,           /* DSP return code from a DSP process query callback.  Tells mixer silence would be produced from read, so go idle and not consume CPU.  Use this to optimize the DSP graph. */
        ERR_DSP_TYPE,              /* DSP operation cannot be performed on a DSP of this type. */
        ERR_FILE_BAD,              /* Error loading file. */
        ERR_FILE_COULDNOTSEEK,     /* Couldn't perform seek operation.  This is a limitation of the medium (ie netstreams) or the file format. */
        ERR_FILE_DISKEJECTED,      /* Media was ejected while reading. */
        ERR_FILE_EOF,              /* End of file unexpectedly reached while trying to read essential data (truncated?). */
        ERR_FILE_ENDOFDATA,        /* End of current chunk reached while trying to read data. */
        ERR_FILE_NOTFOUND,         /* File not found. */
        ERR_FORMAT,                /* Unsupported file or audio format. */
        ERR_HEADER_MISMATCH,       /* There is a version mismatch between the FMOD header and either the FMOD Studio library or the FMOD Low Level library. */
        ERR_HTTP,                  /* A HTTP error occurred. This is a catch-all for HTTP errors not listed elsewhere. */
        ERR_HTTP_ACCESS,           /* The specified resource requires authentication or is forbidden. */
        ERR_HTTP_PROXY_AUTH,       /* Proxy authentication is required to access the specified resource. */
        ERR_HTTP_SERVER_ERROR,     /* A HTTP server error occurred. */
        ERR_HTTP_TIMEOUT,          /* The HTTP request timed out. */
        ERR_INITIALIZATION,        /* FMOD was not initialized correctly to support this function. */
        ERR_INITIALIZED,           /* Cannot call this command after System::init. */
        ERR_INTERNAL,              /* An error occurred that wasn't supposed to.  Contact support. */
        ERR_INVALID_FLOAT,         /* Value passed in was a NaN, Inf or denormalized float. */
        ERR_INVALID_HANDLE,        /* An invalid object handle was used. */
        ERR_INVALID_PARAM,         /* An invalid parameter was passed to this function. */
        ERR_INVALID_POSITION,      /* An invalid seek position was passed to this function. */
        ERR_INVALID_SPEAKER,       /* An invalid speaker was passed to this function based on the current speaker mode. */
        ERR_INVALID_SYNCPOINT,     /* The syncpoint32 did not come from this sound handle. */
        ERR_INVALID_THREAD,        /* Tried to call a function on a thread that is not supported. */
        ERR_INVALID_VECTOR,        /* The vectors passed in are not unit length, or perpendicular. */
        ERR_MAXAUDIBLE,            /* Reached maximum audible playback count for this sound's soundgroup. */
        ERR_MEMORY,                /* Not enough memory or resources. */
        ERR_MEMORY_CANTPOINT,      /* Can't use FMOD_OPENMEMORY_POint32 on non PCM source data, or non mp3/xma/adpcm data if FMOD_CREATECOMPRESSEDSAMPLE was used. */
        ERR_NEEDS3D,               /* Tried to call a command on a 2d sound when the command was meant for 3d sound. */
        ERR_NEEDSHARDWARE,         /* Tried to use a feature that requires hardware support. */
        ERR_NET_CONNECT,           /* Couldn't connect to the specified host. */
        ERR_NET_SOCKET_ERROR,      /* A socket error occurred.  This is a catch-all for socket-related errors not listed elsewhere. */
        ERR_NET_URL,               /* The specified URL couldn't be resolved. */
        ERR_NET_WOULD_BLOCK,       /* Operation on a non-blocking socket could not complete immediately. */
        ERR_NOTREADY,              /* Operation could not be performed because specified sound/DSP connection is not ready. */
        ERR_OUTPUT_ALLOCATED,      /* Error initializing output device, but more specifically, the output device is already in use and cannot be reused. */
        ERR_OUTPUT_CREATEBUFFER,   /* Error creating hardware sound buffer. */
        ERR_OUTPUT_DRIVERCALL,     /* A call to a standard soundcard driver failed, which could possibly mean a bug in the driver or resources were missing or exhausted. */
        ERR_OUTPUT_FORMAT,         /* Soundcard does not support the specified format. */
        ERR_OUTPUT_INIT,           /* Error initializing output device. */
        ERR_OUTPUT_NODRIVERS,      /* The output device has no drivers installed.  If pre-init, FMOD_OUTPUT_NOSOUND is selected as the output mode.  If post-init, the function just fails. */
        ERR_PLUGIN,                /* An unspecified error has been returned from a plugin. */
        ERR_PLUGIN_MISSING,        /* A requested output, dsp unit type or codec was not available. */
        ERR_PLUGIN_RESOURCE,       /* A resource that the plugin requires cannot be found. (ie the DLS file for MIDI playback) */
        ERR_PLUGIN_VERSION,        /* A plugin was built with an unsupported SDK version. */
        ERR_RECORD,                /* An error occurred trying to initialize the recording device. */
        ERR_REVERB_CHANNELGROUP,   /* Reverb properties cannot be set on this channel because a parent channelgroup owns the reverb connection. */
        ERR_REVERB_INSTANCE,       /* Specified instance in FMOD_REVERB_PROPERTIES couldn't be set. Most likely because it is an invalid instance number or the reverb doesn't exist. */
        ERR_SUBSOUNDS,             /* The error occurred because the sound referenced contains subsounds when it shouldn't have, or it doesn't contain subsounds when it should have.  The operation may also not be able to be performed on a parent sound. */
        ERR_SUBSOUND_ALLOCATED,    /* This subsound is already being used by another sound, you cannot have more than one parent to a sound.  Null out the other parent's entry first. */
        ERR_SUBSOUND_CANTMOVE,     /* Shared subsounds cannot be replaced or moved from their parent stream, such as when the parent stream is an FSB file. */
        ERR_TAGNOTFOUND,           /* The specified tag could not be found or there are no tags. */
        ERR_TOOMANYCHANNELS,       /* The sound created exceeds the allowable input channel count.  This can be increased using the 'maxinputchannels' parameter in System::setSoftwareFormat. */
        ERR_TRUNCATED,             /* The retrieved string is too int64 to fit in the supplied buffer and has been truncated. */
        ERR_UNIMPLEMENTED,         /* Something in FMOD hasn't been implemented when it should be! contact support! */
        ERR_UNINITIALIZED,         /* This command failed because System::init or System::setDriver was not called. */
        ERR_UNSUPPORTED,           /* A command issued was not supported by this object.  Possibly a plugin without certain callbacks specified. */
        ERR_VERSION,               /* The version number of this file format is not supported. */
        ERR_EVENT_ALREADY_LOADED,  /* The specified bank has already been loaded. */
        ERR_EVENT_LIVEUPDATE_BUSY, /* The live update connection failed due to the game already being connected. */
        ERR_EVENT_LIVEUPDATE_MISMATCH, /* The live update connection failed due to the game data being out of sync with the tool. */
        ERR_EVENT_LIVEUPDATE_TIMEOUT, /* The live update connection timed out. */
        ERR_EVENT_NOTFOUND,        /* The requested event, bus or vca could not be found. */
        ERR_STUDIO_UNINITIALIZED,  /* The Studio::System object is not yet initialized. */
        ERR_STUDIO_NOT_LOADED,     /* The specified resource is not loaded, so it can't be unloaded. */
        ERR_INVALID_STRING,        /* An invalid string was passed to this function. */
        ERR_ALREADY_LOCKED,        /* The specified resource is already locked. */
        ERR_NOT_LOCKED,            /* The specified resource is not locked, so it can't be unlocked. */
        ERR_RECORD_DISCONNECTED,   /* The specified recording driver has been disconnected. */
        ERR_TOOMANYSAMPLES,        /* The length provided exceed the allowable limit. */
    }


    /*
    [ENUM]
    [
        [DESCRIPTION]
        Used to distinguish if a FMOD_CHANNELCONTROL parameter is actually a channel or a channelgroup.

        [REMARKS]
        Cast the FMOD_CHANNELCONTROL to an FMOD_CHANNEL/FMOD::Channel, or FMOD_CHANNELGROUP/FMOD::ChannelGroup if specific functionality is needed for either class.
        Otherwise use as FMOD_CHANNELCONTROL/FMOD::ChannelControl and use that API.

        [SEE_ALSO]
        Channel::setCallback
        ChannelGroup::setCallback
    ]
    */
    public enum CHANNELCONTROL_TYPE : int32
    {
        CHANNEL,
        CHANNELGROUP
    }

    /*
    [STRUCTURE]
    [
        [DESCRIPTION]
        Structure describing a point32 in 3D space.

        [REMARKS]
        FMOD uses a left handed co-ordinate system by default.
        To use a right handed co-ordinate system specify FMOD_INIT_3D_RIGHTHANDED from FMOD_INITFLAGS in System::init.

        [SEE_ALSO]
        System::set3DListenerAttributes
        System::get3DListenerAttributes
        Channel::set3DAttributes
        Channel::get3DAttributes
        Geometry::addPolygon
        Geometry::setPolygonVertex
        Geometry::getPolygonVertex
        Geometry::setRotation
        Geometry::getRotation
        Geometry::setPosition
        Geometry::getPosition
        Geometry::setScale
        Geometry::getScale
        FMOD_INITFLAGS
    ]
    */
    [CRepr]
    public struct VECTOR
    {
        public float x;        /* X co-ordinate in 3D space. */
        public float y;        /* Y co-ordinate in 3D space. */
        public float z;        /* Z co-ordinate in 3D space. */
    }

    /*
    [STRUCTURE]
    [
        [DESCRIPTION]
        Structure describing a position, velocity and orientation.

        [REMARKS]

        [SEE_ALSO]
        FMOD_VECTOR
        FMOD_DSP_PARAMETER_3DATTRIBUTES
    ]
    */
    [CRepr]
    public struct _3D_ATTRIBUTES
    {
        VECTOR position;
        VECTOR velocity;
        VECTOR forward;
        VECTOR up;
    }

    /*
    [STRUCTURE]
    [
        [DESCRIPTION]
        Structure that is passed into FMOD_FILE_ASYNCREAD_CALLBACK.  Use the information in this structure to perform

        [REMARKS]
        Members marked with [r] mean the variable is modified by FMOD and is for reading purposes only.  Do not change this value.<br>
        Members marked with [w] mean the variable can be written to.  The user can set the value.<br>
        <br>
        Instructions: write to 'buffer', and 'uint8sread' <b>BEFORE</b> setting 'result'.<br>
        As soon as result is set, FMOD will asynchronously continue internally using the data provided in this structure.<br>
        <br>
        Set 'result' to the result expected from a normal file read callback.<br>
        If the read was successful, set it to FMOD_OK.<br>
        If it read some data but hit the end of the file, set it to FMOD_ERR_FILE_EOF.<br>
        If a bad error occurred, return FMOD_ERR_FILE_BAD<br>
        If a disk was ejected, return FMOD_ERR_FILE_DISKEJECTED.<br>

        [SEE_ALSO]
        FMOD_FILE_ASYNCREAD_CALLBACK
        FMOD_FILE_ASYNCCANCEL_CALLBACK
    ]
    */
    [CRepr]
    public struct ASYNCREADINFO
    {
        public int   handle;                     /* [r] The file handle that was filled out in the open callback. */
        public uint32     offset;                     /* [r] Seek position, make sure you read from this file offset. */
        public uint32     sizeuint8s;                  /* [r] how many uint8s requested for read. */
        public int32      priority;                   /* [r] 0 = low importance.  100 = extremely important (ie 'must read now or stuttering may occur') */

        public int   userdata;                   /* [r] User data pointer. */
        public int   buffer;                     /* [w] Buffer to read file data into. */
        public uint32     uint8sread;                  /* [w] Fill this in before setting result code to tell FMOD how many uint8s were read. */
        public ASYNCREADINFO_DONE_CALLBACK   done;  /* [r] FMOD file system wake up function.  Call this when user file read is finished.  Pass result of file read as a parameter. */

    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        These output types are used with System::setOutput / System::getOutput, to choose which output method to use.

        [REMARKS]
        To pass information to the driver when initializing fmod use the *extradriverdata* parameter in System::init for the following reasons.

        - FMOD_OUTPUTTYPE_WAVWRITER     - extradriverdata is a pointer to a char * file name that the wav writer will output to.
        - FMOD_OUTPUTTYPE_WAVWRITER_NRT - extradriverdata is a pointer to a char * file name that the wav writer will output to.
        - FMOD_OUTPUTTYPE_DSOUND        - extradriverdata is cast to a HWND type, so that FMOD can set the focus on the audio for a particular window.
        - FMOD_OUTPUTTYPE_PS3           - extradriverdata is a pointer to a FMOD_PS3_EXTRADRIVERDATA struct. This can be found in fmodps3.h.
        - FMOD_OUTPUTTYPE_XBOX360       - extradriverdata is a pointer to a FMOD_360_EXTRADRIVERDATA struct. This can be found in fmodxbox360.h.

        Currently these are the only FMOD drivers that take extra information.  Other unknown plugins may have different requirements.
    
        Note! If FMOD_OUTPUTTYPE_WAVWRITER_NRT or FMOD_OUTPUTTYPE_NOSOUND_NRT are used, and if the System::update function is being called
        very quickly (ie for a non realtime decode) it may be being called too quickly for the FMOD streamer thread to respond to.
        The result will be a skipping/stuttering output in the captured audio.
    
        To remedy this, disable the FMOD streamer thread, and use FMOD_INIT_STREAM_FROM_UPDATE to avoid skipping in the output stream,
        as it will lock the mixer and the streamer together in the same thread.
    
        [SEE_ALSO]
            System::setOutput
            System::getOutput
            System::setSoftwareFormat
            System::getSoftwareFormat
            System::init
            System::update
            FMOD_INITFLAGS
    ]
    */
    public enum OUTPUTTYPE : int32
    {
        AUTODETECT,      /* Picks the best output mode for the platform. This is the default. */

        UNKNOWN,         /* All - 3rd party plugin, unknown. This is for use with System::getOutput only. */
        NOSOUND,         /* All - Perform all mixing but discard the final output. */
        WAVWRITER,       /* All - Writes output to a .wav file. */
        NOSOUND_NRT,     /* All - Non-realtime version of FMOD_OUTPUTTYPE_NOSOUND. User can drive mixer with System::update at whatever rate they want. */
        WAVWRITER_NRT,   /* All - Non-realtime version of FMOD_OUTPUTTYPE_WAVWRITER. User can drive mixer with System::update at whatever rate they want. */

        DSOUND,          /* Win                  - Direct Sound.                        (Default on Windows XP and below) */
        WINMM,           /* Win                  - Windows Multimedia. */
        WASAPI,          /* Win/WinStore/XboxOne - Windows Audio Session API.           (Default on Windows Vista and above, Xbox One and Windows Store Applications) */
        ASIO,            /* Win                  - Low latency ASIO 2.0. */
        PULSEAUDIO,      /* Linux                - Pulse Audio.                         (Default on Linux if available) */
        ALSA,            /* Linux                - Advanced Linux Sound Architecture.   (Default on Linux if PulseAudio isn't available) */
        COREAUDIO,       /* Mac/iOS              - Core Audio.                          (Default on Mac and iOS) */
        XAUDIO,          /* Xbox 360             - XAudio.                              (Default on Xbox 360) */
        PS3,             /* PS3                  - Audio Out.                           (Default on PS3) */
        AUDIOTRACK,      /* Android              - Java Audio Track.                    (Default on Android 2.2 and below) */
        OPENSL,          /* Android              - OpenSL ES.                           (Default on Android 2.3 and above) */
        WIIU,            /* Wii U                - AX.                                  (Default on Wii U) */
        AUDIOOUT,        /* PS4/PSVita           - Audio Out.                           (Default on PS4 and PS Vita) */
        AUDIO3D,         /* PS4                  - Audio3D. */
        ATMOS,           /* Win                  - Dolby Atmos (WASAPI). */

        MAX,             /* Maximum number of output types supported. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        Specify the destination of log output when using the logging version of FMOD.

        [REMARKS]
        TTY destination can vary depending on platform, common examples include the
        Visual Studio / Xcode output window, stderr and LogCat.

        [SEE_ALSO]
        FMOD_Debug_Initialize
    ]
    */
    public enum DEBUG_MODE : int32
    {
        TTY,        /* Default log location per platform, i.e. Visual Studio output window, stderr, LogCat, etc */
        FILE,       /* Write log to specified file path */
        CALLBACK,   /* Call specified callback with log information */
    }

    /*
    [DEFINE]
    [
        [NAME]
        FMOD_DEBUG_FLAGS

        [DESCRIPTION]
        Specify the requested information to be output when using the logging version of FMOD.

        [REMARKS]

        [SEE_ALSO]
        FMOD_Debug_Initialize
    ]
    */
    //[Flags]
    public enum DEBUG_FLAGS : uint32
    {
        NONE                    = 0x00000000,   /* Disable all messages */
        ERROR                   = 0x00000001,   /* Enable only error messages. */
        WARNING                 = 0x00000002,   /* Enable warning and error messages. */
        LOG                     = 0x00000004,   /* Enable informational, warning and error messages (default). */

        TYPE_MEMORY             = 0x00000100,   /* Verbose logging for memory operations, only use this if you are debugging a memory related issue. */
        TYPE_FILE               = 0x00000200,   /* Verbose logging for file access, only use this if you are debugging a file related issue. */
        TYPE_CODEC              = 0x00000400,   /* Verbose logging for codec initialization, only use this if you are debugging a codec related issue. */
        TYPE_TRACE              = 0x00000800,   /* Verbose logging for internal errors, use this for tracking the origin of error codes. */

        DISPLAY_TIMESTAMPS      = 0x00010000,   /* Display the time stamp of the log message in milliseconds. */
        DISPLAY_LINENUMBERS     = 0x00020000,   /* Display the source code file and line number for where the message originated. */
        DISPLAY_THREAD          = 0x00040000,   /* Display the thread ID of the calling function that generated the message. */
    }

    /*
    [DEFINE]
    [
        [NAME]
        FMOD_MEMORY_TYPE

        [DESCRIPTION]
        Bit fields for memory allocation type being passed into FMOD memory callbacks.

        [REMARKS]
        Remember this is a bitfield.  You may get more than 1 bit set (ie physical + persistent) so do not simply switch on the types!  You must check each bit individually or clear out the bits that you do not want within the callback.<br>
        Bits can be excluded if you want during Memory_Initialize so that you never get them.

        [SEE_ALSO]
        FMOD_MEMORY_ALLOC_CALLBACK
        FMOD_MEMORY_REALLOC_CALLBACK
        FMOD_MEMORY_FREE_CALLBACK
        Memory_Initialize
    ]
    */
    //[Flags]
    public enum MEMORY_TYPE : uint32
    {
        NORMAL             = 0x00000000,       /* Standard memory. */
        STREAM_FILE        = 0x00000001,       /* Stream file buffer, size controllable with System::setStreamBufferSize. */
        STREAM_DECODE      = 0x00000002,       /* Stream decode buffer, size controllable with FMOD_CREATESOUNDEXINFO::decodebuffersize. */
        SAMPLEDATA         = 0x00000004,       /* Sample data buffer.  Raw audio data, usually PCM/MPEG/ADPCM/XMA data. */
        DSP_BUFFER         = 0x00000008,       /* DSP memory block allocated when more than 1 output exists on a DSP node. */
        PLUGIN             = 0x00000010,       /* Memory allocated by a third party plugin. */
        XBOX360_PHYSICAL   = 0x00100000,       /* Requires XPhysicalAlloc / XPhysicalFree. */
        PERSISTENT         = 0x00200000,       /* Persistent memory. Memory will be freed when System::release is called. */
        SECONDARY          = 0x00400000,       /* Secondary memory. Allocation should be in secondary memory. For example RSX on the PS3. */
        ALL                = 0xFFFFFFFF
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        These are speaker types defined for use with the System::setSoftwareFormat command.

        [REMARKS]
        Note below the phrase 'sound channels' is used.  These are the subchannels inside a sound, they are not related and
        have nothing to do with the FMOD class "Channel".<br>
        For example a mono sound has 1 sound channel, a stereo sound has 2 sound channels, and an AC3 or 6 channel wav file have 6 "sound channels".<br>
        <br>
        FMOD_SPEAKERMODE_RAW<br>
        ---------------------<br>
        This mode is for output devices that are not specifically mono/stereo/quad/surround/5.1 or 7.1, but are multichannel.<br>
        Use System::setSoftwareFormat to specify the number of speakers you want to address, otherwise it will default to 2 (stereo).<br>
        Sound channels map to speakers sequentially, so a mono sound maps to output speaker 0, stereo sound maps to output speaker 0 & 1.<br>
        The user assumes knowledge of the speaker order.  FMOD_SPEAKER enumerations may not apply, so raw channel indices should be used.<br>
        Multichannel sounds map input channels to output channels 1:1. <br>
        Channel::setPan and Channel::setPanLevels do not work.<br>
        Speaker levels must be manually set with Channel::setPanMatrix.<br>
        <br>
        FMOD_SPEAKERMODE_MONO<br>
        ---------------------<br>
        This mode is for a 1 speaker arrangement.<br>
        Panning does not work in this speaker mode.<br>
        Mono, stereo and multichannel sounds have each sound channel played on the one speaker unity.<br>
        Mix behavior for multichannel sounds can be set with Channel::setPanMatrix.<br>
        Channel::setPanLevels does not work.<br>
        <br>
        FMOD_SPEAKERMODE_STEREO<br>
        -----------------------<br>
        This mode is for 2 speaker arrangements that have a left and right speaker.<br>
        <li>Mono sounds default to an even distribution between left and right.  They can be panned with Channel::setPan.<br>
        <li>Stereo sounds default to the middle, or full left in the left speaker and full right in the right speaker.
        <li>They can be cross faded with Channel::setPan.<br>
        <li>Multichannel sounds have each sound channel played on each speaker at unity.<br>
        <li>Mix behavior for multichannel sounds can be set with Channel::setPanMatrix.<br>
        <li>Channel::setPanLevels works but only front left and right parameters are used, the rest are ignored.<br>
        <br>
        FMOD_SPEAKERMODE_QUAD<br>
        ------------------------<br>
        This mode is for 4 speaker arrangements that have a front left, front right, surround left and a surround right speaker.<br>
        <li>Mono sounds default to an even distribution between front left and front right.  They can be panned with Channel::setPan.<br>
        <li>Stereo sounds default to the left sound channel played on the front left, and the right sound channel played on the front right.<br>
        <li>They can be cross faded with Channel::setPan.<br>
        <li>Multichannel sounds default to all of their sound channels being played on each speaker in order of input.<br>
        <li>Mix behavior for multichannel sounds can be set with Channel::setPanMatrix.<br>
        <li>Channel::setPanLevels works but rear left, rear right, center and lfe are ignored.<br>
        <br>
        FMOD_SPEAKERMODE_SURROUND<br>
        ------------------------<br>
        This mode is for 5 speaker arrangements that have a left/right/center/surround left/surround right.<br>
        <li>Mono sounds default to the center speaker.  They can be panned with Channel::setPan.<br>
        <li>Stereo sounds default to the left sound channel played on the front left, and the right sound channel played on the front right.
        <li>They can be cross faded with Channel::setPan.<br>
        <li>Multichannel sounds default to all of their sound channels being played on each speaker in order of input.
        <li>Mix behavior for multichannel sounds can be set with Channel::setPanMatrix.<br>
        <li>Channel::setPanLevels works but rear left / rear right are ignored.<br>
        <br>
        FMOD_SPEAKERMODE_5POINT1<br>
        ---------------------------------------------------------<br>
        This mode is for 5.1 speaker arrangements that have a left/right/center/surround left/surround right and a subwoofer speaker.<br>
        <li>Mono sounds default to the center speaker.  They can be panned with Channel::setPan.<br>
        <li>Stereo sounds default to the left sound channel played on the front left, and the right sound channel played on the front right.
        <li>They can be cross faded with Channel::setPan.<br>
        <li>Multichannel sounds default to all of their sound channels being played on each speaker in order of input.
        <li>Mix behavior for multichannel sounds can be set with Channel::setPanMatrix.<br>
        <li>Channel::setPanLevels works but rear left / rear right are ignored.<br>
        <br>
        FMOD_SPEAKERMODE_7POINT1<br>
        ------------------------<br>
        This mode is for 7.1 speaker arrangements that have a left/right/center/surround left/surround right/rear left/rear right
        and a subwoofer speaker.<br>
        <li>Mono sounds default to the center speaker.  They can be panned with Channel::setPan.<br>
        <li>Stereo sounds default to the left sound channel played on the front left, and the right sound channel played on the front right.
        <li>They can be cross faded with Channel::setPan.<br>
        <li>Multichannel sounds default to all of their sound channels being played on each speaker in order of input.
        <li>Mix behavior for multichannel sounds can be set with Channel::setPanMatrix.<br>
        <li>Channel::setPanLevels works and every parameter is used to set the balance of a sound in any speaker.<br>
        <br>

        [SEE_ALSO]
        System::setSoftwareFormat
        System::getSoftwareFormat
        DSP::setChannelFormat
    ]
    */
    public enum SPEAKERMODE : int32
    {
        DEFAULT,          /* Default speaker mode based on operating system/output mode.  Windows = control panel setting, Xbox = 5.1, PS3 = 7.1 etc. */
        RAW,              /* There is no specific speakermode.  Sound channels are mapped in order of input to output.  Use System::setSoftwareFormat to specify speaker count. See remarks for more information. */
        MONO,             /* The speakers are monaural. */
        STEREO,           /* The speakers are stereo. */
        QUAD,             /* 4 speaker setup.  This includes front left, front right, surround left, surround right.  */
        SURROUND,         /* 5 speaker setup.  This includes front left, front right, center, surround left, surround right. */
        _5POINT1,         /* 5.1 speaker setup.  This includes front left, front right, center, surround left, surround right and an LFE speaker. */
        _7POINT1,         /* 7.1 speaker setup.  This includes front left, front right, center, surround left, surround right, back left, back right and an LFE speaker. */

        MAX,              /* Maximum number of speaker modes supported. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        Assigns an enumeration for a speaker index.

        [REMARKS]

        [SEE_ALSO]
        System::setSpeakerPosition
        System::getSpeakerPosition
    ]
    */
    public enum SPEAKER : int32
    {
        FRONT_LEFT,
        FRONT_RIGHT,
        FRONT_CENTER,
        LOW_FREQUENCY,
        SURROUND_LEFT,
        SURROUND_RIGHT,
        BACK_LEFT,
        BACK_RIGHT,

        MAX,               /* Maximum number of speaker types supported. */
    }

    /*
    [DEFINE]
    [
        [NAME]
        FMOD_CHANNELMASK

        [DESCRIPTION]
        These are bitfields to describe for a certain number of channels in a signal, which channels are being represented.<br>
        For example, a signal could be 1 channel, but contain the LFE channel only.<br>

        [REMARKS]
        FMOD_CHANNELMASK_BACK_CENTER is not represented as an output speaker in fmod - but it is encountered in input formats and is down or upmixed appropriately to the nearest speakers.<br>

        [SEE_ALSO]
        DSP::setChannelFormat
        DSP::getChannelFormat
        FMOD_SPEAKERMODE
    ]
    */
    //[Flags]
	[AllowDuplicates]
    public enum CHANNELMASK : int32
    {
        FRONT_LEFT             = 0x00000001,
        FRONT_RIGHT            = 0x00000002,
        FRONT_CENTER           = 0x00000004,
        LOW_FREQUENCY          = 0x00000008,
        SURROUND_LEFT          = 0x00000010,
        SURROUND_RIGHT         = 0x00000020,
        BACK_LEFT              = 0x00000040,
        BACK_RIGHT             = 0x00000080,
        BACK_CENTER            = 0x00000100,

        MONO                   = (FRONT_LEFT),
        STEREO                 = (FRONT_LEFT | FRONT_RIGHT),
        LRC                    = (FRONT_LEFT | FRONT_RIGHT | FRONT_CENTER),
        QUAD                   = (FRONT_LEFT | FRONT_RIGHT | SURROUND_LEFT | SURROUND_RIGHT),
        SURROUND               = (FRONT_LEFT | FRONT_RIGHT | FRONT_CENTER | SURROUND_LEFT | SURROUND_RIGHT),
        _5POINT1               = (FRONT_LEFT | FRONT_RIGHT | FRONT_CENTER | LOW_FREQUENCY | SURROUND_LEFT | SURROUND_RIGHT),
        _5POINT1_REARS         = (FRONT_LEFT | FRONT_RIGHT | FRONT_CENTER | LOW_FREQUENCY | BACK_LEFT | BACK_RIGHT),
        _7POINT0               = (FRONT_LEFT | FRONT_RIGHT | FRONT_CENTER | SURROUND_LEFT | SURROUND_RIGHT | BACK_LEFT | BACK_RIGHT),
        _7POINT1               = (FRONT_LEFT | FRONT_RIGHT | FRONT_CENTER | LOW_FREQUENCY | SURROUND_LEFT | SURROUND_RIGHT | BACK_LEFT | BACK_RIGHT)
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        When creating a multichannel sound, FMOD will pan them to their default speaker locations, for example a 6 channel sound will default to one channel per 5.1 output speaker.<br>
        Another example is a stereo sound.  It will default to left = front left, right = front right.<br>
        <br>
        This is for sounds that are not 'default'.  For example you might have a sound that is 6 channels but actually made up of 3 stereo pairs, that should all be located in front left, front right only.

        [REMARKS]

        [SEE_ALSO]
        FMOD_CREATESOUNDEXINFO
    ]
    */
    public enum CHANNELORDER : int32
    {
        DEFAULT,              /* Left, Right, Center, LFE, Surround Left, Surround Right, Back Left, Back Right (see FMOD_SPEAKER enumeration)   */
        WAVEFORMAT,           /* Left, Right, Center, LFE, Back Left, Back Right, Surround Left, Surround Right (as per Microsoft .wav WAVEFORMAT structure master order) */
        PROTOOLS,             /* Left, Center, Right, Surround Left, Surround Right, LFE */
        ALLMONO,              /* Mono, Mono, Mono, Mono, Mono, Mono, ... (each channel all the way up to 32 channels are treated as if they were mono) */
        ALLSTEREO,            /* Left, Right, Left, Right, Left, Right, ... (each pair of channels is treated as stereo all the way up to 32 channels) */
        ALSA,                 /* Left, Right, Surround Left, Surround Right, Center, LFE (as per Linux ALSA channel order) */

        MAX,                  /* Maximum number of channel orderings supported. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        These are plugin types defined for use with the System::getNumPlugins,
        System::getPluginInfo and System::unloadPlugin functions.

        [REMARKS]

        [SEE_ALSO]
        System::getNumPlugins
        System::getPluginInfo
        System::unloadPlugin
    ]
    */
    public enum PLUGINTYPE : int32
    {
        OUTPUT,          /* The plugin type is an output module.  FMOD mixed audio will play through one of these devices */
        CODEC,           /* The plugin type is a file format codec.  FMOD will use these codecs to load file formats for playback. */
        DSP,             /* The plugin type is a DSP unit.  FMOD will use these plugins as part of its DSP network to apply effects to output or generate sound in realtime. */

        MAX,             /* Maximum number of plugin types supported. */
    }

    /*
    [STRUCTURE]
    [
        [DESCRIPTION]
        Used to support lists of plugins within the one file.

        [REMARKS]
        The description field is either a pointer to FMOD_DSP_DESCRIPTION, FMOD_OUTPUT_DESCRIPTION, FMOD_CODEC_DESCRIPTION.

        This structure is returned from a plugin as a pointer to a list where the last entry has FMOD_PLUGINTYPE_MAX and
        a null description pointer.

        [SEE_ALSO]
        System::getNumNestedPlugins
        System::getNestedPlugin
    ]
    */
    [CRepr]
    public struct PLUGINLIST
    {
        PLUGINTYPE type;
        int description;
    }

    /*
    [DEFINE]
    [
        [NAME]
        FMOD_INITFLAGS

        [DESCRIPTION]
        Initialization flags.  Use them with System::init in the *flags* parameter to change various behavior.

        [REMARKS]
        Use System::setAdvancedSettings to adjust settings for some of the features that are enabled by these flags.

        [SEE_ALSO]
        System::init
        System::update
        System::setAdvancedSettings
        Channel::set3DOcclusion
    ]
    */
    //[Flags]
    public enum INITFLAGS : int32
    {
        NORMAL                    = 0x00000000, /* Initialize normally */
        STREAM_FROM_UPDATE        = 0x00000001, /* No stream thread is created internally.  Streams are driven from System::update.  Mainly used with non-realtime outputs. */
        MIX_FROM_UPDATE           = 0x00000002, /* Win/Wii/PS3/Xbox/Xbox 360 Only - FMOD Mixer thread is woken up to do a mix when System::update is called rather than waking periodically on its own timer. */
        _3D_RIGHTHANDED           = 0x00000004, /* FMOD will treat +X as right, +Y as up and +Z as backwards (towards you). */
        CHANNEL_LOWPASS           = 0x00000100, /* All FMOD_3D based voices will add a software lowpass filter effect into the DSP chain which is automatically used when Channel::set3DOcclusion is used or the geometry API.   This also causes sounds to sound duller when the sound goes behind the listener, as a fake HRTF style effect.  Use System::setAdvancedSettings to disable or adjust cutoff frequency for this feature. */
        CHANNEL_DISTANCEFILTER    = 0x00000200, /* All FMOD_3D based voices will add a software lowpass and highpass filter effect into the DSP chain which will act as a distance-automated bandpass filter. Use System::setAdvancedSettings to adjust the center frequency. */
        PROFILE_ENABLE            = 0x00010000, /* Enable TCP/IP based host which allows FMOD Designer or FMOD Profiler to connect to it, and view memory, CPU and the DSP network graph in real-time. */
        VOL0_BECOMES_VIRTUAL      = 0x00020000, /* Any sounds that are 0 volume will go virtual and not be processed except for having their positions updated virtually.  Use System::setAdvancedSettings to adjust what volume besides zero to switch to virtual at. */
        GEOMETRY_USECLOSEST       = 0x00040000, /* With the geometry engine, only process the closest polygon rather than accumulating all polygons the sound to listener line intersects. */
        PREFER_DOLBY_DOWNMIX      = 0x00080000, /* When using FMOD_SPEAKERMODE_5POINT1 with a stereo output device, use the Dolby Pro Logic II downmix algorithm instead of the SRS Circle Surround algorithm. */
        THREAD_UNSAFE             = 0x00100000, /* Disables thread safety for API calls. Only use this if FMOD low level is being called from a single thread, and if Studio API is not being used! */
        PROFILE_METER_ALL         = 0x00200000  /* Slower, but adds level metering for every single DSP unit in the graph.  Use DSP::setMeteringEnabled to turn meters off individually. */
    }


    /*
    [ENUM]
    [
        [DESCRIPTION]
        These definitions describe the type of song being played.

        [REMARKS]

        [SEE_ALSO]
        Sound::getFormat
    ]
    */
    public enum SOUND_TYPE : int32
    {
        UNKNOWN,         /* 3rd party / unknown plugin format. */
        AIFF,            /* AIFF. */
        ASF,             /* Microsoft Advanced Systems Format (ie WMA/ASF/WMV). */
        DLS,             /* Sound font / downloadable sound bank. */
        FLAC,            /* FLAC lossless codec. */
        FSB,             /* FMOD Sample Bank. */
        IT,              /* Impulse Tracker. */
        MIDI,            /* MIDI. extracodecdata is a pointer to an FMOD_MIDI_EXTRACODECDATA structure. */
        MOD,             /* Protracker / Fasttracker MOD. */
        MPEG,            /* MP2/MP3 MPEG. */
        OGGVORBIS,       /* Ogg vorbis. */
        PLAYLIST,        /* Information only from ASX/PLS/M3U/WAX playlists */
        RAW,             /* Raw PCM data. */
        S3M,             /* ScreamTracker 3. */
        USER,            /* User created sound. */
        WAV,             /* Microsoft WAV. */
        XM,              /* FastTracker 2 XM. */
        XMA,             /* Xbox360 XMA */
        AUDIOQUEUE,      /* iPhone hardware decoder, supports AAC, ALAC and MP3. extracodecdata is a pointer to an FMOD_AUDIOQUEUE_EXTRACODECDATA structure. */
        AT9,             /* PS4 / PSVita ATRAC 9 format */
        VORBIS,          /* Vorbis */
        MEDIA_FOUNDATION,/* Windows Store Application built in system codecs */
        MEDIACODEC,      /* Android MediaCodec */
        FADPCM,          /* FMOD Adaptive Differential Pulse Code Modulation */

        MAX,             /* Maximum number of sound types supported. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        These definitions describe the native format of the hardware or software buffer that will be used.

        [REMARKS]
        This is the format the native hardware or software buffer will be or is created in.

        [SEE_ALSO]
        System::createSoundEx
        Sound::getFormat
    ]
    */
    public enum SOUND_FORMAT : int32
    {
        NONE,       /* Unitialized / unknown */
        PCM8,       /* 8bit integer PCM data */
        PCM16,      /* 16bit integer PCM data  */
        PCM24,      /* 24bit integer PCM data  */
        PCM32,      /* 32bit integer PCM data  */
        PCMFLOAT,   /* 32bit floating point32 PCM data  */
        BITSTREAM,  /* Sound data is in its native compressed format. */

        MAX         /* Maximum number of sound formats supported. */
    }


    /*
    [DEFINE]
    [
        [NAME]
        FMOD_MODE

        [DESCRIPTION]
        Sound description bitfields, bitwise OR them together for loading and describing sounds.

        [REMARKS]
        By default a sound will open as a static sound that is decompressed fully into memory to PCM. (ie equivalent of FMOD_CREATESAMPLE)<br>
        To have a sound stream instead, use FMOD_CREATESTREAM, or use the wrapper function System::createStream.<br>
        Some opening modes (ie FMOD_OPENUSER, FMOD_OPENMEMORY, FMOD_OPENMEMORY_POINT, FMOD_OPENRAW) will need extra information.<br>
        This can be provided using the FMOD_CREATESOUNDEXINFO structure.
        <br>
        Specifying FMOD_OPENMEMORY_POint32 will POint32 to your memory rather allocating its own sound buffers and duplicating it internally.<br>
        <b><u>This means you cannot free the memory while FMOD is using it, until after Sound::release is called.</b></u>
        With FMOD_OPENMEMORY_POINT, for PCM formats, only WAV, FSB, and RAW are supported.  For compressed formats, only those formats supported by FMOD_CREATECOMPRESSEDSAMPLE are supported.<br>
        With FMOD_OPENMEMORY_POint32 and FMOD_OPENRAW or PCM, if using them together, note that you must pad the data on each side by 16 uint8s.  This is so fmod can modify the ends of the data for looping/interpolation/mixing purposes.  If a wav file, you will need to insert silence, and then reset loop points to stop the playback from playing that silence.<br>
        <br>
        <b>Xbox 360 memory</b> On Xbox 360 Specifying FMOD_OPENMEMORY_POint32 to a virtual memory address will cause FMOD_ERR_INVALID_ADDRESS
        to be returned.  Use physical memory only for this functionality.<br>
        <br>
        FMOD_LOWMEM is used on a sound if you want to minimize the memory overhead, by having FMOD not allocate memory for certain
        features that are not likely to be used in a game environment.  These are :<br>
        1. Sound::getName functionality is removed.  256 uint8s per sound is saved.<br>

        [SEE_ALSO]
        System::createSound
        System::createStream
        Sound::setMode
        Sound::getMode
        Channel::setMode
        Channel::getMode
        Sound::set3DCustomRolloff
        Channel::set3DCustomRolloff
        Sound::getOpenState
    ]
    */
    //[Flags]
    public enum MODE : uint32
    {
        DEFAULT                = 0x00000000,  /* Default for all modes listed below. FMOD_LOOP_OFF, FMOD_2D, FMOD_3D_WORLDRELATIVE, FMOD_3D_INVERSEROLLOFF */
        LOOP_OFF               = 0x00000001,  /* For non looping sounds. (default).  Overrides FMOD_LOOP_NORMAL / FMOD_LOOP_BIDI. */
        LOOP_NORMAL            = 0x00000002,  /* For forward looping sounds. */
        LOOP_BIDI              = 0x00000004,  /* For bidirectional looping sounds. (only works on software mixed static sounds). */
        _2D                    = 0x00000008,  /* Ignores any 3d processing. (default). */
        _3D                    = 0x00000010,  /* Makes the sound positionable in 3D.  Overrides FMOD_2D. */
        CREATESTREAM           = 0x00000080,  /* Decompress at runtime, streaming from the source provided (standard stream).  Overrides FMOD_CREATESAMPLE. */
        CREATESAMPLE           = 0x00000100,  /* Decompress at loadtime, decompressing or decoding whole file into memory as the target sample format. (standard sample). */
        CREATECOMPRESSEDSAMPLE = 0x00000200,  /* Load MP2, MP3, IMAADPCM or XMA into memory and leave it compressed.  During playback the FMOD software mixer will decode it in realtime as a 'compressed sample'.  Can only be used in combination with FMOD_SOFTWARE. */
        OPENUSER               = 0x00000400,  /* Opens a user created static sample or stream. Use FMOD_CREATESOUNDEXINFO to specify format and/or read callbacks.  If a user created 'sample' is created with no read callback, the sample will be empty.  Use FMOD_Sound_Lock and FMOD_Sound_Unlock to place sound data into the sound if this is the case. */
        OPENMEMORY             = 0x00000800,  /* "name_or_data" will be interpreted as a pointer to memory instead of filename for creating sounds. */
        OPENMEMORY_POint32       = 0x10000000,  /* "name_or_data" will be interpreted as a pointer to memory instead of filename for creating sounds.  Use FMOD_CREATESOUNDEXINFO to specify length.  This differs to FMOD_OPENMEMORY in that it uses the memory as is, without duplicating the memory into its own buffers.  Cannot be freed after open, only after Sound::release.   Will not work if the data is compressed and FMOD_CREATECOMPRESSEDSAMPLE is not used. */
        OPENRAW                = 0x00001000,  /* Will ignore file format and treat as raw pcm.  User may need to declare if data is FMOD_SIGNED or FMOD_UNSIGNED */
        OPENONLY               = 0x00002000,  /* Just open the file, dont prebuffer or read.  Good for fast opens for info, or when sound::readData is to be used. */
        ACCURATETIME           = 0x00004000,  /* For FMOD_CreateSound - for accurate FMOD_Sound_GetLength / FMOD_Channel_SetPosition on VBR MP3, AAC and MOD/S3M/XM/IT/MIDI files.  Scans file first, so takes longer to open. FMOD_OPENONLY does not affect this. */
        MPEGSEARCH             = 0x00008000,  /* For corrupted / bad MP3 files.  This will search all the way through the file until it hits a valid MPEG header.  Normally only searches for 4k. */
        NONBLOCKING            = 0x00010000,  /* For opening sounds and getting streamed subsounds (seeking) asyncronously.  Use Sound::getOpenState to poll the state of the sound as it opens or retrieves the subsound in the background. */
        UNIQUE                 = 0x00020000,  /* Unique sound, can only be played one at a time */
        _3D_HEADRELATIVE       = 0x00040000,  /* Make the sound's position, velocity and orientation relative to the listener. */
        _3D_WORLDRELATIVE      = 0x00080000,  /* Make the sound's position, velocity and orientation absolute (relative to the world). (DEFAULT) */
        _3D_INVERSEROLLOFF     = 0x00100000,  /* This sound will follow the inverse rolloff model where mindistance = full volume, maxdistance = where sound stops attenuating, and rolloff is fixed according to the global rolloff factor.  (DEFAULT) */
        _3D_LINEARROLLOFF      = 0x00200000,  /* This sound will follow a linear rolloff model where mindistance = full volume, maxdistance = silence.  */
        _3D_LINEARSQUAREROLLOFF= 0x00400000,  /* This sound will follow a linear-square rolloff model where mindistance = full volume, maxdistance = silence.  Rolloffscale is ignored. */
        _3D_INVERSETAPEREDROLLOFF = 0x00800000,  /* This sound will follow the inverse rolloff model at distances close to mindistance and a linear-square rolloff close to maxdistance. */
        _3D_CUSTOMROLLOFF      = 0x04000000,  /* This sound will follow a rolloff model defined by Sound::set3DCustomRolloff / Channel::set3DCustomRolloff.  */
        _3D_IGNOREGEOMETRY     = 0x40000000,  /* Is not affect by geometry occlusion.  If not specified in Sound::setMode, or Channel::setMode, the flag is cleared and it is affected by geometry again. */
        IGNORETAGS             = 0x02000000,  /* Skips id3v2/asf/etc tag checks when opening a sound, to reduce seek/read overhead when opening files (helps with CD performance). */
        LOWMEM                 = 0x08000000,  /* Removes some features from samples to give a lower memory overhead, like Sound::getName. */
        LOADSECONDARYRAM       = 0x20000000,  /* Load sound into the secondary RAM of supported platform.  On PS3, sounds will be loaded into RSX/VRAM. */
        VIRTUAL_PLAYFROMSTART  = 0x80000000   /* For sounds that start virtual (due to being quiet or low importance), instead of swapping back to audible, and playing at the correct offset according to time, this flag makes the sound play from the start. */
    }


    /*
    [ENUM]
    [
        [DESCRIPTION]
        These values describe what state a sound is in after FMOD_NONBLOCKING has been used to open it.

        [REMARKS]
        With streams, if you are using FMOD_NONBLOCKING, note that if the user calls Sound::getSubSound, a stream will go into FMOD_OPENSTATE_SEEKING state and sound related commands will return FMOD_ERR_NOTREADY.<br>
        With streams, if you are using FMOD_NONBLOCKING, note that if the user calls Channel::getPosition, a stream will go into FMOD_OPENSTATE_SETPOSITION state and sound related commands will return FMOD_ERR_NOTREADY.<br>

        [SEE_ALSO]
        Sound::getOpenState
        FMOD_MODE
    ]
    */
    public enum OPENSTATE : int32
    {
        READY = 0,       /* Opened and ready to play */
        LOADING,         /* Initial load in progress */
        ERROR,           /* Failed to open - file not found, out of memory etc.  See return value of Sound::getOpenState for what happened. */
        CONNECTING,      /* Connecting to remote host (internet sounds only) */
        BUFFERING,       /* Buffering data */
        SEEKING,         /* Seeking to subsound and re-flushing stream buffer. */
        PLAYING,         /* Ready and playing, but not possible to release at this time without stalling the main thread. */
        SETPOSITION,     /* Seeking within a stream to a different position. */

        MAX,             /* Maximum number of open state types. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        These flags are used with SoundGroup::setMaxAudibleBehavior to determine what happens when more sounds
        are played than are specified with SoundGroup::setMaxAudible.

        [REMARKS]
        When using FMOD_SOUNDGROUP_BEHAVIOR_MUTE, SoundGroup::setMuteFadeSpeed can be used to stop a sudden transition.
        Instead, the time specified will be used to cross fade between the sounds that go silent and the ones that become audible.

        [SEE_ALSO]
        SoundGroup::setMaxAudibleBehavior
        SoundGroup::getMaxAudibleBehavior
        SoundGroup::setMaxAudible
        SoundGroup::getMaxAudible
        SoundGroup::setMuteFadeSpeed
        SoundGroup::getMuteFadeSpeed
    ]
    */
    public enum SOUNDGROUP_BEHAVIOR : int32
    {
        BEHAVIOR_FAIL,              /* Any sound played that puts the sound count over the SoundGroup::setMaxAudible setting, will simply fail during System::playSound. */
        BEHAVIOR_MUTE,              /* Any sound played that puts the sound count over the SoundGroup::setMaxAudible setting, will be silent, then if another sound in the group stops the sound that was silent before becomes audible again. */
        BEHAVIOR_STEALLOWEST,       /* Any sound played that puts the sound count over the SoundGroup::setMaxAudible setting, will steal the quietest / least important sound playing in the group. */

        MAX,               /* Maximum number of sound group behaviors. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        These callback types are used with Channel::setCallback.

        [REMARKS]
        Each callback has commanddata parameters passed as int32 unique to the type of callback.<br>
        See reference to FMOD_CHANNELCONTROL_CALLBACK to determine what they might mean for each type of callback.<br>
        <br>
        <b>Note!</b>  Currently the user must call System::update for these callbacks to trigger!

        [SEE_ALSO]
        Channel::setCallback
        ChannelGroup::setCallback
        FMOD_CHANNELCONTROL_CALLBACK
        System::update
    ]
    */
    public enum CHANNELCONTROL_CALLBACK_TYPE : int32
    {
        END,                  /* Called when a sound ends. */
        VIRTUALVOICE,         /* Called when a voice is swapped out or swapped in. */
        SYNCPOINT,            /* Called when a syncpoint32 is encountered.  Can be from wav file markers. */
        OCCLUSION,            /* Called when the channel has its geometry occlusion value calculated.  Can be used to clamp or change the value. */

        MAX,                  /* Maximum number of callback types supported. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        These enums denote special types of node within a DSP chain.

        [REMARKS]

        [SEE_ALSO]
        Channel::getDSP
        ChannelGroup::getDSP
    ]
    */
    public struct CHANNELCONTROL_DSP_INDEX
    {
        public const int32 HEAD    = -1;         /* Head of the DSP chain. */
        public const int32 FADER   = -2;         /* Built in fader DSP. */
        public const int32 PANNER  = -3;         /* Built in panner DSP. */
        public const int32 TAIL    = -4;         /* Tail of the DSP chain. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        Used to distinguish the instance type passed into FMOD_ERROR_CALLBACK.

        [REMARKS]
        Cast the instance of FMOD_ERROR_CALLBACK to the appropriate class indicated by this enum.

        [SEE_ALSO]
    ]
    */
    public enum ERRORCALLBACK_INSTANCETYPE : int32
    {
        NONE,
        SYSTEM,
        CHANNEL,
        CHANNELGROUP,
        CHANNELCONTROL,
        SOUND,
        SOUNDGROUP,
        DSP,
        DSPCONNECTION,
        GEOMETRY,
        REVERB3D,
        STUDIO_SYSTEM,
        STUDIO_EVENTDESCRIPTION,
        STUDIO_EVENTINSTANCE,
        STUDIO_PARAMETERINSTANCE,
        STUDIO_BUS,
        STUDIO_VCA,
        STUDIO_BANK,
        STUDIO_COMMANDREPLAY
    }

    /*
    [STRUCTURE]
    [
        [DESCRIPTION]
        Structure that is passed into FMOD_SYSTEM_CALLBACK for the FMOD_SYSTEM_CALLBACK_ERROR callback type.

        [REMARKS]
        The instance pointer will be a type corresponding to the instanceType enum.

        [SEE_ALSO]
        FMOD_ERRORCALLBACK_INSTANCETYPE
    ]
    */
    [CRepr]
    public struct ERRORCALLBACK_INFO
    {
        public  RESULT                      result;                     /* Error code result */
        public  ERRORCALLBACK_INSTANCETYPE  instancetype;               /* Type of instance the error occurred on */
        public  int                      instance;                   /* Instance pointer */
        private int                      functionname_internal;      /* Function that the error occurred on */
        private int                      functionparams_internal;    /* Function parameters that the error ocurred on */

        //public string functionname   { get { return Marshal.PtrToStringAnsi(functionname_internal); } }
        //public string functionparams { get { return Marshal.PtrToStringAnsi(functionparams_internal); } }
    }

    /*
    [DEFINE]
    [
        [NAME]
        FMOD_SYSTEM_CALLBACK_TYPE

        [DESCRIPTION]
        These callback types are used with System::setCallback.

        [REMARKS]
        Each callback has commanddata parameters passed as void* unique to the type of callback.<br>
        See reference to FMOD_SYSTEM_CALLBACK to determine what they might mean for each type of callback.<br>
        <br>
        <b>Note!</b> Using FMOD_SYSTEM_CALLBACK_DEVICELISTCHANGED (on Mac only) requires the application to be running an event loop which will allow external changes to device list to be detected by FMOD.<br>
        <br>
        <b>Note!</b> The 'system' object pointer will be null for FMOD_SYSTEM_CALLBACK_THREADCREATED and FMOD_SYSTEM_CALLBACK_MEMORYALLOCATIONFAILED callbacks.

        [SEE_ALSO]
        System::setCallback
        System::update
        DSP::addInput
    ]
    */
    //[Flags]
    public enum SYSTEM_CALLBACK_TYPE : uint
    {
        DEVICELISTCHANGED      = 0x00000001,  /* Called from System::update when the enumerated list of devices has changed. */
        DEVICELOST             = 0x00000002,  /* Called from System::update when an output device has been lost due to control panel parameter changes and FMOD cannot automatically recover. */
        MEMORYALLOCATIONFAILED = 0x00000004,  /* Called directly when a memory allocation fails somewhere in FMOD.  (NOTE - 'system' will be NULL in this callback type.)*/
        THREADCREATED          = 0x00000008,  /* Called directly when a thread is created. (NOTE - 'system' will be NULL in this callback type.) */
        BADDSPCONNECTION       = 0x00000010,  /* Called when a bad connection was made with DSP::addInput. Usually called from mixer thread because that is where the connections are made.  */
        PREMIX                 = 0x00000020,  /* Called each tick before a mix update happens. */
        POSTMIX                = 0x00000040,  /* Called each tick after a mix update happens. */
        ERROR                  = 0x00000080,  /* Called when each API function returns an error code, including delayed async functions. */
        MIDMIX                 = 0x00000100,  /* Called each tick in mix update after clocks have been updated before the main mix occurs. */
        THREADDESTROYED        = 0x00000200,  /* Called directly when a thread is destroyed. */
        PREUPDATE              = 0x00000400,  /* Called at start of System::update function. */
        POSTUPDATE             = 0x00000800,  /* Called at end of System::update function. */
        RECORDLISTCHANGED      = 0x00001000,  /* Called from System::update when the enumerated list of recording devices has changed. */
        ALL                    = 0xFFFFFFFF,  /* Pass this mask to System::setCallback to receive all callback types.  */
    }
	
    /*
        FMOD Callbacks
    */
    public delegate RESULT ASYNCREADINFO_DONE_CALLBACK(int info, RESULT result);

    public delegate RESULT DEBUG_CALLBACK           (DEBUG_FLAGS flags, String file, int32 line, String func, String message);

    public delegate RESULT SYSTEM_CALLBACK          (int systemraw, SYSTEM_CALLBACK_TYPE type, int commanddata1, int commanddata2, int userdata);

    public delegate RESULT CHANNEL_CALLBACK         (int channelraw, CHANNELCONTROL_TYPE controltype, CHANNELCONTROL_CALLBACK_TYPE type, int commanddata1, int commanddata2);

    public delegate RESULT SOUND_NONBLOCKCALLBACK   (int soundraw, RESULT result);
    public delegate RESULT SOUND_PCMREADCALLBACK    (int soundraw, int data, uint32 datalen);
    public delegate RESULT SOUND_PCMSETPOSCALLBACK  (int soundraw, int32 subsound, uint32 position, TIMEUNIT postype);

    public delegate RESULT FILE_OPENCALLBACK        (char8* name, ref uint32 filesize, ref int handle, int userdata);
    public delegate RESULT FILE_CLOSECALLBACK       (int handle, int userdata);
    public delegate RESULT FILE_READCALLBACK        (int handle, int buffer, uint32 sizeuint8s, ref uint32 uint8sread, int userdata);
    public delegate RESULT FILE_SEEKCALLBACK        (int handle, uint32 pos, int userdata);
    public delegate RESULT FILE_ASYNCREADCALLBACK   (int handle, int info, int userdata);
    public delegate RESULT FILE_ASYNCCANCELCALLBACK (int handle, int userdata);

    public delegate int MEMORY_ALLOC_CALLBACK    (uint32 size, MEMORY_TYPE type, char8* sourcestr);
    public delegate int MEMORY_REALLOC_CALLBACK  (int ptr, uint32 size, MEMORY_TYPE type, char8* sourcestr);
    public delegate void   MEMORY_FREE_CALLBACK     (int ptr, MEMORY_TYPE type, char8* sourcestr);

    public delegate float  CB_3D_ROLLOFFCALLBACK    (int channelraw, float distance);

    /*
    [ENUM]
    [
        [DESCRIPTION]
        List of interpolation types that the FMOD Ex software mixer supports.

        [REMARKS]
        The default resampler type is FMOD_DSP_RESAMPLER_LINEAR.<br>
        Use System::setSoftwareFormat to tell FMOD the resampling quality you require for FMOD_SOFTWARE based sounds.

        [SEE_ALSO]
        System::setSoftwareFormat
        System::getSoftwareFormat
    ]
    */
    public enum DSP_RESAMPLER : int32
    {
        DEFAULT,         /* Default interpolation method.  Currently equal to FMOD_DSP_RESAMPLER_LINEAR. */
        NOINTERP,        /* No interpolation.  High frequency aliasing hiss will be audible depending on the sample rate of the sound. */
        LINEAR,          /* Linear interpolation (default method).  Fast and good quality, causes very slight lowpass effect on low frequency sounds. */
        CUBIC,           /* Cubic interpolation.  Slower than linear interpolation but better quality. */
        SPLINE,          /* 5 point32 spline interpolation.  Slowest resampling method but best quality. */

        MAX,             /* Maximum number of resample methods supported. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        List of connection types between 2 DSP nodes.

        [REMARKS]
        FMOD_DSP_CONNECTION_TYPE_STANDARD<br>
        ----------------------------------<br>
        Default DSPConnection type.  Audio is mixed from the input to the output DSP's audible buffer, meaning it will be part of the audible signal.  A standard connection will execute its input DSP if it has not been executed before.<br>
        <br>
        FMOD_DSP_CONNECTION_TYPE_SIDECHAIN<br>
        ----------------------------------<br>
        Sidechain DSPConnection type.  Audio is mixed from the input to the output DSP's sidechain buffer, meaning it will NOT be part of the audible signal.  A sidechain connection will execute its input DSP if it has not been executed before.<br>
        The purpose of the separate sidechain buffer in a DSP, is so that the DSP effect can privately access for analysis purposes.  An example of use in this case, could be a compressor which analyzes the signal, to control its own effect parameters (ie a compression level or gain).<br>
        <br>
        For the effect developer, to accept sidechain data, the sidechain data will appear in the FMOD_DSP_STATE struct which is passed into the read callback of a DSP unit.<br>
        FMOD_DSP_STATE::sidechaindata and FMOD_DSP::sidechainchannels will hold the mixed result of any sidechain data flowing into it.<br>
        <br>
        FMOD_DSP_CONNECTION_TYPE_SEND<br>
        -----------------------------<br>
        Send DSPConnection type.  Audio is mixed from the input to the output DSP's audible buffer, meaning it will be part of the audible signal.  A send connection will NOT execute its input DSP if it has not been executed before.<br>
        A send connection will only read what exists at the input's buffer at the time of executing the output DSP unit (which can be considered the 'return')<br>
        <br>
        FMOD_DSP_CONNECTION_TYPE_SEND_SIDECHAIN<br>
        ---------------------------------------<br>
        Send sidechain DSPConnection type.  Audio is mixed from the input to the output DSP's sidechain buffer, meaning it will NOT be part of the audible signal.  A send sidechain connection will NOT execute its input DSP if it has not been executed before.<br>
        A send sidechain connection will only read what exists at the input's buffer at the time of executing the output DSP unit (which can be considered the 'sidechain return').
        <br>
        For the effect developer, to accept sidechain data, the sidechain data will appear in the FMOD_DSP_STATE struct which is passed into the read callback of a DSP unit.<br>
        FMOD_DSP_STATE::sidechaindata and FMOD_DSP::sidechainchannels will hold the mixed result of any sidechain data flowing into it.

        [SEE_ALSO]
        DSP::addInput
        DSPConnection::getType
    ]
    */
    public enum DSPCONNECTION_TYPE : int32
    {
        STANDARD,          /* Default connection type.         Audio is mixed from the input to the output DSP's audible buffer.  */
        SIDECHAIN,         /* Sidechain connection type.       Audio is mixed from the input to the output DSP's sidechain buffer.  */
        SEND,              /* Send connection type.            Audio is mixed from the input to the output DSP's audible buffer, but the input is NOT executed, only copied from.  A standard connection or sidechain needs to make an input execute to generate data. */
        SEND_SIDECHAIN,    /* Send sidechain connection type.  Audio is mixed from the input to the output DSP's sidechain buffer, but the input is NOT executed, only copied from.  A standard connection or sidechain needs to make an input execute to generate data. */

        MAX,               /* Maximum number of DSP connection types supported. */
    }

    /*
    [ENUM]
    [
        [DESCRIPTION]
        List of tag types that could be stored within a sound.  These include id3 tags, metadata from netstreams and vorbis/asf data.

        [REMARKS]

        [SEE_ALSO]
        Sound::getTag
    ]
    */
    public enum TAGTYPE : int32
    {
        UNKNOWN = 0,
        ID3V1,
        ID3V2,
        VORBISCOMMENT,
        SHOUTCAST,
        ICECAST,
        ASF,
        MIDI,
        PLAYLIST,
        FMOD,
        USER,

        MAX                /* Maximum number of tag types supported. */
    }


    /*
    [ENUM]
    [
        [DESCRIPTION]
        List of data types that can be returned by Sound::getTag

        [REMARKS]

        [SEE_ALSO]
        Sound::getTag
    ]
    */
    public enum TAGDATATYPE : int32
    {
        BINARY = 0,
        INT,
        FLOAT,
        STRING,
        STRING_UTF16,
        STRING_UTF16BE,
        STRING_UTF8,
        CDTOC,

        MAX                /* Maximum number of tag datatypes supported. */
    }

    /*
    [STRUCTURE]
    [
        [DESCRIPTION]
        Structure describing a piece of tag data.

        [REMARKS]
        Members marked with [w] mean the user sets the value before passing it to the function.
        Members marked with [r] mean FMOD sets the value to be used after the function exits.

        [SEE_ALSO]
        Sound::getTag
        TAGTYPE
        TAGDATATYPE
    ]
    */
    [CRepr]
    public struct TAG
    {
        public  TAGTYPE           type;         /* [r] The type of this tag. */
        public  TAGDATATYPE       datatype;     /* [r] The type of data that this tag contains */
        private int            name_internal;/* [r] The name of this tag i.e. "TITLE", "ARTIST" etc. */
        public  int            data;         /* [r] Pointer to the tag data - its format is determined by the datatype member */
        public  uint32              datalen;      /* [r] Length of the data contained in this tag */
        public  bool              updated;      /* [r] True if this tag has been updated since last being accessed with Sound::getTag */

        //public string name { get { return Marshal.PtrToStringAnsi(name_internal); } }
    }


    /*
    [DEFINE]
    [
        [NAME]
        FMOD_TIMEUNIT

        [DESCRIPTION]
        List of time types that can be returned by Sound::getLength and used with Channel::setPosition or Channel::getPosition.

        [REMARKS]
        Do not combine flags except FMOD_TIMEUNIT_BUFFERED.

        [SEE_ALSO]
        Sound::getLength
        Channel::setPosition
        Channel::getPosition
    ]
    */
    //[Flags]
    public enum TIMEUNIT : uint32
    {
        MS                = 0x00000001,  /* Milliseconds. */
        PCM               = 0x00000002,  /* PCM Samples, related to milliseconds * samplerate / 1000. */
        PCMuint8S          = 0x00000004,  /* uint8s, related to PCM samples * channels * datawidth (ie 16bit = 2 uint8s). */
        RAWuint8S          = 0x00000008,  /* Raw file uint8s of (compressed) sound data (does not include headers).  Only used by Sound::getLength and Channel::getPosition. */
        PCMFRACTION       = 0x00000010,  /* Fractions of 1 PCM sample.  Unsigned int32 range 0 to 0xFFFFFFFF.  Used for sub-sample granularity for DSP purposes. */
        MODORDER          = 0x00000100,  /* MOD/S3M/XM/IT.  Order in a sequenced module format.  Use Sound::getFormat to determine the format. */
        MODROW            = 0x00000200,  /* MOD/S3M/XM/IT.  Current row in a sequenced module format.  Sound::getLength will return the number if rows in the currently playing or seeked to pattern. */
        MODPATTERN        = 0x00000400,  /* MOD/S3M/XM/IT.  Current pattern in a sequenced module format.  Sound::getLength will return the number of patterns in the song and Channel::getPosition will return the currently playing pattern. */
        BUFFERED          = 0x10000000,  /* Time value as seen by buffered stream.  This is always ahead of audible time, and is only used for processing. */
    }

    /*
    [DEFINE]
    [
        [NAME]
        FMOD_PORT_INDEX

        [DESCRIPTION]

        [REMARKS]

        [SEE_ALSO]
        System::AttachChannelGroupToPort
    ]
    */
    public struct PORT_INDEX
    {
        public const uint64 NONE = 0xFFFFFFFFFFFFFFFF;
    }

    /*
    [STRUCTURE]
    [
        [DESCRIPTION]
        Use this structure with System::createSound when more control is needed over loading.
        The possible reasons to use this with System::createSound are:

        - Loading a file from memory.
        - Loading a file from within another larger (possibly wad/pak) file, by giving the loader an offset and length.
        - To create a user created / non file based sound.
        - To specify a starting subsound to seek to within a multi-sample sounds (ie FSB/DLS) when created as a stream.
        - To specify which subsounds to load for multi-sample sounds (ie FSB/DLS) so that memory is saved and only a subset is actually loaded/read from disk.
        - To specify 'piggyback' read and seek callbacks for capture of sound data as fmod reads and decodes it.  Useful for ripping decoded PCM data from sounds as they are loaded / played.
        - To specify a MIDI DLS sample set file to load when opening a MIDI file.

        See below on what members to fill for each of the above types of sound you want to create.

        [REMARKS]
        This structure is optional!  Specify 0 or NULL in System::createSound if you don't need it!

        <u>Loading a file from memory.</u>

        - Create the sound using the FMOD_OPENMEMORY flag.
        - Mandatory.  Specify 'length' for the size of the memory block in uint8s.
        - Other flags are optional.

        <u>Loading a file from within another larger (possibly wad/pak) file, by giving the loader an offset and length.</u>

        - Mandatory.  Specify 'fileoffset' and 'length'.
        - Other flags are optional.

        <u>To create a user created / non file based sound.</u>

        - Create the sound using the FMOD_OPENUSER flag.
        - Mandatory.  Specify 'defaultfrequency, 'numchannels' and 'format'.
        - Other flags are optional.

        <u>To specify a starting subsound to seek to and flush with, within a multi-sample stream (ie FSB/DLS).</u>

        - Mandatory.  Specify 'initialsubsound'.

        <u>To specify which subsounds to load for multi-sample sounds (ie FSB/DLS) so that memory is saved and only a subset is actually loaded/read from disk.</u>

        - Mandatory.  Specify 'inclusionlist' and 'inclusionlistnum'.

        <u>To specify 'piggyback' read and seek callbacks for capture of sound data as fmod reads and decodes it.  Useful for ripping decoded PCM data from sounds as they are loaded / played.</u>

        - Mandatory.  Specify 'pcmreadcallback' and 'pcmseekcallback'.

        <u>To specify a MIDI DLS sample set file to load when opening a MIDI file.</u>

        - Mandatory.  Specify 'dlsname'.

        Setting the 'decodebuffersize' is for cpu intensive codecs that may be causing stuttering, not file intensive codecs (ie those from CD or netstreams) which are normally
        altered with System::setStreamBufferSize.  As an example of cpu intensive codecs, an mp3 file will take more cpu to decode than a PCM wav file.

        If you have a stuttering effect, then it is using more cpu than the decode buffer playback rate can keep up with.  Increasing the decode buffersize will most likely solve this problem.

        FSB codec.  If inclusionlist and numsubsounds are used together, this will trigger a special mode where subsounds are shuffled down to save memory.  (useful for large FSB
        files where you only want to load 1 sound).  There will be no gaps, ie no null subsounds.  As an example, if there are 10,000 subsounds and there is an inclusionlist with only 1 entry,
        and numsubsounds = 1, then subsound 0 will be that entry, and there will only be the memory allocated for 1 subsound.  Previously there would still be 10,000 subsound pointers and other
        associated codec entries allocated aint64 with it multiplied by 10,000.

        Members marked with [r] mean the variable is modified by FMOD and is for reading purposes only.  Do not change this value.<br>
        Members marked with [w] mean the variable can be written to.  The user can set the value.

        [SEE_ALSO]
        System::createSound
        System::setStreamBufferSize
        FMOD_MODE
        FMOD_SOUND_FORMAT
        FMOD_SOUND_TYPE
        FMOD_CHANNELMASK
        FMOD_CHANNELORDER
    ]
    */
    [CRepr]
    public struct CREATESOUNDEXINFO
    {
        public int32                         cbsize;                 /* [w]   Size of this structure.  This is used so the structure can be expanded in the future and still work on older versions of FMOD Ex. */
        public uint32                        length;                 /* [w]   Optional. Specify 0 to ignore. Size in uint8s of file to load, or sound to create (in this case only if FMOD_OPENUSER is used).  Required if loading from memory.  If 0 is specified, then it will use the size of the file (unless loading from memory then an error will be returned). */
        public uint32                        fileoffset;             /* [w]   Optional. Specify 0 to ignore. Offset from start of the file to start loading from.  This is useful for loading files from inside big data files. */
        public int32                         numchannels;            /* [w]   Optional. Specify 0 to ignore. Number of channels in a sound specified only if OPENUSER is used. */
        public int32                         defaultfrequency;       /* [w]   Optional. Specify 0 to ignore. Default frequency of sound in a sound specified only if OPENUSER is used.  Other formats use the frequency determined by the file format. */
        public SOUND_FORMAT                format;                 /* [w]   Optional. Specify 0 or SOUND_FORMAT_NONE to ignore. Format of the sound specified only if OPENUSER is used.  Other formats use the format determined by the file format.   */
        public uint32                        decodebuffersize;       /* [w]   Optional. Specify 0 to ignore. For streams.  This determines the size of the double buffer (in PCM samples) that a stream uses.  Use this for user created streams if you want to determine the size of the callback buffer passed to you.  Specify 0 to use FMOD's default size which is currently equivalent to 400ms of the sound format created/loaded. */
        public int32                         initialsubsound;        /* [w]   Optional. Specify 0 to ignore. In a multi-sample file format such as .FSB/.DLS/.SF2, specify the initial subsound to seek to, only if CREATESTREAM is used. */
        public int32                         numsubsounds;           /* [w]   Optional. Specify 0 to ignore or have no subsounds.  In a user created multi-sample sound, specify the number of subsounds within the sound that are accessable with Sound::getSubSound / SoundGetSubSound. */
        public int                      inclusionlist;          /* [w]   Optional. Specify 0 to ignore. In a multi-sample format such as .FSB/.DLS/.SF2 it may be desirable to specify only a subset of sounds to be loaded out of the whole file.  This is an array of subsound indicies to load into memory when created. */
        public int32                         inclusionlistnum;       /* [w]   Optional. Specify 0 to ignore. This is the number of integers contained within the */
        public SOUND_PCMREADCALLBACK       pcmreadcallback;        /* [w]   Optional. Specify 0 to ignore. Callback to 'piggyback' on FMOD's read functions and accept or even write PCM data while FMOD is opening the sound.  Used for user sounds created with OPENUSER or for capturing decoded data as FMOD reads it. */
        public SOUND_PCMSETPOSCALLBACK     pcmsetposcallback;      /* [w]   Optional. Specify 0 to ignore. Callback for when the user calls a seeking function such as Channel::setPosition within a multi-sample sound, and for when it is opened.*/
        public SOUND_NONBLOCKCALLBACK      nonblockcallback;       /* [w]   Optional. Specify 0 to ignore. Callback for successful completion, or error while loading a sound that used the FMOD_NONBLOCKING flag.*/
        public int                      dlsname;                /* [w]   Optional. Specify 0 to ignore. Filename for a DLS or SF2 sample set when loading a MIDI file.   If not specified, on windows it will attempt to open /windows/system32/drivers/gm.dls, otherwise the MIDI will fail to open.  */
        public int                      encryptionkey;          /* [w]   Optional. Specify 0 to ignore. Key for encrypted FSB file.  Without this key an encrypted FSB file will not load. */
        public int32                         maxpolyphony;           /* [w]   Optional. Specify 0 to ingore. For sequenced formats with dynamic channel allocation such as .MID and .IT, this specifies the maximum voice count allowed while playing.  .IT defaults to 64.  .MID defaults to 32. */
        public int                      userdata;               /* [w]   Optional. Specify 0 to ignore. This is user data to be attached to the sound during creation.  Access via Sound::getUserData. */
        public SOUND_TYPE                  suggestedsoundtype;     /* [w]   Optional. Specify 0 or FMOD_SOUND_TYPE_UNKNOWN to ignore.  Instead of scanning all codec types, use this to speed up loading by making it jump straight to this codec. */
        public FILE_OPENCALLBACK           fileuseropen;           /* [w]   Optional. Specify 0 to ignore. Callback for opening this file. */
        public FILE_CLOSECALLBACK          fileuserclose;          /* [w]   Optional. Specify 0 to ignore. Callback for closing this file. */
        public FILE_READCALLBACK           fileuserread;           /* [w]   Optional. Specify 0 to ignore. Callback for reading from this file. */
        public FILE_SEEKCALLBACK           fileuserseek;           /* [w]   Optional. Specify 0 to ignore. Callback for seeking within this file. */
        public FILE_ASYNCREADCALLBACK      fileuserasyncread;      /* [w]   Optional. Specify 0 to ignore. Callback for asyncronously reading from this file. */
        public FILE_ASYNCCANCELCALLBACK    fileuserasynccancel;    /* [w]   Optional. Specify 0 to ignore. Callback for cancelling an asyncronous read. */
        public int                      fileuserdata;           /* [w]   Optional. Specify 0 to ignore. User data to be passed into the file callbacks. */
        public int32                         filebuffersize;         /* [w]   Optional. Specify 0 to ignore. Buffer size for reading the file, -1 to disable buffering, or 0 for system default. */
        public CHANNELORDER                channelorder;           /* [w]   Optional. Specify 0 to ignore. Use this to differ the way fmod maps multichannel sounds to speakers.  See FMOD_CHANNELORDER for more. */
        //public CHANNELMASK                 channelmask;            /* [w]   Optional. Specify 0 to ignore. Use this to differ the way fmod maps multichannel sounds to speakers.  See FMOD_CHANNELMASK for more. */
        public int                      initialsoundgroup;      /* [w]   Optional. Specify 0 to ignore. Specify a sound group if required, to put sound in as it is created. */
        public uint32                        initialseekposition;    /* [w]   Optional. Specify 0 to ignore. For streams. Specify an initial position to seek the stream to. */
        public TIMEUNIT                    initialseekpostype;     /* [w]   Optional. Specify 0 to ignore. For streams. Specify the time unit for the position set in initialseekposition. */
        public int32                         ignoresetfilesystem;    /* [w]   Optional. Specify 0 to ignore. Set to 1 to use fmod's built in file system. Ignores setFileSystem callbacks and also FMOD_CREATESOUNEXINFO file callbacks.  Useful for specific cases where you don't want to use your own file system but want to use fmod's file system (ie net streaming). */
        public uint32                        audioqueuepolicy;       /* [w]   Optional. Specify 0 or FMOD_AUDIOQUEUE_CODECPOLICY_DEFAULT to ignore. Policy used to determine whether hardware or software is used for decoding, see FMOD_AUDIOQUEUE_CODECPOLICY for options (iOS >= 3.0 required, otherwise only hardware is available) */
        public uint32                        minmidigranularity;     /* [w]   Optional. Specify 0 to ignore. Allows you to set a minimum desired MIDI mixer granularity. Values smaller than 512 give greater than default accuracy at the cost of more CPU and vise versa. Specify 0 for default (512 samples). */
        public int32                         nonblockthreadid;       /* [w]   Optional. Specify 0 to ignore. Specifies a thread index to execute non blocking load on.  Allows for up to 5 threads to be used for loading at once.  This is to avoid one load blocking another.  Maximum value = 4. */
        public int                      fsbguid;                /* [r/w] Optional. Specify 0 to ignore. Allows you to provide the GUID lookup for cached FSB header info. Once loaded the GUID will be written back to the pointer. This is to avoid seeking and reading the FSB header. */
    }
    /*
    [STRUCTURE]
    [
        [DESCRIPTION]
        Structure defining a reverb environment for FMOD_SOFTWARE based sounds only.<br>

        [REMARKS]
        Note the default reverb properties are the same as the FMOD_PRESET_GENERIC preset.<br>
        Note that integer values that typically range from -10,000 to 1000 are represented in decibels,
        and are of a logarithmic scale, not linear, wheras float values are always linear.<br>
        <br>
        The numerical values listed below are the maximum, minimum and default values for each variable respectively.<br>
        <br>
        Hardware voice / Platform Specific reverb support.<br>
        WII   See FMODWII.H for hardware specific reverb functionality.<br>
        3DS   See FMOD3DS.H for hardware specific reverb functionality.<br>
        PSP   See FMODWII.H for hardware specific reverb functionality.<br>
        <br>
        Members marked with [r] mean the variable is modified by FMOD and is for reading purposes only.  Do not change this value.<br>
        Members marked with [w] mean the variable can be written to.  The user can set the value.<br>
        Members marked with [r/w] are either read or write depending on if you are using System::setReverbProperties (w) or System::getReverbProperties (r).

        [SEE_ALSO]
        System::setReverbProperties
        System::getReverbProperties
        FMOD_REVERB_PRESETS
    ]
    */
#pragma warning disable 414
    [CRepr]
    public struct REVERB_PROPERTIES
    {                            /*        MIN     MAX    DEFAULT   DESCRIPTION */
        public float DecayTime;         /* [r/w]  0.0    20000.0 1500.0  Reverberation decay time in ms                                        */
        public float EarlyDelay;        /* [r/w]  0.0    300.0   7.0     Initial reflection delay time                                         */
        public float LateDelay;         /* [r/w]  0.0    100     11.0    Late reverberation delay time relative to initial reflection          */
        public float HFReference;       /* [r/w]  20.0   20000.0 5000    Reference high frequency (hz)                                         */
        public float HFDecayRatio;      /* [r/w]  10.0   100.0   50.0    High-frequency to mid-frequency decay time ratio                      */
        public float Diffusion;         /* [r/w]  0.0    100.0   100.0   Value that controls the echo density in the late reverberation decay. */
        public float Density;           /* [r/w]  0.0    100.0   100.0   Value that controls the modal density in the late reverberation decay */
        public float LowShelfFrequency; /* [r/w]  20.0   1000.0  250.0   Reference low frequency (hz)                                          */
        public float LowShelfGain;      /* [r/w]  -36.0  12.0    0.0     Relative room effect level at low frequencies                         */
        public float HighCut;           /* [r/w]  20.0   20000.0 20000.0 Relative room effect level at high frequencies                        */
        public float EarlyLateMix;      /* [r/w]  0.0    100.0   50.0    Early reflections level relative to room effect                       */
        public float WetLevel;          /* [r/w]  -80.0  20.0    -6.0    Room effect level (at mid frequencies)
                                  * */
        #region wrapperinternal
        public this(float decayTime, float earlyDelay, float lateDelay, float hfReference,
            float hfDecayRatio, float diffusion, float density, float lowShelfFrequency, float lowShelfGain,
            float highCut, float earlyLateMix, float wetLevel)
        {
            DecayTime = decayTime;
            EarlyDelay = earlyDelay;
            LateDelay = lateDelay;
            HFReference = hfReference;
            HFDecayRatio = hfDecayRatio;
            Diffusion = diffusion;
            Density = density;
            LowShelfFrequency = lowShelfFrequency;
            LowShelfGain = lowShelfGain;
            HighCut = highCut;
            EarlyLateMix = earlyLateMix;
            WetLevel = wetLevel;
        }
        #endregion
    }
#pragma warning restore 414

    /*
    [DEFINE]
    [
    [NAME]
    FMOD_REVERB_PRESETS

    [DESCRIPTION]
    A set of predefined environment PARAMETERS, created by Creative Labs
    These are used to initialize an FMOD_REVERB_PROPERTIES structure statically.
    ie
    FMOD_REVERB_PROPERTIES prop = FMOD_PRESET_GENERIC;

    [SEE_ALSO]
    System::setReverbProperties
    ]
    */
    public class PRESET
    {
        /*                                                                                  Instance  Env   Diffus  Room   RoomHF  RmLF DecTm   DecHF  DecLF   Refl  RefDel   Revb  RevDel  ModTm  ModDp   HFRef    LFRef   Diffus  Densty  FLAGS */
        public static REVERB_PROPERTIES OFF()                 { return REVERB_PROPERTIES(  1000,    7,  11, 5000, 100, 100, 100, 250, 0,    20,  96, -80.0f );}
        public static REVERB_PROPERTIES GENERIC()             { return REVERB_PROPERTIES(  1500,    7,  11, 5000,  83, 100, 100, 250, 0, 14500,  96,  -8.0f );}
        public static REVERB_PROPERTIES PADDEDCELL()          { return REVERB_PROPERTIES(   170,    1,   2, 5000,  10, 100, 100, 250, 0,   160,  84,  -7.8f );}
        public static REVERB_PROPERTIES ROOM()                { return REVERB_PROPERTIES(   400,    2,   3, 5000,  83, 100, 100, 250, 0,  6050,  88,  -9.4f );}
        public static REVERB_PROPERTIES BATHROOM()            { return REVERB_PROPERTIES(  1500,    7,  11, 5000,  54, 100,  60, 250, 0,  2900,  83,   0.5f );}
        public static REVERB_PROPERTIES LIVINGROOM()          { return REVERB_PROPERTIES(   500,    3,   4, 5000,  10, 100, 100, 250, 0,   160,  58, -19.0f );}
        public static REVERB_PROPERTIES STONEROOM()           { return REVERB_PROPERTIES(  2300,   12,  17, 5000,  64, 100, 100, 250, 0,  7800,  71,  -8.5f );}
        public static REVERB_PROPERTIES AUDITORIUM()          { return REVERB_PROPERTIES(  4300,   20,  30, 5000,  59, 100, 100, 250, 0,  5850,  64, -11.7f );}
        public static REVERB_PROPERTIES CONCERTHALL()         { return REVERB_PROPERTIES(  3900,   20,  29, 5000,  70, 100, 100, 250, 0,  5650,  80,  -9.8f );}
        public static REVERB_PROPERTIES CAVE()                { return REVERB_PROPERTIES(  2900,   15,  22, 5000, 100, 100, 100, 250, 0, 20000,  59, -11.3f );}
        public static REVERB_PROPERTIES ARENA()               { return REVERB_PROPERTIES(  7200,   20,  30, 5000,  33, 100, 100, 250, 0,  4500,  80,  -9.6f );}
        public static REVERB_PROPERTIES HANGAR()              { return REVERB_PROPERTIES( 10000,   20,  30, 5000,  23, 100, 100, 250, 0,  3400,  72,  -7.4f );}
        public static REVERB_PROPERTIES CARPETTEDHALLWAY()    { return REVERB_PROPERTIES(   300,    2,  30, 5000,  10, 100, 100, 250, 0,   500,  56, -24.0f );}
        public static REVERB_PROPERTIES HALLWAY()             { return REVERB_PROPERTIES(  1500,    7,  11, 5000,  59, 100, 100, 250, 0,  7800,  87,  -5.5f );}
        public static REVERB_PROPERTIES STONECORRIDOR()       { return REVERB_PROPERTIES(   270,   13,  20, 5000,  79, 100, 100, 250, 0,  9000,  86,  -6.0f );}
        public static REVERB_PROPERTIES ALLEY()               { return REVERB_PROPERTIES(  1500,    7,  11, 5000,  86, 100, 100, 250, 0,  8300,  80,  -9.8f );}
        public static REVERB_PROPERTIES FOREST()              { return REVERB_PROPERTIES(  1500,  162,  88, 5000,  54,  79, 100, 250, 0,   760,  94, -12.3f );}
        public static REVERB_PROPERTIES CITY()                { return REVERB_PROPERTIES(  1500,    7,  11, 5000,  67,  50, 100, 250, 0,  4050,  66, -26.0f );}
        public static REVERB_PROPERTIES MOUNTAINS()           { return REVERB_PROPERTIES(  1500,  300, 100, 5000,  21,  27, 100, 250, 0,  1220,  82, -24.0f );}
        public static REVERB_PROPERTIES QUARRY()              { return REVERB_PROPERTIES(  1500,   61,  25, 5000,  83, 100, 100, 250, 0,  3400, 100,  -5.0f );}
        public static REVERB_PROPERTIES PLAIN()               { return REVERB_PROPERTIES(  1500,  179, 100, 5000,  50,  21, 100, 250, 0,  1670,  65, -28.0f );}
        public static REVERB_PROPERTIES PARKINGLOT()          { return REVERB_PROPERTIES(  1700,    8,  12, 5000, 100, 100, 100, 250, 0, 20000,  56, -19.5f );}
        public static REVERB_PROPERTIES SEWERPIPE()           { return REVERB_PROPERTIES(  2800,   14,  21, 5000,  14,  80,  60, 250, 0,  3400,  66,   1.2f );}
        public static REVERB_PROPERTIES UNDERWATER()          { return REVERB_PROPERTIES(  1500,    7,  11, 5000,  10, 100, 100, 250, 0,   500,  92,   7.0f );}
    }

    /*
    [STRUCTURE]
    [
        [DESCRIPTION]
        Settings for advanced features like configuring memory and cpu usage for the FMOD_CREATECOMPRESSEDSAMPLE feature.

        [REMARKS]
        maxMPEGCodecs / maxADPCMCodecs / maxXMACodecs will determine the maximum cpu usage of playing realtime samples.  Use this to lower potential excess cpu usage and also control memory usage.<br>

        [SEE_ALSO]
        System::setAdvancedSettings
        System::getAdvancedSettings
    ]
    */
    [CRepr]
    public struct ADVANCEDSETTINGS
    {
        public int32                 cbSize;                     /* [w]   Size of this structure.  Use (int32)sizeof(FMOD_ADVANCEDSETTINGS) */
        public int32                 maxMPEGCodecs;              /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_CREATECOMPRESSEDSAMPLE only.  MPEG   codecs consume 30,528 uint8s per instance and this number will determine how many MPEG   channels can be played simultaneously. Default = 32. */
        public int32                 maxADPCMCodecs;             /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_CREATECOMPRESSEDSAMPLE only.  ADPCM  codecs consume  3,128 uint8s per instance and this number will determine how many ADPCM  channels can be played simultaneously. Default = 32. */
        public int32                 maxXMACodecs;               /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_CREATECOMPRESSEDSAMPLE only.  XMA    codecs consume 14,836 uint8s per instance and this number will determine how many XMA    channels can be played simultaneously. Default = 32. */
        public int32                 maxVorbisCodecs;            /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_CREATECOMPRESSEDSAMPLE only.  Vorbis codecs consume 23,256 uint8s per instance and this number will determine how many Vorbis channels can be played simultaneously. Default = 32. */    
        public int32                 maxAT9Codecs;               /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_CREATECOMPRESSEDSAMPLE only.  AT9    codecs consume  8,720 uint8s per instance and this number will determine how many AT9    channels can be played simultaneously. Default = 32. */    
        public int32                 maxFADPCMCodecs;            /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_CREATECOMPRESSEDSAMPLE only.  This number will determine how many FADPCM channels can be played simultaneously. Default = 32. */
        public int32                 maxPCMCodecs;               /* [r/w] Optional. Specify 0 to ignore. For use with PS3 only.                          PCM    codecs consume 12,672 uint8s per instance and this number will determine how many streams and PCM voices can be played simultaneously. Default = 16. */
        public int32                 ASIONumChannels;            /* [r/w] Optional. Specify 0 to ignore. Number of channels available on the ASIO device. */
        public int              ASIOChannelList;            /* [r/w] Optional. Specify 0 to ignore. Pointer to an array of strings (number of entries defined by ASIONumChannels) with ASIO channel names. */
        public int              ASIOSpeakerList;            /* [r/w] Optional. Specify 0 to ignore. Pointer to a list of speakers that the ASIO channels map to.  This can be called after System::init to remap ASIO output. */
        public float               HRTFMinAngle;               /* [r/w] Optional.                      For use with FMOD_INIT_HRTF_LOWPASS.  The angle range (0-360) of a 3D sound in relation to the listener, at which the HRTF function begins to have an effect. 0 = in front of the listener. 180 = from 90 degrees to the left of the listener to 90 degrees to the right. 360 = behind the listener. Default = 180.0. */
        public float               HRTFMaxAngle;               /* [r/w] Optional.                      For use with FMOD_INIT_HRTF_LOWPASS.  The angle range (0-360) of a 3D sound in relation to the listener, at which the HRTF function has maximum effect. 0 = front of the listener. 180 = from 90 degrees to the left of the listener to 90 degrees to the right. 360 = behind the listener. Default = 360.0. */
        public float               HRTFFreq;                   /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_INIT_HRTF_LOWPASS.  The cutoff frequency of the HRTF's lowpass filter function when at maximum effect. (i.e. at HRTFMaxAngle).  Default = 4000.0. */
        public float               vol0virtualvol;             /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_INIT_VOL0_BECOMES_VIRTUAL.  If this flag is used, and the volume is below this, then the sound will become virtual.  Use this value to raise the threshold to a different point32 where a sound goes virtual. */
        public uint32                defaultDecodeBufferSize;    /* [r/w] Optional. Specify 0 to ignore. For streams. This determines the default size of the double buffer (in milliseconds) that a stream uses.  Default = 400ms */
        public uint16              profilePort;                /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_INIT_PROFILE_ENABLE.  Specify the port to listen on for connections by the profiler application. */
        public uint32                geometryMaxFadeTime;        /* [r/w] Optional. Specify 0 to ignore. The maximum time in miliseconds it takes for a channel to fade to the new level when its occlusion changes. */
        public float               distanceFilterCenterFreq;   /* [r/w] Optional. Specify 0 to ignore. For use with FMOD_INIT_DISTANCE_FILTERING.  The default center frequency in Hz for the distance filtering effect. Default = 1500.0. */
        public int32                 reverb3Dinstance;           /* [r/w] Optional. Specify 0 to ignore. Out of 0 to 3, 3d reverb spheres will create a phyical reverb unit on this instance slot.  See FMOD_REVERB_PROPERTIES. */
        public int32                 DSPBufferPoolSize;          /* [r/w] Optional. Specify 0 to ignore. Number of buffers in DSP buffer pool.  Each buffer will be DSPBlockSize * (int32)sizeof(float) * SpeakerModeChannelCount.  ie 7.1 @ 1024 DSP block size = 8 * 1024 * 4 = 32kb.  Default = 8. */
        public uint32                stackSizeStream;            /* [r/w] Optional. Specify 0 to ignore. Specify the stack size for the FMOD Stream thread in uint8s.  Useful for custom codecs that use excess stack.  Default 49,152 (48kb) */
        public uint32                stackSizeNonBlocking;       /* [r/w] Optional. Specify 0 to ignore. Specify the stack size for the FMOD_NONBLOCKING loading thread.  Useful for custom codecs that use excess stack.  Default 65,536 (64kb) */
        public uint32                stackSizeMixer;             /* [r/w] Optional. Specify 0 to ignore. Specify the stack size for the FMOD mixer thread.  Useful for custom dsps that use excess stack.  Default 49,152 (48kb) */
        public DSP_RESAMPLER       resamplerMethod;            /* [r/w] Optional. Specify 0 to ignore. Resampling method used with fmod's software mixer.  See FMOD_DSP_RESAMPLER for details on methods. */
        public uint32                commandQueueSize;           /* [r/w] Optional. Specify 0 to ignore. Specify the command queue size for thread safe processing.  Default 2048 (2kb) */
        public uint32                randomSeed;                 /* [r/w] Optional. Specify 0 to ignore. Seed value that FMOD will use to initialize its internal random number generators. */
    }

    /*
    [DEFINE]
    [
        [NAME]
        FMOD_DRIVER_STATE

        [DESCRIPTION]
        Flags that provide additional information about a particular driver.

        [REMARKS]

        [SEE_ALSO]
        System::getRecordDriverInfo
    ]
    */
    //[Flags]
    public enum DRIVER_STATE : uint32
    {
        CONNECTED = 0x00000001, /* Device is currently plugged in. */
        DEFAULT   = 0x00000002, /* Device is the users preferred choice. */
    }

    /*
        FMOD System factory functions.  Use this to create an FMOD System Instance.  below you will see System init/close to get started.
    */
    public class Factory
    {
        public static RESULT System_Create(out System system)
        {
            system = null;

            RESULT result   = RESULT.OK;
            int rawPtr   = 0;

            result = FMOD_System_Create(out rawPtr, VERSION.number);
            if (result != RESULT.OK)
            {
                return result;
            }

            system = new System(rawPtr);

            return result;
        }


        #region importfunctions

        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Create                      (out int system, uint headerversion);

        #endregion
    }

    public class Memory
    {
        public static RESULT Initialize(int poolmem, int32 poollen, MEMORY_ALLOC_CALLBACK useralloc, MEMORY_REALLOC_CALLBACK userrealloc, MEMORY_FREE_CALLBACK userfree, MEMORY_TYPE memtypeflags)
        {
            return FMOD_Memory_Initialize(poolmem, poollen, useralloc, userrealloc, userfree, memtypeflags);
        }

        public static RESULT GetStats(out int32 currentalloced, out int32 maxalloced)
        {
            return GetStats(out currentalloced, out maxalloced, false);
        }

        public static RESULT GetStats(out int32 currentalloced, out int32 maxalloced, bool blocking)
        {
            return FMOD_Memory_GetStats(out currentalloced, out maxalloced, blocking);
        }


        #region importfunctions

        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Memory_Initialize(int poolmem, int32 poollen, MEMORY_ALLOC_CALLBACK useralloc, MEMORY_REALLOC_CALLBACK userrealloc, MEMORY_FREE_CALLBACK userfree, MEMORY_TYPE memtypeflags);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Memory_GetStats(out int32 currentalloced, out int32 maxalloced, bool blocking);

        #endregion
    }

    public class Debug
    {
        public static RESULT Initialize(DEBUG_FLAGS flags, DEBUG_MODE mode, DEBUG_CALLBACK callback, String filename)
        {
            return FMOD_Debug_Initialize(flags, mode, callback, filename);
        }


        #region importfunctions

        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Debug_Initialize(DEBUG_FLAGS flags, DEBUG_MODE mode, DEBUG_CALLBACK callback, String filename);

        #endregion
    }

    public class HandleBase
    {
        public this(int newPtr)
        {
            rawPtr = newPtr;
        }

        public bool isValid()
        {
            return rawPtr != (int)0;
        }

        public int getRaw()
        {
            return rawPtr;
        }

        protected int rawPtr;

        #region equality

        /*public override bool Equals(Object obj)
        {
            return Equals(obj as HandleBase);
        }*/

        public bool Equals(HandleBase p)
        {
            // Equals if p not null and handle is the same
            return ((Object)p != null && rawPtr == p.rawPtr);
        }
        public int32 GetHashCode()
        {
            return (int32)rawPtr;
        }
        public static bool operator ==(HandleBase a, HandleBase b)
        {
            // If both are null, or both are same instance, return true.
            /*if (Object.ReferenceEquals(a, b))
            {
                return true;
            }*/

			if ((Object)a == (Object)b)
				return true;

            // If one is null, but not both, return false.
            if (((Object)a == null) || ((Object)b == null))
            {
                return false;
            }
            // Return true if the handle matches
            return (a.rawPtr == b.rawPtr);
        }
        public static bool operator !=(HandleBase a, HandleBase b)
        {
            return !(a == b);
        }
        #endregion

    }

    /*
        'System' API.
    */
    public class System : HandleBase
    {
        public RESULT release                ()
        {
            RESULT result = FMOD_System_Release(rawPtr);
            if (result == RESULT.OK)
            {
                rawPtr = 0;
            }
            return result;
        }


        // Pre-init functions.
        public RESULT setOutput              (OUTPUTTYPE output)
        {
            return FMOD_System_SetOutput(rawPtr, output);
        }
        public RESULT getOutput              (out OUTPUTTYPE output)
        {
            return FMOD_System_GetOutput(rawPtr, out output);
        }
        public RESULT getNumDrivers          (out int32 numdrivers)
        {
            return FMOD_System_GetNumDrivers(rawPtr, out numdrivers);
        }
        public RESULT getDriverInfo          (int32 id, String name, int32 namelen, out Guid guid, out int32 systemrate, out SPEAKERMODE speakermode, out int32 speakermodechannels)
        {
            //int stringMem = Marshal.AllocHGlobal(name.Capacity);

			char8* stringMem = new char8[4096]*;
            RESULT result = FMOD_System_GetDriverInfo(rawPtr, id, stringMem, namelen, out guid, out systemrate, out speakermode, out speakermodechannels);
			name.Append(stringMem);

            //StringMarshalHelper.NativeToBuilder(name, stringMem);
            //Marshal.FreeHGlobal(stringMem);

            return result;
        }
        public RESULT setDriver              (int32 driver)
        {
            return FMOD_System_SetDriver(rawPtr, driver);
        }
        public RESULT getDriver              (out int32 driver)
        {
            return FMOD_System_GetDriver(rawPtr, out driver);
        }
        public RESULT setSoftwareChannels    (int32 numsoftwarechannels)
        {
            return FMOD_System_SetSoftwareChannels(rawPtr, numsoftwarechannels);
        }
        public RESULT getSoftwareChannels    (out int32 numsoftwarechannels)
        {
            return FMOD_System_GetSoftwareChannels(rawPtr, out numsoftwarechannels);
        }
        public RESULT setSoftwareFormat      (int32 samplerate, SPEAKERMODE speakermode, int32 numrawspeakers)
        {
            return FMOD_System_SetSoftwareFormat(rawPtr, samplerate, speakermode, numrawspeakers);
        }
        public RESULT getSoftwareFormat      (out int32 samplerate, out SPEAKERMODE speakermode, out int32 numrawspeakers)
        {
            return FMOD_System_GetSoftwareFormat(rawPtr, out samplerate, out speakermode, out numrawspeakers);
        }
        public RESULT setDSPBufferSize       (uint32 bufferlength, int32 numbuffers)
        {
            return FMOD_System_SetDSPBufferSize(rawPtr, bufferlength, numbuffers);
        }
        public RESULT getDSPBufferSize       (out uint32 bufferlength, out int32 numbuffers)
        {
            return FMOD_System_GetDSPBufferSize(rawPtr, out bufferlength, out numbuffers);
        }
        public RESULT setFileSystem          (FILE_OPENCALLBACK useropen, FILE_CLOSECALLBACK userclose, FILE_READCALLBACK userread, FILE_SEEKCALLBACK userseek, FILE_ASYNCREADCALLBACK userasyncread, FILE_ASYNCCANCELCALLBACK userasynccancel, int32 blockalign)
        {
            return FMOD_System_SetFileSystem(rawPtr, useropen, userclose, userread, userseek, userasyncread, userasynccancel, blockalign);
        }
        public RESULT attachFileSystem       (FILE_OPENCALLBACK useropen, FILE_CLOSECALLBACK userclose, FILE_READCALLBACK userread, FILE_SEEKCALLBACK userseek)
        {
            return FMOD_System_AttachFileSystem(rawPtr, useropen, userclose, userread, userseek);
        }
        public RESULT setAdvancedSettings    (ref ADVANCEDSETTINGS settings)
        {
            settings.cbSize = (int32)sizeof(ADVANCEDSETTINGS);
            return FMOD_System_SetAdvancedSettings(rawPtr, ref settings);
        }
        public RESULT getAdvancedSettings    (ref ADVANCEDSETTINGS settings)
        {
            settings.cbSize = (int32)sizeof(ADVANCEDSETTINGS);
            return FMOD_System_GetAdvancedSettings(rawPtr, ref settings);
        }
        public RESULT setCallback            (SYSTEM_CALLBACK callback, SYSTEM_CALLBACK_TYPE callbackmask)
        {
            return FMOD_System_SetCallback(rawPtr, callback, callbackmask);
        }

        // Plug-in support.
        public RESULT setPluginPath          (String path)
        {
            return FMOD_System_SetPluginPath(rawPtr, path.CStr());
        }
        public RESULT loadPlugin             (String filename, out uint32 handle, uint32 priority)
        {
            return FMOD_System_LoadPlugin(rawPtr, filename.CStr(), out handle, priority);
        }
        public RESULT loadPlugin             (String filename, out uint32 handle)
        {
            return loadPlugin(filename, out handle, 0);
        }
        public RESULT unloadPlugin           (uint32 handle)
        {
            return FMOD_System_UnloadPlugin(rawPtr, handle);
        }
        public RESULT getNumNestedPlugins    (uint32 handle, out int32 count)
        {
            return FMOD_System_GetNumNestedPlugins(rawPtr, handle, out count);
        }
        public RESULT getNestedPlugin        (uint32 handle, int32 index, out uint32 nestedhandle)
        {
            return FMOD_System_GetNestedPlugin(rawPtr, handle, index, out nestedhandle);
        }
        public RESULT getNumPlugins          (PLUGINTYPE plugintype, out int32 numplugins)
        {
            return FMOD_System_GetNumPlugins(rawPtr, plugintype, out numplugins);
        }
        public RESULT getPluginHandle        (PLUGINTYPE plugintype, int32 index, out uint32 handle)
        {
            return FMOD_System_GetPluginHandle(rawPtr, plugintype, index, out handle);
        }
        public RESULT getPluginInfo          (uint32 handle, out PLUGINTYPE plugintype, String name, int32 namelen, out uint32 version)
        {
            //int stringMem = Marshal.AllocHGlobal(name.Capacity);

			char8* stringMem = scope char8[namelen]*;
            RESULT result = FMOD_System_GetPluginInfo(rawPtr, handle, out plugintype, stringMem, namelen, out version);
			name.Append(stringMem);

            //StringMarshalHelper.NativeToBuilder(name, stringMem);
            //Marshal.FreeHGlobal(stringMem);

            return result;
        }
        public RESULT setOutputByPlugin      (uint32 handle)
        {
            return FMOD_System_SetOutputByPlugin(rawPtr, handle);
        }
        public RESULT getOutputByPlugin      (out uint32 handle)
        {
            return FMOD_System_GetOutputByPlugin(rawPtr, out handle);
        }
        public RESULT createDSPByPlugin(uint32 handle, out DSP dsp)
        {
            dsp = null;

            int dspraw;
            RESULT result = FMOD_System_CreateDSPByPlugin(rawPtr, handle, out dspraw);
            dsp = new DSP(dspraw);

            return result;
        }
        public RESULT getDSPInfoByPlugin(uint32 handle, out int description)
        {
            return FMOD_System_GetDSPInfoByPlugin(rawPtr, handle, out description);
        }
        /*
        public RESULT registerCodec(ref CODEC_DESCRIPTION description, out uint32 handle, uint32 priority)
        {
            return FMOD_System_RegisterCodec(rawPtr, ref description, out handle, priority);
        }
        */
        public RESULT registerDSP(ref DSP_DESCRIPTION description, out uint32 handle)
        {
            return FMOD_System_RegisterDSP(rawPtr, ref description, out handle);
        }
        /*
        public RESULT registerOutput(ref OUTPUT_DESCRIPTION description, out uint32 handle)
        {
            return FMOD_System_RegisterOutput(rawPtr, ref description, out handle);
        }
        */

        // Init/Close.
        public RESULT init                   (int32 maxchannels, INITFLAGS flags, int extradriverdata)
        {
            return FMOD_System_Init(rawPtr, maxchannels, flags, extradriverdata);
        }
        public RESULT close                  ()
        {
            return FMOD_System_Close(rawPtr);
        }


        // General post-init system functions.
        public RESULT update                 ()
        {
            return FMOD_System_Update(rawPtr);
        }

        public RESULT setSpeakerPosition(SPEAKER speaker, float x, float y, bool active)
        {
            return FMOD_System_SetSpeakerPosition(rawPtr, speaker, x, y, active);
        }
        public RESULT getSpeakerPosition(SPEAKER speaker, out float x, out float y, out bool active)
        {
            return FMOD_System_GetSpeakerPosition(rawPtr, speaker, out x, out y, out active);
        }
        public RESULT setStreamBufferSize(uint32 filebuffersize, TIMEUNIT filebuffersizetype)
        {
            return FMOD_System_SetStreamBufferSize(rawPtr, filebuffersize, filebuffersizetype);
        }
        public RESULT getStreamBufferSize(out uint32 filebuffersize, out TIMEUNIT filebuffersizetype)
        {
            return FMOD_System_GetStreamBufferSize(rawPtr, out filebuffersize, out filebuffersizetype);
        }
        public RESULT set3DSettings          (float dopplerscale, float distancefactor, float rolloffscale)
        {
            return FMOD_System_Set3DSettings(rawPtr, dopplerscale, distancefactor, rolloffscale);
        }
        public RESULT get3DSettings          (out float dopplerscale, out float distancefactor, out float rolloffscale)
        {
            return FMOD_System_Get3DSettings(rawPtr, out dopplerscale, out distancefactor, out rolloffscale);
        }
        public RESULT set3DNumListeners      (int32 numlisteners)
        {
            return FMOD_System_Set3DNumListeners(rawPtr, numlisteners);
        }
        public RESULT get3DNumListeners      (out int32 numlisteners)
        {
            return FMOD_System_Get3DNumListeners(rawPtr, out numlisteners);
        }
        public RESULT set3DListenerAttributes(int32 listener, ref VECTOR pos, ref VECTOR vel, ref VECTOR forward, ref VECTOR up)
        {
            return FMOD_System_Set3DListenerAttributes(rawPtr, listener, ref pos, ref vel, ref forward, ref up);
        }
        public RESULT get3DListenerAttributes(int32 listener, out VECTOR pos, out VECTOR vel, out VECTOR forward, out VECTOR up)
        {
            return FMOD_System_Get3DListenerAttributes(rawPtr, listener, out pos, out vel, out forward, out up);
        }
        public RESULT set3DRolloffCallback   (CB_3D_ROLLOFFCALLBACK callback)
        {
            return FMOD_System_Set3DRolloffCallback   (rawPtr, callback);
        }
        public RESULT mixerSuspend           ()
        {
            return FMOD_System_MixerSuspend(rawPtr);
        }
        public RESULT mixerResume            ()
        {
            return FMOD_System_MixerResume(rawPtr);
        }
        public RESULT getDefaultMixMatrix    (SPEAKERMODE sourcespeakermode, SPEAKERMODE targetspeakermode, float[] matrix, int32 matrixhop)
        {
            return FMOD_System_GetDefaultMixMatrix(rawPtr, sourcespeakermode, targetspeakermode, matrix, matrixhop);
        }
        public RESULT getSpeakerModeChannels (SPEAKERMODE mode, out int32 channels)
        {
            return FMOD_System_GetSpeakerModeChannels(rawPtr, mode, out channels);
        }

        // System information functions.
        public RESULT getVersion             (out uint32 version)
        {
            return FMOD_System_GetVersion(rawPtr, out version);
        }
        public RESULT getOutputHandle        (out int handle)
        {
            return FMOD_System_GetOutputHandle(rawPtr, out handle);
        }
        public RESULT getChannelsPlaying     (out int32 channels, out int32 realchannels)
        {
            return FMOD_System_GetChannelsPlaying(rawPtr, out channels, out realchannels);
        }
        public RESULT getCPUUsage            (out float dsp, out float stream, out float geometry, out float update, out float total)
        {
            return FMOD_System_GetCPUUsage(rawPtr, out dsp, out stream, out geometry, out update, out total);
        }
        public RESULT getFileUsage            (out Int64 sampleuint8sRead, out Int64 streamuint8sRead, out Int64 otheruint8sRead)
        {
            return FMOD_System_GetFileUsage(rawPtr, out sampleuint8sRead, out streamuint8sRead, out otheruint8sRead);
        }
        public RESULT getSoundRAM            (out int32 currentalloced, out int32 maxalloced, out int32 total)
        {
            return FMOD_System_GetSoundRAM(rawPtr, out currentalloced, out maxalloced, out total);
        }

        // Sound/DSP/Channel/FX creation and retrieval.
        public RESULT createSound            (String name, MODE mode, ref CREATESOUNDEXINFO exinfo, out Sound sound)
        {
            sound = null;

            char8* stringData;
            stringData = name.CStr();
            
            exinfo.cbsize = (int32)sizeof(CREATESOUNDEXINFO);
			//int offset = offsetof(CREATESOUNDEXINFO, channelmask);

            int soundraw;
            RESULT result = FMOD_System_CreateSound(rawPtr, stringData, mode, ref exinfo, out soundraw);
            sound = new Sound(soundraw);

            return result;
        }
        public RESULT createSound            (uint8[] data, MODE mode, ref CREATESOUNDEXINFO exinfo, out Sound sound)
        {
            sound = null;

            exinfo.cbsize = (int32)sizeof(CREATESOUNDEXINFO);

            int soundraw;
            RESULT result = FMOD_System_CreateSound(rawPtr, (char8*)&data[0], mode, ref exinfo, out soundraw);
            sound = new Sound(soundraw);

            return result;
        }
        public RESULT createSound            (String name, MODE mode, out Sound sound)
        {
            CREATESOUNDEXINFO exinfo = CREATESOUNDEXINFO();
            exinfo.cbsize = (int32)sizeof(CREATESOUNDEXINFO);

            return createSound(name, mode, ref exinfo, out sound);
        }
        public RESULT createStream            (String name, MODE mode, ref CREATESOUNDEXINFO exinfo, out Sound sound)
        {
            sound = null;

            char8* stringData;
            stringData = name.CStr();
            
            exinfo.cbsize = (int32)sizeof(CREATESOUNDEXINFO);

            int soundraw;
            RESULT result = FMOD_System_CreateStream(rawPtr, stringData, mode, ref exinfo, out soundraw);
            sound = new Sound(soundraw);

            return result;
        }
        public RESULT createStream            (uint8[] data, MODE mode, ref CREATESOUNDEXINFO exinfo, out Sound sound)
        {
            sound = null;

            exinfo.cbsize = (int32)sizeof(CREATESOUNDEXINFO);

            int soundraw;
            RESULT result = FMOD_System_CreateStream(rawPtr, (char8*)&data[0], mode, ref exinfo, out soundraw);
            sound = new Sound(soundraw);

            return result;
        }
        public RESULT createStream            (String name, MODE mode, out Sound sound)
        {
            CREATESOUNDEXINFO exinfo = CREATESOUNDEXINFO();
            exinfo.cbsize = (int32)sizeof(CREATESOUNDEXINFO);

            return createStream(name, mode, ref exinfo, out sound);
        }
        public RESULT createDSP              (ref DSP_DESCRIPTION description, out DSP dsp)
        {
            dsp = null;

            int dspraw;
            RESULT result = FMOD_System_CreateDSP(rawPtr, ref description, out dspraw);
            dsp = new DSP(dspraw);

            return result;
        }
        public RESULT createDSPByType          (DSP_TYPE type, out DSP dsp)
        {
            dsp = null;

            int dspraw;
            RESULT result = FMOD_System_CreateDSPByType(rawPtr, type, out dspraw);
            dsp = new DSP(dspraw);

            return result;
        }
        public RESULT createChannelGroup     (String name, out ChannelGroup channelgroup)
        {
            channelgroup = null;

            char8* stringData = name.CStr();

            int channelgroupraw;
            RESULT result = FMOD_System_CreateChannelGroup(rawPtr, stringData, out channelgroupraw);
            channelgroup = new ChannelGroup(channelgroupraw);

            return result;
        }
        public RESULT createSoundGroup       (String name, out SoundGroup soundgroup)
        {
            soundgroup = null;

            char8* stringData = name.CStr();

            int soundgroupraw;
            RESULT result = FMOD_System_CreateSoundGroup(rawPtr, stringData, out soundgroupraw);
            soundgroup = new SoundGroup(soundgroupraw);

            return result;
        }
        public RESULT createReverb3D         (out Reverb3D reverb)
        {
            int reverbraw;
            RESULT result = FMOD_System_CreateReverb3D(rawPtr, out reverbraw);
            reverb = new Reverb3D(reverbraw);

            return result;
        }
        public RESULT playSound              (Sound sound, ChannelGroup channelGroup, bool paused, out Channel channel)
        {
            channel = null;

            int channelGroupRaw = (channelGroup != null) ? channelGroup.getRaw() : 0;

            int channelraw;
            RESULT result = FMOD_System_PlaySound(rawPtr, sound.getRaw(), channelGroupRaw, paused, out channelraw);
            channel = new Channel(channelraw);

            return result;
        }
        public RESULT playDSP                (DSP dsp, ChannelGroup channelGroup, bool paused, out Channel channel)
        {
            channel = null;

            int channelGroupRaw = (channelGroup != null) ? channelGroup.getRaw() : 0;

            int channelraw;
            RESULT result = FMOD_System_PlayDSP(rawPtr, dsp.getRaw(), channelGroupRaw, paused, out channelraw);
            channel = new Channel(channelraw);

            return result;
        }
        public RESULT getChannel             (int32 channelid, out Channel channel)
        {
            channel = null;

            int channelraw;
            RESULT result = FMOD_System_GetChannel(rawPtr, channelid, out channelraw);
            channel = new Channel(channelraw);

            return result;
        }
        public RESULT getMasterChannelGroup  (out ChannelGroup channelgroup)
        {
            channelgroup = null;

            int channelgroupraw;
            RESULT result = FMOD_System_GetMasterChannelGroup(rawPtr, out channelgroupraw);
            channelgroup = new ChannelGroup(channelgroupraw);

            return result;
        }
        public RESULT getMasterSoundGroup    (out SoundGroup soundgroup)
        {
            soundgroup = null;

            int soundgroupraw;
            RESULT result = FMOD_System_GetMasterSoundGroup(rawPtr, out soundgroupraw);
            soundgroup = new SoundGroup(soundgroupraw);

            return result;
        }

        // Routing to ports.
        public RESULT attachChannelGroupToPort(uint32 portType, uint64 portIndex, ChannelGroup channelgroup, bool passThru = false)
        {
            return FMOD_System_AttachChannelGroupToPort(rawPtr, portType, portIndex, channelgroup.getRaw(), passThru);
        }
        public RESULT detachChannelGroupFromPort(ChannelGroup channelgroup)
        {
            return FMOD_System_DetachChannelGroupFromPort(rawPtr, channelgroup.getRaw());
        }

        // Reverb api.
        public RESULT setReverbProperties    (int32 instance, ref REVERB_PROPERTIES prop)
        {
            return FMOD_System_SetReverbProperties(rawPtr, instance, ref prop);
        }
        public RESULT getReverbProperties    (int32 instance, out REVERB_PROPERTIES prop)
        {
            return FMOD_System_GetReverbProperties(rawPtr, instance, out prop);
        }

        // System level DSP functionality.
        public RESULT lockDSP            ()
        {
            return FMOD_System_LockDSP(rawPtr);
        }
        public RESULT unlockDSP          ()
        {
            return FMOD_System_UnlockDSP(rawPtr);
        }

        // Recording api
        public RESULT getRecordNumDrivers    (out int32 numdrivers, out int32 numconnected)
        {
            return FMOD_System_GetRecordNumDrivers(rawPtr, out numdrivers, out numconnected);
        }
        public RESULT getRecordDriverInfo(int32 id, String name, int32 namelen, out Guid guid, out int32 systemrate, out SPEAKERMODE speakermode, out int32 speakermodechannels, out DRIVER_STATE state)
        {
            //int stringMem = Marshal.AllocHGlobal(name.Capacity);

			char8* stringMem = scope char8[namelen]*;
            RESULT result = FMOD_System_GetRecordDriverInfo(rawPtr, id, stringMem, namelen, out guid, out systemrate, out speakermode, out speakermodechannels, out state);
			name.Append(stringMem);

            //StringMarshalHelper.NativeToBuilder(name, stringMem);
            //Marshal.FreeHGlobal(stringMem);

            return result;
        }
        public RESULT getRecordPosition      (int32 id, out uint32 position)
        {
            return FMOD_System_GetRecordPosition(rawPtr, id, out position);
        }
        public RESULT recordStart            (int32 id, Sound sound, bool loop)
        {
            return FMOD_System_RecordStart(rawPtr, id, sound.getRaw(), loop);
        }
        public RESULT recordStop             (int32 id)
        {
            return FMOD_System_RecordStop(rawPtr, id);
        }
        public RESULT isRecording            (int32 id, out bool recording)
        {
            return FMOD_System_IsRecording(rawPtr, id, out recording);
        }

        // Geometry api
        public RESULT createGeometry         (int32 maxpolygons, int32 maxvertices, out Geometry geometry)
        {
            geometry = null;

            int geometryraw;
            RESULT result = FMOD_System_CreateGeometry(rawPtr, maxpolygons, maxvertices, out geometryraw);
            geometry = new Geometry(geometryraw);

            return result;
        }
        public RESULT setGeometrySettings    (float maxworldsize)
        {
            return FMOD_System_SetGeometrySettings(rawPtr, maxworldsize);
        }
        public RESULT getGeometrySettings    (out float maxworldsize)
        {
            return FMOD_System_GetGeometrySettings(rawPtr, out maxworldsize);
        }
        public RESULT loadGeometry(int data, int32 datasize, out Geometry geometry)
        {
            geometry = null;

            int geometryraw;
            RESULT result = FMOD_System_LoadGeometry(rawPtr, data, datasize, out geometryraw);
            geometry = new Geometry(geometryraw);

            return result;
        }
        public RESULT getGeometryOcclusion    (ref VECTOR listener, ref VECTOR source, out float direct, out float reverb)
        {
            return FMOD_System_GetGeometryOcclusion(rawPtr, ref listener, ref source, out direct, out reverb);
        }

        // Network functions
        public RESULT setNetworkProxy               (String proxy)
        {
            return FMOD_System_SetNetworkProxy(rawPtr, proxy.CStr());
        }
        public RESULT getNetworkProxy               (String proxy, int32 proxylen)
        {
            //int stringMem = Marshal.AllocHGlobal(proxy.Capacity);

			char8* stringMem = scope char8[proxylen]*;
            RESULT result = FMOD_System_GetNetworkProxy(rawPtr, stringMem, proxylen);
			proxy.Append(stringMem);

            //StringMarshalHelper.NativeToBuilder(proxy, stringMem);
            //Marshal.FreeHGlobal(stringMem);

            return result;
        }
        public RESULT setNetworkTimeout      (int32 timeout)
        {
            return FMOD_System_SetNetworkTimeout(rawPtr, timeout);
        }
        public RESULT getNetworkTimeout(out int32 timeout)
        {
            return FMOD_System_GetNetworkTimeout(rawPtr, out timeout);
        }

        // Userdata set/get
        public RESULT setUserData            (int userdata)
        {
            return FMOD_System_SetUserData(rawPtr, userdata);
        }
        public RESULT getUserData            (out int userdata)
        {
            return FMOD_System_GetUserData(rawPtr, out userdata);
        }


        #region importfunctions
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Release                (int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetOutput              (int system, OUTPUTTYPE output);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetOutput              (int system, out OUTPUTTYPE output);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetNumDrivers          (int system, out int32 numdrivers);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetDriverInfo          (int system, int32 id, char8* name, int32 namelen, out Guid guid, out int32 systemrate, out SPEAKERMODE speakermode, out int32 speakermodechannels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetDriver              (int system, int32 driver);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetDriver              (int system, out int32 driver);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetSoftwareChannels    (int system, int32 numsoftwarechannels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetSoftwareChannels    (int system, out int32 numsoftwarechannels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetSoftwareFormat      (int system, int32 samplerate, SPEAKERMODE speakermode, int32 numrawspeakers);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetSoftwareFormat      (int system, out int32 samplerate, out SPEAKERMODE speakermode, out int32 numrawspeakers);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetDSPBufferSize       (int system, uint32 bufferlength, int32 numbuffers);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetDSPBufferSize       (int system, out uint32 bufferlength, out int32 numbuffers);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetFileSystem          (int system, FILE_OPENCALLBACK useropen, FILE_CLOSECALLBACK userclose, FILE_READCALLBACK userread, FILE_SEEKCALLBACK userseek, FILE_ASYNCREADCALLBACK userasyncread, FILE_ASYNCCANCELCALLBACK userasynccancel, int32 blockalign);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_AttachFileSystem       (int system, FILE_OPENCALLBACK useropen, FILE_CLOSECALLBACK userclose, FILE_READCALLBACK userread, FILE_SEEKCALLBACK userseek);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetPluginPath          (int system, char8* path);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_LoadPlugin             (int system, char8* filename, out uint32 handle, uint32 priority);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_UnloadPlugin           (int system, uint32 handle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetNumNestedPlugins    (int system, uint32 handle, out int32 count);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetNestedPlugin        (int system, uint32 handle, int32 index, out uint32 nestedhandle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetNumPlugins          (int system, PLUGINTYPE plugintype, out int32 numplugins);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetPluginHandle        (int system, PLUGINTYPE plugintype, int32 index, out uint32 handle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetPluginInfo          (int system, uint32 handle, out PLUGINTYPE plugintype, char8* name, int32 namelen, out uint32 version);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_CreateDSPByPlugin      (int system, uint32 handle, out int dsp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetOutputByPlugin      (int system, uint32 handle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetOutputByPlugin      (int system, out uint32 handle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetDSPInfoByPlugin     (int system, uint32 handle, out int description);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        //private static extern RESULT FMOD_System_RegisterCodec          (int system, out CODEC_DESCRIPTION description, out uint32 handle, uint32 priority);
        //[Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_RegisterDSP            (int system, ref DSP_DESCRIPTION description, out uint32 handle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        //private static extern RESULT FMOD_System_RegisterOutput         (int system, ref OUTPUT_DESCRIPTION description, out uint32 handle);
        //[Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Init                   (int system, int32 maxchannels, INITFLAGS flags, int extradriverdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Close                  (int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Update                 (int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetAdvancedSettings    (int system, ref ADVANCEDSETTINGS settings);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetAdvancedSettings    (int system, ref ADVANCEDSETTINGS settings);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Set3DRolloffCallback   (int system, CB_3D_ROLLOFFCALLBACK callback);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_MixerSuspend           (int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_MixerResume            (int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetDefaultMixMatrix    (int system, SPEAKERMODE sourcespeakermode, SPEAKERMODE targetspeakermode, float[] matrix, int32 matrixhop);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetSpeakerModeChannels (int system, SPEAKERMODE mode, out int32 channels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetCallback            (int system, SYSTEM_CALLBACK callback, SYSTEM_CALLBACK_TYPE callbackmask);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetSpeakerPosition     (int system, SPEAKER speaker, float x, float y, bool active);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetSpeakerPosition     (int system, SPEAKER speaker, out float x, out float y, out bool active);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Set3DSettings          (int system, float dopplerscale, float distancefactor, float rolloffscale);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Get3DSettings          (int system, out float dopplerscale, out float distancefactor, out float rolloffscale);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Set3DNumListeners      (int system, int32 numlisteners);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Get3DNumListeners      (int system, out int32 numlisteners);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Set3DListenerAttributes(int system, int32 listener, ref VECTOR pos, ref VECTOR vel, ref VECTOR forward, ref VECTOR up);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_Get3DListenerAttributes(int system, int32 listener, out VECTOR pos, out VECTOR vel, out VECTOR forward, out VECTOR up);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetStreamBufferSize    (int system, uint32 filebuffersize, TIMEUNIT filebuffersizetype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetStreamBufferSize    (int system, out uint32 filebuffersize, out TIMEUNIT filebuffersizetype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetVersion             (int system, out uint32 version);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetOutputHandle        (int system, out int handle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetChannelsPlaying     (int system, out int32 channels, out int32 realchannels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetCPUUsage            (int system, out float dsp, out float stream, out float geometry, out float update, out float total);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetFileUsage            (int system, out Int64 sampleuint8sRead, out Int64 streamuint8sRead, out Int64 otheruint8sRead);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetSoundRAM            (int system, out int32 currentalloced, out int32 maxalloced, out int32 total);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_CreateSound            (int system, char8* name_or_data, MODE mode, ref CREATESOUNDEXINFO exinfo, out int sound);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_CreateStream           (int system, char8* name_or_data, MODE mode, ref CREATESOUNDEXINFO exinfo, out int sound);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_CreateDSP              (int system, ref DSP_DESCRIPTION description, out int dsp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_CreateDSPByType        (int system, DSP_TYPE type, out int dsp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_CreateChannelGroup     (int system, char8* name, out int channelgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_CreateSoundGroup       (int system, char8* name, out int soundgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_CreateReverb3D         (int system, out int reverb);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_PlaySound              (int system, int sound, int channelGroup, bool paused, out int channel);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_PlayDSP                (int system, int dsp, int channelGroup, bool paused, out int channel);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetChannel             (int system, int32 channelid, out int channel);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetMasterChannelGroup  (int system, out int channelgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetMasterSoundGroup    (int system, out int soundgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_AttachChannelGroupToPort  (int system, uint32 portType, uint64 portIndex, int channelgroup, bool passThru);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_DetachChannelGroupFromPort(int system, int channelgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetReverbProperties    (int system, int32 instance, ref REVERB_PROPERTIES prop);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetReverbProperties    (int system, int32 instance, out REVERB_PROPERTIES prop);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_LockDSP                (int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_UnlockDSP              (int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetRecordNumDrivers    (int system, out int32 numdrivers, out int32 numconnected);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetRecordDriverInfo    (int system, int32 id, char8* name, int32 namelen, out Guid guid, out int32 systemrate, out SPEAKERMODE speakermode, out int32 speakermodechannels, out DRIVER_STATE state);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetRecordPosition      (int system, int32 id, out uint32 position);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_RecordStart            (int system, int32 id, int sound, bool loop);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_RecordStop             (int system, int32 id);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_IsRecording            (int system, int32 id, out bool recording);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_CreateGeometry         (int system, int32 maxpolygons, int32 maxvertices, out int geometry);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetGeometrySettings    (int system, float maxworldsize);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetGeometrySettings    (int system, out float maxworldsize);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_LoadGeometry           (int system, int data, int32 datasize, out int geometry);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetGeometryOcclusion   (int system, ref VECTOR listener, ref VECTOR source, out float direct, out float reverb);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetNetworkProxy        (int system, char8* proxy);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetNetworkProxy        (int system, char8* proxy, int32 proxylen);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetNetworkTimeout      (int system, int32 timeout);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetNetworkTimeout      (int system, out int32 timeout);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_SetUserData            (int system, int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_System_GetUserData            (int system, out int userdata);
        #endregion

        #region wrapperinternal

        public this(int raw)
            : base(raw)
        {
        }

        #endregion
    }


    /*
        'Sound' API.
    */
    public class Sound : HandleBase
    {
        public RESULT release                 ()
        {
            RESULT result = FMOD_Sound_Release(rawPtr);
            if (result == RESULT.OK)
            {
                rawPtr = 0;
            }
            return result;
        }
        public RESULT getSystemObject         (out System system)
        {
            system = null;

            int systemraw;
            RESULT result = FMOD_Sound_GetSystemObject(rawPtr, out systemraw);
            system = new System(systemraw);

            return result;
        }

        // Standard sound manipulation functions.
        public RESULT @lock                   (uint32 offset, uint32 length, out int ptr1, out int ptr2, out uint32 len1, out uint32 len2)
        {
            return FMOD_Sound_Lock(rawPtr, offset, length, out ptr1, out ptr2, out len1, out len2);
        }
        public RESULT unlock                  (int ptr1,  int ptr2, uint32 len1, uint32 len2)
        {
            return FMOD_Sound_Unlock(rawPtr, ptr1, ptr2, len1, len2);
        }
        public RESULT setDefaults             (float frequency, int32 priority)
        {
            return FMOD_Sound_SetDefaults(rawPtr, frequency, priority);
        }
        public RESULT getDefaults             (out float frequency, out int32 priority)
        {
            return FMOD_Sound_GetDefaults(rawPtr, out frequency, out priority);
        }
        public RESULT set3DMinMaxDistance     (float min, float max)
        {
            return FMOD_Sound_Set3DMinMaxDistance(rawPtr, min, max);
        }
        public RESULT get3DMinMaxDistance     (out float min, out float max)
        {
            return FMOD_Sound_Get3DMinMaxDistance(rawPtr, out min, out max);
        }
        public RESULT set3DConeSettings       (float insideconeangle, float outsideconeangle, float outsidevolume)
        {
            return FMOD_Sound_Set3DConeSettings(rawPtr, insideconeangle, outsideconeangle, outsidevolume);
        }
        public RESULT get3DConeSettings       (out float insideconeangle, out float outsideconeangle, out float outsidevolume)
        {
            return FMOD_Sound_Get3DConeSettings(rawPtr, out insideconeangle, out outsideconeangle, out outsidevolume);
        }
        public RESULT set3DCustomRolloff      (ref VECTOR points, int32 numpoints)
        {
            return FMOD_Sound_Set3DCustomRolloff(rawPtr, ref points, numpoints);
        }
        public RESULT get3DCustomRolloff      (out int points, out int32 numpoints)
        {
            return FMOD_Sound_Get3DCustomRolloff(rawPtr, out points, out numpoints);
        }
        public RESULT getSubSound             (int32 index, out Sound subsound)
        {
            subsound = null;

            int subsoundraw;
            RESULT result = FMOD_Sound_GetSubSound(rawPtr, index, out subsoundraw);
            subsound = new Sound(subsoundraw);

            return result;
        }
        public RESULT getSubSoundParent(out Sound parentsound)
        {
            parentsound = null;

            int subsoundraw;
            RESULT result = FMOD_Sound_GetSubSoundParent(rawPtr, out subsoundraw);
            parentsound = new Sound(subsoundraw);

            return result;
        }
        public RESULT getName                 (String name, int32 namelen)
        {
            //int stringMem = Marshal.AllocHGlobal(name.Capacity);
			char8* stringMem = scope char8[namelen]*;

            RESULT result = FMOD_Sound_GetName(rawPtr, stringMem, namelen);
			name.Append(stringMem);

            //StringMarshalHelper.NativeToBuilder(name, stringMem);
            //Marshal.FreeHGlobal(stringMem);

            return result;
        }
        public RESULT getLength               (out uint32 length, TIMEUNIT lengthtype)
        {
            return FMOD_Sound_GetLength(rawPtr, out length, lengthtype);
        }
        public RESULT getFormat               (out SOUND_TYPE type, out SOUND_FORMAT format, out int32 channels, out int32 bits)
        {
            return FMOD_Sound_GetFormat(rawPtr, out type, out format, out channels, out bits);
        }
        public RESULT getNumSubSounds         (out int32 numsubsounds)
        {
            return FMOD_Sound_GetNumSubSounds(rawPtr, out numsubsounds);
        }
        public RESULT getNumTags              (out int32 numtags, out int32 numtagsupdated)
        {
            return FMOD_Sound_GetNumTags(rawPtr, out numtags, out numtagsupdated);
        }
        public RESULT getTag                  (String name, int32 index, out TAG tag)
        {
            return FMOD_Sound_GetTag(rawPtr, name, index, out tag);
        }
        public RESULT getOpenState            (out OPENSTATE openstate, out uint32 percentbuffered, out bool starving, out bool diskbusy)
        {
            return FMOD_Sound_GetOpenState(rawPtr, out openstate, out percentbuffered, out starving, out diskbusy);
        }
        public RESULT readData                (int buffer, uint32 lenuint8s, out uint32 read)
        {
            return FMOD_Sound_ReadData(rawPtr, buffer, lenuint8s, out read);
        }
        public RESULT seekData                (uint32 pcm)
        {
            return FMOD_Sound_SeekData(rawPtr, pcm);
        }
        public RESULT setSoundGroup           (SoundGroup soundgroup)
        {
            return FMOD_Sound_SetSoundGroup(rawPtr, soundgroup.getRaw());
        }
        public RESULT getSoundGroup           (out SoundGroup soundgroup)
        {
            soundgroup = null;

            int soundgroupraw;
            RESULT result = FMOD_Sound_GetSoundGroup(rawPtr, out soundgroupraw);
            soundgroup = new SoundGroup(soundgroupraw);

            return result;
        }

        // Synchronization point32 API.  These points can come from markers embedded in wav files, and can also generate channel callbacks.
        public RESULT getNumSyncPoints        (out int32 numsyncpoints)
        {
            return FMOD_Sound_GetNumSyncPoints(rawPtr, out numsyncpoints);
        }
        public RESULT getSyncPoint            (int32 index, out int point)
        {
            return FMOD_Sound_GetSyncPoint(rawPtr, index, out point);
        }
        public RESULT getSyncPointInfo        (int point, String name, int32 namelen, out uint32 offset, TIMEUNIT offsettype)
        {
            //int stringMem = Marshal.AllocHGlobal(name.Capacity);

			char8* stringMem = scope char8[namelen]*;
            RESULT result = FMOD_Sound_GetSyncPointInfo(rawPtr, point, stringMem, namelen, out offset, offsettype);
			name.Append(stringMem);

            //StringMarshalHelper.NativeToBuilder(name, stringMem);
            //Marshal.FreeHGlobal(stringMem);

            return result;
        }
        public RESULT addSyncPoint32            (uint32 offset, TIMEUNIT offsettype, String name, out int point)
        {
            return FMOD_Sound_AddSyncPoint(rawPtr, offset, offsettype, name, out point);
        }
        public RESULT deleteSyncPoint32         (int point)
        {
            return FMOD_Sound_DeleteSyncPoint(rawPtr, point);
        }

        // Functions also in Channel class but here they are the 'default' to save having to change it in Channel all the time.
        public RESULT setMode                 (MODE mode)
        {
            return FMOD_Sound_SetMode(rawPtr, mode);
        }
        public RESULT getMode                 (out MODE mode)
        {
            return FMOD_Sound_GetMode(rawPtr, out mode);
        }
        public RESULT setLoopCount            (int32 loopcount)
        {
            return FMOD_Sound_SetLoopCount(rawPtr, loopcount);
        }
        public RESULT getLoopCount            (out int32 loopcount)
        {
            return FMOD_Sound_GetLoopCount(rawPtr, out loopcount);
        }
        public RESULT setLoopPoints           (uint32 loopstart, TIMEUNIT loopstarttype, uint32 loopend, TIMEUNIT loopendtype)
        {
            return FMOD_Sound_SetLoopPoints(rawPtr, loopstart, loopstarttype, loopend, loopendtype);
        }
        public RESULT getLoopPoints           (out uint32 loopstart, TIMEUNIT loopstarttype, out uint32 loopend, TIMEUNIT loopendtype)
        {
            return FMOD_Sound_GetLoopPoints(rawPtr, out loopstart, loopstarttype, out loopend, loopendtype);
        }

        // For MOD/S3M/XM/IT/MID sequenced formats only.
        public RESULT getMusicNumChannels     (out int32 numchannels)
        {
            return FMOD_Sound_GetMusicNumChannels(rawPtr, out numchannels);
        }
        public RESULT setMusicChannelVolume   (int32 channel, float volume)
        {
            return FMOD_Sound_SetMusicChannelVolume(rawPtr, channel, volume);
        }
        public RESULT getMusicChannelVolume   (int32 channel, out float volume)
        {
            return FMOD_Sound_GetMusicChannelVolume(rawPtr, channel, out volume);
        }
        public RESULT setMusicSpeed(float speed)
        {
            return FMOD_Sound_SetMusicSpeed(rawPtr, speed);
        }
        public RESULT getMusicSpeed(out float speed)
        {
            return FMOD_Sound_GetMusicSpeed(rawPtr, out speed);
        }

        // Userdata set/get.
        public RESULT setUserData             (int userdata)
        {
            return FMOD_Sound_SetUserData(rawPtr, userdata);
        }
        public RESULT getUserData             (out int userdata)
        {
            return FMOD_Sound_GetUserData(rawPtr, out userdata);
        }


        #region importfunctions
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_Release                 (int sound);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetSystemObject         (int sound, out int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_Lock                   (int sound, uint32 offset, uint32 length, out int ptr1, out int ptr2, out uint32 len1, out uint32 len2);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_Unlock                  (int sound, int ptr1,  int ptr2, uint32 len1, uint32 len2);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_SetDefaults             (int sound, float frequency, int32 priority);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetDefaults             (int sound, out float frequency, out int32 priority);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_Set3DMinMaxDistance     (int sound, float min, float max);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_Get3DMinMaxDistance     (int sound, out float min, out float max);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_Set3DConeSettings       (int sound, float insideconeangle, float outsideconeangle, float outsidevolume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_Get3DConeSettings       (int sound, out float insideconeangle, out float outsideconeangle, out float outsidevolume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_Set3DCustomRolloff      (int sound, ref VECTOR points, int32 numpoints);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_Get3DCustomRolloff      (int sound, out int points, out int32 numpoints);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetSubSound             (int sound, int32 index, out int subsound);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetSubSoundParent       (int sound, out int parentsound);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetName                 (int sound, char8* name, int32 namelen);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetLength               (int sound, out uint32 length, TIMEUNIT lengthtype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetFormat               (int sound, out SOUND_TYPE type, out SOUND_FORMAT format, out int32 channels, out int32 bits);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetNumSubSounds         (int sound, out int32 numsubsounds);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetNumTags              (int sound, out int32 numtags, out int32 numtagsupdated);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetTag                  (int sound, char8* name, int32 index, out TAG tag);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetOpenState            (int sound, out OPENSTATE openstate, out uint32 percentbuffered, out bool starving, out bool diskbusy);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_ReadData                (int sound, int buffer, uint32 lenuint8s, out uint32 read);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_SeekData                (int sound, uint32 pcm);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_SetSoundGroup           (int sound, int soundgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetSoundGroup           (int sound, out int soundgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetNumSyncPoints        (int sound, out int32 numsyncpoints);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetSyncPoint            (int sound, int32 index, out int point);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetSyncPointInfo        (int sound, int point, char8* name, int32 namelen, out uint32 offset, TIMEUNIT offsettype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_AddSyncPoint            (int sound, uint32 offset, TIMEUNIT offsettype, char8* name, out int point);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_DeleteSyncPoint         (int sound, int point);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_SetMode                 (int sound, MODE mode);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetMode                 (int sound, out MODE mode);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_SetLoopCount            (int sound, int32 loopcount);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetLoopCount            (int sound, out int32 loopcount);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_SetLoopPoints           (int sound, uint32 loopstart, TIMEUNIT loopstarttype, uint32 loopend, TIMEUNIT loopendtype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetLoopPoints           (int sound, out uint32 loopstart, TIMEUNIT loopstarttype, out uint32 loopend, TIMEUNIT loopendtype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetMusicNumChannels     (int sound, out int32 numchannels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_SetMusicChannelVolume   (int sound, int32 channel, float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetMusicChannelVolume   (int sound, int32 channel, out float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_SetMusicSpeed           (int sound, float speed);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetMusicSpeed           (int sound, out float speed);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_SetUserData             (int sound, int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Sound_GetUserData             (int sound, out int userdata);
        #endregion

        #region wrapperinternal

        public this(int raw)
            : base(raw)
        {
        }

        #endregion
    }


    /*
        'ChannelControl' API
    */
    public class ChannelControl : HandleBase
    {
        public RESULT getSystemObject(out System system)
        {
            system = null;

            int systemraw;
            RESULT result = FMOD_ChannelGroup_GetSystemObject(rawPtr, out systemraw);
            system = new System(systemraw);

            return result;
        }

        // General control functionality for Channels and ChannelGroups.
        public RESULT stop()
        {
            return FMOD_ChannelGroup_Stop(rawPtr);
        }
        public RESULT setPaused(bool paused)
        {
            return FMOD_ChannelGroup_SetPaused(rawPtr, paused);
        }
        public RESULT getPaused(out bool paused)
        {
            return FMOD_ChannelGroup_GetPaused(rawPtr, out paused);
        }
        public RESULT setVolume(float volume)
        {
            return FMOD_ChannelGroup_SetVolume(rawPtr, volume);
        }
        public RESULT getVolume(out float volume)
        {
            return FMOD_ChannelGroup_GetVolume(rawPtr, out volume);
        }
        public RESULT setVolumeRamp(bool ramp)
        {
            return FMOD_ChannelGroup_SetVolumeRamp(rawPtr, ramp);
        }
        public RESULT getVolumeRamp(out bool ramp)
        {
            return FMOD_ChannelGroup_GetVolumeRamp(rawPtr, out ramp);
        }
        public RESULT getAudibility(out float audibility)
        {
            return FMOD_ChannelGroup_GetAudibility(rawPtr, out audibility);
        }
        public RESULT setPitch(float pitch)
        {
            return FMOD_ChannelGroup_SetPitch(rawPtr, pitch);
        }
        public RESULT getPitch(out float pitch)
        {
            return FMOD_ChannelGroup_GetPitch(rawPtr, out pitch);
        }
        public RESULT setMute(bool mute)
        {
            return FMOD_ChannelGroup_SetMute(rawPtr, mute);
        }
        public RESULT getMute(out bool mute)
        {
            return FMOD_ChannelGroup_GetMute(rawPtr, out mute);
        }
        public RESULT setReverbProperties(int32 instance, float wet)
        {
            return FMOD_ChannelGroup_SetReverbProperties(rawPtr, instance, wet);
        }
        public RESULT getReverbProperties(int32 instance, out float wet)
        {
            return FMOD_ChannelGroup_GetReverbProperties(rawPtr, instance, out wet);
        }
        public RESULT setLowPassGain(float gain)
        {
            return FMOD_ChannelGroup_SetLowPassGain(rawPtr, gain);
        }
        public RESULT getLowPassGain(out float gain)
        {
            return FMOD_ChannelGroup_GetLowPassGain(rawPtr, out gain);
        }
        public RESULT setMode(MODE mode)
        {
            return FMOD_ChannelGroup_SetMode(rawPtr, mode);
        }
        public RESULT getMode(out MODE mode)
        {
            return FMOD_ChannelGroup_GetMode(rawPtr, out mode);
        }
        public RESULT setCallback(CHANNEL_CALLBACK callback)
        {
            return FMOD_ChannelGroup_SetCallback(rawPtr, callback);
        }
        public RESULT isPlaying(out bool isplaying)
        {
            return FMOD_ChannelGroup_IsPlaying(rawPtr, out isplaying);
        }

        // Panning and level adjustment.
        public RESULT setPan(float pan)
        {
            return FMOD_ChannelGroup_SetPan(rawPtr, pan);
        }
        public RESULT setMixLevelsOutput(float frontleft, float frontright, float center, float lfe, float surroundleft, float surroundright, float backleft, float backright)
        {
            return FMOD_ChannelGroup_SetMixLevelsOutput(rawPtr, frontleft, frontright, center, lfe,
                surroundleft, surroundright, backleft, backright);
        }
        public RESULT setMixLevelsInput(float[] levels, int32 numlevels)
        {
            return FMOD_ChannelGroup_SetMixLevelsInput(rawPtr, levels, numlevels);
        }
        public RESULT setMixMatrix(float[] matrix, int32 outchannels, int32 inchannels, int32 inchannel_hop)
        {
            return FMOD_ChannelGroup_SetMixMatrix(rawPtr, matrix, outchannels, inchannels, inchannel_hop);
        }
        public RESULT getMixMatrix(float[] matrix, out int32 outchannels, out int32 inchannels, int32 inchannel_hop)
        {
            return FMOD_ChannelGroup_GetMixMatrix(rawPtr, matrix, out outchannels, out inchannels, inchannel_hop);
        }

        // Clock based functionality.
        public RESULT getDSPClock(out uint64 dspclock, out uint64 parentclock)
        {
            return FMOD_ChannelGroup_GetDSPClock(rawPtr, out dspclock, out parentclock);
        }
        public RESULT setDelay(uint64 dspclock_start, uint64 dspclock_end, bool stopchannels)
        {
            return FMOD_ChannelGroup_SetDelay(rawPtr, dspclock_start, dspclock_end, stopchannels);
        }
        public RESULT getDelay(out uint64 dspclock_start, out uint64 dspclock_end, out bool stopchannels)
        {
            return FMOD_ChannelGroup_GetDelay(rawPtr, out dspclock_start, out dspclock_end, out stopchannels);
        }
        public RESULT addFadePoint(uint64 dspclock, float volume)
        {
            return FMOD_ChannelGroup_AddFadePoint(rawPtr, dspclock, volume);
        }
        public RESULT setFadePointRamp(uint64 dspclock, float volume)
        {
            return FMOD_ChannelGroup_SetFadePointRamp(rawPtr, dspclock, volume);
        }
        public RESULT removeFadePoints(uint64 dspclock_start, uint64 dspclock_end)
        {
            return FMOD_ChannelGroup_RemoveFadePoints(rawPtr, dspclock_start, dspclock_end);
        }
        public RESULT getFadePoints(ref uint32 numpoints, uint64[] point_dspclock, float[] point_volume)
        {
            return FMOD_ChannelGroup_GetFadePoints(rawPtr, ref numpoints, point_dspclock, point_volume);
        }

        // DSP effects.
        public RESULT getDSP(int32 index, out DSP dsp)
        {
            dsp = null;

            int dspraw;
            RESULT result = FMOD_ChannelGroup_GetDSP(rawPtr, index, out dspraw);
            dsp = new DSP(dspraw);

            return result;
        }
        public RESULT addDSP(int32 index, DSP dsp)
        {
            return FMOD_ChannelGroup_AddDSP(rawPtr, index, dsp.getRaw());
        }
        public RESULT removeDSP(DSP dsp)
        {
            return FMOD_ChannelGroup_RemoveDSP(rawPtr, dsp.getRaw());
        }
        public RESULT getNumDSPs(out int32 numdsps)
        {
            return FMOD_ChannelGroup_GetNumDSPs(rawPtr, out numdsps);
        }
        public RESULT setDSPIndex(DSP dsp, int32 index)
        {
            return FMOD_ChannelGroup_SetDSPIndex(rawPtr, dsp.getRaw(), index);
        }
        public RESULT getDSPIndex(DSP dsp, out int32 index)
        {
            return FMOD_ChannelGroup_GetDSPIndex(rawPtr, dsp.getRaw(), out index);
        }
        public RESULT overridePanDSP(DSP pan)
        {
            return FMOD_ChannelGroup_OverridePanDSP(rawPtr, pan.getRaw());
        }

        // 3D functionality.
        public RESULT set3DAttributes(ref VECTOR pos, ref VECTOR vel, ref VECTOR alt_pan_pos)
        {
            return FMOD_ChannelGroup_Set3DAttributes(rawPtr, ref pos, ref vel, ref alt_pan_pos);
        }
        public RESULT get3DAttributes(out VECTOR pos, out VECTOR vel, out VECTOR alt_pan_pos)
        {
            return FMOD_ChannelGroup_Get3DAttributes(rawPtr, out pos, out vel, out alt_pan_pos);
        }
        public RESULT set3DMinMaxDistance(float mindistance, float maxdistance)
        {
            return FMOD_ChannelGroup_Set3DMinMaxDistance(rawPtr, mindistance, maxdistance);
        }
        public RESULT get3DMinMaxDistance(out float mindistance, out float maxdistance)
        {
            return FMOD_ChannelGroup_Get3DMinMaxDistance(rawPtr, out mindistance, out maxdistance);
        }
        public RESULT set3DConeSettings(float insideconeangle, float outsideconeangle, float outsidevolume)
        {
            return FMOD_ChannelGroup_Set3DConeSettings(rawPtr, insideconeangle, outsideconeangle, outsidevolume);
        }
        public RESULT get3DConeSettings(out float insideconeangle, out float outsideconeangle, out float outsidevolume)
        {
            return FMOD_ChannelGroup_Get3DConeSettings(rawPtr, out insideconeangle, out outsideconeangle, out outsidevolume);
        }
        public RESULT set3DConeOrientation(ref VECTOR orientation)
        {
            return FMOD_ChannelGroup_Set3DConeOrientation(rawPtr, ref orientation);
        }
        public RESULT get3DConeOrientation(out VECTOR orientation)
        {
            return FMOD_ChannelGroup_Get3DConeOrientation(rawPtr, out orientation);
        }
        public RESULT set3DCustomRolloff(ref VECTOR points, int32 numpoints)
        {
            return FMOD_ChannelGroup_Set3DCustomRolloff(rawPtr, ref points, numpoints);
        }
        public RESULT get3DCustomRolloff(out int points, out int32 numpoints)
        {
            return FMOD_ChannelGroup_Get3DCustomRolloff(rawPtr, out points, out numpoints);
        }
        public RESULT set3DOcclusion(float directocclusion, float reverbocclusion)
        {
            return FMOD_ChannelGroup_Set3DOcclusion(rawPtr, directocclusion, reverbocclusion);
        }
        public RESULT get3DOcclusion(out float directocclusion, out float reverbocclusion)
        {
            return FMOD_ChannelGroup_Get3DOcclusion(rawPtr, out directocclusion, out reverbocclusion);
        }
        public RESULT set3DSpread(float angle)
        {
            return FMOD_ChannelGroup_Set3DSpread(rawPtr, angle);
        }
        public RESULT get3DSpread(out float angle)
        {
            return FMOD_ChannelGroup_Get3DSpread(rawPtr, out angle);
        }
        public RESULT set3DLevel(float level)
        {
            return FMOD_ChannelGroup_Set3DLevel(rawPtr, level);
        }
        public RESULT get3DLevel(out float level)
        {
            return FMOD_ChannelGroup_Get3DLevel(rawPtr, out level);
        }
        public RESULT set3DDopplerLevel(float level)
        {
            return FMOD_ChannelGroup_Set3DDopplerLevel(rawPtr, level);
        }
        public RESULT get3DDopplerLevel(out float level)
        {
            return FMOD_ChannelGroup_Get3DDopplerLevel(rawPtr, out level);
        }
        public RESULT set3DDistanceFilter(bool custom, float customLevel, float centerFreq)
        {
            return FMOD_ChannelGroup_Set3DDistanceFilter(rawPtr, custom, customLevel, centerFreq);
        }
        public RESULT get3DDistanceFilter(out bool custom, out float customLevel, out float centerFreq)
        {
            return FMOD_ChannelGroup_Get3DDistanceFilter(rawPtr, out custom, out customLevel, out centerFreq);
        }

        // Userdata set/get.
        public RESULT setUserData(int userdata)
        {
            return FMOD_ChannelGroup_SetUserData(rawPtr, userdata);
        }
        public RESULT getUserData(out int userdata)
        {
            return FMOD_ChannelGroup_GetUserData(rawPtr, out userdata);
        }

        #region importfunctions

        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Stop(int channelgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetPaused(int channelgroup, bool paused);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetPaused(int channelgroup, out bool paused);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetVolume(int channelgroup, out float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetVolumeRamp(int channelgroup, bool ramp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetVolumeRamp(int channelgroup, out bool ramp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetAudibility(int channelgroup, out float audibility);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetPitch(int channelgroup, float pitch);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetPitch(int channelgroup, out float pitch);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetMute(int channelgroup, bool mute);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetMute(int channelgroup, out bool mute);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetReverbProperties(int channelgroup, int32 instance, float wet);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetReverbProperties(int channelgroup, int32 instance, out float wet);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetLowPassGain(int channelgroup, float gain);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetLowPassGain(int channelgroup, out float gain);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetMode(int channelgroup, MODE mode);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetMode(int channelgroup, out MODE mode);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetCallback(int channelgroup, CHANNEL_CALLBACK callback);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_IsPlaying(int channelgroup, out bool isplaying);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetPan(int channelgroup, float pan);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetMixLevelsOutput(int channelgroup, float frontleft, float frontright, float center, float lfe, float surroundleft, float surroundright, float backleft, float backright);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetMixLevelsInput(int channelgroup, float[] levels, int32 numlevels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetMixMatrix(int channelgroup, float[] matrix, int32 outchannels, int32 inchannels, int32 inchannel_hop);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetMixMatrix(int channelgroup, float[] matrix, out int32 outchannels, out int32 inchannels, int32 inchannel_hop);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetDSPClock(int channelgroup, out uint64 dspclock, out uint64 parentclock);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetDelay(int channelgroup, uint64 dspclock_start, uint64 dspclock_end, bool stopchannels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetDelay(int channelgroup, out uint64 dspclock_start, out uint64 dspclock_end, out bool stopchannels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_AddFadePoint(int channelgroup, uint64 dspclock, float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetFadePointRamp(int channelgroup, uint64 dspclock, float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_RemoveFadePoints(int channelgroup, uint64 dspclock_start, uint64 dspclock_end);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetFadePoints(int channelgroup, ref uint32 numpoints, uint64[] point_dspclock, float[] point_volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DAttributes(int channelgroup, ref VECTOR pos, ref VECTOR vel, ref VECTOR alt_pan_pos);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DAttributes(int channelgroup, out VECTOR pos, out VECTOR vel, out VECTOR alt_pan_pos);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DMinMaxDistance(int channelgroup, float mindistance, float maxdistance);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DMinMaxDistance(int channelgroup, out float mindistance, out float maxdistance);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DConeSettings(int channelgroup, float insideconeangle, float outsideconeangle, float outsidevolume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DConeSettings(int channelgroup, out float insideconeangle, out float outsideconeangle, out float outsidevolume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DConeOrientation(int channelgroup, ref VECTOR orientation);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DConeOrientation(int channelgroup, out VECTOR orientation);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DCustomRolloff(int channelgroup, ref VECTOR points, int32 numpoints);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DCustomRolloff(int channelgroup, out int points, out int32 numpoints);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DOcclusion(int channelgroup, float directocclusion, float reverbocclusion);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DOcclusion(int channelgroup, out float directocclusion, out float reverbocclusion);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DSpread(int channelgroup, float angle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DSpread(int channelgroup, out float angle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DLevel(int channelgroup, float level);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DLevel(int channelgroup, out float level);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DDopplerLevel(int channelgroup, float level);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DDopplerLevel(int channelgroup, out float level);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Set3DDistanceFilter(int channelgroup, bool custom, float customLevel, float centerFreq);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Get3DDistanceFilter(int channelgroup, out bool custom, out float customLevel, out float centerFreq);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetSystemObject(int channelgroup, out int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetVolume(int channelgroup, float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetDSP(int channelgroup, int32 index, out int dsp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_AddDSP(int channelgroup, int32 index, int dsp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_RemoveDSP(int channelgroup, int dsp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetNumDSPs(int channelgroup, out int32 numdsps);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetDSPIndex(int channelgroup, int dsp, int32 index);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetDSPIndex(int channelgroup, int dsp, out int32 index);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_OverridePanDSP(int channelgroup, int pan);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_SetUserData(int channelgroup, int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetUserData(int channelgroup, out int userdata);

        #endregion

        #region wrapperinternal

        protected this(int raw)
            : base(raw)
        {
        }

        #endregion
    }


    /*
        'Channel' API
    */
    public class Channel : ChannelControl
    {
        // Channel specific control functionality.
        public RESULT setFrequency          (float frequency)
        {
            return FMOD_Channel_SetFrequency(getRaw(), frequency);
        }
        public RESULT getFrequency          (out float frequency)
        {
            return FMOD_Channel_GetFrequency(getRaw(), out frequency);
        }
        public RESULT setPriority           (int32 priority)
        {
            return FMOD_Channel_SetPriority(getRaw(), priority);
        }
        public RESULT getPriority           (out int32 priority)
        {
            return FMOD_Channel_GetPriority(getRaw(), out priority);
        }
        public RESULT setPosition           (uint32 position, TIMEUNIT postype)
        {
            return FMOD_Channel_SetPosition(getRaw(), position, postype);
        }
        public RESULT getPosition           (out uint32 position, TIMEUNIT postype)
        {
            return FMOD_Channel_GetPosition(getRaw(), out position, postype);
        }
        public RESULT setChannelGroup       (ChannelGroup channelgroup)
        {
            return FMOD_Channel_SetChannelGroup(getRaw(), channelgroup.getRaw());
        }
        public RESULT getChannelGroup       (out ChannelGroup channelgroup)
        {
            channelgroup = null;

            int channelgroupraw;
            RESULT result = FMOD_Channel_GetChannelGroup(getRaw(), out channelgroupraw);
            channelgroup = new ChannelGroup(channelgroupraw);

            return result;
        }
        public RESULT setLoopCount(int32 loopcount)
        {
            return FMOD_Channel_SetLoopCount(getRaw(), loopcount);
        }
        public RESULT getLoopCount(out int32 loopcount)
        {
            return FMOD_Channel_GetLoopCount(getRaw(), out loopcount);
        }
        public RESULT setLoopPoints(uint32 loopstart, TIMEUNIT loopstarttype, uint32 loopend, TIMEUNIT loopendtype)
        {
            return FMOD_Channel_SetLoopPoints(getRaw(), loopstart, loopstarttype, loopend, loopendtype);
        }
        public RESULT getLoopPoints(out uint32 loopstart, TIMEUNIT loopstarttype, out uint32 loopend, TIMEUNIT loopendtype)
        {
            return FMOD_Channel_GetLoopPoints(getRaw(), out loopstart, loopstarttype, out loopend, loopendtype);
        }

        // Information only functions.
        public RESULT isVirtual             (out bool isvirtual)
        {
            return FMOD_Channel_IsVirtual(getRaw(), out isvirtual);
        }
        public RESULT getCurrentSound       (out Sound sound)
        {
            sound = null;

            int soundraw;
            RESULT result = FMOD_Channel_GetCurrentSound(getRaw(), out soundraw);
            sound = new Sound(soundraw);

            return result;
        }
        public RESULT getIndex              (out int32 index)
        {
            return FMOD_Channel_GetIndex(getRaw(), out index);
        }

        #region importfunctions

        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_SetFrequency          (int channel, float frequency);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetFrequency          (int channel, out float frequency);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_SetPriority           (int channel, int32 priority);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetPriority           (int channel, out int32 priority);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_SetChannelGroup       (int channel, int channelgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetChannelGroup       (int channel, out int channelgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_IsVirtual             (int channel, out bool isvirtual);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetCurrentSound       (int channel, out int sound);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetIndex              (int channel, out int32 index);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_SetPosition           (int channel, uint32 position, TIMEUNIT postype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetPosition           (int channel, out uint32 position, TIMEUNIT postype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_SetMode               (int channel, MODE mode);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetMode               (int channel, out MODE mode);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_SetLoopCount          (int channel, int32 loopcount);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetLoopCount          (int channel, out int32 loopcount);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_SetLoopPoints         (int channel, uint32  loopstart, TIMEUNIT loopstarttype, uint32  loopend, TIMEUNIT loopendtype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetLoopPoints         (int channel, out uint32 loopstart, TIMEUNIT loopstarttype, out uint32 loopend, TIMEUNIT loopendtype);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_SetUserData           (int channel, int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Channel_GetUserData           (int channel, out int userdata);
        #endregion

        #region wrapperinternal

        public this(int raw)
            : base(raw)
        {
        }

        #endregion
    }


    /*
        'ChannelGroup' API
    */
    public class ChannelGroup : ChannelControl
    {
        public RESULT release                ()
        {
            RESULT result = FMOD_ChannelGroup_Release(getRaw());
            if (result == RESULT.OK)
            {
                rawPtr = 0;
            }
            return result;
        }

        // Nested channel groups.
        public RESULT addGroup               (ChannelGroup group, bool propagatedspclock, out DSPConnection connection)
        {
			connection = null;
			
			int connectionRaw;
            RESULT result = FMOD_ChannelGroup_AddGroup(getRaw(), group.getRaw(), propagatedspclock, out connectionRaw);
			connection = new DSPConnection(connectionRaw);
			
			return result;
        }
        public RESULT getNumGroups           (out int32 numgroups)
        {
            return FMOD_ChannelGroup_GetNumGroups(getRaw(), out numgroups);
        }
        public RESULT getGroup               (int32 index, out ChannelGroup group)
        {
            group = null;

            int groupraw;
            RESULT result = FMOD_ChannelGroup_GetGroup(getRaw(), index, out groupraw);
            group = new ChannelGroup(groupraw);

            return result;
        }
        public RESULT getParentGroup         (out ChannelGroup group)
        {
            group = null;

            int groupraw;
            RESULT result = FMOD_ChannelGroup_GetParentGroup(getRaw(), out groupraw);
            group = new ChannelGroup(groupraw);

            return result;
        }

        // Information only functions.
        public RESULT getName                (String name, int32 namelen)
        {
            //int stringMem = Marshal.AllocHGlobal(name.Capacity);

			char8* stringMem = scope char8[namelen]*;
            RESULT result = FMOD_ChannelGroup_GetName(getRaw(), stringMem, namelen);
			name.Append(stringMem);

            //StringMarshalHelper.NativeToBuilder(name, stringMem);
            //Marshal.FreeHGlobal(stringMem);

            return result;
        }
        public RESULT getNumChannels         (out int32 numchannels)
        {
            return FMOD_ChannelGroup_GetNumChannels(getRaw(), out numchannels);
        }
        public RESULT getChannel             (int32 index, out Channel channel)
        {
            channel = null;

            int channelraw;
            RESULT result = FMOD_ChannelGroup_GetChannel(getRaw(), index, out channelraw);
            channel = new Channel(channelraw);

            return result;
        }

        #region importfunctions
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_Release          (int channelgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_AddGroup         (int channelgroup, int group, bool propagatedspclock, out int connection);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetNumGroups     (int channelgroup, out int32 numgroups);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetGroup         (int channelgroup, int32 index, out int group);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetParentGroup   (int channelgroup, out int group);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetName          (int channelgroup, char8* name, int32 namelen);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetNumChannels   (int channelgroup, out int32 numchannels);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_ChannelGroup_GetChannel       (int channelgroup, int32 index, out int channel);
        #endregion

        #region wrapperinternal

        public this(int raw)
            : base(raw)
        {
        }

        #endregion
    }


    /*
        'SoundGroup' API
    */
    public class SoundGroup : HandleBase
    {
        public RESULT release                ()
        {
            RESULT result = FMOD_SoundGroup_Release(getRaw());
            if (result == RESULT.OK)
            {
                rawPtr = 0;
            }
            return result;
        }

        public RESULT getSystemObject        (out System system)
        {
            system = null;

            int systemraw;
            RESULT result = FMOD_SoundGroup_GetSystemObject(rawPtr, out systemraw);
            system = new System(systemraw);

            return result;
        }

        // SoundGroup control functions.
        public RESULT setMaxAudible          (int32 maxaudible)
        {
            return FMOD_SoundGroup_SetMaxAudible(rawPtr, maxaudible);
        }
        public RESULT getMaxAudible          (out int32 maxaudible)
        {
            return FMOD_SoundGroup_GetMaxAudible(rawPtr, out maxaudible);
        }
        public RESULT setMaxAudibleBehavior  (SOUNDGROUP_BEHAVIOR behavior)
        {
            return FMOD_SoundGroup_SetMaxAudibleBehavior(rawPtr, behavior);
        }
        public RESULT getMaxAudibleBehavior  (out SOUNDGROUP_BEHAVIOR behavior)
        {
            return FMOD_SoundGroup_GetMaxAudibleBehavior(rawPtr, out behavior);
        }
        public RESULT setMuteFadeSpeed       (float speed)
        {
            return FMOD_SoundGroup_SetMuteFadeSpeed(rawPtr, speed);
        }
        public RESULT getMuteFadeSpeed       (out float speed)
        {
            return FMOD_SoundGroup_GetMuteFadeSpeed(rawPtr, out speed);
        }
        public RESULT setVolume       (float volume)
        {
            return FMOD_SoundGroup_SetVolume(rawPtr, volume);
        }
        public RESULT getVolume       (out float volume)
        {
            return FMOD_SoundGroup_GetVolume(rawPtr, out volume);
        }
        public RESULT stop       ()
        {
            return FMOD_SoundGroup_Stop(rawPtr);
        }

        // Information only functions.
        public RESULT getName                (String name, int32 namelen)
        {
            //int stringMem = Marshal.AllocHGlobal(name.Capacity);

			char8* stringMem = scope char8[namelen]*;

            RESULT result = FMOD_SoundGroup_GetName(rawPtr, stringMem, namelen);
			name.Append(stringMem);

            //StringMarshalHelper.NativeToBuilder(name, stringMem);
            //Marshal.FreeHGlobal(stringMem);

            return result;
        }
        public RESULT getNumSounds           (out int32 numsounds)
        {
            return FMOD_SoundGroup_GetNumSounds(rawPtr, out numsounds);
        }
        public RESULT getSound               (int32 index, out Sound sound)
        {
            sound = null;

            int soundraw;
            RESULT result = FMOD_SoundGroup_GetSound(rawPtr, index, out soundraw);
            sound = new Sound(soundraw);

            return result;
        }
        public RESULT getNumPlaying          (out int32 numplaying)
        {
            return FMOD_SoundGroup_GetNumPlaying(rawPtr, out numplaying);
        }

        // Userdata set/get.
        public RESULT setUserData            (int userdata)
        {
            return FMOD_SoundGroup_SetUserData(rawPtr, userdata);
        }
        public RESULT getUserData            (out int userdata)
        {
            return FMOD_SoundGroup_GetUserData(rawPtr, out userdata);
        }

        #region importfunctions
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_Release            (int soundgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetSystemObject    (int soundgroup, out int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_SetMaxAudible      (int soundgroup, int32 maxaudible);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetMaxAudible      (int soundgroup, out int32 maxaudible);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_SetMaxAudibleBehavior(int soundgroup, SOUNDGROUP_BEHAVIOR behavior);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetMaxAudibleBehavior(int soundgroup, out SOUNDGROUP_BEHAVIOR behavior);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_SetMuteFadeSpeed   (int soundgroup, float speed);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetMuteFadeSpeed   (int soundgroup, out float speed);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_SetVolume          (int soundgroup, float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetVolume          (int soundgroup, out float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_Stop               (int soundgroup);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetName            (int soundgroup, char8* name, int32 namelen);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetNumSounds       (int soundgroup, out int32 numsounds);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetSound           (int soundgroup, int32 index, out int sound);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetNumPlaying      (int soundgroup, out int32 numplaying);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_SetUserData        (int soundgroup, int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_SoundGroup_GetUserData        (int soundgroup, out int userdata);
        #endregion

        #region wrapperinternal

        public this(int raw)
            : base(raw)
        {
        }

        #endregion
    }


    /*
        'DSP' API
    */
    public class DSP : HandleBase
    {
        public RESULT release                   ()
        {
            RESULT result = FMOD_DSP_Release(getRaw());
            if (result == RESULT.OK)
            {
                rawPtr = 0;
            }
            return result;
        }
        public RESULT getSystemObject           (out System system)
        {
            system = null;

            int systemraw;
            RESULT result = FMOD_DSP_GetSystemObject(rawPtr, out systemraw);
            system = new System(systemraw);

            return result;
        }

        // Connection / disconnection / input and output enumeration.
        public RESULT addInput(DSP target, out DSPConnection connection, DSPCONNECTION_TYPE type)
        {
            connection = null;

            int dspconnectionraw;
            RESULT result = FMOD_DSP_AddInput(rawPtr, target.getRaw(), out dspconnectionraw, type);
            connection = new DSPConnection(dspconnectionraw);

            return result;
        }
        public RESULT disconnectFrom            (DSP target, DSPConnection connection)
        {
            return FMOD_DSP_DisconnectFrom(rawPtr, target.getRaw(), connection.getRaw());
        }
        public RESULT disconnectAll             (bool inputs, bool outputs)
        {
            return FMOD_DSP_DisconnectAll(rawPtr, inputs, outputs);
        }
        public RESULT getNumInputs              (out int32 numinputs)
        {
            return FMOD_DSP_GetNumInputs(rawPtr, out numinputs);
        }
        public RESULT getNumOutputs             (out int32 numoutputs)
        {
            return FMOD_DSP_GetNumOutputs(rawPtr, out numoutputs);
        }
        public RESULT getInput                  (int32 index, out DSP input, out DSPConnection inputconnection)
        {
            input = null;
            inputconnection = null;

            int dspinputraw;
            int dspconnectionraw;
            RESULT result = FMOD_DSP_GetInput(rawPtr, index, out dspinputraw, out dspconnectionraw);
            input = new DSP(dspinputraw);
            inputconnection = new DSPConnection(dspconnectionraw);

            return result;
        }
        public RESULT getOutput                 (int32 index, out DSP output, out DSPConnection outputconnection)
        {
            output = null;
            outputconnection = null;

            int dspoutputraw;
            int dspconnectionraw;
            RESULT result = FMOD_DSP_GetOutput(rawPtr, index, out dspoutputraw, out dspconnectionraw);
            output = new DSP(dspoutputraw);
            outputconnection = new DSPConnection(dspconnectionraw);

            return result;
        }

        // DSP unit control.
        public RESULT setActive                 (bool active)
        {
            return FMOD_DSP_SetActive(rawPtr, active);
        }
        public RESULT getActive                 (out bool active)
        {
            return FMOD_DSP_GetActive(rawPtr, out active);
        }
        public RESULT setBypass(bool bypass)
        {
            return FMOD_DSP_SetBypass(rawPtr, bypass);
        }
        public RESULT getBypass(out bool bypass)
        {
            return FMOD_DSP_GetBypass(rawPtr, out bypass);
        }
        public RESULT setWetDryMix(float prewet, float postwet, float dry)
        {
            return FMOD_DSP_SetWetDryMix(rawPtr, prewet, postwet, dry);
        }
        public RESULT getWetDryMix(out float prewet, out float postwet, out float dry)
        {
            return FMOD_DSP_GetWetDryMix(rawPtr, out prewet, out postwet, out dry);
        }
        public RESULT setChannelFormat(CHANNELMASK channelmask, int32 numchannels, SPEAKERMODE source_speakermode)
        {
            return FMOD_DSP_SetChannelFormat(rawPtr, channelmask, numchannels, source_speakermode);
        }
        public RESULT getChannelFormat(out CHANNELMASK channelmask, out int32 numchannels, out SPEAKERMODE source_speakermode)
        {
            return FMOD_DSP_GetChannelFormat(rawPtr, out channelmask, out numchannels, out source_speakermode);
        }
        public RESULT getOutputChannelFormat(CHANNELMASK inmask, int32 inchannels, SPEAKERMODE inspeakermode, out CHANNELMASK outmask, out int32 outchannels, out SPEAKERMODE outspeakermode)
        {
            return FMOD_DSP_GetOutputChannelFormat(rawPtr, inmask, inchannels, inspeakermode, out outmask, out outchannels, out outspeakermode);
        }
        public RESULT reset                     ()
        {
            return FMOD_DSP_Reset(rawPtr);
        }

        // DSP parameter control.
        public RESULT setParameterFloat(int32 index, float value)
        {
            return FMOD_DSP_SetParameterFloat(rawPtr, index, value);
        }
        public RESULT setParameterInt(int32 index, int32 value)
        {
            return FMOD_DSP_SetParameterInt(rawPtr, index, value);
        }
        public RESULT setParameterBool(int32 index, bool value)
        {
            return FMOD_DSP_SetParameterBool(rawPtr, index, value);
        }
        public RESULT setParameterData(int32 index, uint8[] data)
        {
            return FMOD_DSP_SetParameterData(rawPtr, index, &data[0], (uint32)data.Count);
        }
        public RESULT getParameterFloat(int32 index, out float value)
        {
            int valuestr = 0;
            return FMOD_DSP_GetParameterFloat(rawPtr, index, out value, valuestr, 0);
        }
        public RESULT getParameterInt(int32 index, out int32 value)
        {
            int valuestr = 0;
            return FMOD_DSP_GetParameterInt(rawPtr, index, out value, valuestr, 0);
        }
        public RESULT getParameterBool(int32 index, out bool value)
        {
            return FMOD_DSP_GetParameterBool(rawPtr, index, out value, 0, 0);
        }
        public RESULT getParameterData(int32 index, out int data, out uint32 length)
        {
            return FMOD_DSP_GetParameterData(rawPtr, index, out data, out length, 0, 0);
        }
        public RESULT getNumParameters          (out int32 numparams)
        {
            return FMOD_DSP_GetNumParameters(rawPtr, out numparams);
        }
        public RESULT getParameterInfo          (int32 index, out DSP_PARAMETER_DESC desc)
        {
            int descPtr;
            RESULT result = FMOD_DSP_GetParameterInfo(rawPtr, index, out descPtr);
            if (result == RESULT.OK)
            {
                desc = *(DSP_PARAMETER_DESC*)(void*)descPtr;
            }
            else
            {
                desc = DSP_PARAMETER_DESC();
            }
            return result;
        }
        public RESULT getDataParameterIndex(int32 datatype, out int32 index)
        {
            return FMOD_DSP_GetDataParameterIndex     (rawPtr, datatype, out index);
        }
        public RESULT showConfigDialog          (int hwnd, bool show)
        {
            return FMOD_DSP_ShowConfigDialog          (rawPtr, hwnd, show);
        }

        //  DSP attributes.
        public RESULT getInfo                   (String name, out uint32 version, out int32 channels, out int32 configwidth, out int32 configheight)
        {
            //int nameMem = Marshal.AllocHGlobal(32);
			char8* nameMem = new char8[32]*;
            RESULT result = FMOD_DSP_GetInfo(rawPtr, nameMem, out version, out channels, out configwidth, out configheight);
			name.Append(nameMem);
            //StringMarshalHelper.NativeToBuilder(name, nameMem);
            //Marshal.FreeHGlobal(nameMem);
            return result;
        }
        public RESULT getType                   (out DSP_TYPE type)
        {
            return FMOD_DSP_GetType(rawPtr, out type);
        }
        public RESULT getIdle                   (out bool idle)
        {
            return FMOD_DSP_GetIdle(rawPtr, out idle);
        }

        // Userdata set/get.
        public RESULT setUserData               (int userdata)
        {
            return FMOD_DSP_SetUserData(rawPtr, userdata);
        }
        public RESULT getUserData               (out int userdata)
        {
            return FMOD_DSP_GetUserData(rawPtr, out userdata);
        }

        // Metering.
        public RESULT setMeteringEnabled(bool inputEnabled, bool outputEnabled)
        {
            return FMOD_DSP_SetMeteringEnabled(rawPtr, inputEnabled, outputEnabled);
        }
        public RESULT getMeteringEnabled(out bool inputEnabled, out bool outputEnabled)
        {
            return FMOD_DSP_GetMeteringEnabled(rawPtr, out inputEnabled, out outputEnabled);
        }

        public RESULT getMeteringInfo(DSP_METERING_INFO inputInfo, DSP_METERING_INFO outputInfo)
        {
            return FMOD_DSP_GetMeteringInfo(rawPtr, inputInfo, outputInfo);
        }

        #region importfunctions

        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_Release                   (int dsp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetSystemObject           (int dsp, out int system);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_AddInput                  (int dsp, int target, out int connection, DSPCONNECTION_TYPE type);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_DisconnectFrom            (int dsp, int target, int connection);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_DisconnectAll             (int dsp, bool inputs, bool outputs);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetNumInputs              (int dsp, out int32 numinputs);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetNumOutputs             (int dsp, out int32 numoutputs);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetInput                  (int dsp, int32 index, out int input, out int inputconnection);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetOutput                 (int dsp, int32 index, out int output, out int outputconnection);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_SetActive                 (int dsp, bool active);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetActive                 (int dsp, out bool active);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_SetBypass                 (int dsp, bool bypass);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetBypass                 (int dsp, out bool bypass);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_SetWetDryMix              (int dsp, float prewet, float postwet, float dry);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetWetDryMix              (int dsp, out float prewet, out float postwet, out float dry);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_SetChannelFormat          (int dsp, CHANNELMASK channelmask, int32 numchannels, SPEAKERMODE source_speakermode);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetChannelFormat          (int dsp, out CHANNELMASK channelmask, out int32 numchannels, out SPEAKERMODE source_speakermode);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetOutputChannelFormat    (int dsp, CHANNELMASK inmask, int32 inchannels, SPEAKERMODE inspeakermode, out CHANNELMASK outmask, out int32 outchannels, out SPEAKERMODE outspeakermode);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_Reset                     (int dsp);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_SetParameterFloat         (int dsp, int32 index, float value);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_SetParameterInt           (int dsp, int32 index, int32 value);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_SetParameterBool          (int dsp, int32 index, bool value);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_SetParameterData          (int dsp, int32 index, uint8* data, uint32 length);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetParameterFloat         (int dsp, int32 index, out float value, int valuestr, int32 valuestrlen);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetParameterInt           (int dsp, int32 index, out int32 value, int valuestr, int32 valuestrlen);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetParameterBool          (int dsp, int32 index, out bool value, int valuestr, int32 valuestrlen);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetParameterData          (int dsp, int32 index, out int data, out uint32 length, int valuestr, int32 valuestrlen);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetNumParameters          (int dsp, out int32 numparams);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetParameterInfo          (int dsp, int32 index, out int desc);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetDataParameterIndex     (int dsp, int32 datatype, out int32 index);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_ShowConfigDialog          (int dsp, int hwnd, bool show);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetInfo                   (int dsp, char8* name, out uint32 version, out int32 channels, out int32 configwidth, out int32 configheight);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetType                   (int dsp, out DSP_TYPE type);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetIdle                   (int dsp, out bool idle);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_SetUserData               (int dsp, int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSP_GetUserData               (int dsp, out int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        public static extern RESULT FMOD_DSP_SetMeteringEnabled         (int dsp, bool inputEnabled, bool outputEnabled);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        public static extern RESULT FMOD_DSP_GetMeteringEnabled         (int dsp, out bool inputEnabled, out bool outputEnabled);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        public static extern RESULT FMOD_DSP_GetMeteringInfo            (int dsp, DSP_METERING_INFO inputInfo, DSP_METERING_INFO outputInfo);
        #endregion

        #region wrapperinternal

        public this(int raw)
            : base(raw)
        {
        }

        #endregion
    }


    /*
        'DSPConnection' API
    */
    public class DSPConnection : HandleBase
    {
        public RESULT getInput              (out DSP input)
        {
            input = null;

            int dspraw;
            RESULT result = FMOD_DSPConnection_GetInput(rawPtr, out dspraw);
            input = new DSP(dspraw);

            return result;
        }
        public RESULT getOutput             (out DSP output)
        {
            output = null;

            int dspraw;
            RESULT result = FMOD_DSPConnection_GetOutput(rawPtr, out dspraw);
            output = new DSP(dspraw);

            return result;
        }
        public RESULT setMix                (float volume)
        {
            return FMOD_DSPConnection_SetMix(rawPtr, volume);
        }
        public RESULT getMix                (out float volume)
        {
            return FMOD_DSPConnection_GetMix(rawPtr, out volume);
        }
        public RESULT setMixMatrix(float[] matrix, int32 outchannels, int32 inchannels, int32 inchannel_hop)
        {
            return FMOD_DSPConnection_SetMixMatrix(rawPtr, matrix, outchannels, inchannels, inchannel_hop);
        }
        public RESULT getMixMatrix(float[] matrix, out int32 outchannels, out int32 inchannels, int32 inchannel_hop)
        {
            return FMOD_DSPConnection_GetMixMatrix(rawPtr, matrix, out outchannels, out inchannels, inchannel_hop);
        }
        public RESULT getType(out DSPCONNECTION_TYPE type)
        {
            return FMOD_DSPConnection_GetType(rawPtr, out type);
        }

        // Userdata set/get.
        public RESULT setUserData(int userdata)
        {
            return FMOD_DSPConnection_SetUserData(rawPtr, userdata);
        }
        public RESULT getUserData(out int userdata)
        {
            return FMOD_DSPConnection_GetUserData(rawPtr, out userdata);
        }

        #region importfunctions
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSPConnection_GetInput        (int dspconnection, out int input);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSPConnection_GetOutput       (int dspconnection, out int output);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSPConnection_SetMix          (int dspconnection, float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSPConnection_GetMix          (int dspconnection, out float volume);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSPConnection_SetMixMatrix    (int dspconnection, float[] matrix, int32 outchannels, int32 inchannels, int32 inchannel_hop);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSPConnection_GetMixMatrix    (int dspconnection, float[] matrix, out int32 outchannels, out int32 inchannels, int32 inchannel_hop);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSPConnection_GetType         (int dspconnection, out DSPCONNECTION_TYPE type);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSPConnection_SetUserData     (int dspconnection, int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_DSPConnection_GetUserData     (int dspconnection, out int userdata);
        #endregion

        #region wrapperinternal

        public this(int raw)
            : base(raw)
        {
        }

        #endregion
    }

    /*
        'Geometry' API
    */
    public class Geometry : HandleBase
    {
        public RESULT release               ()
        {
            RESULT result = FMOD_Geometry_Release(getRaw());
            if (result == RESULT.OK)
            {
                rawPtr = 0;
            }
            return result;
        }

        // Polygon manipulation.
        public RESULT addPolygon            (float directocclusion, float reverbocclusion, bool doublesided, int32 numvertices, VECTOR[] vertices, out int32 polygonindex)
        {
            return FMOD_Geometry_AddPolygon(rawPtr, directocclusion, reverbocclusion, doublesided, numvertices, vertices, out polygonindex);
        }
        public RESULT getNumPolygons        (out int32 numpolygons)
        {
            return FMOD_Geometry_GetNumPolygons(rawPtr, out numpolygons);
        }
        public RESULT getMaxPolygons        (out int32 maxpolygons, out int32 maxvertices)
        {
            return FMOD_Geometry_GetMaxPolygons(rawPtr, out maxpolygons, out maxvertices);
        }
        public RESULT getPolygonNumVertices (int32 index, out int32 numvertices)
        {
            return FMOD_Geometry_GetPolygonNumVertices(rawPtr, index, out numvertices);
        }
        public RESULT setPolygonVertex      (int32 index, int32 vertexindex, ref VECTOR vertex)
        {
            return FMOD_Geometry_SetPolygonVertex(rawPtr, index, vertexindex, ref vertex);
        }
        public RESULT getPolygonVertex      (int32 index, int32 vertexindex, out VECTOR vertex)
        {
            return FMOD_Geometry_GetPolygonVertex(rawPtr, index, vertexindex, out vertex);
        }
        public RESULT setPolygonAttributes  (int32 index, float directocclusion, float reverbocclusion, bool doublesided)
        {
            return FMOD_Geometry_SetPolygonAttributes(rawPtr, index, directocclusion, reverbocclusion, doublesided);
        }
        public RESULT getPolygonAttributes  (int32 index, out float directocclusion, out float reverbocclusion, out bool doublesided)
        {
            return FMOD_Geometry_GetPolygonAttributes(rawPtr, index, out directocclusion, out reverbocclusion, out doublesided);
        }

        // Object manipulation.
        public RESULT setActive             (bool active)
        {
            return FMOD_Geometry_SetActive(rawPtr, active);
        }
        public RESULT getActive             (out bool active)
        {
            return FMOD_Geometry_GetActive(rawPtr, out active);
        }
        public RESULT setRotation           (ref VECTOR forward, ref VECTOR up)
        {
            return FMOD_Geometry_SetRotation(rawPtr, ref forward, ref up);
        }
        public RESULT getRotation           (out VECTOR forward, out VECTOR up)
        {
            return FMOD_Geometry_GetRotation(rawPtr, out forward, out up);
        }
        public RESULT setPosition           (ref VECTOR position)
        {
            return FMOD_Geometry_SetPosition(rawPtr, ref position);
        }
        public RESULT getPosition           (out VECTOR position)
        {
            return FMOD_Geometry_GetPosition(rawPtr, out position);
        }
        public RESULT setScale              (ref VECTOR scale)
        {
            return FMOD_Geometry_SetScale(rawPtr, ref scale);
        }
        public RESULT getScale              (out VECTOR scale)
        {
            return FMOD_Geometry_GetScale(rawPtr, out scale);
        }
        public RESULT save                  (int data, out int32 datasize)
        {
            return FMOD_Geometry_Save(rawPtr, data, out datasize);
        }

        // Userdata set/get.
        public RESULT setUserData               (int userdata)
        {
            return FMOD_Geometry_SetUserData(rawPtr, userdata);
        }
        public RESULT getUserData               (out int userdata)
        {
            return FMOD_Geometry_GetUserData(rawPtr, out userdata);
        }

        #region importfunctions
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_Release              (int geometry);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_AddPolygon           (int geometry, float directocclusion, float reverbocclusion, bool doublesided, int32 numvertices, VECTOR[] vertices, out int32 polygonindex);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetNumPolygons       (int geometry, out int32 numpolygons);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetMaxPolygons       (int geometry, out int32 maxpolygons, out int32 maxvertices);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetPolygonNumVertices(int geometry, int32 index, out int32 numvertices);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_SetPolygonVertex     (int geometry, int32 index, int32 vertexindex, ref VECTOR vertex);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetPolygonVertex     (int geometry, int32 index, int32 vertexindex, out VECTOR vertex);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_SetPolygonAttributes (int geometry, int32 index, float directocclusion, float reverbocclusion, bool doublesided);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetPolygonAttributes (int geometry, int32 index, out float directocclusion, out float reverbocclusion, out bool doublesided);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_SetActive            (int geometry, bool active);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetActive            (int geometry, out bool active);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_SetRotation          (int geometry, ref VECTOR forward, ref VECTOR up);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetRotation          (int geometry, out VECTOR forward, out VECTOR up);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_SetPosition          (int geometry, ref VECTOR position);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetPosition          (int geometry, out VECTOR position);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_SetScale             (int geometry, ref VECTOR scale);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetScale             (int geometry, out VECTOR scale);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_Save                 (int geometry, int data, out int32 datasize);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_SetUserData          (int geometry, int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Geometry_GetUserData          (int geometry, out int userdata);
        #endregion

        #region wrapperinternal

        public this(int raw)
            : base(raw)
        {
        }

        #endregion
    }


    /*
        'Reverb3D' API
    */
    public class Reverb3D : HandleBase
    {
        public RESULT release()
        {
            RESULT result = FMOD_Reverb3D_Release(getRaw());
            if (result == RESULT.OK)
            {
                rawPtr = 0;
            }
            return result;
        }

        // Reverb manipulation.
        public RESULT set3DAttributes(ref VECTOR position, float mindistance, float maxdistance)
        {
            return FMOD_Reverb3D_Set3DAttributes(rawPtr, ref position, mindistance, maxdistance);
        }
        public RESULT get3DAttributes(ref VECTOR position, ref float mindistance, ref float maxdistance)
        {
            return FMOD_Reverb3D_Get3DAttributes(rawPtr, ref position, ref mindistance, ref maxdistance);
        }
        public RESULT setProperties(ref REVERB_PROPERTIES properties)
        {
            return FMOD_Reverb3D_SetProperties(rawPtr, ref properties);
        }
        public RESULT getProperties(ref REVERB_PROPERTIES properties)
        {
            return FMOD_Reverb3D_GetProperties(rawPtr, ref properties);
        }
        public RESULT setActive(bool active)
        {
            return FMOD_Reverb3D_SetActive(rawPtr, active);
        }
        public RESULT getActive(out bool active)
        {
            return FMOD_Reverb3D_GetActive(rawPtr, out active);
        }

        // Userdata set/get.
        public RESULT setUserData(int userdata)
        {
            return FMOD_Reverb3D_SetUserData(rawPtr, userdata);
        }
        public RESULT getUserData(out int userdata)
        {
            return FMOD_Reverb3D_GetUserData(rawPtr, out userdata);
        }

        #region importfunctions
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Reverb3D_Release(int reverb);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Reverb3D_Set3DAttributes(int reverb, ref VECTOR position, float mindistance, float maxdistance);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Reverb3D_Get3DAttributes(int reverb, ref VECTOR position, ref float mindistance, ref float maxdistance);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Reverb3D_SetProperties(int reverb, ref REVERB_PROPERTIES properties);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Reverb3D_GetProperties(int reverb, ref REVERB_PROPERTIES properties);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Reverb3D_SetActive(int reverb, bool active);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Reverb3D_GetActive(int reverb, out bool active);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Reverb3D_SetUserData(int reverb, int userdata);
        [Import(VERSION.dll), CLink, CallingConvention(.Stdcall)]
        private static extern RESULT FMOD_Reverb3D_GetUserData(int reverb, out int userdata);
        #endregion

        #region wrapperinternal

        public this(int raw)
            : base(raw)
        {
        }

        #endregion
    }

    class StringMarshalHelper
    {
        /*static internal void NativeToBuilder(String builder, int nativeMem)
        {
            uint8[] uint8s = new uint8[builder.Capacity];
            Marshal.Copy(nativeMem, uint8s, 0, builder.Capacity);
			int32 strlen = Array.IndexOf(uint8s, (uint8)0);
			if (strlen > 0)
			{
				String str = Encoding.UTF8.GetString(uint8s, 0, strlen);
				builder.Append(str);
			}
        }*/
    }
}
