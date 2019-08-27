using System;
using Beefy;
using Beefy.widgets;

namespace BIStubUI
{
	class BIApp : BFApp
	{
		public function void InstallFunc(StringView dest, StringView filter);
		public function int ProgressFunc();
		public function void CancelFunc();

		public InstallFunc mInstallFunc;
		public ProgressFunc mProgressFunc;
		public CancelFunc mCancelFunc;

		Widget mRootWidget;
		WidgetWindow mMainWindow;

		const int cWidth = 700;
		const int cHeight = 700;

		public override void Init()
		{
			base.Init();

			BFWindow.Flags windowFlags = BFWindow.Flags.Border | BFWindow.Flags.SysMenu | //| BFWindow.Flags.CaptureMediaKeys |
			    BFWindow.Flags.Caption | BFWindow.Flags.Minimize | BFWindow.Flags.QuitOnClose;
			
			mRootWidget = new Widget();
			mMainWindow = new WidgetWindow(null, "Beef Installer", 0, 0, cWidth, cHeight, windowFlags, mRootWidget);
			mMainWindow.SetMinimumSize(480, 360);
			mMainWindow.mIsMainWindow = true;
		}
	}

	static
	{
		public static BIApp gApp;
	}
}
