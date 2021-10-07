using Beefy.widgets;
using Beefy.gfx;
using Beefy.theme.dark;
using System;
using Beefy.theme;

namespace BeefPerf
{
	class WorkspaceWidget : Widget
	{
		DarkEditWidget mClientEdit;
		DarkEditWidget mSessionEdit;
		DarkCheckBox mEnableCB;

		int EntriesYStart
		{
			get
			{
				return GS!(110);
			}
		}

		public this()
		{
			mEnableCB = new DarkCheckBox();
			mEnableCB.Label = "Allow Connections";
			mEnableCB.Checked = gApp.Listening;
			mEnableCB.mOnValueChanged.Add(new () =>
				{
					gApp.Listening = mEnableCB.Checked;
					mEnableCB.Checked = gApp.Listening;
				});
			AddWidget(mEnableCB);

			mClientEdit = new DarkEditWidget();
			AddWidget(mClientEdit);

			mSessionEdit = new DarkEditWidget();
			AddWidget(mSessionEdit);
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			g.DrawString("Client Name Filter", mClientEdit.mX, mClientEdit.mY - GS!(20));
			g.DrawString("Session Name Filter", mSessionEdit.mX, mSessionEdit.mY - GS!(20));


			/*using (g.PushColor(0xFFFF0000))
				g.FillRect(0, 0, mWidth, mHeight);

			using (g.PushColor(0xFFFF00FF))
				g.FillRect(0, 0, mWidth, 20);*/

			g.SetFont(DarkTheme.sDarkTheme.mSmallFont);

			float curY = EntriesYStart;
			float boxHeight = 60;
			for (int clientIdx = gApp.mSessions.Count - 1; clientIdx >= 0; clientIdx--)
			{
				var session = gApp.mSessions[clientIdx];
				using (g.PushTranslate(8, curY))
				{
					float width = Math.Max(mParent.mWidth - 16, 140);
					g.DrawBox(DarkTheme.sDarkTheme.GetImage(.Bkg), 0, 0, width, boxHeight);

					if (gApp.mCurSession == session)
					{
						using (g.PushColor(ThemeColors.Theme.SelectedOutline.Color))
							g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Outline), 0, 0, width, boxHeight);
					}

					var sessionName = scope String();
					if (session.mSessionName != null)
						sessionName.Append(session.mSessionName);
					if (sessionName.IsWhiteSpace)
						sessionName.Append("<Unnamed Session>");

					var clientName = scope String();
					if (session.mClientName != null)
						clientName.Append(session.mClientName);
					if (clientName.IsWhiteSpace)
						clientName.Append("<Unnamed Client>");

					//clientName.Append("This makes it a long string!");
					//sessionName.Append("LONG!!!!");
					float clientNameWidth = Math.Min(g.mFont.GetWidth(clientName), width * 0.5f);

					g.DrawString(sessionName, 4, 2, .Left, width - clientNameWidth - GS!(12), .Ellipsis);
					g.DrawString(clientName, width - clientNameWidth - 4, 2, .Left, clientNameWidth, .Ellipsis);

					if (!session.mSessionOver)
					{
						//using (g.PushColor(((mUpdateCnt / 20) % 2 == 0) ? 0xFFFFFFFF : 0xD0FFFFFF))
							//g.Draw(DarkTheme.sDarkTheme.GetImage(.RedDot), width - 20, 0);
						
						using (g.PushColor(((mUpdateCnt / 20) % 2 == 0) ? 0xFFFF0000 : 0xD0FF0000))
						{
							//g.Draw(DarkTheme.sDarkTheme.GetImage(.RedDot), width - 20, 0);
							g.FillRect(-3, 0, 5, boxHeight);
						}
					}

					var startTime = scope String();
					session.mConnectTime.ToString(startTime, "dddd, MMMM d, yyyy @ h:mm:ss tt");
					g.DrawString(startTime, 4, 20, .Left, width - 8, .Ellipsis);

					String timeStr = scope String();
					double timeUS = session.TicksToUS(session.mCurTick - session.mFirstTick);
					BpClient.TimeToStr(timeUS, timeStr, false);
					g.DrawString(timeStr, 4, 40, .Left, width - 8, .Ellipsis);

					g.DrawString(scope String()..AppendF("BPS: {0}k", session.mDispBPS), 4, 40, .Right, width - 8);
				}

				curY += boxHeight + 4;
			}
		}

		BpSession GetSessionIdxAt(float x, float y)
		{
			if ((x < 8) || (x > mWidth - 8))
				return null;

			int32 boxHeight = 60;
			int32 spacing = boxHeight + 4;
			int yStart = EntriesYStart;
			if (y < yStart)
				return null;

			int idx = (int32)(y - yStart) / spacing;
			int ofs = (int32)(y - yStart) % spacing;

			if (idx >= gApp.mSessions.Count)
				return null;
			if (ofs > boxHeight)
				return null;
			return gApp.mSessions[gApp.mSessions.Count - idx - 1];
		}

		public float GetWantHeight()
		{
			int32 boxHeight = 60;
			int32 spacing = boxHeight + 4;
			return EntriesYStart + spacing * gApp.mSessions.Count;
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);

			var session = GetSessionIdxAt(x, y);
			if (session == null)
				return;

			gApp.SetSession(session);
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}

		public void ResizeComponents()
		{
			mEnableCB.Resize(4, 0, mWidth - 8, 20);
			mClientEdit.Resize(4, 40, mWidth - 8, 20);
			mSessionEdit.Resize(4, 80, mWidth - 8, 20);
		}
	}

	class WorkspacePanel : Widget
	{
		ScrollableWidget mScrollableWidget;
		WorkspaceWidget mWorkspaceWidget;

		public this()
		{
			mWorkspaceWidget = new WorkspaceWidget();
			mWorkspaceWidget.Resize(0, 0, 200, 200);

			mScrollableWidget = new ScrollableWidget();
			mScrollableWidget.InitScrollbars(false, true);
			mScrollableWidget.mScrollContent = mWorkspaceWidget;
			mScrollableWidget.mScrollContentContainer.AddWidget(mWorkspaceWidget);
			AddWidget(mScrollableWidget);
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			//g.DrawBox(DarkTheme.sDarkTheme.GetImage(.Bkg), 0, 0, mWidth, mHeight);
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);

			mScrollableWidget.Resize(0, 0, width, height - 0);
		}
		
		public override void Update()
		{
			base.Update();
			mWorkspaceWidget.mWidth = mWorkspaceWidget.mParent.mWidth;
			mWorkspaceWidget.mHeight = mWorkspaceWidget.GetWantHeight();
		}

		public bool PassesFilter(BpClient client)
		{
			String clientFilter = scope .();
			mWorkspaceWidget.[Friend]mClientEdit.GetText(clientFilter);
			clientFilter.Trim();
			if (!clientFilter.IsEmpty)
			{
				if ((client.mClientName == null) || (!client.mClientName.Contains(clientFilter, true)))
					return false;
			}

			String sessionFilter = scope .();
			mWorkspaceWidget.[Friend]mSessionEdit.GetText(sessionFilter);
			sessionFilter.Trim();
			if (!sessionFilter.IsEmpty)
			{
				if ((client.mSessionName == null) || (!client.mSessionName.Contains(sessionFilter, true)))
					return false;
			}

			return true;
		}
	}
}
