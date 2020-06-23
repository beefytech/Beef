using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.events;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using IDE.Compiler;
using System.Diagnostics;

namespace IDE.ui
{
    class NavigationBar : DarkComboBox
    {
		enum EntryType
		{
			Unknown,
			Method,
			Property,
			Class,
			Enum,
			Struct,
			TypeAlias
		}

        class Entry
        {
            public String mText ~ delete _;
			public EntryType mEntryType;
            public int32 mLine;
            public int32 mLineChar;
        }

        public static Dictionary<String, int32> sMRU = new Dictionary<String, int32>() ~ delete _;
        public static int32 sCurrentMRUIndex = 1;

        SourceViewPanel mSourceViewPanel;
        List<Entry> mEntries = new List<Entry>() ~ DeleteContainerAndItems!(_);
        List<Entry> mShownEntries = new List<Entry>() ~ delete _;
        String mFilterString ~ delete _;
		String mCurLocation = new String() ~ delete _;		
		bool mIgnoreChange = false;

        public this(SourceViewPanel sourceViewPanel)
        {
            mSourceViewPanel = sourceViewPanel;
            mLabelAlign = FontAlign.Left;
            Label = "";
            mLabelX = GS!(16);
            mPopulateMenuAction.Add(new => PopulateNavigationBar);
            MakeEditable();
            mEditWidget.mOnContentChanged.Add(new => NavigationBarChanged);
            mEditWidget.mOnKeyDown.Add(new => EditKeyDownHandler);
			mEditWidget.mOnGotFocus.Add(new (widget) => mEditWidget.mEditWidgetContent.SelectAll());
			mEditWidget.mEditWidgetContent.mWantsUndo = false;
			mFocusDropdown = false;
        }

		public ~this()
		{
		}

		static ~this()
		{
			for (var key in sMRU.Keys)
				delete key;
		}

		public void SetLocation(String location)
		{
			if (mCurMenuWidget == null)
			{
				mIgnoreChange = true;
				mEditWidget.SetText(location);
				// SetText can attempt to scroll to the right to make the cursor position visible.  Just scroll back to the start.
				mEditWidget.HorzScrollTo(0);
				mIgnoreChange = false;
			}
		}

        void EditKeyDownHandler(KeyDownEvent evt)
        {
            if (evt.mKeyCode == KeyCode.Escape)
                mSourceViewPanel.FocusEdit();
        }

        void GetEntries()
        {
			DeleteAndClearItems!(mEntries);
            ResolveParams resolveParams = scope ResolveParams();
            mSourceViewPanel.Classify(ResolveType.GetNavigationData, resolveParams);
            if (resolveParams.mNavigationData != null)
            {
				var autocompleteLines = scope List<StringView>(resolveParams.mNavigationData.Split('\n'));
				autocompleteLines.Sort(scope (a, b) => StringView.Compare(a, b, true));

                for (var autocompleteLineView in autocompleteLines)
                {
                    if (autocompleteLineView.Length == 0)
                        continue;
                    Entry entry = new Entry();
                    
					int idx = 0;
					for (var lineData in autocompleteLineView.Split('\t'))
					{
						switch (idx)
	                    {
                        case 0: entry.mText = new String(lineData);
						case 1:
							switch(lineData)
							{
							case "method": entry.mEntryType = .Method;
							case "property": entry.mEntryType = .Property;
							case "class": entry.mEntryType = .Class;
							case "enum": entry.mEntryType = .Enum;
							case "struct": entry.mEntryType = .Struct;
							case "typealias": entry.mEntryType = .TypeAlias;
							default:
							}
	                    case 2: entry.mLine = int32.Parse(lineData);
	                    case 3: entry.mLineChar = int32.Parse(lineData);
						}
						++idx;
					}
                    mEntries.Add(entry);
                }
            }
        }

        private void PopulateNavigationBar(Menu menu)
        {
			List<StringView> findStrs = null;
			if (mFilterString != null)
			 	findStrs = scope:: List<StringView>(mFilterString.Split(' '));

            EntryLoop: for (int32 entryIdx = 0; entryIdx < mEntries.Count; entryIdx++)            
            {
                var entry = mEntries[entryIdx];
				if (findStrs != null)
				{
					for (let findStr in findStrs)
					{
					    if (entry.mText.IndexOf(findStr, true) == -1)
					        continue EntryLoop;
					}
				}

                mShownEntries.Add(entry);
                var menuItem = menu.AddItem(entry.mText);
				switch (entry.mEntryType)
				{
				case .Method: menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Method);
				case .Property: menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Property);
				case .Class: menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Type_Class);
				case .Enum: menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Type_ValueType);
				case .Struct: menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.Type_ValueType);
				case .TypeAlias: menuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(.MemoryArrowSingle);
				default:
				}
				
                menuItem.mOnMenuItemSelected.Add(new (evt) => ShowEntry(entryIdx, entry));
            }
        }		

        void ShowEntry(int32 entryIdx, Entry entry)
        {
            //sMRU[entry.mText] = sCurrentMRUIndex++;
			String* keyPtr;
			int32* valPtr;
			if (sMRU.TryAdd(entry.mText, out keyPtr, out valPtr))
				*keyPtr = new String(entry.mText);
			*valPtr = sCurrentMRUIndex++;

            mSourceViewPanel.ShowFileLocation(-1, entry.mLine, entry.mLineChar, LocatorType.Always);            
        }

        private void NavigationBarChanged(EditEvent theEvent)
        {
			if (mIgnoreChange)
				return;

            var editWidget = (EditWidget)theEvent.mSender;
            var searchText = scope String();
            editWidget.GetText(searchText);
			searchText.Trim();
            mFilterString = searchText;
            ShowDropdown();
            mFilterString = null;
        }

		bool mIgnoreShowDropdown;

        public override MenuWidget ShowDropdown()
        {
			if (mIgnoreShowDropdown)
				return null;
			mIgnoreShowDropdown = true;
			defer { mIgnoreShowDropdown = false; }

			/*var stopWatch = scope Stopwatch();
			stopWatch.Start();*/
			//Profiler.StartSampling();

            if (!mEditWidget.mHasFocus)
                SetFocus();

            if (mCurMenuWidget == null)
                GetEntries();

            if (mFilterString == null)
                mEditWidget.Content.SelectAll();

            mShownEntries.Clear();
            base.ShowDropdown();

            int32 bestItem = -1;
            int32 bestPri = -1;
            var menuWidget = (DarkMenuWidget)mCurMenuWidget;

            for (int32 itemIdx = 0; itemIdx < menuWidget.mItemWidgets.Count; itemIdx++)
            {
                var menuItemWidget = (DarkMenuItem)menuWidget.mItemWidgets[itemIdx];

                int32 pri;
                sMRU.TryGetValue(menuItemWidget.mMenuItem.mLabel, out pri);
                if (pri > bestPri)
                {
                    bestItem = itemIdx;
                    bestPri = pri;
                }
            }

            if (bestItem != -1)
            {
				mCurMenuWidget.mOnSelectionChanged.Add(new => SelectionChanged);
                mCurMenuWidget.SetSelection(bestItem);
            }

			//Profiler.StopSampling();

			//stopWatch.Stop();
			//Debug.WriteLine("Time: {0}", stopWatch.ElapsedMilliseconds);
			return menuWidget;
        }

		void SelectionChanged(int selIdx)
		{
			if (mEditWidget.mEditWidgetContent.HasSelection())
			{
				bool prevIgnoreShowDropdown = mIgnoreShowDropdown;
				mIgnoreShowDropdown = true;
				mEditWidget.SetText("");
				mIgnoreShowDropdown = prevIgnoreShowDropdown;
			}
		}

        public override void MenuClosed()
        {
            mSourceViewPanel.FocusEdit();
        }
    }
}
