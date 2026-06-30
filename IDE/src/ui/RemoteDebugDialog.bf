using System;
using System.Collections;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;

namespace IDE.ui
{
	public class RemoteDebugDialog : DarkDialog
	{
		// Which debugger + connection method is being configured. The remote debugger is chosen
		// explicitly here (independent of the preferences "debugger kind", which only selects the
		// default *native* debugger). The selected method drives both the fields shown and the
		// "@<tag>" suffix appended to the launch path, which is what selects the backend.
		enum ConnKind
		{
			LldbRemote, // <elf>@{lldb_hw|lldb}:<host>:<port>
			GdbServer,  // <elf>@{gdb_hw|gdb}:<host>:<port>[;<gdbexe>]
			GdbSsh      // <elf>@{gdb_ssh_hw|gdb_ssh}:<server>
		}

		enum FieldKind
		{
			Host,
			Port,
			SshServer,
			ElfPath,
			GdbExe,
			HwBreakpoints
		}

		struct FieldSpec
		{
			public FieldKind mKind;
			public StringView mLabel;
			public bool mRemote; // For path fields: true = a path on the remote machine (no "..." browse)

			public this(FieldKind kind, StringView label, bool remote = false)
			{
				mKind = kind;
				mLabel = label;
				mRemote = remote;
			}
		}

		class MethodInfo
		{
			public String mName ~ delete _;
			public ConnKind mConnKind;
			public List<FieldSpec> mFields = new .() ~ delete _;
		}

		List<MethodInfo> mMethods = new .() ~ DeleteContainerAndItems!(_);
		MethodInfo mSelectedMethod;

		DarkComboBox mMethodCombo;
		Dictionary<FieldKind, Widget> mFieldWidgets = new .() ~ delete _; // Widgets owned by the widget tree

		// Remember last-used selections across opens (independent of the preferences debugger kind).
		static String sLastMethodName = new String("LLDB - GDB Remote Protocol") ~ delete _;
		static String sLastHost       = new String("localhost") ~ delete _;
		static String sLastPort       = new String("3333") ~ delete _;
		static String sLastSshServer  = new String() ~ delete _;
		static String sLastElfPath    = new String() ~ delete _;
		static String sLastGdbExe     = new String() ~ delete _;
		static bool   sLastHardwareBreakpoints = true;

		public this()
		{
			Title = "Remote Debug";
			mWindowFlags = .ClientSized | .TopMost | .Caption | .Border | .SysMenu | .PopupPosition;
			mButtonBottomMargin = GS!(6);
			mButtonRightMargin = GS!(6);

			BuildMethods();

			AddOkCancelButtons(new (evt) => { evt.mCloseDialog = false; Connect(); }, null, 0, 1);

			mMethodCombo = new DarkComboBox();
			mMethodCombo.mPopulateMenuAction.Add(new (menu) =>
				{
					for (var method in mMethods)
					{
						var item = menu.AddItem(method.mName);
						item.mOnMenuItemSelected.Add(new (selItem) => { SelectMethod(selItem.mLabel); });
					}
				});
			AddDialogComponent(mMethodCombo);

			mSelectedMethod = FindMethod(sLastMethodName) ?? mMethods[0];
			mMethodCombo.Label = mSelectedMethod.mName;

			RebuildFields();
		}

		void BuildMethods()
		{
			MethodInfo Add(StringView name, ConnKind connKind)
			{
				var mi = new MethodInfo();
				mi.mName = new String(name);
				mi.mConnKind = connKind;
				mMethods.Add(mi);
				return mi;
			}

			var lldb = Add("LLDB - GDB Remote Protocol", .LldbRemote);
			lldb.mFields.Add(.(.Host, "Host (remote target):"));
			lldb.mFields.Add(.(.Port, "Port:"));
			lldb.mFields.Add(.(.ElfPath, "ELF / Symbol File (optional):"));
			lldb.mFields.Add(.(.HwBreakpoints, "Hardware Breakpoints"));

			var gdbServer = Add("GDB - gdbserver (Remote)", .GdbServer);
			gdbServer.mFields.Add(.(.Host, "Host (remote target):"));
			gdbServer.mFields.Add(.(.Port, "Port:"));
			gdbServer.mFields.Add(.(.ElfPath, "ELF / Symbol File (optional):"));
			gdbServer.mFields.Add(.(.GdbExe, "GDB Executable (e.g. arm-none-eabi-gdb):"));
			gdbServer.mFields.Add(.(.HwBreakpoints, "Hardware Breakpoints"));

			var gdbSsh = Add("GDB - SSH", .GdbSsh);
			gdbSsh.mFields.Add(.(.SshServer, "SSH Server (e.g. user@host):"));
			gdbSsh.mFields.Add(.(.ElfPath, "ELF / Symbol File:", true));
			gdbSsh.mFields.Add(.(.HwBreakpoints, "Hardware Breakpoints"));
		}

		MethodInfo FindMethod(StringView name)
		{
			for (var method in mMethods)
				if (method.mName == name)
					return method;
			return null;
		}

		void SelectMethod(StringView name)
		{
			var method = FindMethod(name);
			if ((method == null) || (method == mSelectedMethod))
				return;
			mSelectedMethod = method;
			mMethodCombo.Label = method.mName;
			sLastMethodName.Set(method.mName);
			RebuildFields();
		}

		StringView SeedFor(FieldKind kind)
		{
			switch (kind)
			{
			case .Host: return sLastHost;
			case .Port: return sLastPort;
			case .SshServer: return sLastSshServer;
			case .ElfPath: return sLastElfPath;
			case .GdbExe: return sLastGdbExe;
			default: return "";
			}
		}

		Widget CreateFieldWidget(FieldSpec spec)
		{
			switch (spec.mKind)
			{
			case .Host, .Port, .SshServer:
				{
					var editWidget = new DarkEditWidget();
					editWidget.SetText(scope String(SeedFor(spec.mKind)));
					return editWidget;
				}
			case .ElfPath, .GdbExe:
				{
					if (spec.mRemote)
					{
						// Path lives on the remote machine - no local "..." browse button.
						var editWidget = new DarkEditWidget();
						editWidget.SetText(scope String(SeedFor(spec.mKind)));
						return editWidget;
					}

					// Local path - PathEditWidget(.File) provides the "..." browse button.
					var pathWidget = new PathEditWidget(.File);
					if ((gApp.mWorkspace != null) && (gApp.mWorkspace.mDir != null))
						pathWidget.mDefaultFolderPath = new String(gApp.mWorkspace.mDir);
					pathWidget.SetText(scope String(SeedFor(spec.mKind)));
					return pathWidget;
				}
			case .HwBreakpoints:
				{
					var checkbox = new DarkCheckBox();
					checkbox.Label = "&Hardware Breakpoints";
					checkbox.Checked = sLastHardwareBreakpoints;
					return checkbox;
				}
			}
		}

		bool TryGetEditText(FieldKind kind, String outStr)
		{
			if (mFieldWidgets.TryGetValue(kind, let widget))
			{
				((EditWidget)widget).GetText(outStr);
				return true;
			}
			return false;
		}

		void SaveFieldsToStatics()
		{
			String tmp = scope .();
			if (TryGetEditText(.Host, tmp..Clear())) sLastHost.Set(tmp);
			if (TryGetEditText(.Port, tmp..Clear())) sLastPort.Set(tmp);
			if (TryGetEditText(.SshServer, tmp..Clear())) sLastSshServer.Set(tmp);
			if (TryGetEditText(.ElfPath, tmp..Clear())) sLastElfPath.Set(tmp);
			if (TryGetEditText(.GdbExe, tmp..Clear())) sLastGdbExe.Set(tmp);
			if (mFieldWidgets.TryGetValue(.HwBreakpoints, let widget))
				sLastHardwareBreakpoints = ((DarkCheckBox)widget).Checked;
		}

		void RebuildFields()
		{
			// Preserve current values so shared fields carry across method switches.
			if (!mFieldWidgets.IsEmpty)
				SaveFieldsToStatics();

			// Tear down the existing field widgets.
			for (var widget in mFieldWidgets.Values)
			{
				mTabWidgets.Remove(widget);
				widget.RemoveSelf();
				delete widget;
			}
			mFieldWidgets.Clear();
			mDialogEditWidget = null;

			// Build the field widgets for the selected method.
			for (var spec in mSelectedMethod.mFields)
			{
				var widget = CreateFieldWidget(spec);
				if (let checkbox = widget as DarkCheckBox)
					AddDialogComponent(checkbox);
				else
					AddEdit((EditWidget)widget);
				mFieldWidgets[spec.mKind] = widget;
			}

			ApplySize();
			ResizeComponents();
		}

		float ComputeHeight()
		{
			float curY = GS!(28);
			curY += GS!(46); // method combo

			var fields = mSelectedMethod.mFields;
			for (int i = 0; i < fields.Count; i++)
			{
				let kind = fields[i].mKind;
				if (kind == .HwBreakpoints)
					curY += GS!(28);
				else if ((kind == .Host) && (i + 1 < fields.Count) && (fields[i + 1].mKind == .Port))
				{
					curY += GS!(40);
					i++; // Host and Port share a row
				}
				else
					curY += GS!(40);
			}

			curY += GS!(40); // button area
			return curY;
		}

		void ApplySize()
		{
			mWidth = GS!(440);
			mHeight = ComputeHeight();
			if (mWidgetWindow != null)
			{
				mWidgetWindow.SetMinimumSize((.)GS!(320), (.)mHeight, true);
				mWidgetWindow.ResizeClient((.)mWidth, (.)mHeight);
			}
		}

		void Connect()
		{
			String host = scope .();
			String port = scope .();
			String sshServer = scope .();
			String elfPath = scope .();
			String gdbExe = scope .();

			TryGetEditText(.Host, host);
			TryGetEditText(.Port, port);
			TryGetEditText(.SshServer, sshServer);
			TryGetEditText(.ElfPath, elfPath);
			TryGetEditText(.GdbExe, gdbExe);

			host.Trim();
			port.Trim();
			sshServer.Trim();
			elfPath.Trim();
			gdbExe.Trim();
			IDEUtils.FixFilePath(elfPath);

			bool useHw = false;
			if (mFieldWidgets.TryGetValue(.HwBreakpoints, let widget))
				useHw = ((DarkCheckBox)widget).Checked;

			switch (mSelectedMethod.mConnKind)
			{
			case .LldbRemote, .GdbServer:
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
			case .GdbSsh:
				if (sshServer.IsEmpty)
				{
					gApp.Fail("SSH server cannot be empty");
					return;
				}
			}

			if (gApp.mDebugger.mIsRunning)
			{
				gApp.Fail("An executable is already being debugged");
				return;
			}

			String launchPath = scope .();
			launchPath.Append(elfPath);
			switch (mSelectedMethod.mConnKind)
			{
			case .LldbRemote:
				launchPath.AppendF("@{}:{}:{}", useHw ? "lldb_hw" : "lldb", host, port);
			case .GdbServer:
				launchPath.AppendF("@{}:{}:{}", useHw ? "gdb_hw" : "gdb", host, port);
				if (!gdbExe.IsEmpty)
					launchPath.AppendF(";{}", gdbExe);
			case .GdbSsh:
				launchPath.AppendF("@{}:{}", useHw ? "gdb_ssh_hw" : "gdb_ssh", sshServer);
			}

			sLastHost.Set(host);
			sLastPort.Set(port);
			sLastSshServer.Set(sshServer);
			sLastElfPath.Set(elfPath);
			sLastGdbExe.Set(gdbExe);
			sLastHardwareBreakpoints = useHw;
			sLastMethodName.Set(mSelectedMethod.mName);

			gApp.[Friend]CheckDebugVisualizers();

			var emptyEnv = scope List<char8>();
			if (!gApp.mDebugger.OpenFile(launchPath, launchPath, "", "", emptyEnv, false, false, .None))
			{
				gApp.Fail("Unable to connect to remote target");
				return;
			}

			gApp.mDebugger.mIsRunning = true;
			gApp.mDebugger.RehupBreakpoints(true);
			gApp.mDebugger.Run();

			Close();
		}

		public override void CalcSize()
		{
			mWidth = GS!(440);
			mHeight = ComputeHeight();
		}

		public override void ResizeComponents()
		{
			base.ResizeComponents();

			float x = GS!(6);
			float fullW = mWidth - GS!(12);
			float curY = GS!(28);

			mMethodCombo.Resize(x, curY, fullW, GS!(28));
			curY += GS!(46);

			var fields = mSelectedMethod.mFields;
			for (int i = 0; i < fields.Count; i++)
			{
				let kind = fields[i].mKind;
				var widget = mFieldWidgets[kind];

				if (kind == .HwBreakpoints)
				{
					var checkbox = (DarkCheckBox)widget;
					checkbox.Resize(x, curY, checkbox.CalcWidth(), GS!(22));
					curY += GS!(28);
				}
				else if ((kind == .Host) && (i + 1 < fields.Count) && (fields[i + 1].mKind == .Port))
				{
					float portW = GS!(80);
					float hostW = fullW - portW - GS!(6);
					widget.Resize(x, curY, hostW, GS!(22));
					mFieldWidgets[.Port].Resize(x + hostW + GS!(6), curY, portW, GS!(22));
					curY += GS!(40);
					i++; // Host and Port share a row
				}
				else
				{
					widget.Resize(x, curY, fullW, GS!(22));
					curY += GS!(40);
				}
			}
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			g.DrawString("Method:", GS!(6), mMethodCombo.mY - GS!(18));

			for (var spec in mSelectedMethod.mFields)
			{
				if (spec.mKind == .HwBreakpoints)
					continue;
				if (mFieldWidgets.TryGetValue(spec.mKind, let widget))
					g.DrawString(spec.mLabel, widget.mX, widget.mY - GS!(18));
			}
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}

		public override void AddedToParent()
		{
			base.AddedToParent();
			mWidgetWindow.SetMinimumSize((.)GS!(320), (.)mHeight, true);
		}
	}
}
