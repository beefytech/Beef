using System;
using Beefy;
using Beefy.widgets;
using Beefy.geom;
using Beefy.gfx;

namespace BIStubUI
{
	class BIApp : BFApp
	{
		static int a = 123;

		public function void InstallFunc(StringView dest, StringView filter);
		public function int ProgressFunc();
		public function void CancelFunc();

		const int cWidth = 900;
		const int cHeight = 620;

		public Font mHeaderFont ~ delete _;
		public Font mBodyFont ~ delete _;
		public Font mBtnFont ~ delete _;
		public Font mBoxFont ~ delete _;

		public InstallFunc mInstallFunc;
		public ProgressFunc mProgressFunc;
		public CancelFunc mCancelFunc;

		public Board mBoard;
		public Widget mRootWidget;
		public WidgetWindow mMainWindow;

		public bool mCancelling;
		public bool mWantRehup;
		public int32 mBoardDelay;
		public bool mClosing;

		public ~this()
		{
			Images.Dispose();
		}

		public override void Init()
		{
			base.Init();

			BFWindow.Flags windowFlags = .QuitOnClose
				| .DestAlpha
				;

			GetWorkspaceRect(var workX, var workY, var workWidth, var workHeight);

			mRootWidget = new Widget();
			mMainWindow = new WidgetWindow(null, "Beef Installer",
				workX + (workWidth - cWidth)/2, workY + (workHeight - cHeight) / 2, cWidth, cHeight, windowFlags, mRootWidget);
			mMainWindow.mIsMainWindow = true;
			mMainWindow.mOnHitTest.Add(new (absX, absY) =>
				{
					float x = absX - mMainWindow.mX;
					float y = absY - mMainWindow.mY;

					Widget aWidget = mRootWidget.FindWidgetByCoords(x, y);
					if ((aWidget != null) && (aWidget != mBoard))
						return .NotHandled;

					if (Rect(60, 24, 700, 420).Contains(x, y))
						return .Caption;

					return .NotHandled;
				});

			Font CreateFont(StringView srcName, float fontSize)
			{
				Font font = new Font();
				font.Load(srcName, fontSize);
				font.AddAlternate("Segoe UI Symbol", fontSize);
				font.AddAlternate("Segoe UI Historic", fontSize);
				font.AddAlternate("Segoe UI Emoji", fontSize);
				return font;
			}

			mHeaderFont = CreateFont("Segoe UI Bold", 42);
			mBodyFont = CreateFont("Segoe UI", 22);
			mBoxFont = CreateFont("Segoe UI", 18);
			mBtnFont = CreateFont("Segoe UI", 32);

			Images.Init();
			Sounds.Init();

			SetupBoard();
		}

		void SetupBoard()
		{
			mBoard = new .();
			mRootWidget.AddWidget(mBoard);
			mBoard.Resize(0, 0, cWidth, cHeight);
			mBoard.SetFocus();
		}

		public override void Update(bool batchStart)
		{
			base.Update(batchStart);

			if (mCancelling)
			{
				mClosing = true;
				if ((mBoard != null) && (mBoard.mIsClosed))
					Stop();
			}

			if (mWantRehup)
			{
				mBoard.RemoveSelf();
				DeleteAndNullify!(mBoard);
				mBoardDelay = 30;
				mWantRehup = false;
			}

			if ((mBoardDelay > 0) && (--mBoardDelay == 0))
				SetupBoard();
		}
	}

	static
	{
		public static BIApp gApp;
	}
}
