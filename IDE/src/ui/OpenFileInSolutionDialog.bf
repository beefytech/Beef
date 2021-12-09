using System;
using System.Collections;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Beefy;
using Beefy.events;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using System.Diagnostics;

namespace IDE.ui
{
    class OpenFileInSolutionDialog : DarkDialog
    {
		class TimeEntry
		{
			public DateTime mLastWriteTime;
			public DateTime mCheckedTime;
		}

		protected class EntryListViewItem : DarkListViewItem
		{
			public override void Draw(Graphics g)
			{
				if ((mColumnIdx == 3) && (Label.IsEmpty))
				{
					var pathItem = GetSubItem(2);
					DateTime lastWriteTime = GetLastWriteTime(pathItem.Label);
					String label = scope String();
					lastWriteTime.ToShortDateString(label);
					label.Append(" ");
					lastWriteTime.ToShortTimeString(label);

					Label = label;
				}

				base.Draw(g);
			}
		}

		protected class EntryListView : DarkListView
		{
			protected override ListViewItem CreateListViewItem()
			{
				return new EntryListViewItem();
			}
		}

        public static Dictionary<String, int32> sMRU = new .() ~
			{
				for (let key in _.Keys)
					delete key;
				delete _;
			};
        public static int32 sCurrentMRUIndex = 1;
		static Dictionary<String, TimeEntry> sTimeCache = new .() ~ DeleteDictionaryAndKeysAndValues!(_);

        protected EntryListView mFileList;
        EditWidget mEditWidget;
        List<ProjectSource> mProjectSourceList = new .() ~ delete _;
        public bool mFilterChanged;
        public volatile bool mExitingThread;
        public Thread mDateThread;

		static String sLastSearchString = new String() ~ delete _;
        
        public this()
        {
            mWindowFlags = .ClientSized | .TopMost | .Caption | .Border | .SysMenu | .Resizable | .PopupPosition;

            AddOkCancelButtons(new (evt) => { GotoFile(); }, null, 0, 1);
            //mApplyButton = AddButton("Apply", (evt) => { evt.mCloseDialog = false; ApplyChanges(); });
            
            Title = "Open File in Workspace";

            mButtonBottomMargin = GS!(6);
            mButtonRightMargin = GS!(6);

            mFileList = new EntryListView();            
            mFileList.InitScrollbars(false, true);
            mFileList.mAllowMultiSelect = false;
			mFileList.mOnItemMouseDown.Add(new => ValueMouseDown);

            ListViewColumn column = mFileList.AddColumn(GS!(200), "File");
            column.mMinWidth = GS!(100);
            column = mFileList.AddColumn(GS!(80), "Project");
            column = mFileList.AddColumn(GS!(200), "Path");
            column = mFileList.AddColumn(GS!(200), "Modified");

            AddWidget(mFileList);
            mTabWidgets.Add(mFileList);

            mEditWidget = AddEdit(sLastSearchString);
            mEditWidget.mOnKeyDown.Add(new => EditKeyDownHandler);
            mEditWidget.mOnContentChanged.Add(new (evt) => { mFilterChanged = true; });

			mOnClosed.Add(new () =>
				{
					sLastSearchString.Clear();
					mEditWidget.GetText(sLastSearchString);
				});
        }

        void ShutdownThread()
        {
            if (mDateThread != null)
            {
                mExitingThread = true;
                mDateThread.Join();
                mDateThread = null;
            }
        }        

        void EditKeyDownHandler(KeyDownEvent evt)
        {
			switch (evt.mKeyCode)
			{
			case .Up,
				 .Down,
				 .PageUp,
				 .PageDown:
				mFileList.KeyDown(evt.mKeyCode, false);
			default:
			}

			if (evt.mKeyFlags == .Ctrl)
			{
				switch (evt.mKeyCode)
				{
				case .Home,
					 .End:
					mFileList.KeyDown(evt.mKeyCode, false);
				default:
				}
			}
        }

        public void ValueMouseDown(ListViewItem clickedItem, float x, float y, int32 btnNum, int32 btnCount)
        {
            DarkListViewItem item = (DarkListViewItem)clickedItem.GetSubItem(0);

            mFileList.GetRoot().SelectItemExclusively(item);
            mFileList.SetFocus();

            if ((btnNum == 0) && (btnCount > 1))
            {
                GotoFile();
            }
        }

        void FilterFilesFolder(String filterString, ProjectFolder projectFolder)
        {
            for (var projectEntry in projectFolder.mChildItems)
            {
                var childFolder = projectEntry as ProjectFolder;
                if (childFolder != null)
                    FilterFilesFolder(filterString, childFolder);

                var projectSource = projectEntry as ProjectSource;
                if (projectSource != null)
                {
                    String fileName = scope String();
                    Path.GetFileName(projectSource.mPath, fileName);
                    if (filterString.Length > 0)
                    {
                        if (fileName.IndexOf(filterString, true) == -1)
                            continue;
                    }
                    mProjectSourceList.Add(projectSource);                    
                }
            }
        }
        
        int CompareFileNames(String pathA, String pathB)
        {
            int pathStartA = pathA.LastIndexOf(IDEUtils.cNativeSlash);
            int pathStartB = pathB.LastIndexOf(IDEUtils.cNativeSlash);

            int lenA = pathA.Length - pathStartA - 1;
            int lenB = pathB.Length - pathStartB - 1;

            //int result = String.CompareOrdinal(pathA, pathStartA + 1, pathB, pathStartB + 1, Math.Min(lenA, lenB));
			int result = String.Compare(pathA, pathStartA + 1, pathB, pathStartB + 1, Math.Min(lenA, lenB), true);

            /*if (result == 0)
                result = lenA - lenB;*/
            return result;
        }

		public static DateTime GetLastWriteTime(StringView path)
		{
			var fixedPath = scope String(path);
			IDEUtils.MakeComparableFilePath(fixedPath);
			bool added = sTimeCache.TryAdd(fixedPath, var keyPtr, var valuePtr);
			if (added)
			{
				*keyPtr = new String(fixedPath);

				TimeEntry te = new .();
				te.mLastWriteTime = File.GetLastWriteTime(path);
				te.mCheckedTime = DateTime.Now;
				*valuePtr = te;
				return te.mLastWriteTime;
			}
			else
			{
				TimeEntry te = *valuePtr;
				if ((DateTime.Now - te.mCheckedTime).TotalSeconds > 60)
				{
					te.mCheckedTime = DateTime.Now;
					te.mLastWriteTime = File.GetLastWriteTime(path);
				}
				return te.mLastWriteTime;
			}
		}

		public static void ClearWriteTime(StringView path)
		{
			var fixedPath = scope String(path);
			IDEUtils.MakeComparableFilePath(fixedPath);
			if (sTimeCache.TryGet(fixedPath, var key, var value))
			{
				value.mCheckedTime = default;
			}
		}

        void FilterFiles()
        {
            String filterString = scope String();
            mEditWidget.GetText(filterString);
            filterString.Trim();

            mProjectSourceList.Clear();
            var root = mFileList.GetRoot();
            root.Clear();

            for (var project in IDEApp.sApp.mWorkspace.mProjects)
            {
                FilterFilesFolder(filterString, project.mRootFolder);
            }

            /*var selectedItem = null;// root.FindSelectedItem();
            if ((selectedItem == null) && (root.GetChildCount() > 0))
            {
                selectedItem = root.GetChildAtIndex(0);
                root.SelectItemExclusively(selectedItem);
            }*/
            
            mProjectSourceList.Sort(scope (itemA, itemB) => CompareFileNames(itemA.mPath, itemB.mPath));

            for (var projectSource in mProjectSourceList)
            {
                String fileName = scope String();
                Path.GetFileName(projectSource.mPath, fileName);

                var listViewItem = mFileList.GetRoot().CreateChildItem();
                listViewItem.Label = fileName;

                var subListViewItem = listViewItem.CreateSubItem(1);
                subListViewItem.Label = projectSource.mProject.mProjectName;

                String fullPath = scope String();
                projectSource.GetFullImportPath(fullPath);

                subListViewItem = listViewItem.CreateSubItem(2);
                subListViewItem.Label = fullPath;

                subListViewItem = listViewItem.CreateSubItem(3);
            }

            ListViewItem bestItem = null;
            int32 bestPri = -1;
            for (int32 childIdx = 0; childIdx < root.GetChildCount(); childIdx++)
            {
                var listViewItem = root.GetChildAtIndex(childIdx);
                var projectSource = mProjectSourceList[childIdx];

                int32 pri;
                sMRU.TryGetValue(projectSource.mPath, out pri);
                if (pri > bestPri)
                {
                    bestItem = listViewItem;
                    bestPri = pri;
                }
            }

            if (bestItem != null)
            {
                mFileList.GetRoot().SelectItemExclusively(bestItem);                
                mFileList.EnsureItemVisible(bestItem, true);
            }

            /*ShutdownThread();
            mExitingThread = true;
            mDateThread = new Thread(CheckFileDates);
            mDateThread.Start();*/
        }

        void GotoFile()
        {
			Close();

            var root = mFileList.GetRoot();
            var selectedListViewItem = root.FindFirstSelectedItem();
            if (selectedListViewItem != null)
            {
                var itemIdx = root.mChildItems.IndexOf(selectedListViewItem);
                var projectSource = mProjectSourceList[itemIdx];
				gApp.RecordHistoryLocation(true);
                gApp.ShowProjectItem(projectSource, false);
				if (sMRU.TryAdd(projectSource.mPath, var keyPtr, var valuePtr))
				{
					*keyPtr = new String(projectSource.mPath);
					*valuePtr = sCurrentMRUIndex++;
				}
            }
        }

        public override void AddedToParent()
        {
            base.AddedToParent();
            mEditWidget.SetFocus();

            FilterFiles();
        }

        public override void ResizeComponents()
        {
            base.ResizeComponents();
            
            //var font = DarkTheme.sDarkTheme.mSmallFont;

			float insetSize = GS!(6);
            mFileList.Resize(insetSize, insetSize, mWidth - insetSize - insetSize, mHeight - GS!(66));
            mEditWidget.Resize(insetSize, mFileList.mY + mFileList.mHeight + insetSize, mWidth - insetSize - insetSize, GS!(22));
        }

        public override void CalcSize()
        {
            mWidth = GS!(660);
            mHeight = GS!(512);
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

            if (mFilterChanged)
            {
                FilterFiles();
                mFilterChanged = false;
            }
        }
    }
}
