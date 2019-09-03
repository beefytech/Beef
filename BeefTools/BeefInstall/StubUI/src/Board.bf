using Beefy.widgets;
using Beefy.geom;
using Beefy.gfx;
using System.Diagnostics;
using System;
using System.IO;
using System.Threading;

namespace BIStubUI
{
	class BiButtonWidget : ButtonWidget
	{
		public Image mImage;
		public Image mImageHi;

		public override void Draw(Graphics g)
		{
			if (mMouseOver && mMouseDown)
				g.Draw(mImageHi);
			if (mMouseOver)
		    	g.Draw(mImageHi);
			else
				g.Draw(mImage);

			/*using (g.PushColor(0x8000FF00))
				g.FillRect(0, 0, mWidth, mHeight);*/
		}

		public override void DrawAll(Graphics g)
		{
			using (g.PushColor(mDisabled ? 0xD0A0A0A0 : 0xFFFFFFFF))
				base.DrawAll(g);
		}

		public override void MouseEnter()
		{
			base.MouseEnter();
			if (!mDisabled)
			{
				gApp.SetCursor(.Hand);
				gApp.mSoundManager.PlaySound(Sounds.sMouseOver);
			}
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);
			if (!mDisabled)
				gApp.mSoundManager.PlaySound(Sounds.sButtonPress);
		}

		public override void MouseLeave()
		{
			base.MouseLeave();
			gApp.SetCursor(.Pointer);
		}
	}

	class BiDialogButton : BiButtonWidget
	{
		public String mLabel ~ delete _;

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			g.SetFont(gApp.mBtnFont);
			g.DrawString(mLabel, 0, 14, .Centered, mWidth);
		}
	}

	class BiCheckbox : CheckBox
	{
		public State mState;
		public String mLabel ~ delete _;

		public override bool Checked
		{
			get
			{
				return mState != .Unchecked;
			}

			set
			{
				gApp.mSoundManager.PlaySound(Sounds.sChecked);
				mState = value ? .Checked : .Unchecked;
			}
		}

		public override State State
		{
			get
			{
				return mState;
			}

			set
			{
				mState = value;
			}
		}

		public override void Draw(Graphics g)
		{
			if (mState == .Checked)
				g.Draw(Images.sChecked);
			else
				g.Draw(Images.sUnchecked);

			g.SetFont(gApp.mBodyFont);
			using (g.PushColor(0xFF000000))
				g.DrawString(mLabel, 40, 2);
		}

		public override void MouseEnter()
		{
			base.MouseEnter();
			gApp.SetCursor(.Hand);
		}

		public override void MouseLeave()
		{
			base.MouseLeave();
			gApp.SetCursor(.Pointer);
		}
	}

	class BiInstallPathBox : Widget
	{
		public String mInstallPath = new .() ~ delete _;
		ImageWidget mBrowseButton;

		public this()
		{
			mBrowseButton = new ImageWidget();
			mBrowseButton.mImage = Images.sBrowse;
			mBrowseButton.mDownImage = Images.sBrowseDown;
			mBrowseButton.mOnMouseClick.Add(new (mouseArgs) =>
				{
					var folderDialog = scope FolderBrowserDialog();
					if (folderDialog.ShowDialog(mWidgetWindow).GetValueOrDefault() == .OK)
					{
						var selectedPath = scope String..AppendF(folderDialog.SelectedPath);
						mInstallPath.Set(selectedPath);
					}
				});
			AddWidget(mBrowseButton);
		}

		public void ResizeComponenets()
		{
			mBrowseButton.Resize(mWidth - 30, 2, Images.sBrowse.mWidth, Images.sBrowse.mHeight);
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponenets();
		}

		public override void Update()
		{
			base.Update();
			ResizeComponenets();
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			g.DrawButton(Images.sTextBox, 0, 0, mWidth);
			using (g.PushColor(0xFF000000))
			{
				g.SetFont(gApp.mBodyFont);
				g.DrawString("Installation path", 0, -32);
				g.SetFont(gApp.mBoxFont);
				g.DrawString(mInstallPath, 4, 0, .Left, mWidth - 36, .Ellipsis);
			}
		}
	}

	class Board : Widget
	{
		const float cBodyX = 0;
		const float cBodyY = 20;

		LabelWidget mHeaderLabel;
		BiButtonWidget mCloseButton;
		BiDialogButton mCancelButton;
		BiDialogButton mInstallButton;
		BiCheckbox mInstallForAllCheckbox;
		BiCheckbox mAddToStartCheckbox;
		BiCheckbox mAddToDesktopCheckbox;
		BiCheckbox mStartAfterCheckbox;
		BiInstallPathBox mInstallPathBox;

		float mScale = 0.35f;
		float mScaleVel = 0.2f;

		float mSurprisePct = 1.0f;
		float mHeadRaise = 1.0f;
		float mEatPct;

		int mCloseTicks;
		int mInstallTicks;
		public bool mIsClosed;
		bool mUninstallDone;

		public bool mInstalling;
		public bool mUninstalling;
		public float mInstallPct;

		public this()
		{
			mHeaderLabel = new LabelWidget();
			mHeaderLabel.mLabel = new String("Beef Development Tools");
			mHeaderLabel.mAlign = .Centered;
			mHeaderLabel.mColor = 0xFF000000;
			mHeaderLabel.mFont = gApp.mHeaderFont;
			mHeaderLabel.mMouseVisible = false;
			AddWidget(mHeaderLabel);

			mCloseButton = new BiButtonWidget();
			mCloseButton.mImage = Images.sClose;
			mCloseButton.mImageHi = Images.sCloseHi;
			mCloseButton.mOnMouseClick.Add(new (mouseArgs) =>
				{
					gApp.mCancelling = true;
				});
			mCloseButton.mMouseInsets = new Insets(4, 4, 4, 4);
			AddWidget(mCloseButton);

			mInstallForAllCheckbox = new BiCheckbox();
			mInstallForAllCheckbox.mState = .Checked;
			mInstallForAllCheckbox.mLabel = new String("Install for all users");
			AddWidget(mInstallForAllCheckbox);

			mAddToStartCheckbox = new BiCheckbox();
			mAddToStartCheckbox.mState = .Checked;
			mAddToStartCheckbox.mLabel = new String("Add to Start menu");
			AddWidget(mAddToStartCheckbox);

			mAddToDesktopCheckbox = new BiCheckbox();
			mAddToDesktopCheckbox.mState = .Checked;
			mAddToDesktopCheckbox.mLabel = new String("Add to desktop");
			AddWidget(mAddToDesktopCheckbox);

			mStartAfterCheckbox = new BiCheckbox();
			mStartAfterCheckbox.mState = .Checked;
			mStartAfterCheckbox.mLabel = new String("Run after install");
			AddWidget(mStartAfterCheckbox);

			mInstallPathBox = new BiInstallPathBox();
			AddWidget(mInstallPathBox);

			mCancelButton = new BiDialogButton();
			mCancelButton.mLabel = new .("Cancel");
			mCancelButton.mImage = Images.sButton;
			mCancelButton.mImageHi = Images.sButtonHi;
			mCancelButton.mOnMouseClick.Add(new (mouseArgs) =>
				{
					//gApp.mCancelling = true;
				});
			mCancelButton.mMouseInsets = new Insets(4, 4, 4, 4);
			AddWidget(mCancelButton);

			mInstallButton = new BiDialogButton();
			mInstallButton.mLabel = new .("Install");
			mInstallButton.mImage = Images.sButton;
			mInstallButton.mImageHi = Images.sButtonHi;
			mInstallButton.mOnMouseClick.Add(new (mouseArgs) =>
				{
					StartInstall();
				});
			mInstallButton.mMouseInsets = new Insets(4, 4, 4, 4);
			AddWidget(mInstallButton);

			////

			int pidl = 0;
			Windows.SHGetSpecialFolderLocation(gApp.mMainWindow.HWND, Windows.CSIDL_PROGRAM_FILES, ref pidl);
			if (pidl != 0)
			{
				char8* selectedPathCStr = scope char8[Windows.MAX_PATH]*;
				Windows.SHGetPathFromIDList(pidl, selectedPathCStr);
				mInstallPathBox.mInstallPath.Set(StringView(selectedPathCStr));
			}
			else
			{
				mInstallPathBox.mInstallPath.Set(@"C:\Program Files");
			}
			mInstallPathBox.mInstallPath.Append(@"\BeefLang");
		}

		void Uninstall()
		{
			Thread.Sleep(10000);
			mUninstallDone = true;
		}

		void StartInstall()
		{
			mInstalling = true;
			mInstallButton.mDisabled = true;
			mInstallButton.mMouseVisible = false;
			mInstallPathBox.mVisible = false;
		}

		public override void Draw(Graphics g)
		{
			float bodyX = cBodyX;
			float bodyY = cBodyY;

			g.Draw(Images.sBody, bodyX, bodyY);

			float headRaise = mHeadRaise;
			headRaise += Math.Sin(Math.Clamp((mEatPct - 0.2f) * 1.4f, 0, 1.0f) * Math.PI_f*6) * 0.02f;
			
			float headX = bodyX + 664 - headRaise * 6;
			float headY = bodyY + 192 - headRaise * 30;

			headY += Math.Clamp(Math.Sin(Math.PI_f * mEatPct) * 3.0f, 0, 1) * 8.0f;

			Images.sHead.mPixelSnapping = .Never;
			Images.sEyesOpen.mPixelSnapping = .Never;
			Images.sEyesClosed.mPixelSnapping = .Never;
			g.Draw(Images.sHead, headX, headY);
			g.Draw((mSurprisePct > 0) ? Images.sEyesOpen : Images.sEyesClosed, headX + 70, headY + 190);

			if (mInstalling)
			{
				//float installDiv = 1000.0f;
				//mInstallPct = (mUpdateCnt % installDiv) / installDiv;

				//mInstallPct = 1.0f;

				float totalWidth = 410;
				float fillWidth = totalWidth * (mInstallPct*0.9f + 0.1f);
				if (mUninstalling)
					fillWidth = totalWidth * mInstallPct;

				float barX = 200;
				float barY = 280;

				float barHeight = Images.sPBBarBottom.mHeight;
				using (g.PushClip(barX, barY, totalWidth, barHeight))
				{
					g.DrawButton(Images.sPBBarBottom, barX, barY, totalWidth);

					Color colorLeft = 0x800288E9;
					Color colorRight = 0x80FFFFFF;
					if (gApp.mClosing)
					{
						colorLeft = 0x80000000;
						colorRight = 0x800288E9;
					}
					g.FillRectGradient(barX, barY, fillWidth, barHeight, colorLeft, colorRight, colorLeft, colorRight);

					float barPct = (mInstallTicks % 60) / 60.0f;
					for (int i = 0; i < 16; i++)
					{
						Images.sPBBarHilite.mPixelSnapping = .Never;
						using (g.PushColor(0x22FFFFFF))
							g.Draw(Images.sPBBarHilite, barX - 16 - totalWidth + fillWidth + (i + barPct) * 26, barY + 6);
					}

					g.DrawButton(Images.sPBBarEmpty, barX + fillWidth - 30, barY + 5, totalWidth - fillWidth + 40);

					g.DrawButton(Images.sPBFrameTop, barX, barY, totalWidth);

					g.DrawButton(Images.sPBFrameGlow, barX, barY, fillWidth);
				}
			}

			/*g.SetFont(gApp.mHdrFont);
			using (g.PushColor(0xFF000000))
				g.DrawString("Beef Development Tools", 400, 20, .Centered);*/
		}

		public override void MouseMove(float x, float y)
		{
			if (Rect(60, 24, 700, 420).Contains(x, y))
			{
				gApp.SetCursor(.SizeNESW);
			}
			else
			{
				gApp.SetCursor(.Pointer);
			}

			base.MouseMove(x, y);
		}

		public override void DrawAll(Graphics g)
		{
			int cBodyX = 0;
			int cBodyY = 0;

			/*using (g.PushColor(0x80FF0000))
				g.FillRect(0, 0, mWidth, mHeight);*/

			//float scaleX = (Math.Cos(mUpdateCnt * 0.1f) + 1.0f) * 0.5f;
			//float scaleY = scaleX;
			float scaleX = mScale;
			float scaleY = mScale;

			if ((Math.Abs(scaleX - 1.0f) < 0.001) && (Math.Abs(scaleY - 1.0f) < 0.001))
				base.DrawAll(g);
			else using (g.PushScale(scaleX, scaleY, cBodyX + 400, cBodyY + 560))
				base.DrawAll(g);
		}

		public bool IsDecompressing
		{
			get
			{
				//return gApp.
				return false;
			}
		}

		public override void Update()
		{
			base.Update();

			ResizeComponents();

			if (gApp.mClosing)
			{
				mCancelButton.mDisabled = true;
				mCancelButton.mMouseVisible = false;

				if ((mInstalling) && (!mUninstallDone))
				{
					if ((!IsDecompressing) && (!mUninstalling))
					{
						mUninstalling = true;
						ThreadPool.QueueUserWorkItem(new => Uninstall);
					}
					mInstallTicks--;
					if (mInstallTicks < 0)
						mInstallTicks = 0x3FFFFFFF;
				}

				if (mInstallPct > 0)
				{
					mInstallPct = (mInstallPct * 0.98f) - 0.003f;
					if (!mUninstallDone)
						mInstallPct = Math.Max(mInstallPct, 0.1f);
					return;
				}

				if ((mInstalling) && (!mUninstallDone))
				{
					return;
				}

				if (mCloseTicks == 0)
				{
					gApp.mSoundManager.PlaySound(Sounds.sAbort);
					mScaleVel = 0.055f;
				}
				mCloseTicks++;

				mScaleVel *= 0.90f;
				mScaleVel -= 0.01f;
				mScale += mScaleVel;
				mSurprisePct = 1.0f;
				mHeadRaise = Math.Clamp(mHeadRaise + 0.2f, 0, 1.0f);

				if (mScale <= 0)
				{
					mScale = 0.0f;
				}

				if (mCloseTicks == 60)
					mIsClosed = true;

				return;
			}

			if (mInstalling)
				mInstallTicks++;

			if (mUpdateCnt == 1)
				gApp.mSoundManager.PlaySound(Sounds.sBoing);

			float sizeTarget = Math.Min(0.5f + mUpdateCnt * 0.05f, 1.0f);

			float scaleDiff = sizeTarget - mScale;
			mScaleVel += scaleDiff * 0.05f;
			mScaleVel *= 0.80f;
			mScale += mScaleVel;

			mSurprisePct = Math.Max(mSurprisePct - 0.005f, 0);
			if (mUpdateCnt > 240)
			{
				mHeadRaise = Math.Max(mHeadRaise * 0.95f - 0.01f, 0);
			}

			if (mEatPct == 0.0f)
			{
				if ((mUpdateCnt == 600) || (mUpdateCnt % 2400 == 0))
				{
					mEatPct = 0.0001f;
				}
			}
			else
			{
				let prev = mEatPct;
				mEatPct += 0.004f;
				if ((prev < 0.2f) && (mEatPct >= 0.2f))
					gApp.mSoundManager.PlaySound(Sounds.sEating);

				if (mEatPct >= 1.0f)
				{
					//Debug.WriteLine("Eat done");
					mEatPct = 0;
				}
			}

			if (mUpdateCnt % 2200 == 0)
			{
				mSurprisePct = 0.5f;
			}
		}

		public override void UpdateAll()
		{
			base.UpdateAll();

			if (mWidgetWindow.IsKeyDown(.Control))
			{
				for (int i < 2)
					base.UpdateAll();
			}
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);
		}

		public override void KeyDown(KeyCode keyCode, bool isRepeat)
		{
			base.KeyDown(keyCode, isRepeat);

			if (keyCode == .Space)
			{
				gApp.mWantRehup = true;
			}
		}

		void UpdateComponent(Widget widget, int updateOffset, float centerX = 0.5f, float centerY = 0.5f, float speedMult = 1.0f)
		{
			float pct = Math.Clamp((mUpdateCnt - 50 - updateOffset) * 0.25f * speedMult, 0, 1.0f);
			//float pct = Math.Clamp((mUpdateCnt - 50 - updateOffset) * 0.02f, 0, 1.0f);
			if (pct == 0)
			{
				widget.SetVisible(false);
				return;
			}
			widget.SetVisible(true);
			if (pct == 1)
			{
				widget.ClearTransform();
				return;
			}
			
			Matrix matrix = .IdentityMatrix;
			matrix.Translate(-widget.mWidth * centerX, -widget.mHeight * centerY);
			matrix.Scale(pct, pct);
			matrix.Translate(widget.mWidth * centerX, widget.mHeight * centerY);
			matrix.Translate(widget.mX, widget.mY);
			widget.Transform = matrix;
		}

		void ResizeComponents()
		{
			float headerWidth = mHeaderLabel.CalcWidth();
			mHeaderLabel.Resize(cBodyX + 375 - headerWidth/2, cBodyY + 60, headerWidth, 60);
			UpdateComponent(mHeaderLabel, 0, 0.5f, 2.0f, 0.4f);

			mCloseButton.Resize(cBodyX + 660, cBodyY + 55, mCloseButton.mImage.mWidth, mCloseButton.mImage.mHeight);
			UpdateComponent(mCloseButton, 5);

			mInstallForAllCheckbox.Resize(cBodyX + 120, cBodyY + 136, 48, 48);
			UpdateComponent(mInstallForAllCheckbox, 10);

			mAddToStartCheckbox.Resize(cBodyX + 418, cBodyY + 136, 48, 48);
			UpdateComponent(mAddToStartCheckbox, 12);

			mAddToDesktopCheckbox.Resize(cBodyX + 120, cBodyY + 190, 48, 48);
			UpdateComponent(mAddToDesktopCheckbox, 14);

			mStartAfterCheckbox.Resize(cBodyX + 418, cBodyY + 190, 48, 48);
			UpdateComponent(mStartAfterCheckbox, 16);

			if (!mInstalling)
			{
				mInstallPathBox.Resize(cBodyX + 122, cBodyY + 276, 508, Images.sTextBox.mHeight);
				UpdateComponent(mInstallPathBox, 5, 0.1f, 0.5f, 0.4f);
			}

			mCancelButton.Resize(cBodyX + 180, cBodyY + 320, mCancelButton.mImage.mWidth, mCancelButton.mImage.mHeight);
			UpdateComponent(mCancelButton, 13, 0.5f, 0.2f);

			mInstallButton.Resize(cBodyX + 404, cBodyY + 320, mInstallButton.mImage.mWidth, mInstallButton.mImage.mHeight);
			UpdateComponent(mInstallButton, 15, 0.5f, 0.2f);
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}
	}
}
