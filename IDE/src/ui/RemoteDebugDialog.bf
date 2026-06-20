using System;
using System.Collections;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using System.IO;

namespace IDE.ui
{
	public class RemoteDebugDialog : DarkDialog
	{
		DarkEditWidget mHostEdit;
		DarkEditWidget mPortEdit;
		PathComboBox mElfPathCombo;
		PathComboBox mGdbExeCombo; // Only created when GDB is the active debugger
		DarkCheckBox mHardwareBreakpointsCheckbox;

		// The debugger is chosen globally; LLDB uses in-process liblldb (no external
		// executable), so the GDB-executable field is only relevant for GDB.
		bool mUseLLDB = (gApp.mSettings.mDebuggerSettings.mDebuggerKind == .LLDB);

		static String sLastHost    = new String("localhost") ~ delete _;
		static String sLastPort    = new String("3333") ~ delete _;
		static String sLastElfPath = new String() ~ delete _;
		static String sLastGdbExe  = new String() ~ delete _;
		static bool   sLastHardwareBreakpoints = true;

		public this()
		{
			Title = "Remote Debug";
			mWindowFlags = .ClientSized | .TopMost | .Caption | .Border | .SysMenu | .PopupPosition;
			mButtonBottomMargin = GS!(6);
			mButtonRightMargin = GS!(6);

			AddOkCancelButtons(new (evt) => { evt.mCloseDialog = false; Connect(); }, null, 0, 1);

			mHostEdit = new DarkEditWidget();
			mHostEdit.SetText(sLastHost);
			AddWidget(mHostEdit);
			AddEdit(mHostEdit);

			mPortEdit = new DarkEditWidget();
			mPortEdit.SetText(sLastPort);
			AddWidget(mPortEdit);
			AddEdit(mPortEdit);

			mElfPathCombo = new PathComboBox();
			mElfPathCombo.MakeEditable(new PathEditWidget());
			mElfPathCombo.Label = sLastElfPath;
			mElfPathCombo.mPopulateMenuAction.Add(new (dlg) =>
				{
					var item = dlg.AddItem("< Browse... >");
					item.mOnMenuItemSelected.Add(new (selItem) => { BrowseElf(); });
				});
			AddWidget(mElfPathCombo);
			AddEdit(mElfPathCombo.mEditWidget);

			if (!mUseLLDB)
			{
				mGdbExeCombo = new PathComboBox();
				mGdbExeCombo.MakeEditable(new PathEditWidget());
				mGdbExeCombo.Label = sLastGdbExe;
				mGdbExeCombo.mPopulateMenuAction.Add(new (dlg) =>
					{
						var item = dlg.AddItem("< Browse... >");
						item.mOnMenuItemSelected.Add(new (selItem) => { BrowseGdbExe(); });
					});
				AddWidget(mGdbExeCombo);
				AddEdit(mGdbExeCombo.mEditWidget);
			}

			mHardwareBreakpointsCheckbox = new DarkCheckBox();
			mHardwareBreakpointsCheckbox.Label = "&Hardware Breakpoints";
			mHardwareBreakpointsCheckbox.Checked = sLastHardwareBreakpoints;
			AddDialogComponent(mHardwareBreakpointsCheckbox);
		}

		void BrowseElf()
		{
#if !CLI
			var fileDialog = scope System.IO.OpenFileDialog();
			fileDialog.ShowReadOnly = false;
			fileDialog.Title = "Select ELF / Symbol File";
			fileDialog.Multiselect = false;
			fileDialog.ValidateNames = true;
			fileDialog.SetFilter("ELF files (*.elf)|*.elf|All files (*.*)|*.*");
			mWidgetWindow.PreModalChild();
			if (fileDialog.ShowDialog(gApp.GetActiveWindow()) case .Ok)
			{
				var fileNames = fileDialog.FileNames;
				if (!fileNames.IsEmpty)
					mElfPathCombo.Label = fileNames[0];
			}
#endif
		}

		void BrowseGdbExe()
		{
#if !CLI
			var fileDialog = scope System.IO.OpenFileDialog();
			fileDialog.ShowReadOnly = false;
			fileDialog.Title = "Select GDB Executable";
			fileDialog.Multiselect = false;
			fileDialog.ValidateNames = true;
			fileDialog.SetFilter("Executables (*.exe)|*.exe|All files (*.*)|*.*");
			mWidgetWindow.PreModalChild();
			if (fileDialog.ShowDialog(gApp.GetActiveWindow()) case .Ok)
			{
				var fileNames = fileDialog.FileNames;
				if (!fileNames.IsEmpty)
					mGdbExeCombo.Label = fileNames[0];
			}
#endif
		}

		void Connect()
		{
			String host = scope String();
			mHostEdit.GetText(host);
			host.Trim();
			String port = scope String();
			mPortEdit.GetText(port);
			port.Trim();
			String elfPath = scope String(mElfPathCombo.Label);
			elfPath.Trim();
			IDEUtils.FixFilePath(elfPath);
			String gdbExe = scope String();
			if (mGdbExeCombo != null)
				gdbExe.Set(mGdbExeCombo.Label);
			gdbExe.Trim();

			if (host.IsEmpty)
			{
				gApp.Fail("Host address cannot be empty");
				return;
			}
			if (port.IsEmpty)
			{
				gApp.Fail("Port cannot be empty");
				return;
			}

			if (gApp.mDebugger.mIsRunning)
			{
				gApp.Fail("An executable is already being debugged");
				return;
			}

			bool useHw = mHardwareBreakpointsCheckbox.Checked;
			bool useLLDB = mUseLLDB;

			String launchPath = scope String();
			launchPath.Append(elfPath);
			if (useLLDB)
				launchPath.AppendF("@{}:{}:{}", useHw ? "lldb_hw" : "lldb", host, port);
			else
			{
				launchPath.AppendF("@{}:{}:{}", useHw ? "gdb_hw" : "gdb", host, port);
				if (!gdbExe.IsEmpty)
					launchPath.AppendF(";{}", gdbExe);
			}

			sLastHost.Set(host);
			sLastPort.Set(port);
			sLastElfPath.Set(elfPath);
			sLastGdbExe.Set(gdbExe);
			sLastHardwareBreakpoints = useHw;

			gApp.[Friend]CheckDebugVisualizers();

			var emptyEnv = scope List<char8>();
			if (!gApp.mDebugger.OpenFile(launchPath, launchPath, "", "", emptyEnv, false, false, .None))
			{
				gApp.Fail(scope String()..AppendF("Unable to connect to remote target '{0}:{1}'", host, port));
				return;
			}

			gApp.mDebugger.mIsRunning = true;
			gApp.mDebugger.RehupBreakpoints(true);
			gApp.mDebugger.Run();

			Close();
		}

		public override void CalcSize()
		{
			mWidth = GS!(400);
			mHeight = mUseLLDB ? GS!(180) : GS!(218);
		}

		public override void ResizeComponents()
		{
			base.ResizeComponents();

			float curY = GS!(30);
			float fullW = mWidth - GS!(12);

			float portW = GS!(80);
			float hostW = fullW - portW - GS!(6);
			mHostEdit.Resize(GS!(6), curY, hostW, GS!(22));
			mPortEdit.Resize(GS!(6) + hostW + GS!(6), curY, portW, GS!(22));
			curY += GS!(42);

			// ELF / symbol file path
			mElfPathCombo.Resize(GS!(6), curY, fullW, GS!(22));
			curY += GS!(38);

			// GDB executable path (GDB only)
			if (mGdbExeCombo != null)
			{
				mGdbExeCombo.Resize(GS!(6), curY, fullW, GS!(22));
				curY += GS!(38);
			}

			mHardwareBreakpointsCheckbox.Resize(GS!(6), curY, mHardwareBreakpointsCheckbox.CalcWidth(), GS!(22));
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			g.DrawString("Host:", GS!(6), mHostEdit.mY - GS!(18));
			g.DrawString("Port:", mPortEdit.mX, mPortEdit.mY - GS!(18));
			g.DrawString("ELF / Symbol File (optional):", GS!(6), mElfPathCombo.mY - GS!(18));
			if (mGdbExeCombo != null)
				g.DrawString("GDB Executable (optional, e.g. arm-none-eabi-gdb):", GS!(6), mGdbExeCombo.mY - GS!(18));
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}

		public override void AddedToParent()
		{
			base.AddedToParent();
			mWidgetWindow.SetMinimumSize(GS!(300), mUseLLDB ? GS!(180) : GS!(218), true);
		}
	}
}
