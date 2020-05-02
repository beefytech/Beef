#if false

using System;
using Beefy;
using Beefy.utils;
using System.Collections;
using Beefy.widgets;
using System.Diagnostics;
using Beefy.res;
using Beefy.gfx;
using Beefy.theme;
using Beefy.theme.dark;

namespace IDE
{
	class Board : Widget
	{
		EditWidget mEditWidget ~ delete _;

		public this()
		{
			ThemeFactory.mDefault = DarkTheme.Get();
			mEditWidget = DarkTheme.sDarkTheme.CreateEditWidget(this, 5, 5, 400, 400);
			mEditWidget.Content.mIsMultiline = true;
			mEditWidget.Content.mWordWrap = false;
		}						   	

		public override void Draw(Beefy.gfx.Graphics  g)
		{
			base.Draw(g);

			using (g.PushColor(0xFF000030))
				g.FillRect(0, 0, mWidth, mHeight);

			using (g.PushColor(0xFFFF0000))
				g.FillRect(10 + (mUpdateCnt % 200), 10, 200, 200);

			g.SetFont(IDEApp.sApp.mCodeFont);
			g.DrawString("This is a test string", 50, 50);
		}

		public override void Update()
		{
			base.Update();

			if ((mUpdateCnt % 100) == 0)
			{
				Debug.WriteLine("Tick: {0}", mUpdateCnt);
			}
		}
	}

	class IDEApp : BFApp
	{
		WidgetWindow mMainWindow ~ delete _;
		Board mBoard ~ delete _;
		public Font mCodeFont ~ delete _;
		public new static IDEApp sApp;

		public override void Init()
		{
			sApp = this;

			base.Init();

			mBoard = new Board();
			mBoard.Resize(0, 0, 640, 480);

			mMainWindow = new WidgetWindow(null, "Main Window", 64, 64, 640, 480, 
            	BFWindowBase.Flags.Border | BFWindowBase.Flags.Caption | BFWindowBase.Flags.QuitOnClose | BFWindowBase.Flags.SysMenu | BFWindowBase.Flags.Minimize, mBoard);

			String fileName = scope String();
			fileName.Append(BFApp.sApp.mInstallDir, "fonts/SourceCodePro9.fnt");
			mCodeFont = Font.LoadFromFile(fileName);
		}
	}

	class Program
	{
		public static int Main(string[] args)
		{
			string testStr = scope String();
            testStr.AppendF("0x{0:x}L", (long)0x123456789ABCDEF'UL);

			IDEApp app = new IDEApp();

			long val = long.Parse("ABC", System.Globalization.NumberStyles.HexNumber);

			app.Init();
			app.Run();
			delete app;


			/*StructuredData sd = StructuredData.LoadFromFile("c:/temp/test.json");
			String str = stack String();
			sd.GetString(str, "Hey");

			delete sd;

			//Object obj = null;

			var outStr = stack String();
			outStr.AppendF("A{0} B{1}", 123, "Yo");
			string text = scope String();
			Utils.LoadTextFile(text, "c:/temp/Test.txt");
			
			//int[] intArr = stack {3, 6, 8};

			Dictionary<int, float> testMap = new Dictionary<int, float>();
			testMap[3] = 4.5f;
			testMap[4] = 5.6f;
			testMap[6] = 6.7f;

			for (var pair in testMap)
			{
				var key = pair.Key;
				var val = pair.Value;
				//
			}

			{
				int crap = 123;
			}

			float val = testMap[3];
			(void)val;
			val = testMap[4];
			val = testMap[6];

			PrintF("Hello!");		  */
			return 0;
		}
	}
}

#endif