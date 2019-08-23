using System;
using Beefy;
using Beefy.theme.dark;
using Beefy.widgets;
using Beefy.gfx;

namespace IDE.ui
{
	class ProfileDialog : IDEDialog
	{
		EditWidget mDescEdit;
		DarkComboBox mThreadCombo;
		EditWidget mSampleRateEdit;

		public this()
		{
			mTitle = new String("Profile");
			mText = new String("");

			mDescEdit = AddEdit("");

			mThreadCombo = new DarkComboBox();
			mThreadCombo.Label = "All Threads";
			mThreadCombo.mPopulateMenuAction.Add(new => PopulateThreadList);
			AddDialogComponent(mThreadCombo);

			mWindowFlags = BFWindow.Flags.ClientSized | BFWindow.Flags.TopMost | BFWindow.Flags.Caption |
			    BFWindow.Flags.Border | BFWindow.Flags.SysMenu | BFWindow.Flags.Resizable;

			mSampleRateEdit = AddEdit(scope String()..AppendF("{0}", gApp.mSettings.mDebuggerSettings.mProfileSampleRate));

			AddOkCancelButtons(new (evt) => { StartProfiling(); }, null, 0, 1);
		}

		void StartProfiling()
		{
			var str = scope String();
			mThreadCombo.GetLabel(str);
			int spacePos = str.IndexOf(' ');
			if (spacePos > 0)
				str.RemoveToEnd(spacePos);
			int threadId = int.Parse(str).GetValueOrDefault();

			str.Clear();
			mSampleRateEdit.GetText(str);
			int sampleRate = int.Parse(str).GetValueOrDefault();
			if (sampleRate < 0)
				sampleRate = gApp.mSettings.mDebuggerSettings.mProfileSampleRate;
			sampleRate = Math.Clamp(sampleRate, 10, 10000);

			str.Clear();
			mDescEdit.GetText(str);

			gApp.mProfilePanel.StartProfiling(threadId, str, sampleRate);
		}

		void PopulateThreadList(Menu menu)
		{
			var subItem = menu.AddItem("All Threads");
			subItem.mOnMenuItemSelected.Add(new (evt) => { mThreadCombo.Label = "All Threads"; });

			String threadInfo = scope .();
			gApp.mDebugger.GetThreadInfo(threadInfo);

			for (var infoLine in threadInfo.Split('\n'))
			{
				if (@infoLine.Pos == 0)
					continue;

				var infoSections = infoLine.Split('\t');
				StringView id = infoSections.GetNext().GetValueOrDefault();
				StringView name = infoSections.GetNext().GetValueOrDefault();

				var str = scope String();
				str.AppendF("{0} - {1}", id, name);
				subItem = menu.AddItem(str);
				subItem.mOnMenuItemSelected.Add(new (item) => { mThreadCombo.Label = item.mLabel; });
			}
		}

		public override void ResizeComponents()
		{
		    base.ResizeComponents();

			float curY = GS!(30);

			mDescEdit.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(22));
			curY += GS!(42);
			mThreadCombo.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(24));
			curY += GS!(42);
			mSampleRateEdit.Resize(GS!(6), curY, mWidth - GS!(6) - GS!(6), GS!(24));
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			DrawLabel(g, mDescEdit, "Profile Description (Optional)");
			DrawLabel(g, mThreadCombo, "Thread");
			DrawLabel(g, mSampleRateEdit, "Sample Rate");
		}

		public override void CalcSize()
		{
		    mWidth = GS!(280);
		    mHeight = GS!(180);
		}
	}
}
