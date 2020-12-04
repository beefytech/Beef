using Beefy.theme.dark;
using System;
using Beefy.widgets;
using Beefy.gfx;
using Beefy.events;
using Beefy.utils;
using System.Diagnostics;
using IDE.Compiler;
using System.Collections;
using System.Threading;

namespace IDE.ui
{
	class ClassViewPanel : Panel
	{
		public class ClassViewListViewItem : IDEListViewItem
		{
		    public float mLabelOffset;
		    public Object mRefObject;
			public DataKind mKind;
			public PendingEntry mMemberInfo ~ delete _;
			public bool mProcessedLabel;

		    protected override float GetLabelOffset()
		    {
		        return mLabelOffset;
		    }

		    public override float ResizeComponents(float xOffset)
		    {
		        return base.ResizeComponents(xOffset);
		    }

		    public override void DrawSelect(Graphics g)
		    {
				bool hasFocus = mListView.mHasFocus;
				if ((mWidgetWindow.mFocusWidget != null) && (mWidgetWindow.mFocusWidget.HasParent(mListView)))
					hasFocus = true;
				let parent = (ClassViewListView)mListView;
				if (parent.mPanel.mFindClassDialog != null)
					hasFocus = true;
		        using (g.PushColor(hasFocus ? 0xFFFFFFFF : 0x80FFFFFF))
		            base.DrawSelect(g);
		    }

		    public override void Draw(Graphics g)
		    {
				if ((!mProcessedLabel) && (mLabel != null))
				{
					uint32 color = 0;
					switch (mMemberInfo.mKind)
					{
					case .Globals:
						/*color = SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Identifier];
						color = Color.Mult(color, Color.Get(0.8f));*/
					case .Namespace:
						color = SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Namespace];
					case .Interface,
						 .Class,
						 .ValueType:
						IDEUtils.ColorizeCodeString(mLabel, .Type);
					case .Field,
						 .Property:
						IDEUtils.ColorizeCodeString(mLabel, .Field);
					case .Method,
						 .MethodOverride:
						IDEUtils.ColorizeCodeString(mLabel, .Method);
					default:
					}

					if (color != 0)
						IDEUtils.InsertColorChange(mLabel, 0, color);
						
					mProcessedLabel = true;
				}

		        base.Draw(g);

		        if (mRefObject != null)
		        {
		            bool hasChanged = false;
		            var workspace = mRefObject as Workspace;
		            if (workspace != null)
		                hasChanged = workspace.mHasChanged;
		            var project = mRefObject as Project;
		            if (project != null)
		                hasChanged = project.mHasChanged;
		            if (hasChanged)
		                g.DrawString("*", g.mFont.GetWidth(mLabel) + mLabelOffset + LabelX + 1, 0);
		        }
		    }

			public void Init()
			{
				Image icon = null;
				
				switch (mKind)
				{
				case .Project:
					icon = DarkTheme.sDarkTheme.GetImage(.Project);
				case .Namespace:
					icon = DarkTheme.sDarkTheme.GetImage(.Namespace);
				case .Class:
					icon = DarkTheme.sDarkTheme.GetImage(.Type_Class);
				case .Interface:
					icon = DarkTheme.sDarkTheme.GetImage(.Interface);
				case .ValueType:
					icon = DarkTheme.sDarkTheme.GetImage(.Type_ValueType);
				case .Field:
					icon = DarkTheme.sDarkTheme.GetImage(.Field);
				case .Property:
					icon = DarkTheme.sDarkTheme.GetImage(.Property);
				case .Method,
					 .MethodOverride:
					icon = DarkTheme.sDarkTheme.GetImage(.Method);
				default:
				}
				IconImage = icon;
			}
		}

		public class ClassViewListView : IDEListView
		{
			public ClassViewPanel mPanel;

		    protected override ListViewItem CreateListViewItem()
		    {
		        var anItem = new ClassViewListViewItem();
		        return anItem;
		    }

			public override void KeyDown(KeyCode keyCode, bool isRepeat)
			{
				base.KeyDown(keyCode, isRepeat);

				if (keyCode == .Return)
				{
					if (var item = (ClassViewListViewItem)GetRoot().FindFocusedItem())
						mPanel.ShowItem(item);
				}
				else if (keyCode == .Tab)
				{
					if (this == mPanel.mTypeLV)
					{
						if (mWidgetWindow.IsKeyDown(.Shift))
							mPanel.mSearchEdit.SetFocus();
						else if (mPanel.mMemberLV != null)
						{
							if (mPanel.mMemberLV.GetRoot().GetChildCount() > 0)
							{
								if (mPanel.mMemberLV.GetRoot().FindFocusedItem() == null)
									mPanel.mMemberLV.GetRoot().GetChildAtIndex(0).Focused = true;
								mPanel.mMemberLV.SetFocus();
							}
						}
						else
							mPanel.mSearchEdit.SetFocus();
					}
					else
					{
						if (mWidgetWindow.IsKeyDown(.Shift))
							mPanel.mTypeLV.SetFocus();
					}
				}
			}
		}

		public class TypeArea : DockedWidget
		{
			public ClassViewPanel mPanel;

			public override void Resize(float x, float y, float width, float height)
			{
				base.Resize(x, y, width, height);
				mPanel.mSearchEdit.Resize(GS!(2), GS!(4), width - GS!(4), GS!(24));
				mPanel.mTypeLV.Resize(0, GS!(30), width, height - GS!(30));
			}

			public override void DrawAll(Beefy.gfx.Graphics g)
			{
				base.DrawAll(g);
				using (g.PushColor(0x80FFFFFF))
					g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Bkg), 0, mHeight - GS!(1), mWidth, GS!(4));
			}
		}

		public class PendingEntry : IHashable, IEquatable<PendingEntry>
		{
			public DataKind mKind;
			public bool mShow = true;
			public String mName ~ delete _;
			public PendingEntry mParent;
			public HashSet<PendingEntry> mChildren;
			public List<PendingEntry> mSortedList;
			public Object mRefObject;

			public String mFile ~ delete _;
			public int mLine;
			public int mColumn;

			public this()
			{
			}

			public this(DataKind kind, String name)
			{
				mKind = kind;
				mName = name;
			}

			public ~this()
			{
				if (mSortedList != null)
				{
					DeleteContainerAndItems!(mSortedList);
					delete mChildren;
				}
				else
				{
					DeleteContainerAndItems!(mChildren);
				}
			}

			void Detach()
			{
				mName = null;
			}

			public bool Equals(PendingEntry other)
			{
				return this == other;
			}

			public static bool operator==(PendingEntry val1, PendingEntry val2)
			{
				if (((Object)val1 == null) || ((Object)val2 == null))
					return ((Object)val1 == null) && ((Object)val2 == null);
				if (val1.mKind != val2.mKind)
				{
					if ((val1.mKind == .Project) || (val2.mKind == .Project))
						return false;
					if ((val1.mKind != .Namespace) && (val2.mKind != .Namespace))
						return false;
				}
				return (val1.mName == val2.mName);
			}

			public int GetHashCode()
			{
				return mName.GetHashCode();
			}

			public PendingEntry AddChild(DataKind kind, String name)
			{
				if (mChildren == null)
				{
					mChildren = new HashSet<PendingEntry>();
					PendingEntry child = new PendingEntry(kind, new String(name));
					if (mKind != .Root)
						child.mParent = this;
					Debug.Assert(child != (Object)this);
					mChildren.Add(child);
					return child;
				}
				
                PendingEntry pendingEntry = scope PendingEntry(kind, name);
				PendingEntry* entryPtr;
				if (mChildren.Add(pendingEntry, out entryPtr))
				{
					let child = new PendingEntry(kind, new String(name));
					if (mKind != .Root)
						child.mParent = this;
					Debug.Assert(child != (Object)this);
					*entryPtr = child;
				}
				else
				{
					if (kind > (*entryPtr).mKind)
						(*entryPtr).mKind = kind;
				}
				pendingEntry.Detach();
				return *entryPtr;
			}
			
			public void Sort()
			{
				if (mChildren == null)
					return;

				mSortedList = new List<PendingEntry>();
				for (var entry in mChildren)
				{
					entry.Sort();
					mSortedList.Add(entry);
				}
				mSortedList.Sort(scope (a, b) =>
					{
						DataKind lk = a.mKind;
						DataKind rk = b.mKind;
						if (lk < .Field)
							lk = .ValueType;
						if (rk < .Field)
							rk = .ValueType;
						int comp = lk <=> rk;
						if (comp != 0)
							return comp;
						return a.mName.CompareTo(b.mName, true);
					});
			}
		}

		public enum DataKind
		{
			Root,
			Project,
			Globals,
			Namespace,
			Interface,
			Class,
			ValueType,

			Field,
			Property,
			Method,
			MethodOverride,
		}

		class PendingInfo
		{
			public PendingEntry mPendingRoot ~ delete _;
			public String mTypeStr ~ delete _;
			public String mSearchStr ~ delete _;
			public WaitEvent mDoneEvent = new .() ~ delete _;
		}

		public DarkDockingFrame mDockingFrame;
		public DarkEditWidget mSearchEdit;
		public ClassViewListView mTypeLV;
		public ClassViewListView mMemberLV;
		public int32 mLastCompileRevision;
		public int32 mCompileRevisionDirtyDelay;
		public bool mTypesDirty = true;
		public bool mMembersDirty = true;
		public int32 mWorkWait = -1;
		public bool mWantsSubmit;

		public String mRequestedTypeName = new .() ~ delete _;
		public String mRequestedSearchStr = new .() ~ delete _;
		PendingInfo mPendingInfo ~ delete _;
		public List<PendingEntry> mHiddenEntries = new .() ~ DeleteContainerAndItems!(_);
		public FindClassDialog mFindClassDialog;

		public this(FindClassDialog findClassDialog = null)
		{
			mFindClassDialog = findClassDialog;
			mSearchEdit = new DarkEditWidget();
			mSearchEdit.mOnKeyDown.Add(new (evt) =>
				{
					if ((evt.mKeyCode == .Tab) && (evt.mKeyFlags == 0))
					{
						mTypeLV.SetFocus();
					}
				});

		    mTypeLV = new ClassViewListView();
			mTypeLV.mPanel = this;
		    mTypeLV.SetShowHeader(false);
		    mTypeLV.InitScrollbars(true, true);
		    mTypeLV.mOnFocusChanged.Add(new => FocusChangedHandler);
			mTypeLV.mOnItemMouseDown.Add(new => ItemMouseDown);
			mTypeLV.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);
			mTypeLV.mOnKeyDown.Add(new => ListViewKeyDown_ShowMenu);

			if (mFindClassDialog == null)
			{
				mMemberLV = new ClassViewListView();
				mMemberLV.mPanel = this;
				mMemberLV.SetShowHeader(false);
				mMemberLV.InitScrollbars(true, true);
				mMemberLV.mOnItemMouseDown.Add(new => ItemMouseDown);
				mMemberLV.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);
				mMemberLV.mOnKeyDown.Add(new => ListViewKeyDown_ShowMenu);
			
				var typeDock = new TypeArea();
				typeDock.mPanel = this;
				typeDock.AddWidget(mSearchEdit);
				typeDock.AddWidget(mTypeLV);
				var memberDock = new DockingProxy(mMemberLV);
	
				mDockingFrame = new DarkDockingFrame();
				mDockingFrame.mDrawBkg = false;
				mDockingFrame.AddDockedWidget(typeDock, null, .Top);
				mDockingFrame.AddDockedWidget(memberDock, typeDock, .Bottom);
				AddWidget(mDockingFrame);
			}
			else
			{
				AddWidget(mTypeLV);
				AddWidget(mSearchEdit);

				mFindClassDialog.[Friend]mTabWidgets.Add(mTypeLV);
				mFindClassDialog.[Friend]mTabWidgets.Add(mSearchEdit);

				mSearchEdit.mOnKeyDown.Add(new => EditKeyDownHandler);
				mSearchEdit.mOnSubmit.Add(new (evt) =>
					{
						mWantsSubmit = true;
					});
			}

		    //mListView.mDragEndHandler.Add(new => HandleDragEnd);
		    //mListView.mDragUpdateHandler.Add(new => HandleDragUpdate);
		    
		    mTypeLV.mOnMouseDown.Add(new => ListViewMouseDown);

		    /*ListViewColumn column = mTypeLV.AddColumn(100, "Name");
		    column.mMinWidth = 40;*/
		    
			SetScaleData();

			/*ListView lv = new MemberListView();
			AddWidget(lv);
			lv.Resize(100, 100, 300, 300);
			let lvItem = lv.GetRoot().CreateChildItem();
			lvItem.Label = "Yoofster";
			lvItem.Focused = true;*/
		}

		public ~this()
		{
			if (mPendingInfo != null)
				mPendingInfo.mDoneEvent.WaitFor();
		}

		void EditKeyDownHandler(KeyDownEvent evt)
		{
			switch (evt.mKeyCode)
			{
			case .Up,
				 .Down,
				 .Left,
				 .Right,
				 .PageUp,
				 .PageDown:
				mTypeLV.KeyDown(evt.mKeyCode, false);
			default:
			}

			if (evt.mKeyFlags == .Ctrl)
			{
				switch (evt.mKeyCode)
				{
				case .Home,
					 .End:
					mTypeLV.KeyDown(evt.mKeyCode, false);
				default:
				}
			}
		}

		void SetScaleData()
		{
			//mTypeLV.mColumns[0].mMinWidth = GS!(40);

			mTypeLV.mIconX = GS!(20);
			mTypeLV.mOpenButtonX = GS!(4);
			mTypeLV.mLabelX = GS!(44);
			mTypeLV.mChildIndent = GS!(16);
			mTypeLV.mHiliteOffset = GS!(-2);

			if (mMemberLV != null)
			{
				mMemberLV.mIconX = GS!(20);
				mMemberLV.mOpenButtonX = GS!(4);
				mMemberLV.mLabelX = GS!(44);
				mMemberLV.mChildIndent = GS!(16);
				mMemberLV.mHiliteOffset = GS!(-2);
			}
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			SetScaleData();
		}

		public override void FocusForKeyboard()
		{
			base.FocusForKeyboard();
			mTypeLV.SetFocus();
		}

		public void StartRebuildUI()
		{
			Debug.Assert(mPendingInfo.mPendingRoot == null);
			mPendingInfo.mPendingRoot = new .();

		    for (var project in IDEApp.sApp.mWorkspace.mProjects)
		    {
				mPendingInfo.mPendingRoot.AddChild(.Project, project.mProjectName);
			}    
		}

		public override void Serialize(StructuredData data)
		{
		    base.Serialize(data);

		    data.Add("Type", "ClassViewPanel");
		}

		public override bool Deserialize(StructuredData data)
		{
		    return base.Deserialize(data);
		}

		void FocusChangedHandler(ListViewItem listViewItem)
		{
		    if (listViewItem.Focused)
		    {

		    }
		}

		void ShowRightClickMenu(ProjectItem projectItem, float x, float y)
		{            
		 
		}

		bool GetName(PendingEntry entry, String name)
		{
			if (entry.mParent != null)
			{
				if (GetName(entry.mParent, name))
					name.Append(".");
			}

			if (entry.mKind == .Project)
			{
				name.Append(entry.mName);
				name.Append(":");
				return false;
			}

			name.Append(entry.mName);
			return true;
		}

		bool GetName(ClassViewListViewItem item, String name)
		{
			if (item == null)
				return false;

			if (item.mMemberInfo != null)
				return GetName(item.mMemberInfo, name);

			if (item.mKind == .Project)
			{
				name.Append(item.mLabel);
				name.Append(":");
				return false;
			}
			let parentItem = (ClassViewListViewItem)item.mParentItem;
			if (GetName(parentItem, name))
				name.Append(".");
			name.Append(item.mLabel);
			return true;
		}

		void ShowItem(ClassViewListViewItem item)
		{
			void Show(String file, int line, int column)
			{
				gApp.RecordHistoryLocation(true);
				gApp.ShowSourceFileLocation(file, -1, -1, line, column, .Always);
				if (mFindClassDialog != null)
					mFindClassDialog.Close();
			}

			if (item.mMemberInfo != null)
			{
				let info = item.mMemberInfo;
				if (info.mFile != null)
				{
					Show(info.mFile, info.mLine, info.mColumn);
					return;
				}
			}

			String typeName = scope .();
			GetName(item, typeName);
			String info = scope .();
			gApp.mBfResolveCompiler.GetTypeDefInfo(typeName, info);

			for (let str in info.Split('\n'))
			{
				if (str.IsEmpty)
					continue;
				if (str[0] == 'S')
				{
					var srcInfo = str.Substring(1).Split('\t');
					let filePath = srcInfo.GetNext().Get();
					int lineNum = int.Parse(srcInfo.GetNext());
					int column = int.Parse(srcInfo.GetNext());
					Show(scope String()..Append(filePath), lineNum, column);
				}
			}
		}

		public void ItemMouseDown(ListViewItem item, float x, float y, int32 btnNum, int32 btnCount)
		{
			ListViewItemMouseDown(item, x, y, btnNum, btnCount);
			let baseItem = (ClassViewListViewItem)item.GetSubItem(0);
			if ((btnNum == 0) && (btnCount == 2))
			{
				ShowItem(baseItem);
			}
		}

		void ListViewMouseDown(MouseEvent theEvent)
		{
		    // We clicked off all items, so deselect
			mTypeLV.GetRoot().WithSelectedItems(scope (item) => { item.Selected = false; } );
		    
		    if (theEvent.mBtn == 1)
		    {
		        float aX, aY;
		        theEvent.GetRootCoords(out aX, out aY);
		        ShowRightClickMenu(null, aX, aY);
		    }
		}

		void ParseTypeInfo(String str)
		{
			List<PendingEntry> projectEntries = scope List<PendingEntry>();
			PendingEntry curProject = null;

			List<PendingEntry> partialRefs = scope List<PendingEntry>();

			char8[] seps = scope char8[] ('.', '+');

			for (var subStrRef in str.Split('\n'))
			{
				String param = scope String();
				param.Reference(subStrRef);
				if (param.Length == 0)
					continue;

				char8 cmd = param[0];
				param.AdjustPtr(1);

				if (cmd == 'S')
					continue;

				int addIdx = -1;
				PendingEntry parentEntry = null;

				if (cmd == '>')
				{
					int atPos = param.IndexOf('@');
					addIdx = int.Parse(StringView(param, 0, atPos)).Get();
					param.AdjustPtr(atPos + 1);
					cmd = param[0];
					param.AdjustPtr(1);
				}
				else if (cmd == '<')
				{
					int atPos = param.IndexOf('@');
					int refIdx = int.Parse(StringView(param, 0, atPos)).Get();
					parentEntry = partialRefs[refIdx];
					param.AdjustPtr(atPos + 1);
					cmd = param[0];
					param.AdjustPtr(1);
				}
				else if (cmd == ':')
				{
					parentEntry = mPendingInfo.mPendingRoot;
					cmd = param[0];
					param.AdjustPtr(1);
				}

				switch (cmd)
				{
				case '=':
					int32 idx = int32.Parse(param);
					curProject = projectEntries[idx];
				case '+':
					curProject = mPendingInfo.mPendingRoot.AddChild(.Project, param);
					projectEntries.Add(curProject);
					if (mPendingInfo.mSearchStr != null)
						curProject.mShow = false;
				case 'i', 'c', 'v', 'g':
					var curEntry = curProject;

					DataKind kind = .ValueType;
					if (cmd == 'i')
						kind = .Interface;
					else if (cmd == 'c')
						kind = .Class;
					else if (cmd == 'g')
						kind = .Globals;

					if (parentEntry != null)
					{
						if (parentEntry == (Object)mPendingInfo.mPendingRoot)
						{
							curEntry = parentEntry.AddChild(kind, param);
							curEntry.mParent = curProject;
							break;
						}
						
						if (parentEntry.mName == param)
						{
							parentEntry.mKind = kind;
							break;
						}

						param.AdjustPtr(parentEntry.mName.Length + 1);
						curEntry = parentEntry;
					}

					int nameIdx = 0;
					for (var subName in param.Split(seps))
					{
						if (nameIdx < addIdx)
						{
							nameIdx++;
							continue;
						}

						String subNameStr = scope String();

						bool isLast = subName.Ptr + subName.Length == param.Ptr + param.Length;

						if (nameIdx == addIdx)
						{
							DataKind insertKind = isLast ? kind : .Namespace;
							subNameStr.Reference(StringView(param, 0, @subName.MatchPos));
							curEntry = mPendingInfo.mPendingRoot.AddChild(insertKind, subNameStr);
							curEntry.mParent = curProject;
							Debug.Assert(curProject != (Object)curEntry);
							partialRefs.Add(curEntry);
							nameIdx++;
							addIdx = -1;
							continue;
						}

						DataKind insertKind = kind;
						if (!isLast)
						{
							char8 nextChar = subName.Ptr[subName.Length];
							if (nextChar == '.')
								insertKind = .Namespace;
						}
						
						subNameStr.Reference(subName);
						curEntry = curEntry.AddChild(insertKind, subNameStr);

						nameIdx++;
					}
				default:
					DataKind kind = default;
					switch (cmd)
					{
					case 'I': kind = .Interface;
					case 'C': kind = .Class;
					case 'V': kind = .ValueType;
					case 'F': kind = .Field;
					case 'P': kind = .Property;
					case 'M': kind = .Method;
					case 'o': kind = .MethodOverride;
					}
					var itr = param.Split('\t');
					let entry = mPendingInfo.mPendingRoot.AddChild(kind, scope String()..Reference(itr.GetNext().Get()));
					if (entry.mFile == null)
					{
						if (itr.GetNext() case .Ok(let fileName))
						{
							entry.mFile = new String(fileName);
							entry.mLine = int.Parse(itr.GetNext()).Get();
							entry.mColumn = int.Parse(itr.GetNext()).Get();
						}
					}
				}

				Debug.Assert(addIdx == -1);
			}

			mPendingInfo.mPendingRoot.Sort();
		}

		void BkgGetTypeList()
		{
			var bfSystem = gApp.mBfResolveSystem;
			var bfCompiler = gApp.mBfResolveCompiler;
			
			bfSystem.Lock(0);
			String outStr = scope String();
			if (mPendingInfo.mSearchStr != null)
				bfCompiler.GetTypeDefMatches(mPendingInfo.mSearchStr, outStr);
			else
				bfCompiler.GetTypeDefList(outStr);
			bfSystem.Unlock();

			ParseTypeInfo(outStr);
			
			mPendingInfo.mDoneEvent.Set();
		}

		void BkgGetTypeInfo()
		{
			var bfSystem = gApp.mBfResolveSystem;
			var bfCompiler = gApp.mBfResolveCompiler;

			bfSystem.Lock(0);
			String info = scope .();
			bfCompiler.GetTypeDefInfo(mPendingInfo.mTypeStr, info);
			bfSystem.Unlock();
			ParseTypeInfo(info);
			
			mPendingInfo.mDoneEvent.Set();
		}

		void HandleEntries(ClassViewListViewItem listViewItem, PendingEntry pendingEntry)
		{
			int32 curIdx = 0;

			if (pendingEntry.mSortedList != null)
			{
				for (var child in pendingEntry.mSortedList)
				{
					if (!child.mShow)
					{
						@child.Current = null;
						mHiddenEntries.Add(child);
						continue;
					}

					ClassViewListViewItem childListViewItem;
					if (curIdx < listViewItem.GetChildCount())
					{
						childListViewItem = (ClassViewListViewItem)listViewItem.GetChildAtIndex(curIdx);
					}
					else
					{
	                	childListViewItem = (ClassViewListViewItem)listViewItem.CreateChildItem();
					}
					childListViewItem.Label = child.mName;
					childListViewItem.mRefObject = pendingEntry.mRefObject;
					childListViewItem.mOpenOnDoubleClick = false;
					childListViewItem.mKind = child.mKind;
					childListViewItem.mProcessedLabel = false;
					DeleteAndNullify!(childListViewItem.mMemberInfo);
					//if (child.mFile != null)
					{
						childListViewItem.mMemberInfo = child;
						@child.Current = null;
					}

					childListViewItem.Init();

					HandleEntries(childListViewItem, child);
					curIdx++;
				}
			}

			while (curIdx < listViewItem.GetChildCount())
				listViewItem.RemoveChildItemAt(curIdx);
			listViewItem.TryUnmakeParent();
		}

		public override void Update()
		{
			base.Update();

			var focusedItem = (ClassViewListViewItem)mTypeLV.GetRoot().FindFocusedItem();
			var focusedStr = scope String();
			if (focusedItem != null)
				GetName(focusedItem, focusedStr);

			int32 compileRevision = gApp.mBfResolveCompiler.GetCompileRevision();
			if (mLastCompileRevision != compileRevision)
			{
				mCompileRevisionDirtyDelay = 30;
				mLastCompileRevision = compileRevision;
			}

			if ((mCompileRevisionDirtyDelay > 0) && (--mCompileRevisionDirtyDelay == 0))
			{
				mTypesDirty = true;
				mMembersDirty = true;
			}

			var searchStr = scope String();
			mSearchEdit.GetText(searchStr);
			searchStr.Trim();

			if (mPendingInfo != null)
			{
				mWorkWait++;
				if (gApp.mUpdateCnt % 10 == 0)
					MarkDirty();
				if (mPendingInfo.mDoneEvent.WaitFor(0))
				{
					if (mPendingInfo.mTypeStr != null)
					{
						if (mPendingInfo.mTypeStr == focusedStr)
							HandleEntries((ClassViewListViewItem)mMemberLV.GetRoot(), mPendingInfo.mPendingRoot);
					}
					else if (mPendingInfo.mPendingRoot != null)
					{
						ClearAndDeleteItems(mHiddenEntries);
						HandleEntries((ClassViewListViewItem)mTypeLV.GetRoot(), mPendingInfo.mPendingRoot);
						focusedItem = (ClassViewListViewItem)mTypeLV.GetRoot().FindFocusedItem();
						if ((focusedItem == null) && (mTypeLV.GetRoot().GetChildCount() != 0))
						{
							mTypeLV.GetRoot().GetChildAtIndex(0).Focused = true;
						}
					}

					DeleteAndNullify!(mPendingInfo);
					mWorkWait = -1;
					MarkDirty();
				}
			}

			if (mPendingInfo == null)
			{
				bool TryStartWork()
				{
					mWorkWait++;
					return !gApp.mBfResolveCompiler.IsPerformingBackgroundOperation();
				}

				if (!searchStr.IsEmpty)
				{
					bool hasFocus = mHasFocus;
					if ((mWidgetWindow.mFocusWidget != null) && (mWidgetWindow.mFocusWidget.HasParent(this)))
						hasFocus = true;
					if ((searchStr != mRequestedSearchStr) ||
						((mTypesDirty) && (hasFocus)))
					{
						if (TryStartWork())
						{
							mPendingInfo = new .();
							mPendingInfo.mPendingRoot = new .();
							mPendingInfo.mSearchStr = new String(searchStr);
							gApp.mBfResolveCompiler.DoBackground(new => BkgGetTypeList);
							mRequestedSearchStr.Set(searchStr);
							mTypesDirty = false;
						}
					}
				}
				else
				{
					if ((searchStr != mRequestedSearchStr) || (mTypesDirty))
					{
						if (TryStartWork())
						{
							mPendingInfo = new .();
							StartRebuildUI();
							//Debug.WriteLine("Rebuilding UI...");
							gApp.mBfResolveCompiler.DoBackground(new => BkgGetTypeList);
							mRequestedSearchStr.Set(searchStr);
							mTypesDirty = false;
						}
					}
				}

				if ((mPendingInfo == null) && (mMemberLV != null))
				{
					if ((mRequestedTypeName != focusedStr) || (mMembersDirty))
					{
						if (TryStartWork())
						{
							mPendingInfo = new PendingInfo();
							mPendingInfo.mTypeStr = new String(focusedStr);
							mPendingInfo.mPendingRoot = new .();
							gApp.mBfResolveCompiler.DoBackground(new => BkgGetTypeInfo);
							mRequestedTypeName.Set(focusedStr);
							mMembersDirty = false;
						}
					}
				}
			}

			if (mWorkWait == -1)
			{
				if (mWantsSubmit)
				{
					mTypeLV.KeyDown(.Return, false);
					mWantsSubmit = false;
				}
			}
		}

		public override void Resize(float x, float y, float width, float height)
		{
		    base.Resize(x, y, width, height);
			if (mDockingFrame != null)
		    	mDockingFrame.Resize(0, 0, mWidth, mHeight);
			else
			{
				float insetSize = GS!(6);
				mTypeLV.Resize(GS!(6), GS!(6), mWidth - GS!(12), mHeight - GS!(32));
				mSearchEdit.Resize(insetSize, mTypeLV.mY + mTypeLV.mHeight + insetSize, mWidth - insetSize - insetSize, GS!(22));
			}
		}

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);

			void DrawRefresh(Widget widget, int life)
			{
				widget.SelfToOtherTranslate(this, 0, 0, var x, var y);

				using (g.PushColor(0x60505050))
					g.FillRect(x, y, widget.Width, widget.Height);
				IDEUtils.DrawWait(g, x + widget.Width/2, y + widget.Height/2, life);
			}

			if (mWorkWait >= 30)
			{
				bool hasFocus = mHasFocus;
				if ((mWidgetWindow.mFocusWidget != null) && (mWidgetWindow.mFocusWidget.HasParent(this)))
					hasFocus = true;
				bool hasContent = (mTypeLV.GetRoot().GetChildCount() > 0) || (!mSearchEdit.IsWhiteSpace());
				if ((hasFocus) || (!hasContent))
				{
					if (mPendingInfo?.mTypeStr != null)
						DrawRefresh(mMemberLV, mUpdateCnt);
					else
						DrawRefresh(mTypeLV, mUpdateCnt);
				}
			}

			if (mFindClassDialog != null)
				IDEUtils.DrawOutline(g, mTypeLV);
		}
	}
}
