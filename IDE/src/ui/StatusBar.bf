using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.geom;
using IDE.Debugger;
using IDE;
using System.Diagnostics;
using IDE.Compiler;

namespace IDE.ui
{
    public class StatusBar : Widget
    {
        public int32 mClangCommandQueueSize;
        public DarkComboBox mConfigComboBox;
        public DarkComboBox mPlatformComboBox;
		public bool mWasCompiling;
		public int mEvalCount;
		public ImageWidget mCancelSymSrvButton;
		public int mDirtyDelay;
		public int mStatusBoxUpdateCnt = -1;

        public this()
        {
            mConfigComboBox = new DarkComboBox();            
            mConfigComboBox.mPopulateMenuAction.Add(new => PopulateConfigMenu);
            AddWidget(mConfigComboBox);

            mPlatformComboBox = new DarkComboBox();            
            mPlatformComboBox.mPopulateMenuAction.Add(new => PopulatePlatformMenu);
            AddWidget(mPlatformComboBox);
        }

        void PopulateConfigMenu(Menu menu)
        {
			var nameList = scope List<String>(gApp.mWorkspace.mConfigs.Keys);
			nameList.Sort(scope (lhs, rhs) => lhs.CompareTo(rhs, true));
            for (var configName in nameList)
            {
                String dispStr = configName;
                /*if (gApp.mConfigName == configName)
                {
					dispStr = stack String();
                    dispStr.AppendF("Active({0})", configName);
				}*/
                var item = menu.AddItem(dispStr);
				if (gApp.mConfigName == configName)
					item.mBold = true;
                item.mOnMenuItemSelected.Add(new (evt) => { SelectConfig(configName); });
            }
        }

        public void SelectConfig(String configName)
        {
            mConfigComboBox.Label = configName;
            gApp.mConfigName.Set(configName);
            gApp.CurrentWorkspaceConfigChanged();
			MarkDirty();
        }

        void SelectPlatform(String platformName)
        {
            mPlatformComboBox.Label = platformName;
            gApp.mPlatformName.Set(platformName);
            gApp.CurrentWorkspaceConfigChanged();
			MarkDirty();
        }

        void PopulatePlatformMenu(Menu menu)
        {
			var nameList = scope List<String>();
			gApp.mWorkspace.GetPlatformList(nameList);
            for (var platformName in nameList)
            {
                String dispStr = platformName;
                /*if (gApp.mPlatformName == platformName)
                { 
					dispStr = stack String();
                    dispStr.AppendF("Active({0})", platformName);
				}*/
                var item = menu.AddItem(dispStr);
				if (gApp.mPlatformName == platformName)
					item.mBold = true;

                item.mOnMenuItemSelected.Add(new (evt) => { SelectPlatform(platformName); });
            }
        }

        void ResizeComponents()
        {
			int btnLeft = gApp.mSettings.mEnableDevMode ? GS!(380) : GS!(300);
            mConfigComboBox.Resize(mWidth - btnLeft, 0, GS!(120), mHeight + 2);
            mPlatformComboBox.Resize(mWidth - btnLeft - GS!(120), 0, GS!(120), mHeight + 2);

			if (mCancelSymSrvButton != null)
				mCancelSymSrvButton.Resize(GS!(546), 0, GS!(20), GS!(20));
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeComponents();
        }

		// This marks us dirty on the next frame also
		void MarkDirtyEx(int dirtyDelay = 1)
		{
			mDirtyDelay = Math.Min(dirtyDelay, mDirtyDelay);
			MarkDirty();
		}

        public override void Update()
        {
            base.Update();

			if (mDirtyDelay > 0)
			{
				if (--mDirtyDelay == 0)
					MarkDirty();
			}

			if (mStatusBoxUpdateCnt != -1)
				mStatusBoxUpdateCnt++;

			if (gApp.mWorkspace.IsInitialized)
			{
				mConfigComboBox.Label = gApp.mConfigName;
				mPlatformComboBox.Label = gApp.mPlatformName;
			}
			else
			{
	            mConfigComboBox.Label = "";
	            mPlatformComboBox.Label = "";
			}

            bool canChangeConfig = !gApp.IsCompiling && !gApp.mDebugger.mIsRunning && !gApp.AreTestsRunning() && gApp.mWorkspace.IsInitialized;
            mConfigComboBox.mDisabled = !canChangeConfig;
            mPlatformComboBox.mDisabled = !canChangeConfig;

			if ((gApp.IsCompiling || gApp.mRunningTestScript) && (mUpdateCnt % 8 == 0))
				MarkDirtyEx(8);

			var debugState = gApp.mDebugger.GetRunState();
			//if (mWidgetWindow.IsKeyDown(.Control))
				//debugState = .SearchingSymSrv;

			if (debugState == .DebugEval)
			{
				mEvalCount++;
				MarkDirtyEx();
			}
			else
				mEvalCount = 0;

			if (debugState == .SearchingSymSrv)
			{
				MarkDirtyEx();

				if (mCancelSymSrvButton == null)
				{
					mCancelSymSrvButton = new ImageWidget();
					mCancelSymSrvButton.mImage = DarkTheme.sDarkTheme.GetImage(.Close);
					mCancelSymSrvButton.mOverImage = DarkTheme.sDarkTheme.GetImage(.CloseOver);
					mCancelSymSrvButton.mOnMouseClick.Add(new (evt) => { gApp.mDebugger.CancelSymSrv(); });
					AddWidget(mCancelSymSrvButton);
					ResizeComponents();
				}

				float len = GS!(200);
				float x = GS!(350);
				Rect completionRect = Rect(x, GS!(1), len, GS!(17));

				Point mousePos;
				if (DarkTooltipManager.CheckMouseover(this, 25, out mousePos, true))
				{
					if (completionRect.Contains(mousePos.x, mousePos.y))
					{
						DarkTooltipManager.ShowTooltip(gApp.mSymSrvStatus, this, mousePos.x, mousePos.y);
					}
				}
			}
			else
			{
				if ((DarkTooltipManager.sTooltip != null) && (DarkTooltipManager.sTooltip.mRelWidget == this))
					DarkTooltipManager.sTooltip.Close();

				if (mCancelSymSrvButton != null)
				{
					RemoveAndDelete(mCancelSymSrvButton);
					mCancelSymSrvButton = null;
				}
			}

			if ((gApp.mBfResolveCompiler != null) && (gApp.mBfResolveCompiler.IsPerformingBackgroundOperation())
#if IDE_C_SUPPORT
                || (gApp.mResolveClang.IsPerformingBackgroundOperation()) ||
				(gApp.mDepClang.IsPerformingBackgroundOperation())
#endif
                )
			{
				MarkDirtyEx();
			}
        }

        public override void Draw(Graphics g)        
        {
            bool atError = gApp.mDebugger.GetRunState() == DebugManager.RunState.Exception;

            uint32 bkgColor = 0xFF404040;
            if (atError)
                bkgColor = 0xFF800000;
            if (gApp.IsCompiling)
                bkgColor = 0xFF303060;
            else if (gApp.mDebugger.mIsRunning)
                bkgColor = 0xFFCA5100;
			else if (gApp.AreTestsRunning())
				bkgColor = 0xFF562143;

            using (g.PushColor(bkgColor))
                g.FillRect(0, 0, mWidth, mHeight);

			// Helps debug MarkDirtys
			/*using (g.PushColor(Color.FromHSV(Utils.RandFloat(), 1.0f, 1.0f)))
			{
				g.FillRect(0, mHeight - 4, 4, 4);
			}*/

            g.SetFont(DarkTheme.sDarkTheme.mSmallFont);            

            var activeDocument = gApp.GetActiveDocumentPanel();
            if (activeDocument is SourceViewPanel)
            {
                var sourceViewPanel = (SourceViewPanel)activeDocument;
                int32 line;
                int32 column;
                sourceViewPanel.GetCursorPosition(out line, out column);

				/*var ewc = sourceViewPanel.mEditWidget.Content;
				int cursorPos = ewc.CursorTextPos;
				if (cursorPos < ewc.mData.mTextLength)
				{
					ewc.mData.mTextIdData.Prepare();
					g.DrawString(StackStringFormat!("Id {0}", ewc.mData.mTextIdData.GetIdAtIndex(cursorPos)), mWidth - GS!(310), 0);
				}*/


				/*line = 8'888'888;
				column = 8'888'888;*/

				if (gApp.mSettings.mEnableDevMode)
					g.DrawString(StackStringFormat!("Idx {0}", sourceViewPanel.mEditWidget.Content.CursorTextPos), mWidth - GS!(240), 0);
                g.DrawString(StackStringFormat!("Ln {0}", line + 1), mWidth - GS!(150), 0);
                g.DrawString(StackStringFormat!("Col {0}", column + 1), mWidth - GS!(78), 0);
            }

            using (g.PushColor(0xFF101010))
            {                
                //g.FillRect(50 + ((mUpdateCnt) % 50) * 2, 0, 3, mHeight);
            }

            var bfBuildCompiler = gApp.mBfBuildCompiler;
#if IDE_C_SUPPORT
            var buildClang = gApp.mDepClang;
#endif
            
            float? completionPct = null;

            if (bfBuildCompiler.HasQueuedCommands())
            {
                completionPct = bfBuildCompiler.GetCompletionPercentage();
                Debug.Assert(completionPct.GetValueOrDefault() >= 0);
            }
#if IDE_C_SUPPORT
            else if ((gApp.IsCompiling) && (buildClang.mCompileWaitsForQueueEmpty))
            {                
                mClangCommandQueueSize = Math.Max(mClangCommandQueueSize, buildClang.GetCommandQueueSize());
                if (mClangCommandQueueSize != 0)
                {
                    int32 itemsLeft = buildClang.GetCommandQueueSize();
                    completionPct = (mClangCommandQueueSize - itemsLeft) / (float)mClangCommandQueueSize;
                    Debug.Assert(completionPct.GetValueOrDefault() >= 0);
                    if (itemsLeft == 0)
                        mClangCommandQueueSize = 0;
                }
            }
#endif

			//completionPct = 0.4f;
            if (completionPct.HasValue)
            {                
                Rect completionRect = Rect(GS!(200), GS!(2), GS!(120), GS!(15));
                using (g.PushColor(0xFF000000))
                    g.FillRect(completionRect.mX, completionRect.mY, completionRect.mWidth, completionRect.mHeight);
                completionRect.Inflate(GS!(-1), GS!(-1));
                using (g.PushColor(0xFF00FF00))
                    g.FillRect(completionRect.mX, completionRect.mY, completionRect.mWidth * completionPct.Value, completionRect.mHeight);
            }
            else if ((gApp.mDebugger.mIsRunning) && (gApp.HaveSourcesChanged()))
            {
                Rect completionRect = Rect(GS!(200), GS!(1), GS!(120), GS!(17));
                using (g.PushColor(0x60000000))
                    g.FillRect(completionRect.mX, completionRect.mY, completionRect.mWidth, completionRect.mHeight);
                completionRect.Inflate(-1, -1);
                using (g.PushColor(0x40202080))
                    g.FillRect(completionRect.mX, completionRect.mY, completionRect.mWidth, completionRect.mHeight);

                g.DrawString("Source Changed", GS!(200), GS!(-1.3f), FontAlign.Centered, GS!(120));
            }

			void DrawStatusBox(StringView str)
			{
				if (mStatusBoxUpdateCnt == -1)
					mStatusBoxUpdateCnt = 0;

				float len = GS!(200);
				float x = GS!(350);
				Rect completionRect = Rect(x, GS!(1), len, GS!(17));
				using (g.PushColor(0x60000000))
				    g.FillRect(completionRect.mX, completionRect.mY, completionRect.mWidth, completionRect.mHeight);
				completionRect.Inflate(-1, -1);
				//float pulseSpeed = Math.Min(mStatusBoxUpdateCnt * 0.001f, 0.2f);
				float pulseSpeed = 0.2f;
				float pulsePct = -Math.Cos(Math.Max(mStatusBoxUpdateCnt - 30, 0) * pulseSpeed);
				using (g.PushColor(Color.FromHSV(0.1f, 0.5f, (float)Math.Max(pulsePct * 0.15f + 0.3f, 0.3f))))
				    g.FillRect(completionRect.mX, completionRect.mY, completionRect.mWidth, completionRect.mHeight);

				if (mCancelSymSrvButton != null)
					mCancelSymSrvButton.mX = completionRect.Right - GS!(16);

				g.DrawString(str, x, (int)GS!(-1.3f), FontAlign.Centered, len);
			}

			if (gApp.mKeyChordState != null)
			{
				String chordState = scope String();
				gApp.mKeyChordState.mCommandMap.ToString(chordState);
				chordState.Append(", <Awaiting Key>...");
				DrawStatusBox(chordState);
			}
			else if (mCancelSymSrvButton != null)
			{
				DrawStatusBox("Retrieving Debug Symbols...  ");
			}
			else if (mEvalCount > 20)
			{
				DrawStatusBox("Evaluating Expression");
			}
			else if (gApp.mRunningTestScript)
			{
				DrawStatusBox("Running Script");
			}
			else if ((gApp.mBuildContext != null) && (!completionPct.HasValue))
			{
				DrawStatusBox("Custom Build Commands...");
			}
			else
				mStatusBoxUpdateCnt = -1;

			if (gApp.mSettings.mEnableDevMode)
			{
	            g.DrawString(StackStringFormat!("FPS: {0}", gApp.mLastFPS), GS!(4), 0);

	            String resolveStr = scope String();
				let bfResolveCompiler = gApp.mBfResolveCompiler;
	            if ((bfResolveCompiler != null) && (gApp.mBfResolveCompiler.mThreadWorker.mThreadRunning))
				{
	                resolveStr.Append("B");
				}
				if ((bfResolveCompiler != null) && (gApp.mBfResolveCompiler.mThreadWorkerHi.mThreadRunning))
				{
				    resolveStr.Append("H");
				}
#if IDE_C_SUPPORT
	            if (gApp.mResolveClang.IsPerformingBackgroundOperation())
	            {
	                if (resolveStr.Length > 0)
	                    resolveStr.Append(" & ");
	                resolveStr.Append("Clang");
	            }
	            if (gApp.mDepClang.IsPerformingBackgroundOperation())
	            {
	                if (resolveStr.Length > 0)
	                    resolveStr.Append(" ");
	                resolveStr.Append("ClangB");
	            }
#endif

	            /*if (BfPassInstance.sPassInstances.Count > 0)
	            {
	                //resolveStr += String.Format(" PassInstances: {0}", BfPassInstance.sPassInstances.Count);
	
	                resolveStr += "ResolvePasses: {";
	
	                //foreach (var passInstance in BfPassInstance.sPassInstances)
	                for (int passIdx = 0; passIdx < BfPassInstance.sPassInstances.Count; passIdx++)
	                {
	                    var passInstance = BfPassInstance.sPassInstances[passIdx];
	                    if (passIdx > 0)
	                        resolveStr += ", ";
	                    if (passInstance.mDbgStr != null)
	                        resolveStr += passInstance.mDbgStr;
	                    resolveStr += String.Format(" #{0}", passInstance.mId);                        
	                }
	
	                resolveStr += "}";
	            }*/
	
	            if (resolveStr.Length != 0)
	                g.DrawString(resolveStr, GS!(100), 0);
			}
        }
    }
}
