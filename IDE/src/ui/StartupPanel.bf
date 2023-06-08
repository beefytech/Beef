using System;
using System.IO;
using System.Collections;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;

using IDE.util;
using Beefy.events;
using Beefy.theme;
using System.Diagnostics;

namespace IDE.ui
{
	class StartupPanel : Panel
	{
		class RecentWorkspacesScrollWidget : ScrollableWidget
		{
			public Font mTitleFont;

			public this()
			{
				mScrollContent = new Widget();
				mScrollContentContainer.AddWidget(mScrollContent);
			}

			public void AddContentWidget(Widget widget)
			{
				mScrollContent.AddWidget(widget);
			}

			public void ResizeSelf(float x, float y, float width, float height)
			{
				const float MARGIN = 3;
				float currentY = 0;

				float fillWidth = width - (mVertScrollbar?.Width).GetValueOrDefault();

				if (mScrollContent.mChildWidgets != null && fillWidth >= 0)
				{
					for (let widget in mScrollContent.mChildWidgets)
					{
						widget.Resize(0, currentY, fillWidth, GS!(30));
						currentY += widget.Height + MARGIN;
					}
				}

				if (currentY > height)
					InitScrollbars(false, true);

				Resize(x, y, width, Math.Min(currentY, height));
				mScrollContent.Resize(0, 0, fillWidth, currentY);

				UpdateScrollbars();
			}

			public override void Draw(Graphics g)
			{
				g.DrawBox(DarkTheme.sDarkTheme.GetImage(.EditBox), 0, GS!(-30), mWidth, mHeight + GS!(30));

				g.SetFont(mTitleFont);

				using (g.PushColor(gApp.mSettings.mUISettings.mColors.mText))
					g.DrawString("Recent Workspaces", 0, GS!(-30), .Centered, mWidth, .Ellipsis);

				base.Draw(g);
			}
		}

		class RecentWorkspaceItem : Widget
		{
			public static Font s_Font;
			append String mPath;

			public bool mPinned;
			public RecentWorkspacesScrollWidget mRecentsParent;

			public String Path
			{
				get => mPath;
				set => mPath.Set(value);
			}

			public override void Draw(Graphics g)
			{
				if (mMouseOver)
				{
					using (g.PushColor(gApp.mSettings.mUISettings.mColors.mSelectedOutline))
						g.DrawBox(DarkTheme.sDarkTheme.GetImage(.MenuSelect), GS!(2), GS!(2), mWidth - GS!(2), mHeight - GS!(4));
					/*using (g.PushColor(0x80000000))
						g.FillRect(0, 0, mWidth, mHeight);*/
					
				}

				g.SetFont(s_Font);
				g.DrawString(mPath, 10, 0, .Left, mWidth - 10);
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

			public override void Update()
			{
				base.Update();

				if ((DarkTooltipManager.CheckMouseover(this, 20, let mousePoint)) && (s_Font.GetWidth(mPath) > mWidth - GS!(12)))
				{
				    DarkTooltipManager.ShowTooltip(mPath, this, mousePoint.x, mousePoint.y);
				}
			}

			public override void MouseClicked(float x, float y, float origX, float origY, int32 btn)
			{
				base.MouseClicked(x, y, origX, origY, btn);

				if (btn == 0)
				{
					TryOpenWorkspace();
				}
				else if (btn == 1)
				{
					Menu menu = new .();
					menu.AddItem("Open").mOnMenuItemSelected.Add(new (menu) => { TryOpenWorkspace(); });
					menu.AddItem();
					menu.AddItem("Open Containing Folder").mOnMenuItemSelected.Add(new (menu) => { OpenContainingFolder(); });
					menu.AddItem("Copy Path").mOnMenuItemSelected.Add(new (menu) => {
						gApp.SetClipboardData("text", mPath.CStr(), (int32)mPath.Length + 1, true);
					});
					menu.AddItem();
					menu.AddItem("Remove").mOnMenuItemSelected.Add(new (menu) => { RemoveFromRecent(); });
					MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);
					menuWidget.Init(this, x, y);
				}
			}

			void ShowNotFoundDialog()
			{
				IDEApp.Beep(.Error);

				let aDialog = ThemeFactory.mDefault.CreateDialog(
					"Beef IDE",
					"Workspace couldn't be found. Do you want to remove the reference from recent list?",
					DarkTheme.sDarkTheme.mIconError);

				aDialog.AddYesNoButtons(new (theEvent) => {
					RemoveFromRecent();
				}, null, 0, 1);
				aDialog.PopupWindow(gApp.GetActiveWindow());
			}

			void OpenContainingFolder()
			{
				if (!Directory.Exists(mPath))
				{
					ShowNotFoundDialog();
					return;
				}

				ProcessStartInfo psi = scope ProcessStartInfo();
				psi.SetFileName(mPath);
				psi.UseShellExecute = true;
				psi.SetVerb("Open");

				var process = scope SpawnedProcess();
				process.Start(psi).IgnoreError();
			}

			void TryOpenWorkspace()
			{
				const String WORKSPACE_FILENAME = "BeefSpace.toml";
				String path = scope .(mPath.Length + WORKSPACE_FILENAME.Length + 2);
				System.IO.Path.InternalCombine(path, mPath, WORKSPACE_FILENAME);
				if (!File.Exists(path))
				{
					ShowNotFoundDialog();
					return;
				}

				gApp.[Friend]mDeferredOpen = .Workspace;
				String.NewOrSet!(gApp.[Friend]mDeferredOpenFileName, path);
			}

			void RemoveFromRecent()
			{
				let recentWorkspaces = gApp.mSettings.mRecentFiles.mRecents[(int)RecentFiles.RecentKind.OpenedWorkspace];

				var index = recentWorkspaces.mList.IndexOf(mPath);
				if (index == -1)
					return;

				delete recentWorkspaces.mList[index];
				recentWorkspaces.mList.RemoveAt(index);
				recentWorkspaces.mMenuItems[index].Dispose();
				recentWorkspaces.mMenuItems.RemoveAt(index);

				RemoveSelf();
				mRecentsParent.RehupSize();
				delete this;
			}
		}

		DarkButton mCreateBtn;
		DarkButton mOpenWorkspaceBtn;
		DarkButton mOpenProjectBtn;
		DarkButton mWelcomeBtn;
		DarkButton mOpenDocBtn;

		RecentWorkspacesScrollWidget mRecentsScrollWidget;
		public Font mMedFont ~ delete _;
		public Font mSmallFont ~ delete _;

		public this()
		{
			mCreateBtn = new .()
			{
				Label = "Create Workspace"
			};
			mCreateBtn.mOnMouseClick.Add(new (event) => {
				gApp.[Friend]mDeferredOpen = .NewWorkspaceOrProject;
			});
			AddWidget(mCreateBtn);

			mOpenWorkspaceBtn = new .()
			{
				Label = "Open Workspace"
			};
			mOpenWorkspaceBtn.mOnMouseClick.Add(new (event) => {
				gApp.DoOpenWorkspace(false);
			});
			AddWidget(mOpenWorkspaceBtn);

			mOpenProjectBtn = new .()
			{
				Label = "Open Project"
			};
			mOpenProjectBtn.mOnMouseClick.Add(new (event) => {
				gApp.Cmd_OpenProject();
			});
			AddWidget(mOpenProjectBtn);

			mWelcomeBtn = new .()
			{
				Label = "Open Sample"
			};
			mWelcomeBtn.mOnMouseClick.Add(new (event) => { gApp.[Friend]ShowWelcome(); });
			AddWidget(mWelcomeBtn);

			mOpenDocBtn = new .()
			{
				Label = "Open Documentation",
			};
			mOpenDocBtn.mOnMouseClick.Add(new (event) => { OpenDocumentation(); });
			AddWidget(mOpenDocBtn);

			mRecentsScrollWidget = new .();
			let recentList = gApp.mSettings.mRecentFiles.mRecents[(int)RecentFiles.RecentKind.OpenedWorkspace].mList;
			for (let recent in recentList)
			{
				var workspaceItem = new RecentWorkspaceItem()
				{
					Path = recent,
					mRecentsParent = mRecentsScrollWidget
				};
				mRecentsScrollWidget.AddContentWidget(workspaceItem);
			}

			AddWidget(mRecentsScrollWidget);
		}

		public override bool WantsSerialization => false;

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);

			DeleteAndNullify!(mMedFont);
			DeleteAndNullify!(mSmallFont);

			RehupSize();
		}

		public override void DrawAll(Graphics g)
		{
			if (mMedFont == null)
				mMedFont = new Font()..Load("Segoe UI Bold", 20.0f * DarkTheme.sScale);

			if (mSmallFont == null)
				mSmallFont = new Font()..Load("Segoe UI Bold", 16.0f * DarkTheme.sScale);


			this.mRecentsScrollWidget.mTitleFont = mMedFont;
			RecentWorkspaceItem.s_Font = mSmallFont;

			base.DrawAll(g);
		}


		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);

			let autoPos = DarkButton[](mCreateBtn, mOpenWorkspaceBtn, mOpenProjectBtn, mWelcomeBtn, mOpenDocBtn);

			int32 startX = GS!(20);
			int32 currentY = GS!(20);
			int32 buttonWidth = GS!(250);
			int32 buttonheight = GS!(35);

			for (let button in autoPos)
			{
				button.Resize(startX, currentY, buttonWidth, buttonheight);
				currentY += buttonheight + GS!(10);
			}

			float offsetX = buttonWidth + GS!(35);
			float offsetY = GS!(50);

			mRecentsScrollWidget.ResizeSelf(offsetX, offsetY, Math.Clamp(mWidth - offsetX - GS!(4), 0, GS!(700)), Math.Clamp(mHeight - offsetY - GS!(4), 0, GS!(700)));
		}

		void OpenDocumentation()
		{
			ProcessStartInfo psi = scope ProcessStartInfo();
			psi.SetFileName("https://www.beeflang.org/docs/");
			psi.UseShellExecute = true;
			psi.SetVerb("Open");

			var process = scope SpawnedProcess();
			process.Start(psi).IgnoreError();
		}
	}
}