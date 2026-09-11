using System;
using System.IO;

namespace FMOD;

public struct DspCaptureInfo
{
    public uint64 frames, firstDSPClock;
    public uint32 sampleRate, channels;
    public int32 complete, formatChanged;
    public float peak, rms;
    public uint64 clippedSamples;
}

public class DspCapture
{
    struct CaptureState
    {
        public DspCaptureInfo info;
        public float* samples;
        public uint64 limit;
        public bool recording, haveSignal;
    }

    FMOD.System mSystem;
    ChannelGroup mGroup;
    DSP mDSP;
    CaptureState* mState;
    bool mAttached;
    DspCaptureInfo mLastInfo;

    public bool Active => mState != null;
    public ~this() { Cancel(); }

    // The system and group must remain alive until Stop or Cancel completes.
    public RESULT Start(FMOD.System system, ChannelGroup group, int32 seconds)
    {
        if (Active) return .ERR_ALREADY_LOCKED;
        if ((system == null) || (group == null) || (seconds < 1) || (seconds > 30)) return .ERR_INVALID_PARAM;
        int32 rate, raw;
        SPEAKERMODE mode;
        var result = system.getSoftwareFormat(out rate, out mode, out raw);
        if (result != .OK) return result;
        let limit = (uint64)rate * (uint64)seconds;
        if ((rate <= 0) || (limit * 8 * sizeof(float) > 128 * 1024 * 1024)) return .ERR_INVALID_PARAM;
        mSystem = system;
        mGroup = group;
        mLastInfo = default;
        mState = new CaptureState();
        mState.info.sampleRate = (uint32)rate;
        mState.limit = limit;
        mState.recording = true;
        mState.samples = new float[(int)limit * 8]*;

        DSP_DESCRIPTION description = default;
        description.pluginsdkversion = DSP_DESCRIPTION.PLUGIN_SDK_VERSION;
        let name = "FMOD Beef Capture";
        Internal.MemCpy(&description.name, name.Ptr, name.Length);
        description.numinputbuffers = description.numoutputbuffers = 1;
        description.create = => Create;
        description.read = => Read;
        description.shouldiprocess = => ShouldProcess;
        description.userdata = (int)(void*)mState;
        result = system.createDSP(ref description, out mDSP);
        if (result == .OK)
        {
            result = group.addDSP(CHANNELCONTROL_DSP_INDEX.HEAD, mDSP);
            mAttached = result == .OK;
        }
        if (result != .OK) Cancel();
        return result;
    }

    public RESULT GetInfo(out DspCaptureInfo info)
    {
        info = mLastInfo;
        if (!Active) return .OK;
        let result = mSystem.lockDSP();
        if (result != .OK) return result;
        info = mState.info;
        return mSystem.unlockDSP();
    }

    RESULT Detach()
    {
        if (mDSP == null) return .OK;
        var result = mSystem.lockDSP();
        if (result != .OK) return result;
        mState.recording = false;
        result = mSystem.unlockDSP();
        if (result != .OK) return result;
        if (mAttached)
        {
            result = mGroup.removeDSP(mDSP);
            if (result != .OK) return result;
            mAttached = false;
        }
        result = mDSP.release();
        if (result != .OK) return result;
        delete mDSP;
        mDSP = null;
        return .OK;
    }

    void FreeState()
    {
        if (mState != null)
        {
            delete mState.samples;
            delete mState;
            mState = null;
        }
        mSystem = null;
        mGroup = null;
    }

    public RESULT Cancel()
    {
        let result = Detach();
        if (result != .OK) return result;
        FreeState();
        return .OK;
    }

    public RESULT Stop(StringView path, out DspCaptureInfo info)
    {
        info = mLastInfo;
        if (!Active) return .OK;
        let result = Detach();
        if (result != .OK) return result;
        mLastInfo = mState.info;
        RESULT status = .OK;
        if (mLastInfo.channels == 0) status = .ERR_NOTREADY;
        else if (WriteWav(path) case .Err) status = .ERR_FILE_BAD;
        info = mLastInfo;
        FreeState();
        return status;
    }

    Result<void> WriteWav(StringView path)
    {
        let file = scope FileStream();
        Try!(file.Create(path));
        var info = ref mLastInfo;
        let sampleCount = (int)(info.frames * info.channels);
        let bytes = (uint32)sampleCount * 2;
        Try!(file.Write("RIFF"));
        Try!(file.Write(bytes + 36));
        Try!(file.Write("WAVEfmt "));
        Try!(file.Write((uint32)16));
        Try!(file.Write((uint16)1));
        Try!(file.Write((uint16)info.channels));
        Try!(file.Write(info.sampleRate));
        Try!(file.Write(info.sampleRate * info.channels * 2));
        Try!(file.Write((uint16)(info.channels * 2)));
        Try!(file.Write((uint16)16));
        Try!(file.Write("data"));
        Try!(file.Write(bytes));
        double squares = 0;
        int16[4096] pcm = ?;
        for (int at = 0; at < sampleCount;)
        {
            let count = Math.Min(4096, sampleCount - at);
            for (int i = 0; i < count; i++)
            {
                var sample = mState.samples[at + i];
                if (!sample.IsFinite) sample = 0;
                info.peak = Math.Max(info.peak, Math.Abs(sample));
                squares += (double)sample * sample;
                if (Math.Abs(sample) >= 1) info.clippedSamples++;
                pcm[i] = (int16)Math.Round(Math.Clamp(sample, -1.0f, 1.0f) * 32767);
            }
            Try!(file.Write(Span<int16>(&pcm[0], count)));
            at += count;
        }
        info.rms = sampleCount > 0 ? (float)Math.Sqrt(squares / sampleCount) : 0;
        Try!(file.Flush());
        return file.Close();
    }

    [CallingConvention(.Stdcall)]
    static RESULT Create(ref DSP_STATE state)
    {
        return state.callbacks.getuserdata(ref state, out state.plugindata);
    }

    [CallingConvention(.Stdcall)]
    static RESULT ShouldProcess(ref DSP_STATE state, int32 idle, uint32 frames, CHANNELMASK mask, int32 channels, SPEAKERMODE mode)
    {
        return .OK;
    }

    [CallingConvention(.Stdcall)]
    static RESULT Read(ref DSP_STATE state, int inputBuffer, int outputBuffer, uint32 frames, int32 channels, ref int32 outputChannels)
    {
        outputChannels = channels;
        let input = (float*)(void*)inputBuffer;
        let output = (float*)(void*)outputBuffer;
        if ((channels > 0) && (input != output))
            Internal.MemCpy(output, input, (int)frames * channels * sizeof(float));
        let capture = (CaptureState*)(void*)state.plugindata;
        if ((!capture.recording) || (channels <= 0)) return .OK;
        var info = ref capture.info;
        bool silent = true;
        for (int i = 0; i < (int)frames * channels; i++)
            if (input[i] != 0) { silent = false; break; }
        if ((channels > 8) || ((!silent) && (capture.haveSignal) && (info.channels != (uint32)channels)))
        {
            info.formatChanged = info.complete = 1;
            capture.recording = false;
            return .OK;
        }
        // Idle buses can collapse to mono; zero frames fit any eventual channel layout.
        if ((info.channels == 0) || ((!silent) && (!capture.haveSignal))) info.channels = (uint32)channels;
        if (info.frames == 0)
        {
            uint32 offset, length;
            state.callbacks.getclock(ref state, out info.firstDSPClock, out offset, out length);
        }
        let take = Math.Min((uint64)frames, capture.limit - info.frames);
        let destination = capture.samples + (int)(info.frames * info.channels);
        if (silent) Internal.MemSet(destination, 0, (int)(take * info.channels) * sizeof(float));
        else Internal.MemCpy(destination, input, (int)take * channels * sizeof(float));
        capture.haveSignal |= !silent;
        info.frames += take;
        if (info.frames == capture.limit)
        {
            info.complete = 1;
            capture.recording = false;
        }
        return .OK;
    }
}
