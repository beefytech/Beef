using System;
using System.Collections;
using System.Text;
using System.Diagnostics;
using System.IO;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.theme;
using Beefy.theme.dark;
using Beefy.utils;
using Beefy.res;
using Beefy.geom;
using System.Threading;
using Beefy.sound;
#if MONOTOUCH
using MonoTouch;
#endif

#if STUDIO_CLIENT || STUDIO_HOST
using Beefy.ipc;
#endif

namespace Beefy
{   
    public enum Cursor
    {
	    Pointer,
	    Hand,
	    Dragging,
	    Text,
	    CircleSlash,
	    Sizeall,
	    SizeNESW,
	    SizeNS,
	    SizeNWSE,
	    SizeWE,	
	    Wait,
	    None,	
	    COUNT
    }

    public class BFApp
#if STUDIO_CLIENT
        : IStudioClient
#endif
    {
        public delegate void UpdateDelegate(bool batchStart);
		public delegate void UpdateFDelegate(float updatePct);
        public delegate void DrawDelegate(bool forceDraw);

        public static BFApp sApp;
        public int32 mUpdateCnt;
        public bool mIsUpdateBatchStart;
		public bool mAutoDirty = true;
        public Graphics mGraphics ~ delete _;
        int32 mRefreshRate = 60;
        public String mInstallDir ~ delete _;
        public String mUserDataDir ~ delete _;
        public List<BFWindow> mWindows = new List<BFWindow>() ~ delete _;
		List<Object> mDeferredDeletes = new List<Object>() ~ delete _;
		public bool mShuttingDown = false;
		public bool mStopping;
		public bool mStarted;

        public ResourceManager mResourceManager ~ delete _;
		public SoundManager mSoundManager = new SoundManager() ~ delete _;

        public int32 mFPSDrawCount;
        public int32 mFPSUpdateCount;
        public int32 mLastFPS;
        public uint32 mLastFPSUpdateCnt;

		public Matrix4? mColorMatrix;
		public ConstantDataDefinition mColorMatrixDataDef = new ConstantDataDefinition(16, new ConstantDataDefinition.DataType[] ( ConstantDataDefinition.DataType.Matrix | ConstantDataDefinition.DataType.PixelShaderUsage )) ~ delete _;

		[CallingConvention(.Stdcall), CLink]
		static extern void Lib_Startup(int32 argc, char8** argv, void* startupCallback);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_GetDesktopResolution(out int32 width, out int32 height);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_GetWorkspaceRect(out int32 x, out int32 y, out int32 width, out int32 height);

		[CallingConvention(.Stdcall), CLink]
		static extern void BFApp_GetWorkspaceRectFrom(int32 x, int32 y, int32 width, int32 height, out int32 outX, out int32 outY, out int32 outWidth, out int32 outHeight);

		[CallingConvention(.Stdcall), CLink]
		static extern void BFApp_SetOptionString(char8* name, char8* value);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_Create();

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_Delete();

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_SetRefreshRate(int32 rate);

        [CallingConvention(.Stdcall), CLink]
        public static extern void BFApp_SetDrawEnabled(int32 enabled);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_Init();

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_Run();

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_Shutdown();

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_SetCallbacks(void* updateDelegate, void* updateFDelegate, void* drawDelegate);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* BFApp_GetInstallDir();

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_SetCursor(int32 cursor);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BFApp_GetClipboardData(char8* format, out int32 size);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_ReleaseClipboardData(void* ptr);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFApp_SetClipboardData(char8* format, void* ptr, int32 size, int32 resetClipboard);

		[CallingConvention(.Stdcall), CLink]
		public static extern void BFApp_CheckMemory();

		[CallingConvention(.Stdcall), CLink]
		public static extern void BFApp_RehupMouse();

		[CallingConvention(.Stdcall), CLink]
		public static extern void* BFApp_GetSoundManager();

		[CallingConvention(.Stdcall), CLink]
		public static extern int BFApp_GetCriticalThreadId(int32 idx);

        UpdateDelegate mUpdateDelegate ~ delete _;
		UpdateFDelegate mUpdateFDelegate ~ delete _;
        DrawDelegate mDrawDelegate ~ delete _;
		
#if STUDIO_CLIENT
        public bool mTrackingDraw = false;
        public IPCClientManager mIPCClientManager;
        public IPCProxy<IStudioHost> mStudioHost;
        public static IStudioHost StudioHostProxy
        {
            get
            {
                return sApp.mStudioHost.Proxy;
            }            
        }    
#endif

#if STUDIO_CLIENT        
        public IPCEndpoint<IStudioClient> mStudioClient;         
#endif

#if MONOTOUCH
		[MonoPInvokeCallback(typeof(DrawDelegate))]
		static void Static_Draw()
		{
			sApp.Draw();
		}
		[MonoPInvokeCallback(typeof(UpdateDelegate))]
		static void Static_Update()
		{
			sApp.Update();
		}
#endif
		public static void SetOption(StringView name, StringView value)
		{
			BFApp_SetOptionString(name.ToScopeCStr!(), value.ToScopeCStr!());
		}
        
        static void Static_Draw(bool forceDraw)
        {
            sApp.Draw(forceDraw);
        }
        
        static void Static_Update(bool batchStart)
        {
            sApp.Update(batchStart);
        }

		static void Static_UpdateF(float updatePct)
		{
		    sApp.UpdateF(updatePct);
		}

        float mLastUpdateDelta; // In seconds

        public this()
        {
            Utils.GetTickCountMicro();

			//mColorMatrix = Matrix4.Identity;
			/*mColorMatrix = Matrix4(
	            1, 1, 1, 1,
	            1, 1, 1, 1,
	            1, 1, 1, 1,
	            1, 1, 1, 1);*/

            sApp = this;
            BFApp_Create();
#if STUDIO_CLIENT
            BFApp_SetRefreshRate(0); // Let studio dictate updating/drawing
            mUpdateDelegate = LocalUpdate;
            mDrawDelegate = LocalDraw;
#else
            BFApp_SetRefreshRate(mRefreshRate);
			
			mUpdateDelegate = new => Static_Update;
			mUpdateFDelegate = new => Static_UpdateF;
			mDrawDelegate = new => Static_Draw;
#endif
            BFApp_SetCallbacks(mUpdateDelegate.GetFuncPtr(), mUpdateFDelegate.GetFuncPtr(),  mDrawDelegate.GetFuncPtr());
        }

#if STUDIO_CLIENT
        int mStudioUpdateCnt = 0;
        int mCount = 0;
        uint mLocalUpdateTick;
        public virtual void LocalUpdate()
        {
            mLocalUpdateTick = Utils.GetTickCount();

            if (mHostIsPerfRecording)
            {
                PerfTimer.Log("HostPerfRecording Done at: {0}", Utils.GetTickCount());

                PerfTimer.StopRecording();
                PerfTimer.DbgPrint();
                mHostIsPerfRecording = false;
            }

            IPCClientManager.sIPCClientManager.Update();

            if (mHostIsPerfRecording)            
                PerfTimer.Log("Update Done at: {0}", Utils.GetTickCount());

            IPCClientManager.sIPCClientManager.RemoteSyncToRead();            

            if (mHostIsPerfRecording)
                PerfTimer.Log("RemoteSyncToRead Done at: {0}", Utils.GetTickCount());

            mStudioUpdateCnt++;
            mStudioHost.Proxy.ClientUpdated(mStudioUpdateCnt);

            if (mHostIsPerfRecording)
                PerfTimer.Log("ClientUpdated Done at: {0}", Utils.GetTickCount());

            mCount++;            
        }

        public virtual void LocalDraw()
        {
        }

        public void TrackDraw()
        {
            if (!mTrackingDraw)
                return;            

            StackTrace st = new StackTrace(true);
            StringBuilder sb = new StringBuilder();
            var frames = st.GetFrames();
            for (int frameNum = 0; frameNum < frames.Length; frameNum++)            
            {
                var frame = frames[frameNum];
                sb.Append(frame.GetMethod().DeclaringType.ToString());
                sb.Append(".");
                sb.Append(frame.GetMethod().Name);
                sb.Append("(");
                int paramIdx = 0;
                for (var param in frame.GetMethod().GetParameters())
                {
                    if (paramIdx > 0)
                        sb.Append(", ");
                    sb.Append(param.ParameterType.ToString());
                    sb.Append(" ");
                    sb.Append(param.Name);
                    paramIdx++;
                }
                sb.Append(")");
                sb.Append("|");
                sb.Append(frame.GetFileName());
                sb.Append("|");
                sb.Append(frame.GetFileLineNumber());
                sb.AppendLine();
            }
        }
#endif

        public int32 RefreshRate
        {
            get
            {
                return mRefreshRate;
            }

            set
            {
                mRefreshRate = value;
                BFApp_SetRefreshRate(mRefreshRate);
            }
        }        

        // Simulation seconds since last update
        public float UpdateDelta
        {
            get
            {
                if (mRefreshRate != 0)
                    return 1.0f / mRefreshRate;
                return mLastUpdateDelta;
            }
        }

		public BFWindow FocusedWindow
		{
			get
			{
				for (var window in mWindows)
					if (window.mHasFocus)
						return window;
				return null;
			}
		}

		public static void Startup(String[] args, Action startupCallback)
		{
			/*string[] newArgs = new string[args.Length + 1];
			Array.Copy(args, 0, newArgs, 1, args.Length);
			newArgs[0] = System.Reflection.Assembly.GetEntryAssembly().Location;
			Lib_Startup(newArgs.Length, newArgs, startupCallback);*/
			
			char8*[] char8PtrArr = scope char8*[args.Count];
			for (int32 i = 0; i < args.Count; i++)
				char8PtrArr[i] = args[i];
			Lib_Startup((int32)args.Count, char8PtrArr.CArray(), startupCallback.GetFuncPtr());
		}

        public virtual bool HandleCommandLineParam(String key, String value)
        {
			return false;
        }

		public virtual void UnhandledCommandLine(String key, String value)
		{

		}

		public virtual void ParseCommandLine(String[] args)
		{
			for (var str in args)
			{
				int eqPos = str.IndexOf('=');
				if (eqPos == -1)
				{
					if (!HandleCommandLineParam(str, null))
						UnhandledCommandLine(str, null);
				}	
				else
				{
					var cmd = scope String(str, 0, eqPos);
					var param = scope String(str, eqPos + 1);
					if (!HandleCommandLineParam(cmd, param))
						UnhandledCommandLine(cmd, param);
				}
			}
		}

        public virtual void ParseCommandLine(String theString)
        {
            List<String> stringList = scope List<String>();

            String curString = null;
            bool hadSpace = false;
            bool inQuote = false;
            for (int32 i = 0; i < theString.Length; i++)
            {
                char8 c = theString[i];

                if ((theString[i] == ' ') && (!inQuote))
                {
                    hadSpace = true;
                }
                else
                {
                    if (hadSpace)
                    {
                        if (!inQuote)
                        {
                            if (c == '=')
                            {
                            }
                            else
                            {
                                if (curString != null)
                                {
                                    stringList.Add(curString);
                                    curString = null;
                                }
                            }
                        }
                        hadSpace = false;
                    }

                    if (curString == null)
                        curString = scope:: String();

                    if (c == '"')
                    {
                        inQuote = !inQuote;
                    }
                    else
                    {
                        curString.Append(theString[i]);
                    }
                }
                
            }

            if (curString != null)
                stringList.Add(curString);

            for (String param in stringList)
            {
                int32 eqPos = (int32)param.IndexOf('=');
                if (eqPos != -1)
				{
					String key = scope String(param, 0, eqPos);
					String value = scope String(param, eqPos + 1);

                    if (!HandleCommandLineParam(key, value))
						UnhandledCommandLine(key, value);
				}
                else
                    if (!HandleCommandLineParam(param, null))
						UnhandledCommandLine(param, null);
            }
        }

        public void GetDesktopResolution(out int width, out int height)
        {
			int32 widthOut;
			int32 heightOut;
            BFApp_GetDesktopResolution(out widthOut, out heightOut);
			width = widthOut;
			height = heightOut;
        }

        public void GetWorkspaceRect(out int x, out int y, out int width, out int height)
        {
			int32 xOut;
			int32 yOut;
			int32 widthOut;
			int32 heightOut;
            BFApp_GetWorkspaceRect(out xOut, out yOut, out widthOut, out heightOut);
			x = xOut;
			y = yOut;
			width = widthOut;
			height = heightOut;
        }

		public void GetWorkspaceRectFrom(int fromX, int fromY, int fromWidth, int fromHeight, out int outX, out int outY, out int outWidth, out int outHeight)
		{
			int32 xOut;
			int32 yOut;
			int32 widthOut;
			int32 heightOut;
		    BFApp_GetWorkspaceRectFrom((.)fromX, (.)fromY, (.)fromWidth, (.)fromHeight, out xOut, out yOut, out widthOut, out heightOut);
			outX = xOut;
			outY = yOut;
			outWidth = widthOut;
			outHeight = heightOut;
		}

        public bool HasModalDialogs()
        {
            for (var window in mWindows)
            {
                if (window.mWindowFlags.HasFlag(BFWindowBase.Flags.Modal))
                    return true;
            }
            return false;
        }

        public bool HasPopupMenus()
        {
            for (var window in mWindows)
            {
                var widgetWindow = window as WidgetWindow;
                if ((widgetWindow != null) && (widgetWindow.mRootWidget is MenuContainer))
                    return true;
            }
            return false;
        }

        public virtual void Init()
        {
			scope AutoBeefPerf("BFApp.Init");

#if STUDIO_CLIENT
            mIPCClientManager = IPCClientManager.sIPCClientManager = new IPCClientManager();
            mIPCClientManager.Init();
            
            mStudioClient = IPCEndpoint<IStudioClient>.CreateNamed(this);
            mStudioHost = IPCProxy<IStudioHost>.CreateNamed();
            mIPCClientManager.mRemoteProcessId = mStudioHost.Proxy.ClientConnected(mStudioClient.ObjId, Process.GetCurrentProcess().Id);
            mIPCClientManager.mConnecting = false;
#endif
            BFApp_Init();

			mSoundManager.[Friend]mNativeSoundManager = BFApp_GetSoundManager();

            Interlocked.Fence();
       
            mInstallDir = new String(BFApp_GetInstallDir());
            mUserDataDir = new String(mInstallDir);

            mResourceManager = new ResourceManager();
            String resFileName = scope String();
            resFileName.Append(mInstallDir, "Resources.json");
            if (File.Exists(resFileName))
            {
                StructuredData structuredData = scope StructuredData();
                structuredData.Load(resFileName);
                mResourceManager.ParseConfigData(structuredData);
            }

			for (int32 i = 0; true; i++)
			{
				int threadId = BFApp_GetCriticalThreadId(i);
				if (threadId == 0)
					break;
				GC.ExcludeThreadId(threadId);
			}
        }

        public void InitGraphics()
        {
			DefaultVertex.Init();
			ModelDef.VertexDef.Init();

            mGraphics = new Graphics();

			String filePath = scope String();
			filePath.Append(mInstallDir, "shaders/Std");

			if (mColorMatrix != null)
				filePath.Append("_hue");

            Shader shader = Shader.CreateFromFile(filePath, DefaultVertex.sVertexDefinition);
            //shader.SetTechnique("Standard");            
            mGraphics.mDefaultShader = shader;
            mGraphics.mDefaultRenderState = RenderState.Create();
            mGraphics.mDefaultRenderState.mIsFromDefaultRenderState = true;
            mGraphics.mDefaultRenderState.Shader = shader;

			filePath.Append("_font");
			Shader textShader = Shader.CreateFromFile(filePath, DefaultVertex.sVertexDefinition);
			mGraphics.mTextShader = textShader;
			//shader.SetTechnique("Standard");            
			//mGraphics.mDefaultShader = textShader;
			//mGraphics.mDefaultRenderState = RenderState.Create();
			//mGraphics.mDefaultRenderState.mIsFromDefaultRenderState = true;
			//mGraphics.mDefaultRenderState.Shader = shader;

			//mGraphics.mDefaultShader = textShader;
			//mGraphics.mDefaultRenderState.Shader = textShader;
        }

        public ~this()
        {
#if STUDIO_HOST || STUDIO_CLIENT
            if (IPCManager.sIPCManager != null)
                IPCManager.sIPCManager.Dispose();
#endif
			Debug.Assert(mShuttingDown, "Shutdown must be called before deleting the app");

			ProcessDeferredDeletes();

			BFApp_Delete();

			ShutdownCompleted();
        }

		public void DeferDelete(Object obj)
		{
			//Slow, paranoid check.
			//Debug.Assert(!mDeferredDeletes.Contains(obj));

			mDeferredDeletes.Add(obj);
		}

		public void ProcessDeferredDeletes()
		{
			for (int32 i = 0; i < mDeferredDeletes.Count; i++)
				delete mDeferredDeletes[i];
			mDeferredDeletes.Clear();
		}

		public virtual void ShutdownCompleted()
		{

		}

        public virtual void Shutdown()
        {
			Debug.Assert(!mShuttingDown, "Shutdown can only be called once");
			/*if (!mStarted)
				Debug.Assert(mStopping, "Shutdown can only be called after the app is stopped");*/

			mShuttingDown = true;
            //Dispose();
        }

        public virtual void Run()
        {
			if (mStopping)
				return;
            BFApp_Run();
        }

		public virtual void Stop()
		{
			mStopping = true;
			while (mWindows.Count > 0)
				mWindows[0].Close(true);

			BFApp_Shutdown();
		}

        uint32 mPrevUpdateTickCount;
        uint32 mUpdateTickCount;
        uint32 mLastEndedTickCount;

        public virtual void Update(bool batchStart)
        {
            //Utils.BFRT_CPP("gBFGC.MutatorSectionEntry()");

            mIsUpdateBatchStart = batchStart;
            mUpdateCnt++;

            mPrevUpdateTickCount = mUpdateTickCount;
            mUpdateTickCount = Utils.GetTickCount();
            if (mPrevUpdateTickCount != 0)
                mLastUpdateDelta = (mUpdateTickCount - mPrevUpdateTickCount) / 1000.0f;

            using (PerfTimer.ZoneStart("BFApp.Update"))
            {
                mFPSUpdateCount++;

                if (mRefreshRate == 0)
                {
                    int32 elapsed = (int32)(mUpdateTickCount - mLastFPSUpdateCnt);
                    if (elapsed > 1000)
                    {
                        mLastFPS = mFPSDrawCount;
                        mLastFPSUpdateCnt = mUpdateTickCount;

                        mFPSUpdateCount = 0;
                        mFPSDrawCount = 0;                        
                    }
                }
                else
                {
                    if (mFPSUpdateCount == mRefreshRate)
                    {
                        mLastFPS = mFPSDrawCount;
                        //Debug.WriteLine("FPS: " + mLastFPS);

                        mFPSUpdateCount = 0;
                        mFPSDrawCount = 0;
                    }
                }                

                //GC.Collect();
                using (PerfTimer.ZoneStart("BFApp.Update mWindows updates"))
                {                    
                    for (int32 windowIdx = 0; windowIdx < mWindows.Count; windowIdx++)
                    {
                        mWindows[windowIdx].Update();
                    }
                }
            }

            mLastEndedTickCount = Utils.GetTickCount();
			ProcessDeferredDeletes();

            //Utils.BFRT_CPP("gBFGC.MutatorSectionExit()");
        }        

		public virtual void UpdateF(float updatePct)
		{
			for (int32 windowIdx = 0; windowIdx < mWindows.Count; windowIdx++)
			{
			    mWindows[windowIdx].UpdateF(updatePct);
			}
		}

        public virtual void DoDraw()
        {
        }

#if STUDIO_CLIENT
        bool mHostIsPerfRecording;

        public virtual void HostIsPerfRecording(bool isPerfRecording, int drawCount)
        {
            mHostIsPerfRecording = isPerfRecording;
            if (mHostIsPerfRecording)
            {
                PerfTimer.StartRecording();
                PerfTimer.Log("HostIsPerfRecording {0}", drawCount);
                PerfTimer.Log("PrevUpdateTickCount: {0}", mPrevUpdateTickCount);
                PerfTimer.Log("UpdateTickCount: {0}", mUpdateTickCount);
                PerfTimer.Log("LocalUpdateTick: {0}", mLocalUpdateTick);
                
                //using (PerfTimer.ZoneStart("HostIsPerfRecording"))
                    //PerfTimer.Log("Got HostIsPerfRecording");
            }
        }
#endif
        
        public virtual void Draw(bool forceDraw)
        {
#if STUDIO_CLIENT            
            
#endif
            PerfTimer.ZoneStart("BFApp.Draw");
            PerfTimer.Message("Client Draw Start");

            mFPSDrawCount++;

#if STUDIO_CLIENT
            mStudioHost.Proxy.FrameStarted(mUpdateCnt);
#endif
            if (mGraphics == null)
                InitGraphics();

            mGraphics.StartDraw();

            for (BFWindow window in mWindows)
            {
                if ((window.mVisible) &&
					((window.mIsDirty) || (mAutoDirty) || (forceDraw)))
                {
                    window.PreDraw(mGraphics);
					if (mColorMatrix != null)
					{
						/*mColorMatrix = Matrix4(
							0.90f,  0.10f,  0.00f, 0,
							0.00f,  0.90f,  0.10f, 0,
							0.10f,  0.00f,  1.05f, 0,
							    0,      0,      0, 1);*/

						mGraphics.SetVertexShaderConstantData(0, &mColorMatrix.ValueRef, mColorMatrixDataDef);
					}
                    window.Draw(mGraphics);
                    window.PostDraw(mGraphics);
					window.mIsDirty = false;
                }
            }
            mGraphics.EndDraw();

#if STUDIO_CLIENT            
            PerfTimer.Log("Sending FrameDone");
            mStudioHost.Proxy.FrameDone();
            //mFrameDonesSent++;
#endif

            PerfTimer.ZoneEnd();                        
        }

		public void MarkDirty()
		{
			for (var window in mWindows)
			{
				if (var widgetWindow = window as WidgetWindow)
				{
					widgetWindow.mIsDirty = true;
				}
			}	
		}

        Cursor curCursor;
        public virtual void SetCursor(Cursor cursor)
        {
            if (curCursor == cursor)
                return;            
            curCursor = cursor;            
            BFApp_SetCursor((int32)cursor);
        }

        public virtual void* GetClipboardData(String format, out int32 size, int waitTime = 500)
        {
			Stopwatch sw = null;
			repeat
			{
				if (sw != null)
					Thread.Sleep(1);
				void* result = BFApp_GetClipboardData(format, out size);
				if (size != -1)
					return result;
				if (waitTime == 0)
					return null;
				if (sw == null)
					sw = scope:: .()..Start();
			}
            while (waitTime < sw.ElapsedMilliseconds);
			return null;
        }

        public virtual void ReleaseClipboardData(void* ptr)
        {
            BFApp_ReleaseClipboardData(ptr);
        }

        public virtual bool GetClipboardText(String outStr, int waitTime = 500)
        {
			return GetClipboardTextData("text", outStr, waitTime);
        }

		public virtual bool GetClipboardText(String outStr, String extra)
		{
			GetClipboardTextData("bf_text", extra);
			return GetClipboardTextData("text", outStr);
		}

		public bool GetClipboardTextData(String format, String outStr, int waitTime = 500)
		{
			int32 aSize;
			void* clipboardData = GetClipboardData(format, out aSize, waitTime);
			if (clipboardData == null)
			    return false;

			outStr.Append((char8*)clipboardData);
			ReleaseClipboardData(clipboardData);
			return true;
		}

        public virtual void SetClipboardData(String format, void* ptr, int32 size, bool resetClipboard)
        {
            BFApp_SetClipboardData(format, ptr, size, resetClipboard ? 1 : 0);
        }

		public void SetClipboardText(String text, String extra)
		{
			SetClipboardData("text", text.CStr(), (int32)text.Length + 1, true);
			SetClipboardData("bf_text", extra.CStr(), (int32)extra.Length + 1, false);
		}

        public virtual void SetClipboardText(String text)
        {
            SetClipboardText(text, "");
        }

#if STUDIO_CLIENT
        public int GetProcessId()
        {
            return Process.GetCurrentProcess().Id;
        }
#endif	 
		
		public void RehupMouse()
		{
			BFApp_RehupMouse();
		}
    }
}
