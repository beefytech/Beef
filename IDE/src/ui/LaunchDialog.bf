using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using System.IO;
using System.Diagnostics;
using Beefy.theme;

namespace IDE.ui
{    
    public class LaunchDialog : IDEDialog
    {
		const String cBrowseCmd = "< Browse ... >";
		const String cCurProjectCmd = "< Current Debug Setting >";

		PathComboBox mTargetCombo;
        DarkComboBox mArgumentsCombo;
        DarkComboBox mWorkingDirCombo;
		DarkComboBox mEnvironmentCombo;

		DarkCheckBox mDebugCheckbox;
		DarkCheckBox mPausedCheckbox;
		DarkCheckBox mProfileCheckbox;
		bool mIgnoreContentChanged;

        public this()
        {
			mSettingHistoryManager = gApp.mLaunchHistoryManager;

			mTitle = new .("Launch Executable");
            mWindowFlags = .ClientSized | .TopMost | .Caption | .Border | .SysMenu | .Resizable | .PopupPosition;

            AddOkCancelButtons(new (evt) => { evt.mCloseDialog = false; Launch(); }, null, 0, 1);

            mButtonBottomMargin = GS!(6);
            mButtonRightMargin = GS!(6);

			mTargetCombo = new PathComboBox();
			mTargetCombo.MakeEditable(new PathEditWidget());
			mTargetCombo.Label = "";
			mTargetCombo.mPopulateMenuAction.Add(new => PopulateTargetMenu);
			AddWidget(mTargetCombo);
			AddEdit(mTargetCombo.mEditWidget);

			mArgumentsCombo = new DarkComboBox();
			mArgumentsCombo.MakeEditable();
			mArgumentsCombo.Label = "";
			mArgumentsCombo.mPopulateMenuAction.Add(new => PopulateArgumentsMenu);
			AddWidget(mArgumentsCombo);
			AddEdit(mArgumentsCombo.mEditWidget);

            mWorkingDirCombo = new PathComboBox();
			mWorkingDirCombo.MakeEditable(new PathEditWidget());
            mWorkingDirCombo.Label = "";
            mWorkingDirCombo.mPopulateMenuAction.Add(new => PopulateWorkingDirMenu);
			AddWidget(mWorkingDirCombo);
            AddEdit(mWorkingDirCombo.mEditWidget);

			mEnvironmentCombo = new DarkComboBox();
			mEnvironmentCombo.MakeEditable();
			mEnvironmentCombo.Label = "";
			mEnvironmentCombo.mPopulateMenuAction.Add(new => PopulateEnvironmentMenu);
			AddWidget(mEnvironmentCombo);
			AddEdit(mEnvironmentCombo.mEditWidget);

			mDebugCheckbox = new DarkCheckBox();
			mDebugCheckbox.Label = "&Debug";
			mDebugCheckbox.Checked = true;
			AddDialogComponent(mDebugCheckbox);

			mPausedCheckbox = new DarkCheckBox();
			mPausedCheckbox.Label = "&Start Paused";
			mPausedCheckbox.Checked = false;
			AddDialogComponent(mPausedCheckbox);

			mProfileCheckbox = new DarkCheckBox();
			mProfileCheckbox.Label = "P&rofile";
			mProfileCheckbox.Checked = false;
			AddDialogComponent(mProfileCheckbox);

			//mPrevButton = new DarkButton();
			//mPrevButton.Label = "< Prev";

			CreatePrevNextButtons();
		}

		protected override void UseProps(PropertyBag propBag)
		{
			mTargetCombo.Label = propBag.Get<String>("Target") ?? "";
			mArgumentsCombo.Label = propBag.Get<String>("Arguments") ?? "";
			mWorkingDirCombo.Label = propBag.Get<String>("WorkingDir") ?? "";
			mEnvironmentCombo.Label = propBag.Get<String>("EnvVars") ?? "";
			mDebugCheckbox.Checked = propBag.Get<bool>("Debug", true);
			mPausedCheckbox.Checked = propBag.Get<bool>("StartPaused");
			mProfileCheckbox.Checked = propBag.Get<bool>("Profile");
		}

		void SaveProps()
		{
			PropertyBag propBag = new PropertyBag();
			propBag.Add("Target", mTargetCombo.Label);
			propBag.Add("Arguments", mArgumentsCombo.Label);
			propBag.Add("WorkingDir", mWorkingDirCombo.Label);
			propBag.Add("EnvVars", mEnvironmentCombo.Label);
			propBag.Add("Debug", mDebugCheckbox.Checked);
			propBag.Add("StartPaused", mPausedCheckbox.Checked);
			propBag.Add("Profile", mProfileCheckbox.Checked);

			mSettingHistoryManager.Add(propBag);
		}

		bool GetStartupOption(delegate String (Project.Options options) dlg, String outString)
		{
			if (gApp.mWorkspace.mStartupProject != null)
			{
				let options = gApp.GetCurProjectOptions(gApp.mWorkspace.mStartupProject);
				let workspaceOptions = gApp.GetCurWorkspaceOptions();

				var inStr = dlg(options);
				if (inStr == null)
					return false;

				gApp.ResolveConfigString(gApp.mPlatformName, workspaceOptions, gApp.mWorkspace.mStartupProject, options, inStr, "", outString);
				return true;
			}
			return false;
		}

		void DoBrowse()
		{
#if !CLI
			String str = scope String();
			GetStartupOption(scope (options) => options.mDebugOptions.mCommand, str);

			String dir = scope String();
			Path.GetDirectoryPath(str, dir).IgnoreError();
			IDEUtils.FixFilePath(dir);

			var fileDialog = scope System.IO.OpenFileDialog();
			fileDialog.ShowReadOnly = false;
			fileDialog.Title = "Select Executable";
			fileDialog.Multiselect = true;
			if (!dir.IsEmpty)
				fileDialog.InitialDirectory = dir;
			fileDialog.ValidateNames = true;
			fileDialog.DefaultExt = ".exe";
			fileDialog.SetFilter("Executables (*.exe)|*.exe|All files (*.*)|*.*");
			mWidgetWindow.PreModalChild();
			if (fileDialog.ShowDialog(gApp.GetActiveWindow()) case .Ok)
			{
				var fileNames = fileDialog.FileNames;
				if (!fileNames.IsEmpty)
					mTargetCombo.Label = fileNames[0];
			}
#endif
		}

		void PopulateTargetMenu(Menu menu)
		{
			var item = menu.AddItem(cBrowseCmd);
			item.mOnMenuItemSelected.Add(new (dlg) =>
				{
					DoBrowse();
				});

			item = menu.AddItem(cCurProjectCmd);
			item.mOnMenuItemSelected.Add(new (dlg) =>
			{
				mIgnoreContentChanged = true;
				String str = scope String();
				GetStartupOption(scope (options) => options.mDebugOptions.mCommand, str);
				mTargetCombo.Label = str;
				mIgnoreContentChanged = false;
			});
		}

		void PopulateArgumentsMenu(Menu menu)
		{
			var item = menu.AddItem(cCurProjectCmd);
			item.mOnMenuItemSelected.Add(new (dlg) =>
			{
				mIgnoreContentChanged = true;
				String str = scope String();
				GetStartupOption(scope (options) => options.mDebugOptions.mCommandArguments, str);
				mArgumentsCombo.Label = str;
				mIgnoreContentChanged = false;
			});
		}

		void PopulateWorkingDirMenu(Menu menu)
		{
			var item = menu.AddItem(cCurProjectCmd);
			item.mOnMenuItemSelected.Add(new (dlg) =>
			{
				mIgnoreContentChanged = true;
				String str = scope String();
				GetStartupOption(scope (options) => options.mDebugOptions.mWorkingDirectory, str);
				mWorkingDirCombo.Label = str;
				mIgnoreContentChanged = false;
			});
		}

		void PopulateEnvironmentMenu(Menu menu)
		{
			var item = menu.AddItem(cCurProjectCmd);
			item.mOnMenuItemSelected.Add(new (dlg) =>
			{
				mIgnoreContentChanged = true;
				String str = scope String();

				for (int i = 0; true; i++)
				{
					var curStr = scope String();

					if (!GetStartupOption(scope (options) =>
						{
							if (i < options.mDebugOptions.mEnvironmentVars.Count)
								return options.mDebugOptions.mEnvironmentVars[i];
							return null;
						}, curStr))
						break;

					if (!str.IsEmpty)
						str.Append(";");
					str.Append(curStr);
				}
				mEnvironmentCombo.Label = str;
				mIgnoreContentChanged = false;
			});
		}

		public static void DoLaunch(LaunchDialog dialog, String targetPath, String arguments, String workingDir, String envVarStr, bool startPaused, bool debug)
		{
			var workingDir;
			
			if (!File.Exists(targetPath))
			{
				dialog?.mTargetCombo.SetFocus();
				gApp.Fail(scope String()..AppendF("Unable to locate target file '{0}'", targetPath));
				return;
			}

			if (workingDir.IsWhiteSpace)
			{
				workingDir = scope:: .(@workingDir);
				Path.GetDirectoryPath(targetPath, workingDir);
			}
			if (workingDir.EndsWith('\\') || workingDir.EndsWith('/'))
				workingDir.RemoveFromEnd(1);

			if (!Directory.Exists(workingDir))
			{
				dialog?.mWorkingDirCombo.SetFocus();
				gApp.Fail(scope String()..AppendF("Unable to locate working directory '{0}'", workingDir));
				return;
			}

			if (gApp.mDebugger.mIsRunning)
			{
				gApp.Fail("An executable is already being debugged");
				return;
			}

			gApp.mProfilePanel.Clear();
			gApp.mDebugger.ClearInvalidBreakpoints();

			gApp.mExecutionPaused = false;
			gApp.mTargetDidInitBreak = false;
			gApp.mTargetHadFirstBreak = false;

			if (!debug)
			{
				ProcessStartInfo procInfo = scope ProcessStartInfo();
				procInfo.UseShellExecute = false;
				procInfo.SetWorkingDirectory(workingDir);
				procInfo.SetArguments(arguments);
				for (var envVar in envVarStr.Split(';'))
				{
					envVar.Trim();
					if (envVar.IsEmpty)
						continue;
					int eqPos = envVar.IndexOf('=', 1);
					if (eqPos == -1)
					{
						gApp.OutputErrorLine("Invalid environment variable: {0}", envVar);
					}
					else
					{
						procInfo.AddEnvironmentVariable(StringView(envVar, 0, eqPos), StringView(envVar, eqPos + 1));
					}
				}
					
				procInfo.SetFileName(targetPath);

				var spawnedProcess = scope SpawnedProcess();
				if (spawnedProcess.Start(procInfo) case .Err)
				{
					gApp.Fail(scope String()..AppendF("Unable to start executable: {0}", targetPath));
				}
				return;
			}

			if (startPaused)
			{
				gApp.mTargetDidInitBreak = true;
				gApp.mTargetHadFirstBreak = true;
			}

			var envVars = scope Dictionary<String, String>();
			defer { for (var kv in envVars) { delete kv.key; delete kv.value; } }
			Environment.GetEnvironmentVariables(envVars);

			for (var envVar in envVarStr.Split(';'))
			{
				envVar.Trim();
				if (envVar.IsEmpty)
					continue;
				int eqPos = envVar.IndexOf('=', 1);
				if (eqPos == -1)
				{
					gApp.OutputErrorLine("Invalid environment variable: {0}", envVar);
				}
				else
				{
					Environment.SetEnvironmentVariable(envVars, StringView(envVar, 0, eqPos), StringView(envVar, eqPos + 1));
				}
			}

			var envBlock = scope List<char8>();
			Environment.EncodeEnvironmentVariables(envVars, envBlock);

			if (!gApp.mDebugger.OpenFile(targetPath, targetPath, arguments, workingDir, envBlock, false, false))
			{
				gApp.Fail(scope String()..AppendF("Unable to open executable for debugging: {0}", targetPath));
			    return;
			}

			gApp.[Friend]CheckDebugVisualizers();
			gApp.mDebugger.mIsRunning = true;
			gApp.mDebugger.RehupBreakpoints(true);
			gApp.mDebugger.Run();
		}

        void Launch()
        {
            var targetPath = scope String();
			targetPath.Append(mTargetCombo.Label);
			IDEUtils.FixFilePath(targetPath);

			String workingDir = scope String();
			
			workingDir.Append(mWorkingDirCombo.Label);
			if (Path.IsPathRooted(workingDir))
			{
				defer targetPath.Remove(0, targetPath.Length);
				Path.GetAbsolutePath(targetPath, workingDir, targetPath);
			}
			else
			{
				String targetDir = scope .();
				Path.GetDirectoryPath(targetPath, targetDir);
				if (!targetDir.IsWhiteSpace)
				{
					defer workingDir.Remove(0, workingDir.Length);
					Path.GetAbsolutePath(workingDir, targetDir, workingDir);
				}
			}

			String envVarStr = scope String();
			envVarStr.Append(mEnvironmentCombo.Label);

			String arguments = scope String();
			arguments.Append(mArgumentsCombo.Label);

			DoLaunch(this, targetPath, arguments, workingDir, envVarStr, mPausedCheckbox.Checked, mDebugCheckbox.Checked);

			SaveProps();

			Close();
        }

        public override void ResizeComponents()
        {
            base.ResizeComponents();

            float curY = GS!(30);
			
			mTargetCombo.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));

            curY += GS!(42);
            mArgumentsCombo.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));

			curY += GS!(42);
			mWorkingDirCombo.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));

			curY += GS!(42);
			mEnvironmentCombo.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));

			curY += GS!(28);
			mDebugCheckbox.Resize(GS!(6), curY, mProfileCheckbox.CalcWidth(), GS!(22));
			curY += GS!(22);
			mPausedCheckbox.Resize(GS!(6), curY, mPausedCheckbox.CalcWidth(), GS!(22));
			curY += GS!(22);
			mProfileCheckbox.Resize(GS!(6), curY, mProfileCheckbox.CalcWidth(), GS!(22));
        }

        public override void CalcSize()
        {
            mWidth = GS!(400);
            mHeight = GS!(260);
        }

		public override void AddedToParent()
		{
			base.AddedToParent();
			RehupMinSize();
		}

		void RehupMinSize()
		{
			mWidgetWindow.SetMinimumSize(GS!(280), GS!(280), true);
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			RehupMinSize();
		}

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeComponents();
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

					using (g.PushColor(ThemeColors.Theme.Text.Color)) {
			g.DrawString("Target Path:", GS!(6), mTargetCombo.mY - GS!(18));
            g.DrawString("Arguments:", GS!(6), mArgumentsCombo.mY - GS!(18));
            g.DrawString("Working Directory:", GS!(6), mWorkingDirCombo.mY - GS!(18));
			g.DrawString("Environment Variables:", GS!(6), mEnvironmentCombo.mY - GS!(18));
					}
        }

		public override void Update()
		{
			base.Update();
		}
    }
}
