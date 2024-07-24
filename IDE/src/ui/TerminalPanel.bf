#pragma warning disable 168

using System;
using Beefy.geom;
using Beefy.gfx;
using System.Text;
using Beefy.theme.dark;
using System.Security.Cryptography;
using Beefy.widgets;
using Beefy.events;
using System.Diagnostics;
using Beefy.utils;
using IDE.util;

namespace IDE.ui;

class TerminalPanel : ConsolePanel
{
	public override void Serialize(StructuredData data)
	{
		data.Add("Type", "TerminalPanel");
	}

	public override void Init()
	{
		var consoleProvider = new BeefConConsoleProvider();
		consoleProvider.mBeefConExePath = new $"{gApp.mInstallDir}/BeefCon.exe";
		consoleProvider.mTerminalExe = new .(gApp.mSettings.mWindowsTerminal);

		mConsoleProvider = consoleProvider;
	}

	public override void AddedToParent()
	{
		var consoleProvider = (BeefConConsoleProvider)mConsoleProvider;
		consoleProvider.mTerminalExe.Set(gApp.mSettings.mWindowsTerminal);
		consoleProvider.mWorkingDir.Set(gApp.mWorkspace.mDir);
		mConsoleProvider.Attach();
	}

	public override void RemovedFromParent(Widget previousParent, WidgetWindow window)
	{
		
	}

	public override void Update()
	{
		base.Update();

		
	}

	public void OpenDirectory(StringView path)
	{
		var consoleProvider = (BeefConConsoleProvider)mConsoleProvider;
		consoleProvider.mWorkingDir.Set(path);
		consoleProvider.Detach();
		consoleProvider.Attach();
	}
}