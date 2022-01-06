using System;
using System.Threading;
using System.Collections;
using Beefy.events;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.widgets;
using Beefy;
using System.Diagnostics;

namespace IDE.ui
{
	class AttachDialog : DarkDialog
	{
		public static Dictionary<String, int32> sMRU = new Dictionary<String, int32>() ~ delete _;
		public static int32 sCurrentMRUIndex = 1;

		protected DarkListView mFileList;
		EditWidget mEditWidget;
		List<Process> mProcessList = new List<Process>() ~ delete _;
		List<Process> mFullProcessList = new List<Process>() ~ DeleteContainerAndItems!(_);
		public bool mFilterChanged;
		public volatile bool mExitingThread;
		public Thread mDateThread;
		public int mDataAgeTicks;

		public this()
		{
			mWindowFlags = BFWindow.Flags.ClientSized | BFWindow.Flags.TopMost | BFWindow.Flags.Caption |
			    BFWindow.Flags.Border | BFWindow.Flags.SysMenu | BFWindow.Flags.Resizable | .PopupPosition;

			AddOkCancelButtons(new (evt) => { Attach(); }, null, 0, 1);
			//mApplyButton = AddButton("Apply", (evt) => { evt.mCloseDialog = false; ApplyChanges(); });

			Title = "Attach to Process";

			mButtonBottomMargin = 6;
			mButtonRightMargin = 6;

			mFileList = new DarkListView();            
			mFileList.InitScrollbars(false, true);
			mFileList.mAllowMultiSelect = false;

			ListViewColumn column = mFileList.AddColumn(240, "Process");
			column.mMinWidth = 100;
			column = mFileList.AddColumn(80, "ID");
			column = mFileList.AddColumn(200, "Title");

			AddWidget(mFileList);
			mTabWidgets.Add(mFileList);

			mEditWidget = AddEdit("");
			mEditWidget.mOnKeyDown.Add(new => EditKeyDownHandler);
			mEditWidget.mOnContentChanged.Add(new (evt) => { mFilterChanged = true; });
		}

		Process GetSelectedProcess()
		{
			var root = mFileList.GetRoot();
			var selectedListViewItem = root.FindFirstSelectedItem();
			if (selectedListViewItem != null)
			{
			    var itemIdx = root.mChildItems.IndexOf(selectedListViewItem);
			    var process = mProcessList[itemIdx];
				return process;
			}
			return null;
		}

		void Attach()
		{
		    var process = GetSelectedProcess();
			if (process != null)
		    	IDEApp.sApp.Attach(process, .None);
		}

		void EditKeyDownHandler(KeyDownEvent evt)
		{
		    if ((evt.mKeyCode == KeyCode.Up) || (evt.mKeyCode == KeyCode.Down) || (evt.mKeyCode == KeyCode.PageUp) || (evt.mKeyCode == KeyCode.PageDown))
		    {
		        mFileList.KeyDown(evt.mKeyCode, false);
		    }
		}

		public void ValueClicked(MouseEvent theEvent)
		{
		    DarkListViewItem clickedItem = (DarkListViewItem)theEvent.mSender;
		    DarkListViewItem item = (DarkListViewItem)clickedItem.GetSubItem(0);

		    mFileList.GetRoot().SelectItemExclusively(item);
		    mFileList.SetFocus();

		    /*if ((theEvent.mBtn == 0) && (theEvent.mBtnCount > 1))
		    {
		        for (int childIdx = 1; childIdx < mListView.GetRoot().GetChildCount(); childIdx++)
		        {
		            var checkListViewItem = mListView.GetRoot().GetChildAtIndex(childIdx);
		            checkListViewItem.IconImage = null;
		        }

		        int selectedIdx = item.mVirtualIdx;
		        IDEApp.sApp.mDebugger.mSelectedCallStackIdx = selectedIdx;
		        IDEApp.sApp.ShowPCLocation(selectedIdx, false, true);
		        IDEApp.sApp.StackPositionChanged();
		    }

		    UpdateIcons();*/
		}

		int CompareFileNames(StringView pathA, StringView pathB)
		{
		    int pathStartA = pathA.LastIndexOf(IDEUtils.cNativeSlash);
		    int pathStartB = pathB.LastIndexOf(IDEUtils.cNativeSlash);

		    int lenA = pathA.Length - pathStartA - 1;
		    int lenB = pathB.Length - pathStartB - 1;

		    //int result = String.CompareOrdinal(pathA, pathStartA + 1, pathB, pathStartB + 1, Math.Min(lenA, lenB));
			int result = String.Compare(pathA.Ptr + pathStartA + 1, lenA, pathB.Ptr + pathStartB + 1, Math.Min(lenA, lenB), false);

		    /*if (result == 0)
		        result = lenA - lenB;*/
		    return result;
		}

		void GetFullProcessList()
		{
			ClearAndDeleteItems(mFullProcessList);			
			mFullProcessList.Clear();
			Process.GetProcesses(mFullProcessList);

			// Remove ourselves from the list
			for (var process in mFullProcessList)
			{
				if (process.Id == Process.CurrentId)
				{
					delete process;
					@process.Remove();
					break;
				}
			}
		}

		void FilterProcesses(bool fullRefresh)
		{
			int selectedPid = -1;
			if (Process selectedProcess = GetSelectedProcess())
				selectedPid = selectedProcess.Id;

		    String filterString = scope String();
		    mEditWidget.GetText(filterString);
		    filterString.Trim();

			double scrollPos = mFileList.mVertPos.v;

			if ((mFullProcessList.Count == 0) || (fullRefresh))
			{
				GetFullProcessList();
			}

			mProcessList.Clear();
			for (var process in mFullProcessList)
			{
				if (filterString.Length > 0)
				{
					var mainWindowTitle = scope String();
					process.GetMainWindowTitle(mainWindowTitle);

				    if ((process.ProcessName.IndexOf(filterString, true) == -1) &&
						(mainWindowTitle.IndexOf(filterString, true) == -1))
				        continue;
				}
				mProcessList.Add(process);
			}

			var root = mFileList.GetRoot();
			root.Clear();

		    /*var selectedItem = null;// root.FindSelectedItem();
		    if ((selectedItem == null) && (root.GetChildCount() > 0))
		    {
		        selectedItem = root.GetChildAtIndex(0);
		        root.SelectItemExclusively(selectedItem);
		    }*/
		    
		    mProcessList.Sort(scope (itemA, itemB) =>
                {
                    int result = CompareFileNames(itemA.ProcessName, itemB.ProcessName);
					if (result == 0)
						return itemA.Id - itemB.Id;
					return result;
                });

		    for (var process in mProcessList)
		    {
		        var listViewItem = mFileList.GetRoot().CreateChildItem();
		        listViewItem.Label = process.ProcessName;
		        listViewItem.mOnMouseDown.Add(new => ValueClicked);

		        var subListViewItem = listViewItem.CreateSubItem(1);
				var processIdStr = scope String();
				process.Id.ToString(processIdStr);
		        subListViewItem.Label = processIdStr;
		        subListViewItem.mOnMouseDown.Add(new => ValueClicked);

		        subListViewItem = listViewItem.CreateSubItem(2);
				var titleStr = scope String();
				process.GetMainWindowTitle(titleStr);
		        subListViewItem.Label = titleStr;
		        subListViewItem.mOnMouseDown.Add(new => ValueClicked);
		    }

			ListViewItem bestItem = null;
			int32 bestPri = -1;
		    for (int32 childIdx = 0; childIdx < root.GetChildCount(); childIdx++)
		    {
		        var listViewItem = root.GetChildAtIndex(childIdx);
		        var process = mProcessList[childIdx];

				if (process.Id == selectedPid)
				{
					bestPri = Int32.MaxValue;
					bestItem = listViewItem;
				}	

				var processName = scope String();
				processName.Reference(process.ProcessName);

		        int32 pri;
		        sMRU.TryGetValue(processName, out pri);
		        if (pri > bestPri)
		        {
		            bestItem = listViewItem;
		            bestPri = pri;                    
		        }
		    }

			mFileList.VertScrollTo(scrollPos);

		    if (bestItem != null)
		    {
		        mFileList.GetRoot().SelectItemExclusively(bestItem);                
		        //mFileList.EnsureItemVisible(bestItem, true);
		    }

		    /*ShutdownThread();
		    mExitingThread = true;
		    mDateThread = new Thread(CheckFileDates);
		    mDateThread.Start();*/
		}

		void GotoFile()
		{
		    var root = mFileList.GetRoot();
		    var selectedListViewItem = root.FindFirstSelectedItem();
		    if (selectedListViewItem != null)
		    {
		        //var itemIdx = root.mChildItems.IndexOf(selectedListViewItem);
		        //var projectSource = mProcessList[itemIdx];
		        //IDEApp.sApp.ShowProjectItem(projectSource);
		        //sMRU[projectSource.mPath] = sCurrentMRUIndex++;
		    }
		}

		public override void AddedToParent()
		{
		    base.AddedToParent();
		    mEditWidget.SetFocus();

		    FilterProcesses(false);
		}

		public override void ResizeComponents()
		{
		    base.ResizeComponents();
		    
		    //var font = DarkTheme.sDarkTheme.mSmallFont;
		    
		    mFileList.Resize(6, 6, mWidth - 6 - 6, mHeight - 20 - 6 - 20 - 6 - 6 - 6);
		    mEditWidget.Resize(6, mFileList.mY + mFileList.mHeight + 6, mWidth - 6 - 6, 20);
		}

		public override void CalcSize()
		{
		    mWidth = 660;
		    mHeight = 512;
		}

		public override void Resize(float x, float y, float width, float height)
		{
		    base.Resize(x, y, width, height);
		    ResizeComponents();
		}

		void Outline(Graphics g, Widget widget, int32 inflateX, int32 inflateY, uint32 color)
		{
		    using (g.PushColor(color))
		    {
		        g.OutlineRect(widget.mX - inflateX, widget.mY - inflateY, widget.mWidth + inflateX * 2, widget.mHeight + inflateY * 2);
		    }
		}

		public override void DrawAll(Graphics g)
		{
		    base.DrawAll(g);

		    //Outline(g, mCategorySelector, 0, 0, 0xFF404040);
		    //Outline(g, mCategorySelector, -1, -1, 0xFF202020);

		    Outline(g, mFileList, 0, 0, IDEApp.cDialogOutlineLightColor);
		    Outline(g, mFileList, -1, -1, IDEApp.cDialogOutlineDarkColor);
		}

		void CheckFileDates()
		{
		    //
		}

		public override void Update()
		{
		    base.Update();

			bool wantsFullRefresh = mDataAgeTicks == 40;
		    if ((mFilterChanged) || (wantsFullRefresh))
		    {
		        FilterProcesses(wantsFullRefresh);
		        mFilterChanged = false;
				mDataAgeTicks = 0;
		    }

			mDataAgeTicks++;
		}
	}
}
