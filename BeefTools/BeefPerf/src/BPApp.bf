using System;
using Beefy;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.theme;
using System.IO;
using System.Threading;
using System.Collections;
using System.Diagnostics;
using Beefy.events;
using Beefy.sys;
using System.Text;
using System.Net;

namespace BeefPerf
{	
	class BPApp : BFApp
	{
		public new static BPApp sApp;
		public float mTimePerFrame = 1.0f / 60;
		public bool mWantsFullscreen = false;
		public int32 mListenPort = 4208;
		//public int32 mListenPort = 135;

		WidgetWindow mMainWindow;
		Widget mRootWidget;

		public Widget mMainWidget;
		public List<String> mLogLines = new List<String>() ~ DeleteContainerAndItems!(_);

		//public PopupMenuManager mPopupMenuManager;
		//public Trainer mTrainer ~ delete _;

		public ScriptManager mScriptManager = new ScriptManager() ~ delete _;
		public DarkDockingFrame mDockingFrame;
		public MainFrame mMainFrame;

		public WorkspacePanel mWorkspacePanel;
		public Board mBoard;
		public ProfilePanel mProfilePanel;
		public FindPanel mFindPanel;
		public Windows.ProcessInformation mProcessInformation;

		public Socket mListenSocket ~ delete _;
		public Thread mSocketThread ~ delete _;
		public List<BpClient> mClients = new List<BpClient>() ~ delete _;
		public List<BpSession> mSessions = new List<BpSession>() ~ DeleteContainerAndItems!(_);
		public Monitor mClientMonitor = new Monitor() ~ delete _;
		public WaitEvent mShutdownEvent = new WaitEvent() ~ delete _;
		public BpSession mCurSession;
		bool mEnableGCCollect = true;

		public bool mIsAutoOpened;
		public int32 mStatBytesReceived;
		public int32 mStatReportTick;
		public int32 mStatBytesPerSec;
		public bool mFailed;
		public int32 mFailIdx;

		public bool Listening
		{
			get
			{
				return mListenSocket.IsOpen;
			}

			set
			{
				if (value)
				{
					if (!mListenSocket.IsOpen)
					{
						if (mListenSocket.Listen(mListenPort) case .Err)
						{
							Fail(scope String()..AppendF("Failed to listen on port {0}", mListenPort));
						}
					}
				}
				else
				{
					if (mListenSocket.IsOpen)
					{
						mListenSocket.Close();
					}
				}
			}
		}

		public this()
		{
			sApp = this;
			gApp = this;

			//RefreshRate = 120;
		}

		public ~this()
		{
			if (mSocketThread != null)
				mSocketThread.Join();

			for (var client in mClients)
			{
				if (!client.IsSessionInitialized)
					delete client;
			}

			Widget.RemoveAndDelete(mWorkspacePanel);
			Widget.RemoveAndDelete(mBoard);
			Widget.RemoveAndDelete(mProfilePanel);
			Widget.RemoveAndDelete(mFindPanel);

			if (!mLogLines.IsEmpty)
			{
				var fs = scope FileStream();
				if (fs.Open("BeefPerf.txt", .Append, .ReadWrite, .ReadWrite, 4096, .None, null) case .Ok)
				{
					var streamWriter = scope StreamWriter(fs, Encoding.UTF8, 4096);
					for (var line in mLogLines)
						streamWriter.WriteLine(line);
				}
			}
		}
		
		static uint32 TimeToUnixTime(DateTime ft)
		{
			// takes the last modified date
			int64 date = ft.ToFileTime();
		
			// 100-nanoseconds = milliseconds * 10000
			int64 adjust = 11644473600000L * 10000;
		
			// removes the diff between 1970 and 1601
			date -= adjust;
		
			// converts back from 100-nanoseconds to seconds
			return (uint32)(date / 10000000L);
		}

		public void LogLine(String text)
		{
			mLogLines.Add(new String(text));
		}

		public void Fail(String text)
		{
		    //MessageBeep(MessageBeepType.Error);

			// Always write to STDOUT even if we're running as a GUI, allowing cases like RunAndWait to pass us a stdout handle
			Console.WriteLine("ERROR: {0}", text);

			mFailIdx++;

			if (gApp.mScriptManager.mRunningCommand)
				return;

			if (mMainWindow == null)
			{
				LogLine(text);

				mFailed = true;
				if (!mShuttingDown)
					Shutdown();
				return;
			}

		    Dialog aDialog = ThemeFactory.mDefault.CreateDialog("ERROR", text, DarkTheme.sDarkTheme.mIconError);
		    aDialog.mDefaultButton = aDialog.AddButton("OK");
		    aDialog.mEscButton = aDialog.mDefaultButton;
		    aDialog.PopupWindow(mMainWindow);
		}

		void StartFakeClient()
		{
			return;
#unwarn
			ProcessStartInfo procInfo = scope ProcessStartInfo();
			procInfo.UseShellExecute = false;
			procInfo.RedirectStandardError = true;
			procInfo.SetFileName(@"c:\proj\BeefPerf\x64\Debug\BeefPerf.exe");
			procInfo.CreateNoWindow = true;

			var spawnedProcess = scope SpawnedProcess();
			spawnedProcess.Start(procInfo).IgnoreError();

			//Process process = Process.Start(procInfo).GetValueOrDefault();
			//delete process;
		}

		public void UpdateTitle()
		{
			String title = scope String();
			if ((mBoard.mPerfView != null) && (mBoard.mPerfView.mSession.mClientName != null))
			{
				title.Append(mBoard.mPerfView.mSession.mClientName);
				title.Append(" - ");
			}
			title.Append("BeefPerf");
			mMainWindow.SetTitle(title);
		}

		public override void Init()
		{
			/*uint timeNow = GetCurrentUnixTime();

			FitEntry[] fitEntries = new FitEntry[200];
			for (int i = 0; i < fitEntries.Length; i++)
			{				
				fitEntries[i].mElapsedTimeMS = i * 500;
				fitEntries[i].mPower = i;
				fitEntries[i].mCadence = 90;
			}

			int[] lapOffsets = scope int[] {100};

			WriteFit("c:\\temp\\test.fit", UnixTimeToFitTime(timeNow), fitEntries.CArray(), fitEntries.Length, lapOffsets.CArray(), lapOffsets.Length);*/

			base.Init();
			mAutoDirty = false;

			Socket.Init();

			mListenSocket = new Socket();
			Listening = true;

			mSocketThread = new Thread(new => SocketThreadProc);
			mSocketThread.Start(false);

			DarkTheme aTheme = new DarkTheme();
			aTheme.Init();
			ThemeFactory.mDefault = aTheme;

			mMainFrame = new MainFrame();
			mDockingFrame = mMainFrame.mDockingFrame;

			BFWindow.Flags windowFlags = BFWindow.Flags.Border | BFWindow.Flags.SysMenu | //| BFWindow.Flags.CaptureMediaKeys |
			    BFWindow.Flags.Caption | BFWindow.Flags.Minimize | BFWindow.Flags.QuitOnClose | BFWindowBase.Flags.Resizable |
                BFWindow.Flags.Menu | BFWindow.Flags.SysMenu;
			if (mWantsFullscreen)
				windowFlags |= BFWindowBase.Flags.Fullscreen;

			//mRootWidget = new Widget();
			mWorkspacePanel = new WorkspacePanel();
			mBoard = new Board();
			mProfilePanel = new ProfilePanel();
			mFindPanel = new FindPanel();

			//mRootWidget = mBoard;
			mMainWindow = new WidgetWindow(null, "BeefPerf", 0, 0, 1600, 1200, windowFlags, mMainFrame);
			mMainWindow.mOnWindowKeyDown.Add(new => SysKeyDown);
			mMainWindow.SetMinimumSize(480, 360);
			mMainWindow.mIsMainWindow = true;
			CreateMenu();

			ShowWorkspacePanel();
			ShowTimelinePanel();
			ShowProfilePanel();
			ShowFindPanel();

			StartFakeClient();

			GC.SetAutoCollectPeriod(mEnableGCCollect ? 20 : -1);
			GC.SetCollectFreeThreshold(mEnableGCCollect ? 64*1024*1024 : -1);
		}

		public override void Stop()
		{
			base.Stop();
			mListenSocket.Close();
		}
		
		void ShowWorkspacePanel()
		{
			if (mWorkspacePanel.mWidgetWindow != null)
			{
				return;
			}
			TabbedView tabbedView = CreateTabbedView();
			SetupTab(tabbedView, "Workspace", mWorkspacePanel);
			tabbedView.SetRequestedSize(320, 320);
			if ((mDockingFrame.mSplitType == .Vert) || (mDockingFrame.mDockedWidgets.Count == 1))
				mDockingFrame.AddDockedWidget(tabbedView, mDockingFrame.mDockedWidgets[0], DockingFrame.WidgetAlign.Left);
			else
				mDockingFrame.AddDockedWidget(tabbedView, null, DockingFrame.WidgetAlign.Top);
		}

		void ShowTimelinePanel()
		{
			if (mBoard.mWidgetWindow != null)
			{
				return;
			}
			TabbedView tabbedView = CreateTabbedView();
			SetupTab(tabbedView, "Timeline", mBoard);
			tabbedView.SetRequestedSize(250, 250);
			tabbedView.mIsFillWidget = true;
			if ((mDockingFrame.mSplitType == .Vert) || (mDockingFrame.mDockedWidgets.Count == 1))
				mDockingFrame.AddDockedWidget(tabbedView, mDockingFrame.mDockedWidgets[0], DockingFrame.WidgetAlign.Right);
			else
				mDockingFrame.AddDockedWidget(tabbedView, null, DockingFrame.WidgetAlign.Top);
		}

		void ShowProfilePanel()
		{
			if (mProfilePanel.mWidgetWindow != null)
			{
				return;
			}

			var tabbedView = CreateTabbedView();
			SetupTab(tabbedView, "Profile", mProfilePanel);
			tabbedView.SetRequestedSize(300, 300);

			if ((mDockingFrame.mSplitType == .Vert) && (mDockingFrame.mDockedWidgets.Count > 0))
				mDockingFrame.AddDockedWidget(tabbedView, mDockingFrame.mDockedWidgets.Back, DockingFrame.WidgetAlign.Left);
			else
				mDockingFrame.AddDockedWidget(tabbedView, null, DockingFrame.WidgetAlign.Bottom);
		}

		void ShowFindPanel()
		{
			if (mFindPanel.mWidgetWindow != null)
			{
				return;
			}

			var tabbedView = CreateTabbedView();
			SetupTab(tabbedView, "Find", mFindPanel);
			tabbedView.SetRequestedSize(300, 300);
			if ((mDockingFrame.mSplitType == .Vert) && (mDockingFrame.mDockedWidgets.Count > 0))
				mDockingFrame.AddDockedWidget(tabbedView, mDockingFrame.mDockedWidgets.Back, DockingFrame.WidgetAlign.Right);
			else
				mDockingFrame.AddDockedWidget(tabbedView, null, DockingFrame.WidgetAlign.Bottom);
		}

		void SetupTab(TabbedView tabbedView, String label, Widget widget)
		{
			var tabButton = tabbedView.AddTab(label, 0, widget, false);
			tabButton.mCloseClickedEvent.Add(new () =>
				{
					var tabbedView = tabButton.mTabbedView;
					tabbedView.RemoveTab(tabButton);
					if (tabbedView.mTabs.IsEmpty)
					{
						tabbedView.mParentDockingFrame.RemoveDockedWidget(tabbedView);
						gApp.DeferDelete(tabbedView);
					}
				});
		}

		void ToggleCheck(IMenu menu, ref bool checkVal)
		{
			checkVal = !checkVal;
			var sysMenu = (SysMenu)menu;
			sysMenu.Modify(null, null, null, true, checkVal ? 1 : 0);
		}

		void ShowInfo()
		{
			int streamSize = 0;
			int lodStreamSize = 0;

			for (var client in mClients)
			{
				for (var thread in client.mThreads)
				{
					for (var streamData in thread.mStreamDataList)
					{
						streamSize += streamData.mBuffer.Count;
					}

					for (var streamLOD in thread.mStreamLODs)
					{
						for (var streamData in streamLOD.mStreamDataList)
						{
							lodStreamSize += streamData.mBuffer.Count;
						}
					}
				}
			}

			Debug.WriteLine("Stream Size: {0}k", streamSize / 1024);
			Debug.WriteLine("LOD Stream Size: {0}k", lodStreamSize / 1024);
		}

		public void CloseSession()
		{
			SetSession(null);
		}

		public void OpenSession()
		{
			List<int> iList = scope List<int>();
			iList.Add(123);

			var fileDialog = scope OpenFileDialog();
			fileDialog.ShowReadOnly = false;
			fileDialog.Title = "Open Session";
			fileDialog.Multiselect = true;
			fileDialog.ValidateNames = true;
			fileDialog.DefaultExt = ".bfps";
			fileDialog.SetFilter("BeefPerf Session (*.bfps)|*.bfps|All files (*.*)|*.*");
			if (fileDialog.ShowDialog() case .Ok)
			{
				for (String origProjFilePath in fileDialog.FileNames)
				{
					var session = new BpSession(false);
					switch (session.Load(origProjFilePath))
					{
					case .Ok:
						mSessions.Add(session);
						MarkDirty();
					case .Err(let err):
						switch (err)
						{
						case .FileNotFound, .FileError:
							Fail(scope String()..AppendF("Failed to open file '{0}'", origProjFilePath));
						case .InvalidData:
							Fail(scope String()..AppendF("Invalid data in file '{0}'", origProjFilePath));
						case .InvalidVersion:
							Fail(scope String()..AppendF("Unsupported version in file '{0}'", origProjFilePath));
						}
						delete session;
					}
				}
			}
		}

		public void SaveSession()
		{
			if (mCurSession == null)
				return;

			var fileDialog = scope SaveFileDialog();
			//fileDialog.ShowReadOnly = false;
			fileDialog.Title = "Save Session";
			fileDialog.Multiselect = true;
			fileDialog.ValidateNames = true;
			fileDialog.DefaultExt = ".bfps";
			fileDialog.SetFilter("BeefPerf Session (*.bfps)|*.bfps|All files (*.*)|*.*");
			if (fileDialog.ShowDialog() case .Ok)
			{
				for (String origProjFilePath in fileDialog.FileNames)
				{
					if (mCurSession.Save(origProjFilePath) case .Err)
					{
						Fail(scope String()..AppendF("Failed to save file '{0}'", origProjFilePath));
					}
				}
			}
		}

		public void RemoveSession()
		{
			if (mCurSession == null)
				return;

			var session = mCurSession;

			SetSession(null);
			mSessions.Remove(session);

			using (mClientMonitor.Enter())
			{
				var client = session as BpClient;
				if (client != null)
					mClients.Remove(client);
				delete session;
			}
			MarkDirty();
		}

		public void RemoveAllSessions()
		{
			SetSession(null);
			
			using (mClientMonitor.Enter())
			{
				ClearAndDeleteItems(mSessions);
				mSessions.Clear();
				mClients.Clear();
			}
			MarkDirty();
		}

		public void CreateMenu()
		{
			SysMenu root = mMainWindow.mSysMenu;

			SysMenu subMenu = root.AddMenuItem("&File");
			subMenu.AddMenuItem("&Open Session", "Ctrl+O", new (menu) => { OpenSession(); });
			subMenu.AddMenuItem("&Save Session", "Ctrl+S", new (menu) => { SaveSession(); });
			subMenu.AddMenuItem("&Close Session", "Ctrl+W", new (menu) => { CloseSession(); });
			subMenu.AddMenuItem("&Remove Session", null, new (menu) => { RemoveSession(); });
			subMenu.AddMenuItem("Remove &All Sessions", null, new (menu) => { RemoveAllSessions(); });
			subMenu.AddMenuItem("&Info", "Ctrl+I", new (menu) => { ShowInfo(); });
			subMenu.AddMenuItem("E&xit", null, new (menu) => { mMainWindow.Close(); });

			subMenu = root.AddMenuItem("&View");
			subMenu.AddMenuItem("&Workspace", "Ctrl+Alt+W", new (menu) => { ShowWorkspacePanel(); });
			subMenu.AddMenuItem("&Timeline", "Ctrl+Alt+T", new (menu) => { ShowTimelinePanel(); });
			subMenu.AddMenuItem("&Profile", "Ctrl+Alt+P", new (menu) => { ShowProfilePanel(); });
			subMenu.AddMenuItem("&Find", "Ctrl+Alt+F", new (menu) => { ShowFindPanel(); });

			subMenu = root.AddMenuItem("&Debug");
			subMenu.AddMenuItem("GC Collect", null, new (menu) =>
				{
				    if (Profiler.StartSampling() case .Ok(let id))
					{
						GC.Collect();
						id.Dispose();
					}
				});
			subMenu.AddMenuItem("Enable GC Collect", null, new (menu) => { ToggleCheck(menu, ref mEnableGCCollect); }, null, null, true, mEnableGCCollect ? 1 : 0);
		}

		public override void Shutdown()
		{
			base.Shutdown();

			mShutdownEvent.Set(true);
		}

		public override bool HandleCommandLineParam(String key, String value)
		{
			switch (key)
			{
			case "-autoOpened":
				mIsAutoOpened = true;
				return true;
			case "-cmd":
				if (value == null)
					return false;

				Socket.Init();

				LogLine(scope String()..AppendF("Cmd: {0}", value));

				bool alreadyRunning = false;

				//
				{
					Socket socket = scope Socket();
					if (socket.Listen(mListenPort) case .Err)
						alreadyRunning = true;
				}

				if (!alreadyRunning)
				{
					LogLine("Launching BeefPerf process");

					var exePath = scope String();

					var curProcess = scope Process();
					curProcess.GetProcessById(Process.CurrentId);
					exePath.Append(curProcess.ProcessName);

					ProcessStartInfo procInfo = scope ProcessStartInfo();
					procInfo.SetFileName(exePath);
					procInfo.SetArguments("-autoOpened");
					SpawnedProcess spawnedProcess = scope SpawnedProcess();
					spawnedProcess.Start(procInfo);
				}

				// Try for 10 seconds
				for (int i < 100)
				{
					Socket socket = scope Socket();
					if (socket.Connect("127.0.0.1", mListenPort) case .Err)
					{
						Thread.Sleep(100);
						continue;
					}

					DynMemStream memStream = scope DynMemStream();
					memStream.Write((int32)0); // Size placeholder

					memStream.Write((uint8)BpCmd.Cmd);
					String cmdStr = value;
					memStream.Write(cmdStr);
					memStream.Write((uint8)0);
					
					(*(int32*)memStream.Ptr) = (int32)memStream.Length - 4;

					// Try 10 seconds to send
					for (int sendItr < 100)
					{
						if (memStream.IsEmpty)
							break;

						switch (socket.Send(memStream.Ptr, memStream.Length))
						{
						case .Ok(let sendLen):
							memStream.RemoveFromStart(sendLen);
						case .Err:
							Fail("Failed to send command");
						}

						if (memStream.IsEmpty)
							break;
						Thread.Sleep(100); // Wait until we can send more...
					}

					// Try 10 seconds for a response
					bool gotResult = false;
					RecvLoop: for (int recvStr < 100)
					{
						int result = 0;
						switch (socket.Recv(&result, 1))
						{
						case .Ok(let recvLen):
							if (recvLen > 0)
							{
								if (result != 1)
									Fail("Command failed");
								gotResult = true;
								break RecvLoop;
							}
						case .Err:
							break;
						}
						Thread.Sleep(100);
					}

					if (!gotResult)
					{
						Fail("Failed to receive command response");
					}

					if (!mShuttingDown)
						Shutdown();
					return true;
				}
				Fail("Failed to connect to BeefPerf");
			case "-fullscreen":
				mWantsFullscreen = true;
			case "-windowed":
				mWantsFullscreen = false;
			default:
				return base.HandleCommandLineParam(key, value);
			}
			return true;
		}

		public override void UnhandledCommandLine(String key, String value)
		{
			Fail(StackStringFormat!("Unhandled command line param: {0}", key));
		}

		void SetupNewWindow(WidgetWindow window)
		{
		    window.mOnWindowKeyDown.Add(new => SysKeyDown);
		    //window.mWindowCloseQueryHandler.Add(new => SecondaryAllowClose);
		}        

		DarkTabbedView CreateTabbedView()
		{
		    var tabbedView = new DarkTabbedView();
		    tabbedView.mSharedData.mOpenNewWindowDelegate.Add(new (fromTabbedView, newWindow) => SetupNewWindow(newWindow));
		    return tabbedView;
		}

		public void PushMainWidget(Widget widget)
		{
			if (mMainWidget != null)
			{
				mMainWidget.RemoveSelf();
				DeferDelete(mMainWidget);
			}

			mMainWidget = widget;

			mRootWidget.AddWidget(widget);
			widget.Resize(0, 0, mRootWidget.mWidth, mRootWidget.mHeight);
			widget.SetFocus();			
		}

		void SocketThreadProc()
		{
			while (true)
			{
				if (mShutdownEvent.WaitFor(0))
					return;

				//TODO: Add client sockets to the select set

				var readSet = default(Socket.FDSet);
				var writeSet = default(Socket.FDSet);
				var exceptSet = default(Socket.FDSet);

				void Add(Socket socket)
				{
					readSet.Add(socket.[Friend]mHandle);
					exceptSet.Add(socket.[Friend]mHandle);
				}

				Add(mListenSocket);
				using (mClientMonitor.Enter())
				{
					for (var client in mClients)
						Add(client.mSocket);
				}
				
#unwarn
				int selectResult = Socket.Select(&readSet, &writeSet, &exceptSet, 20);

				int clientIdx = 0;
				while (true)
				{
					BpClient client = null;
					using (mClientMonitor.Enter())
					{
						if (clientIdx < mClients.Count)
						{
							client = mClients[clientIdx++];
						}
						
						if (client == null)
							break;
	
						client.TryRecv();
					}
				}
			}
		}

		public override void Update(bool batchStart)
		{
			base.Update(batchStart);

			/*if (!mListenSocket.IsConnected)
			{
				Listen();
			}*/

			Socket newConnection = new Socket();
			if (newConnection.AcceptFrom(mListenSocket) case .Ok)
			{
				using (mClientMonitor.Enter())
				{
					BpClient client = new BpClient();
					//client.mProfileId =	Profiler.StartSampling();
					client.mConnectTime = DateTime.Now;
					client.mSocket = newConnection;
					mClients.Add(client);
				}

				/*{
					// Kill all old clients - remove this when we support switching between clients
					ClearAndDeleteItems(mClients);

					mFindPanel.Clear();
					mProfilePanel.Clear();
					mFindPanel.mNeedsRestartSearch = true;

					BpClient client = new BpClient();
					client.mSocket = newConnection;
					mClients.Add(client);

					SetClient(client);
				}*/
			}
			else
				delete newConnection;

			using (mClientMonitor.Enter())
			{
				for (var client in mClients)
				{
					client.Update();
	
					if (client.mSessionOver)
					{
						if (!client.IsSessionInitialized)
							delete client;
						@client.Remove();

						if (mClients.Count == 0)
						{
							gApp.mStatBytesPerSec = 0;
						}
						MarkDirty();
					}
	
					if (mUpdateCnt % 20 == 0)
						MarkDirty();
				}
		}

			if (mUpdateCnt - mStatReportTick >= 60)
			{
				mStatBytesPerSec = mStatBytesReceived;
				mStatBytesReceived = 0;
				mStatReportTick = mUpdateCnt;
			}
		}

		void SysKeyDown(KeyDownEvent evt)
		{
		    var window = (WidgetWindow)evt.mSender;                     

#unwarn
		    Widget focusWidget = window.mFocusWidget;
		    
		    if (evt.mKeyFlags == 0) // No ctrl/shift/alt
		    {
		        switch (evt.mKeyCode)
				{
				default:

				}
			}

			if (evt.mKeyFlags == .Ctrl)
			{
				switch (evt.mKeyCode)
				{
				case (KeyCode)'F':
					ShowFind();
				case (KeyCode)'C':
					if (DarkTooltipManager.sTooltip != null)
					{
						SetClipboardText(DarkTooltipManager.sTooltip.mText);
					}
				default:
				}
			}

		}

		enum ShowTabResult
		{
			Existing,
			OpenedNew
		}

		public void WithDocumentTabbedViews(delegate void(DarkTabbedView) func)
		{
		    for (int32 windowIdx = 0; windowIdx < mWindows.Count; windowIdx++)
		    {
		        var window = mWindows[windowIdx];
		        var widgetWindow = window as WidgetWindow;
		        if (widgetWindow != null)
		        {
		            var darkDockingFrame = widgetWindow.mRootWidget as DarkDockingFrame;
		            if (widgetWindow == mMainWindow)
		                darkDockingFrame = mDockingFrame;

		            if (darkDockingFrame != null)
		            {
		                darkDockingFrame.WithAllDockedWidgets(scope (dockedWidget) =>
		                    {
		                        var tabbedView = dockedWidget as DarkTabbedView;
		                        if (tabbedView != null)
		                            func(tabbedView);
		                    });
		            }
		        }
		    }
		}

		public void WithTabs(delegate void(TabbedView.TabButton) func)
		{
		    WithDocumentTabbedViews(scope (documentTabbedView) =>
		        {
		            documentTabbedView.WithTabs(func);
		        });
		}

		public TabbedView.TabButton GetTab(Widget content)
		{
		    TabbedView.TabButton tab = null;
		    WithTabs(scope [&] (checkTab) =>
		        {
		            if (checkTab.mContent == content)
		                tab = checkTab;
		        });
		    return tab;
		}  

		TabbedView FindTabbedView(DockingFrame dockingFrame, int32 xDir, int32 yDir)
		{
		    bool useFirst = true;
		    if (dockingFrame.mSplitType == DockingFrame.SplitType.Horz)
		    {
		        useFirst = xDir > 0;
		    }
		    else
		        useFirst = yDir > 0;
			
		    for (int32 pass = 0; pass < 2; pass++)
		    {
		        for (int32 i = 0; i < dockingFrame.mDockedWidgets.Count; i++)
		        {
		            if ((useFirst) && (i == 0) && (pass == 0))
		                continue;

		            var widget = dockingFrame.mDockedWidgets[i];
		            if (widget is TabbedView)
		                return (TabbedView)widget;
		            DockingFrame childFrame = widget as DockingFrame;
		            if (childFrame != null)
		            {
		                TabbedView tabbedView = FindTabbedView(childFrame, xDir, yDir);
		                if (tabbedView != null)
		                    return tabbedView;
		            }
		        }
		    }

		    return null;
		}

		ShowTabResult ShowTab(Widget tabContent, String name, float width, bool ownsContent)
		{
			var result = ShowTabResult.Existing;
		    var tabButton = GetTab(tabContent);
		    if (tabButton == null)
		    {
		        TabbedView tabbedView = FindTabbedView(mDockingFrame, -1, 1);
		        if (tabbedView != null)
				{
		            tabButton = tabbedView.AddTab(name, width, tabContent, ownsContent);
					result = ShowTabResult.OpenedNew;
				}
		    }
		    if (tabButton != null)
		        tabButton.Activate();
			return result;
		}

		void ShowPanel(Widget panel, String label)
		{
			//RecordHistoryLocation();
			ShowTab(panel, label, 150, false);
			//panel.FocusForKeyboard();
		}

		public void ShowFind()
		{
		    ShowPanel(mFindPanel, "Find");
			mFindPanel.mEntryEdit.SetFocus();
		}

		public void SetSession(BpSession session)
		{
			if (session == mCurSession)
				return;

			mFindPanel.Clear();
			mProfilePanel.Clear();
			mFindPanel.mNeedsRestartSearch = true;

			mCurSession = session;
			mBoard.ShowSession(session);

			mFindPanel.Show(mBoard.mPerfView);
			mFindPanel.Clear();

			MarkDirty();

			/*if (mBoard.mPerfView != null)
			{
				mBoard.mPerfView.SaveSummary("BfCompiler_Compile", @"c:\temp\save.txt");
			}*/
		}
		
		public void ZoneSelected(PerfView perfView, BPSelection selection)
		{
			mProfilePanel.Show(perfView, selection);
		}

		public void ReportBytesReceived(int32 count)
		{
			mStatBytesReceived += count;
		}

		public bool HandleClientCommand(BpClient client, StringView cmd)
		{
			int failIdx = mFailIdx;
			mScriptManager.Exec(cmd);
			return failIdx == mFailIdx;
		}
		
		public void SessionInitialized(BpClient bpClient)
		{
			mSessions.Add(bpClient);
			UpdateTitle();
			MarkDirty();
		}
	}
}

static
{
	public static BeefPerf.BPApp gApp;
}