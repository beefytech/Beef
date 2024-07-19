using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using Beefy.widgets;
using Beefy.utils;
using Beefy.theme.dark;
using Beefy.events;

namespace IDE.ui
{
	interface IDocumentPanel
	{

	}

    public class Panel : Widget
    {
        public int32 mLastFocusAppUpdateCnt;
		public bool mAutoDelete = true;
		public List<Widget> mTabWidgets = new List<Widget>() ~ delete _;
		public bool mShowingRightClickMenu;

		/// Unscaled
		public virtual float TabWidthOffset
		{
			get
			{
				return 50;
			}
		}

		public virtual bool WantsSerialization
		{
			get
			{
				return true;
			}
		}

        public override void GotFocus()
        {
            base.GotFocus();
            mLastFocusAppUpdateCnt = gApp.mUpdateCnt;
        }

		public override void ParentDeleted()
		{
			if (mAutoDelete)
				base.ParentDeleted();
		}

        public override void AddedToParent()
        {
            base.AddedToParent();
            mLastFocusAppUpdateCnt = gApp.mUpdateCnt;
        }

		public override void RemovedFromParent(Widget previousParent, WidgetWindow window)
		{
			base.RemovedFromParent(previousParent, window);
		}

		public virtual void FocusForKeyboard()
		{

		}

        public override void Serialize(StructuredData data)
        {
            
        }

        public override bool Deserialize(StructuredData data)
        {
            return true;
        }

        public static Panel Create(StructuredData data)
        {
            var type = scope String();
            data.GetString("Type", type);
            Panel panel = null;

			if (type == "")
				return gApp.mTerminalPanel;

            if (type == "CallStackPanel")
                panel = gApp.mCallStackPanel;
            else if (type == "BreakpointPanel")
                panel = gApp.mBreakpointPanel;
			else if (type == "ProfilePanel")
				panel = gApp.mProfilePanel;
            else if (type == "OutputPanel")
            {                
                panel = gApp.mOutputPanel;
            }
			else if (type == "TerminalPanel")
			{                
			    panel = gApp.mTerminalPanel;
			}
            else if (type == "ImmediatePanel")
            {                
                panel = gApp.mImmediatePanel;
            }
			else if (type == "ErrorsPanel")
			{                
			    panel = gApp.mErrorsPanel;
			}
            else if (type == "FindResultsPanel")
            {                
                panel = gApp.mFindResultsPanel;
            }
            else if (type == "ProjectPanel")
            {             
                panel = gApp.mProjectPanel;
            }
			else if (type == "ClassViewPanel")
			{             
			    panel = gApp.mClassViewPanel;
			}
            else if (type == "PropertiesPanel")
            {                
                panel = gApp.mPropertiesPanel;
            }
            else if (type == "SourceViewPanel")
            {
                panel = new SourceViewPanel();
            }
            else if (type == "DisassemblyPanel")
            {
                var disassemblyPanel = new DisassemblyPanel();
                disassemblyPanel.mIsInitialized = true;
                panel = disassemblyPanel;
            }
            else if (type == "ThreadPanel")
            {
                panel = gApp.mThreadPanel;
            }
            else if (type == "WatchPanel")
            {
                panel = gApp.mWatchPanel;
            }
            else if (type == "AutoWatchPanel")
            {
                panel = gApp.mAutoWatchPanel;
            }
            else if (type == "MemoryPanel")
            {
                panel = gApp.mMemoryPanel;
            }
			else if (type == "DiagnosticsPanel")
			{
			    panel = gApp.mDiagnosticsPanel;
			}
			else if (type == "AutoCompletePanel")
			{
			    panel = gApp.mAutoCompletePanel;
			}
			else if (type == "ModulePanel")
			{
			    panel = gApp.mModulePanel;
			}
			else if (type == "BookmarksPanel")
			{                
			    panel = gApp.mBookmarksPanel;
			}
			else if (type == "TerminalPanel")
			{                
			    panel = gApp.mTerminalPanel;
			}
			else if (type == "ConsolePanel")
			{                
			    panel = gApp.mConsolePanel;
			}

            if (panel != null)
            {
                if (!panel.Deserialize(data))
				{
					delete panel;
                    return null;
				}
            }

            Debug.Assert(panel.mParent == null);
            Debug.Assert(panel != null);

            return panel;
        }

		public void RehupScale()
		{
			RehupScale(DarkTheme.sScale, DarkTheme.sScale);
		}

		protected virtual void ShowRightClickMenu(Widget relWidget, float x, float y)
		{

		}

		public void DoShowRightClickMenu(Widget relWidget, float x, float y)
		{
			mShowingRightClickMenu = true;
			ShowRightClickMenu(relWidget, x, y);
			mShowingRightClickMenu = false;
		}

		public void ListViewItemMouseDown(ListViewItem item, float x, float y, int32 btnNum, int32 btnCount)
		{
			var listView = item.mListView;
			listView.SetFocus();
			var baseItem = item.GetSubItem(0);
			if (btnNum == 1)
			{
				if (!baseItem.Selected)
					listView.GetRoot().SelectItem(baseItem, true);
			}
			else
				listView.GetRoot().SelectItem(baseItem, true);
		}

		public void ListViewItemMouseClicked(ListViewItem item, float x, float y, int32 btnNum)
		{
			if (btnNum == 1)
			{
				var listView = item.mListView;
				item.SelfToOtherTranslate(listView.GetRoot(), x, y, var aX, var aY);
				DoShowRightClickMenu(listView.GetRoot(), aX, aY);
			}
		}

		public void ListViewKeyDown_ShowMenu(KeyDownEvent evt)
		{
			if (evt.mKeyCode == .Apps)
			{
				var listView = (ListView)evt.mSender;
				ShowRightClickMenu(listView);
			}
		}

		protected void ShowRightClickMenu(ListView listView)
		{
			var focusedItem = listView.GetRoot().FindFocusedItem();
			if (focusedItem != null)
			{
				focusedItem.SelfToOtherTranslate(listView.GetRoot(), 0, 0, var x, var y);
				x += GS!(20);
				y += GS!(20);
				IDEUtils.ClampMenuCoords(ref x, ref y, listView, scope .(0, 0, GS!(32), GS!(32)));
				DoShowRightClickMenu(listView.GetRoot(), x, y);
			}
		}

		// To group newly-opened panels in frames that make sense
		public virtual bool HasAffinity(Widget otherPanel)
		{
			return otherPanel.GetType() == GetType();
		}

		public virtual bool HandleTab(int dir)
		{
		    return Widget.HandleTab(dir, mTabWidgets);
		}
    }
}
