using Beefy;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.theme;
using Beefy.gfx;
using System;
using Beefy.utils;
using System.IO;
using System.Diagnostics;
using System.Threading;

namespace LogViewer
{
	class LVApp : BFApp
	{
		public WidgetWindow mMainWindow;
		public Board mBoard;
		public Font mFont ~ delete _;

		public this()
		{
			gApp = this;
		}

		public override void Init()
		{
			base.Init();

			var dialog = scope OpenFileDialog();
			dialog.SetFilter("All files (*.*)|*.*");
			dialog.InitialDirectory = mInstallDir;
			dialog.Title = "Open Log";
			let result = dialog.ShowDialog();
			if ((result case .Err) || (dialog.FileNames.Count == 0))
			{
				Stop();
				return;
			}

			BeefPerf.Init("127.0.0.1", "LogViewer");

			DarkTheme darkTheme = new DarkTheme();
			darkTheme.Init();
			ThemeFactory.mDefault = darkTheme;

			BFWindow.Flags windowFlags = BFWindow.Flags.Border | //BFWindow.Flags.SysMenu | //| BFWindow.Flags.CaptureMediaKeys |
			    BFWindow.Flags.Caption | BFWindow.Flags.Minimize | BFWindow.Flags.QuitOnClose | BFWindowBase.Flags.Resizable |
			    BFWindow.Flags.SysMenu;

			mFont = new Font();
			float fontSize = 12;
			mFont.Load(scope String(BFApp.sApp.mInstallDir, "fonts/SourceCodePro-Regular.ttf"), fontSize);
			mFont.AddAlternate("Segoe UI Symbol", fontSize);
			mFont.AddAlternate("Segoe UI Historic", fontSize);
			mFont.AddAlternate("Segoe UI Emoji", fontSize);

			mBoard = new Board();
			mBoard.Load(dialog.FileNames[0]);
			mMainWindow = new WidgetWindow(null, "LogViewer", 0, 0, 1600, 1200, windowFlags, mBoard);
			//mMainWindow.mWindowKeyDownDelegate.Add(new => SysKeyDown);
			mMainWindow.SetMinimumSize(480, 360);
			mMainWindow.mIsMainWindow = true;
		}

		public void Fail(String str, params Object[] paramVals)
		{
			var errStr = scope String();
			errStr.AppendF(str, paramVals);
			Fail(errStr);
		}

		public void Fail(String text)
		{
#if CLI
			Console.WriteLine("ERROR: {0}", text);
			return;
#endif

#unwarn
			//Debug.Assert(Thread.CurrentThread == mMainThread);

		    if (mMainWindow == null)
		    {
		        //Internal.FatalError(StackStringFormat!("FAILED: {0}", text));
				Windows.MessageBoxA(0, text, "FATAL ERROR", 0);
				return;
		    }

		    //Beep(MessageBeepType.Error);

		    Dialog dialog = ThemeFactory.mDefault.CreateDialog("ERROR", text, DarkTheme.sDarkTheme.mIconError);
		    dialog.mDefaultButton = dialog.AddButton("OK");
		    dialog.mEscButton = dialog.mDefaultButton;
		    dialog.PopupWindow(mMainWindow);

			/*if (addWidget != null)
			{
				dialog.AddWidget(addWidget);
				addWidget.mY = dialog.mHeight - 60;
				addWidget.mX = 90;
			}*/
		}
	}

	static
	{
		public static LVApp gApp;
	}
}
