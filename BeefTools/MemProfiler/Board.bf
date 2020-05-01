using System;
using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.geom;
using System.Collections;
using Beefy.utils;
using System.Diagnostics;
using Beefy.theme.dark;
using Beefy.events;

namespace MemProfiler
{
	class Board : Widget
	{
		DarkListView mListView;
		DarkButton mGetButton;

		public this()
		{
			mListView = new DarkListView();
			AddWidget(mListView);

			mListView.AddColumn(200, "Name");
			mListView.AddColumn(200, "Self");
			mListView.AddColumn(200, "Total");
			mListView.InitScrollbars(false, true);

			mGetButton = (DarkButton)DarkTheme.sDarkTheme.CreateButton(this, "Get", 0, 0, 0, 0);
			mGetButton.mMouseClickHandler.Add(new (evt) => { GetData(); });
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);

			mListView.Resize(x, y, width, height - 20);
			mGetButton.Resize(20, mHeight - 20, 128, 20);
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Bkg), 0, 0, mWidth, mHeight);

			g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
			g.DrawString(StackStringFormat!("FPS: {0}", gApp.mLastFPS), mWidth - 128, mHeight - 18);

			g.FillRect(mUpdateCnt % 200 + 200, mHeight - 2, 2, 2);
		}

		void ValueClicked(MouseEvent theEvent)
		{
		    DarkListViewItem clickedItem = (DarkListViewItem)theEvent.mSender;
		    DarkListViewItem item = (DarkListViewItem)clickedItem.GetSubItem(0);

		    mListView.GetRoot().SelectItemExclusively(item);
		    mListView.SetFocus();
		}

		void GetData()
		{
			//const char* cmd = "Get";
			//DWORD bytesRead = 0;

			mListView.GetRoot().Clear();

			String pipeName = scope String();
			pipeName.FormatInto(@"\\.\pipe\HeapDbg_{0}", gApp.mProcessInformation.mProcessId);

			String cmd = "Get";

			char8[] charData = new char8[16*1024*1024];
			defer delete charData;

			int32 bytesRead = 0;
			if (Windows.CallNamedPipeA(pipeName, (void*)cmd, (int32)cmd.Length, (void*)charData.CArray(), (int32)charData.Count, &bytesRead, 0))
			{
				String data = scope String();
				data.Reference(charData.CArray(), bytesRead, charData.Count);

				ListViewItem curItem = mListView.GetRoot();
				
				for (var lineView in data.Split('\n'))
				{
					String lineViewStr = scope String(lineView);

					if (lineViewStr == "-")
					{
						curItem = curItem.mParentItem;
					}
					else
					{
						var childItem = curItem.CreateChildItem();
						childItem.mMouseDownHandler.Add(new => ValueClicked);

						int32 idx = 0;
						for (var dataStr in lineViewStr.Split('\t'))
						{
							if (idx == 0)
								childItem.Label = scope String(dataStr);
							else
							{
								var subItem = childItem.CreateSubItem(idx);
								subItem.Label = scope String(dataStr);
								subItem.mMouseDownHandler.Add(new => ValueClicked);
							}

							idx++;
						}

						curItem = childItem;
					}
					
				}

				//tree->InsertItem()
			}
		}
	}
}

