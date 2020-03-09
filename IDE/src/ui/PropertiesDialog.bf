using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.gfx;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.events;
using Beefy.geom;
using System.IO;

namespace IDE.ui
{
	public class SearchEditWidget : DarkEditWidget
	{
		public int mSearchVersion;

		protected override void SetupInsets()
		{
			mScrollContentInsets.Set(GS!(3), GS!(23), GS!(3), GS!(3));
		}
	}

	public class KeysEditWidgetContent : DarkEditWidgetContent
	{
		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			// Disable
		}
	}

	public class KeysEditWidget : DarkEditWidget
	{
		public this() : base(new KeysEditWidgetContent())
		{
			mEditWidgetContent.mEditWidget = this;
			mOnKeyDown.Add(new => KeyDownEvent);
		}

		void KeyDownEvent(KeyDownEvent evt)
		{
			if ((evt.mKeyCode == .Control) ||
				(evt.mKeyCode == .Shift) ||
				(evt.mKeyCode == .Alt))
				return;

			if ((evt.mKeyFlags == 0) &&
				(evt.mKeyCode == .Escape))
			{
				evt.mHandled = false;
				return;
			}

			var keyState = scope KeyState();
			keyState.mKeyCode = evt.mKeyCode;
			keyState.mKeyFlags = evt.mKeyFlags;
			
			if (keyState.mKeyFlags == 0)
			{
				if (keyState.mKeyCode == .Return)
					return;
			}

			evt.mHandled = true;
			var str = scope String();
			keyState.ToString(str);

			
			if (HasKeys)
			{
				str.Insert(0, ", ");
				mEditWidgetContent.mIsReadOnly = false;
				Content.InsertAtCursor(str);
				mEditWidgetContent.mIsReadOnly = true;
			}
			else
			{
				if ((evt.mKeyFlags == 0) &&
					((evt.mKeyCode == .Delete) || (evt.mKeyCode == .Backspace)) &&
					(Content.HasSelection()))
				{
					if ((evt.mKeyCode == .Delete) || (evt.mKeyCode == .Backspace))
					{
						Content.ClearText();
						return;
					}
				}

				mEditWidgetContent.mIsReadOnly = false;
				Content.InsertAtCursor(str);
				mEditWidgetContent.mIsReadOnly = true;
			}
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);
		}

		public bool HasKeys
		{
			get
			{
				var str = scope String();
				GetText(str);
				return !str.IsEmpty && !str.StartsWith("<");
			}
		}
	}

    public class PropertiesDialog : IDEDialog
    {
		class OwnedStringList : List<String>
		{

		}

        protected class ValueContainer<T>
        {
            public T mValue;
        }

        protected class MoveItemWidget : Widget
        {
            public int32 mArrowDir;

            public override void Draw(Graphics g)
            {
                base.Draw(g);
                //g.FillRect(0, 0, mWidth, mHeight);

                if (mArrowDir == -1)
                {
                    using (g.PushScale(1, -1))
                        g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.ArrowMoveDown), 0, -20);
                }
                else
                {
                    g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.ArrowMoveDown), 0, 0);
                }
            }
        }

        protected class PropPage
        {
			public enum Flags
			{
				None = 0,
				AllowSearch = 1,
				AllowReset = 2,
			}

            public int32 mCategoryType;
            public DarkListView mPropertiesListView ~ delete _;
            public Dictionary<ListViewItem, PropEntry[]> mPropEntries = new .() ~ delete _;
            public bool mHasChanges;
			public Flags mFlags;

			public this()
			{
				//Debug.WriteLine("PropPage this {0}", this);
			}

			public ~this()
			{
				//Debug.WriteLine("PropPage ~this {0}", this);

				for (var propEntryArr in mPropEntries.Values)
				{
					for (var propEntry in propEntryArr)
						delete propEntry;
					delete propEntryArr;
				}
				if (mPropertiesListView.mParent != null)
					mPropertiesListView.RemoveSelf();
			}
        }

        protected class PropEntry
        {
			public enum Flags
			{
				None,
				BrowseForFile,
				BrowseForFolder,
			}

			public Flags mFlags;
            public DarkListViewItem mListViewItem;
            public Object mTarget;
            public String mPropertyName ~ delete _;
            public FieldInfo mFieldInfo;
            public Action mApplyAction ~ delete _;
            public String[] mOptionValues ~ DeleteContainerAndItems!(_);
			public String mNotSetString;
            public List<DarkComboBox> mComboBoxes ~ delete _;
            public DarkCheckBox mCheckBox;
            public Variant mCurValue ~ DisposeVariant(ref _);
            public Variant mOrigValue ~ DisposeVariant(ref _);
			public PropertyBag mProperties ~ delete _;
			public Event<delegate bool()> mOnUpdate ~ _.Dispose();
			public bool mDisabled;
			public uint32? mColorOverride;
			public String mRelPath ~ delete _;
			public bool mIsTypeWildcard;
			public Insets mEditInsets ~ delete _;

			public ~this()
			{
			
			}

			public static void DisposeVariant(ref Variant variant)
			{
				if ((variant.OwnsMemory) && (variant.IsObject))
				{
					var obj = variant.Get<Object>();
					if (var strList = obj as String[])
					{
						for (var str in strList)
							delete str;
					}
					if (var strList = obj as List<String>)
					{
						for (var str in strList)
							delete str;
					}
					if (var keyList = obj as List<KeyState>)
					{
						for (var keyState in keyList)
							delete keyState;
					}
				}
				variant.Dispose();
			}

			public static bool IsVariantEqual(Variant lhs, Variant rhs)
			{
				//var curEq = lhs as IEquatable;
				//if (curEq.Equals(rhs))
					//return false;

				//TODO: Implement equals
				var type = lhs.VariantType;
				if (type.IsObject)
				{
					Object obj = lhs.Get<Object>();
					var iEquatable = obj as IEquatable;
					if (iEquatable != null)
						return iEquatable.Equals(rhs.Get<Object>());
				}

#unwarn
				var origType = rhs.VariantType;
				Debug.Assert(type == rhs.VariantType);

#unwarn
				var keyStateListType = typeof(List<KeyState>);

				if (type == typeof(String))
					return Variant.Equals!<String>(lhs, rhs);
				else if (type == typeof(bool))
					return Variant.Equals!<bool>(lhs, rhs);
				else if (type == typeof(Color))
					return Variant.Equals!<Color>(lhs, rhs);
				else if (type.IsNullable)
				{
					return lhs == rhs;
				}
				else if (type == typeof(List<KeyState>))
				{
					let curVal = lhs.Get<List<KeyState>>();
					let origVal = rhs.Get<List<KeyState>>();
					if (curVal.Count != origVal.Count)
					    return false;
					for (int32 i = 0; i < curVal.Count; i++)
					{
					    if (curVal[i] != origVal[i])
					        return false;
					}
					return true;
				}
				else if (lhs.VariantType.IsGenericType)
				{
				    let curVal = lhs.Get<List<String>>();
				    let origVal = rhs.Get<List<String>>();
				    if (curVal.Count != origVal.Count)
				        return false;
				    for (int32 i = 0; i < curVal.Count; i++)
				    {
				        if (curVal[i] != origVal[i])
				            return false;
				    }
				    return true;
				}
				else // Could be an int or enum
					return Variant.Equals!<int32>(lhs, rhs);
			}

            public bool HasChanged()
            {
				return !IsVariantEqual(mCurValue, mOrigValue);
            }

			public void ApplyValue()
			{
				var type = mCurValue.VariantType;
				if (type == typeof(String))
				{
					if (mApplyAction != null)
						mApplyAction();					
					String prevString = mOrigValue.Get<String>();
					String curString = mCurValue.Get<String>();					
					prevString.Set(curString);
				}
				else if (type == typeof(List<KeyState>))
				{					
					let prevList = mOrigValue.Get<List<KeyState>>();
					let curList = mCurValue.Get<List<KeyState>>();

					ClearAndDeleteItems(prevList);
					for (var keyState in curList)
					{
						prevList.Add(keyState.Clone());
					}
				}
				else if (type == typeof(List<String>))
				{					
					List<String> prevList = mOrigValue.Get<List<String>>();
					List<String> curList = mCurValue.Get<List<String>>();

					ClearAndDeleteItems(prevList);					
					for (var str in curList)
					{
						prevList.Add(new String(str));						
					}
				}
				else
				{
					if (mApplyAction != null)
					{
						mApplyAction();
					}
					else
					{
						Debug.Assert(type.IsPrimitive || type.IsEnum || type.IsNullable);
						Debug.Assert(!mCurValue.OwnsMemory); // 'Large' structs not supported
						mOrigValue = mCurValue;
					}					
					if (mFieldInfo != default(FieldInfo))
						mFieldInfo.SetValue(mTarget, mCurValue);
				}				
			}

			/*public void Init()
			{

			}*/
        }

        protected const uint32 cHeaderColor = 0xFFE8E8E8;

		public class CategoryListViewItem : DarkListViewItem
		{
			public int32 mCategoryIdx;

			public override void DrawSelect(Graphics g)
			{
			    using (g.PushColor(mListView.mHasFocus ? 0xFFFFFFFF : 0x80FFFFFF))
			        base.DrawSelect(g);
			}
		}

		public class CategoryListView : DarkListView
		{
			protected override ListViewItem CreateListViewItem()
			{
				return new CategoryListViewItem();
			}

			public override void KeyDown(KeyCode keyCode, bool isRepeat)
			{
				var focusedListViewItem = GetRoot().FindFocusedItem();

				bool changeFocus = (keyCode == .Tab) && (mWidgetWindow.GetKeyFlags() == .None);
				if ((keyCode == .Right) && ((focusedListViewItem == null) || (focusedListViewItem.GetChildCount() == 0)))
					changeFocus = true;

				if (changeFocus)
				{
					var propertiesDialog = (PropertiesDialog)mParent;
					var propListView = propertiesDialog.mPropPage.mPropertiesListView;
					propListView.SetFocus();
					return;
				}

				base.KeyDown(keyCode, isRepeat);
			}
		}


		public class PropListViewItem : DarkListViewItem
		{
			public override void DrawSelect(Graphics g)
			{
				SelfToOtherTranslate(mListView, 0, 0, var parentX, var parentY);
				var width = mListView.mColumns[0].mWidth;

				using (g.PushColor(mListView.mHasFocus ? 0xFFFFFFFF : 0x80FFFFFF))
					using (g.PushColor(0x20FFFFFF))
						g.FillRect(-parentX, 0, width, mHeight);
			}
		}

		public class PropListView : DarkListView
		{

			protected override ListViewItem CreateListViewItem()
			{
				return new PropListViewItem();
			}

			public override void SetFocus()
			{
				base.SetFocus();

				if (GetRoot().FindFocusedItem() == null)
				{
					for (int idx < GetRoot().GetChildCount())
					{
						var item = GetRoot().GetChildAtIndex(idx);
						if (item.mSelfHeight > 0)
						{
							item.Focused = true;
							break;
						}
					}
				}
			}

			public override void KeyDown(KeyCode keyCode, bool isRepeat)
			{
				var propertiesDialog = (PropertiesDialog)mParent;

				let keyFlags = mWidgetWindow.GetKeyFlags();
				bool changeFocus = (keyCode == .Tab) && (keyFlags == .Shift);
				if (keyCode == .Left)
				{
					var selectedItem = GetRoot().FindFocusedItem();
					if ((selectedItem != null) && (selectedItem.mParentItem == GetRoot()))
						changeFocus = true;
				}

				if (changeFocus)
				{
					propertiesDialog.mCategorySelector.SetFocus();
					return;
				}

				base.KeyDown(keyCode, isRepeat);
			}

			public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
			{
				base.MouseDown(x, y, btn, btnCount);
				GetRoot().SelectItemExclusively(null);
			}
		}

        protected Object mCurPropertiesTarget;
		protected Object[] mCurPropertiesTargets ~ delete _;
        protected CategoryListView mCategorySelector;
        protected bool mCancelingEdit;
        protected PropPage mPropPage;
        protected EditWidget mPropEditWidget;
        protected ButtonWidget mApplyButton;
		protected SearchEditWidget mSearchEdit;
		protected DarkButton mResetButton;
        protected PropEntry[] mEditingProps ~ delete _;
        protected ListViewItem mEditingListViewItem;
        protected Widget mPreviousFocus;
		protected List<CategoryListViewItem> mCategoryListViewItems = new List<CategoryListViewItem>() ~ delete _;
		protected bool mHideSelector;

		protected virtual float TopY
		{
			get
			{
				return GS!(32);
			}
		}

        public this()
        {
			//mMinWidth = GS!(320);

            mWindowFlags = .ClientSized | .TopMost | .Caption | .Border | .SysMenu | .Resizable;

            mButtonBottomMargin = GS!(6);
            mButtonRightMargin = GS!(6);
            AddOkCancelButtons(new (evt) => { evt.mCloseDialog = ApplyChanges(); }, null, 0, 1);
            mApplyButton = AddButton("Apply", new (evt) => { evt.mCloseDialog = false; ApplyChanges(); });

            mCategorySelector = new CategoryListView();
            mCategorySelector.mHiliteOffset = GS!(-2);
            mCategorySelector.SetShowHeader(false);
            mCategorySelector.InitScrollbars(false, false);
            mCategorySelector.mAutoFocus = true;

            mCategorySelector.mAllowMultiSelect = false;
            mCategorySelector.mOnKeyDown.Add(new (key) => ChildKeyDown(key.mKeyCode));
            mCategorySelector.mOnFocusChanged.Add(new (listViewItem) => { if (listViewItem.Focused) CategoryChanged(listViewItem); });
			mCategorySelector.mChildIndent = GS!(16);

            AddWidget(mCategorySelector);
			mTabWidgets.Insert(0, mCategorySelector);
        }

		public ~this()
		{
			
		}

		public override void AddedToParent()
		{
			base.AddedToParent();
			mCategorySelector.SetFocus();
		}

		protected override void RehupMinSize()
		{
			mWidgetWindow.SetMinimumSize(GS!(320), GS!(180), true);
		}

		public override bool HandleTab(int dir)
		{
			if ((dir == 1) && (var propListView = mWidgetWindow.mFocusWidget as PropListView))
			{
				var selectedItem = propListView.GetRoot().FindFocusedItem();
				if ((selectedItem != null) && (selectedItem.GetSubItemCount() == 2))
				{
					MouseEvent evt = scope .();
					var valueItem = selectedItem.GetSubItem(1);
					evt.mX = -1;
					evt.mY = -1;
					evt.mSender = valueItem;
					valueItem.mOnMouseDown(evt);
					return true;
				}
			}

			return base.HandleTab(dir);
		}

        protected void CreatePropPage(int32 categoryType, PropPage.Flags flags = .None)
        {
            mPropPage = new PropPage();
            mPropPage.mCategoryType = categoryType;
			mPropPage.mFlags = flags;

            mPropPage.mPropertiesListView = new PropListView();
			mPropPage.mPropertiesListView.mAutoFocus = false;
            mPropPage.mPropertiesListView.mAllowMultiSelect = false;
            mPropPage.mPropertiesListView.mHiliteOffset = GS!(-2);
            var column = mPropPage.mPropertiesListView.AddColumn(GS!(220), "Title");
            column.mMinWidth = GS!(80);
            mPropPage.mPropertiesListView.AddColumn(GS!(180), "Value");
        }

		protected void RemovePropPage()
		{
			if (mPropPage != null)
			{
				mTabWidgets.Remove(mPropPage.mPropertiesListView);
				mPropPage.mPropertiesListView.RemoveSelf();
				mPropPage = null;
			}
		}

        protected virtual void ShowPropPage(int32 categoryTypeInt)
        {
			RemovePropPage();
			MarkDirty();
        }

		protected void UpdateSearch()
		{
			String searchStr = scope String();
			if (mSearchEdit != null)
			{
				mSearchEdit.GetText(searchStr);
				searchStr.Trim();
			}

			var listView = mPropPage.mPropertiesListView;
			float lineSpacing = listView.mFont.GetLineSpacing();
			if (searchStr.IsEmpty)
			{
				listView.GetRoot().WithItems(scope (item) =>
					{
						item.mSelfHeight = lineSpacing;
					});
			}
			else
			{
				listView.GetRoot().WithItems(scope (listViewItem) =>
					{
						listViewItem.mSelfHeight = 0;
					});

				listView.GetRoot().WithItems(scope (listViewItem) =>
					{
						if (listViewItem.GetSubItemCount() < 2)
							return;

						bool found = false;
	
						if (listViewItem.Label.IndexOf(searchStr, true) != -1)
							found = true;

						for (int32 subItemIdx = 1; subItemIdx < listViewItem.GetSubItemCount(); subItemIdx++)
						{
							var valueItem = listViewItem.GetSubItem(subItemIdx);
							if (valueItem.Label.IndexOf(searchStr, true) != -1)
								found = true;
						}
	
						if (found)
						{
							var checkItem = listViewItem;
							while (checkItem != listView.GetRoot())
							{
								checkItem.mSelfHeight = lineSpacing;
								checkItem = checkItem.mParentItem;
							}
						}
						else
							listViewItem.Selected = false;
					});
			}
			listView.Resized();
		}

		public void AddPropPageWidget()
		{
			int defaultButtonIdx = mTabWidgets.IndexOf(mDefaultButton);
			mTabWidgets.Insert(defaultButtonIdx, mPropPage.mPropertiesListView);
			AddWidget(mPropPage.mPropertiesListView);
			UpdateSearch();
		}

        protected void CategoryChanged(ListViewItem listViewItem)
        {
            //String category = listViewItem.mLabel;
            //int32 categoryType = listViewItem.mParentItem.GetIndexOfChild(listViewItem);
            ShowPropPage(((CategoryListViewItem)listViewItem).mCategoryIdx);
        }

        public bool AssertNotCompilingOrRunning()
        {
            var app = IDEApp.sApp;
            if (app.IsCompiling)
            {
                app.Fail("Changes cannot be applied while compiling.");
                return false;
            }
            if (app.mDebugger.mIsRunning)
            {
                app.Fail("Changes cannot be applied while debugging.");
                return false;
            }
            return true;
        }

		protected void ApplyChanges(PropPage propPage, ref bool hadChanges)
		{
			hadChanges = false;

			for (var propEntries in propPage.mPropEntries.Values)
			{
				for (var propEntry in propEntries)
				{
			        if (propEntry.HasChanged())
			        {
			            hadChanges = true;
						propEntry.ApplyValue();
			        }
				}
				if (propPage == mPropPage)
					UpdatePropertyValue(propEntries);
			}

			propPage.mHasChanges = false;
		}

        protected virtual bool ApplyChanges()
        {
            return true;
        }

        protected void ChildKeyDown(KeyCode keyCode)
        {
            if (keyCode == KeyCode.Escape)
            {
                KeyDown(keyCode, false);
            }
            else if (keyCode == KeyCode.Return)
            {
                mDefaultButton.SetFocus();
                mDefaultButton.KeyDown(keyCode, false);
            }
        }

		protected virtual void ResetSettings()
		{

		}

        public override void ResizeComponents()
        {
            base.ResizeComponents();

            float topY = TopY;
			float propTopY = topY;
            //var font = DarkTheme.sDarkTheme.mSmallFont;

			bool hasTop = false;
			float rightX = mWidth - GS!(12);

			float selectorWidth = GS!(140);

			if (mHideSelector)
				selectorWidth = 0;

			// Reset Settings
			if (mPropPage.mFlags.HasFlag(.AllowReset))
			{
				if (mResetButton == null)
				{
					mResetButton = new DarkButton();
					mResetButton.Label = "Reset Settings";
					mResetButton.mOnMouseClick.Add(new (dlg) =>
						{
							ResetSettings();
						});
					AddWidget(mResetButton);
					mTabWidgets.Insert(1, mResetButton);
				}
				float btnWidth = DarkTheme.sDarkTheme.mSmallFont.GetWidth(mResetButton.Label) + GS!(24);
				mResetButton.Resize(mWidth - btnWidth - GS!(6), topY, btnWidth, GS!(22));
				hasTop = true;
				rightX = Math.Max(mResetButton.mX - GS!(6), 0);
			}
			else if (mResetButton != null)
			{
				mTabWidgets.Remove(mResetButton);
				mResetButton.RemoveSelf();
				DeleteAndNullify!(mResetButton);
			}

			// Search edit
			if (mPropPage.mFlags.HasFlag(.AllowSearch))
			{
				if (mSearchEdit == null)
				{
					mSearchEdit = new SearchEditWidget();
					AddWidget(mSearchEdit);
					mTabWidgets.Insert(1, mSearchEdit);
				}
				float x = selectorWidth + GS!(12);
				if (selectorWidth == 0)
					x = GS!(6);

				mSearchEdit.Resize(x, topY, Math.Max(rightX - x, 0), GS!(22));
				hasTop = true;
			}
			else if (mSearchEdit != null)
			{
				mTabWidgets.Remove(mSearchEdit);
				mSearchEdit.RemoveSelf();
				DeleteAndNullify!(mSearchEdit);
				UpdateSearch();
			}


			if (hasTop)
			{
				propTopY += GS!(26);
			}

			float catRight;
			if (selectorWidth > 0)
			{
				catRight = mCategorySelector.mX + mCategorySelector.mWidth;
				mCategorySelector.SetVisible(true);
				mCategorySelector.Resize(GS!(6), topY, selectorWidth, Math.Max(mHeight - topY - GS!(32), 0));
			}
			else
			{
				catRight = 0;
				mCategorySelector.SetVisible(false);
			}

            mPropPage.mPropertiesListView.Resize(catRight + GS!(6), propTopY, Math.Max(mWidth - catRight - GS!(12), 0), Math.Max(mHeight - propTopY - GS!(32), 0));

            if (mPropEditWidget != null)
            {
				DarkListViewItem editItem = (DarkListViewItem)mEditingListViewItem.GetSubItem(1);
				let propEntry = mEditingProps[0];

				float xPos;
				float yPos;
				editItem.SelfToOtherTranslate(editItem.mListView, 0, -1, out xPos, out yPos);
				Rect rect = .(xPos, yPos, GetValueEditWidth(editItem), GS!(20));
				propEntry.mEditInsets?.ApplyTo(ref rect);

				mPropEditWidget.Resize(rect.Left, rect.Top, rect.Width, rect.mHeight);

                //mPropEditWidget.Resize(mPropEditWidget.mX, mPropEditWidget.mY, GetValueEditWidth(mEditingListViewItem.GetSubItem(1)), mPropEditWidget.mHeight);
                if (mPropEditWidget.mChildWidgets != null)
                {
                    for (var childWidget in mPropEditWidget.mChildWidgets)
                        childWidget.mX = mPropEditWidget.mWidth - childWidget.mWidth;
                }
            }
        }

        void CategoryValueClicked(MouseEvent theEvent)
        {
            DarkListViewItem clickedItem = (DarkListViewItem)theEvent.mSender;
            mCategorySelector.GetRoot().SelectItemExclusively(clickedItem);
            mCategorySelector.SetFocus();
        }

        protected DarkListViewItem AddCategoryItem(DarkListViewItem parent, String name)
        {
            var item = (CategoryListViewItem)parent.CreateChildItem();
            item.Label = name;
            item.mFocusColor = 0xFFA0A0A0;
            item.mOnMouseDown.Add(new => CategoryValueClicked);
			item.mCategoryIdx = (int32)mCategoryListViewItems.Count;
			mCategoryListViewItems.Add(item);
            return item;
        }

        protected void HandleEditLostFocus(Widget widget)
        {
			Debug.Assert(widget == mPropEditWidget);

            EditWidget editWidget = (EditWidget)widget;
            editWidget.mOnLostFocus.Remove(scope => HandleEditLostFocus, true);
            editWidget.mOnSubmit.Remove(scope => HandleSubmit, true);
            editWidget.mOnCancel.Remove(scope => HandleCancel, true);
			mWidgetWindow.mOnMouseWheel.Remove(scope => HandleMouseWheel, true);
			mWidgetWindow.mOnWindowMoved.Remove(scope => HandleWindowMoved, true);

            String newValue = scope String();
            editWidget.GetText(newValue);
            newValue.Trim();

            DarkListViewItem item = (DarkListViewItem)mEditingListViewItem;
            //DarkListViewItem valueItem = (DarkListViewItem)item.GetSubItem(1);

			if (!editWidget.mEditWidgetContent.HasUndoData())
				mCancelingEdit = true;

            if (!mCancelingEdit)
            {
				var editingProp = mEditingProps[0];
				bool multiCopyToOthers = false;

				bool handled = false;
				var curVariantType = editingProp.mCurValue.VariantType;
				if (curVariantType.IsNullable)
				{
					let elementType = ((SpecializedGenericType)curVariantType).GetGenericArg(0);
					if (elementType.[Friend]mTypeCode == .Int32)
					{
						handled = true;
						editingProp.mCurValue.[Friend]mData = 0;
						int32? value = null;
						if ((newValue.Length != 0) && (!String.Equals(newValue, "Not Set", .OrdinalIgnoreCase)))
						{
							if (int32.Parse(newValue) case .Ok(let intVal))
							{
								value = intVal;
							}
							else
							{
								let error = scope String();
								error.AppendF("Invalid number: {0}", newValue);
								gApp.Fail(error);
							}
						}

						*((bool*)&editingProp.mCurValue.[Friend]mData + elementType.[Friend]mSize) = value.HasValue;
						if (value.HasValue)
						{
							void* valueData = editingProp.mCurValue.GetValueData();
							Internal.MemCpy(valueData, &value.[Friend]mValue, elementType.[Friend]mSize);
						}
						multiCopyToOthers = true;
					}
				}

				if (handled)
				{
					//
				}
                else if (editingProp.mListViewItem != item)
                {
                    List<String> curEntries = editingProp.mCurValue.Get<List<String>>();
                    List<String> entries = new List<String>(curEntries.GetEnumerator());

                    for (int32 childIdx = 0; childIdx < editingProp.mListViewItem.GetChildCount(); childIdx++)
                    {
                        if (item == editingProp.mListViewItem.GetChildAtIndex(childIdx))
                        {
                            if (childIdx >= entries.Count)
                                entries.Add(new String(newValue));
                            else
                                entries[childIdx].Set(newValue);
                        }
                    }
					curEntries.Clear();

                    for (int32 i = 0; i < entries.Count; i++)
                    {
                        entries[i].Trim();
                        if (entries[i] == "")
                        {
							delete entries[i];
                            entries.RemoveAt(i);
                            i--;
                        }
                    }
					
					PropEntry.DisposeVariant(ref editingProp.mCurValue);
                    editingProp.mCurValue = Variant.Create(entries, true);
					multiCopyToOthers = true;
                }
                else
                {
					var prevValue = editingProp.mCurValue;
					bool setValue = true;
					
					if (curVariantType == typeof(List<KeyState>))
					{
						if (!newValue.IsEmpty && !newValue.StartsWith("<"))
						{
							let entries = new List<KeyState>();
							KeyState.Parse(newValue, entries);
							editingProp.mCurValue = Variant.Create(entries, true);
						}
						else
							editingProp.mCurValue = DuplicateVariant(editingProp.mOrigValue);
					}
                    else if (curVariantType == typeof(List<String>))
                    {
                        let entryViews = scope List<StringView>(newValue.Split(';'));
						let entries = new List<String>();

                        for (int32 i = 0; i < entryViews.Count; i++)
                        {
							String entry = scope String(entryViews[i]);
							entry.Trim();
                            if (entry.Length > 0)
                                entries.Add(new String(entry));
                        }
                        editingProp.mCurValue = Variant.Create(entries, true);
                    }
                    else if (curVariantType.[Friend]mTypeCode == .Int32)
					{
						if (int32.Parse(newValue) case .Ok(let intVal))
						{
							editingProp.mCurValue = Variant.Create(intVal);
						}
						else
						{
							let error = scope String();
							error.AppendF("Invalid number: {0}", newValue);
							gApp.Fail(error);
						}
					}
					else if (curVariantType == typeof(Color))
					{
						if (int64.Parse(newValue, .HexNumber) case .Ok(let intVal))
						{
							editingProp.mCurValue = Variant.Create((Color)((uint32)intVal | 0xFF000000));
						}
						else
						{
							let error = scope String();
							error.AppendF("Invalid number: {0}", newValue);
							gApp.Fail(error);
							setValue = false;
						}
					}
					else if (curVariantType.[Friend]mTypeCode == .Float)
					{
						if (float.Parse(newValue) case .Ok(let floatVal))
						{
							editingProp.mCurValue = Variant.Create(floatVal);
						}
						else
						{
							let error = scope String();
							error.AppendF("Invalid number: {0}", newValue);
							gApp.Fail(error);
							setValue = false;
						}
					}
					else
                        editingProp.mCurValue = Variant.Create(new String(newValue), true);

					if (setValue)
					{
						PropEntry.DisposeVariant(ref prevValue);
						multiCopyToOthers = true;
					}
                }

				if (multiCopyToOthers)
				{
					for (int propIdx = 1; propIdx < mEditingProps.Count; propIdx++)
					{
						var otherProp = mEditingProps[propIdx];
						PropEntry.DisposeVariant(ref otherProp.mCurValue);
						otherProp.mCurValue = DuplicateVariant(editingProp.mCurValue);
					}
				}

                UpdatePropertyValue(mEditingProps);
            }

            editWidget.RemoveSelf();
			gApp.DeferDelete(editWidget);
            mPropEditWidget = null;
			DeleteAndNullify!(mEditingProps);
            mEditingListViewItem = null;

            if ((mPreviousFocus != null) && (mPreviousFocus.mWidgetWindow != null))
                mPreviousFocus.SetFocus();
            mPreviousFocus = null;

            if (mWidgetWindow.mFocusWidget == null)
                mDefaultButton.SetFocus();
            CheckForChanges();
        }

		protected virtual bool HasChanges()
		{
			return false;
		}

        protected void CheckForChanges()
        {
            bool hasChanges = HasChanges();
            for (var propEntries in mPropPage.mPropEntries.Values)
            {
				for (var propEntry in propEntries)
	                if (propEntry.HasChanged())
	                    hasChanges = true;

				var propEntry = propEntries[0];
                if (propEntry.mComboBoxes != null)
                {
                    for (var comboBox in propEntry.mComboBoxes)
                    {
                        if (comboBox.mCurMenuWidget != null)
                            comboBox.mBkgColor = 0x20FFFFFF;
                        else
                            comboBox.mBkgColor = 0;
                    }
                }
            }

            mPropPage.mHasChanges = hasChanges;
        }

		protected void CheckForChanges(PropPage[] propPages)
		{
		    CheckForChanges();
		    bool hasChanges = false;

		    for (int32 pageIdx = 0; pageIdx < propPages.Count; pageIdx++)
		    {
		        var propPage = propPages[pageIdx];
				bool isCurrent = propPage == mPropPage;
		        if (propPage != null)
		        {
		            var categoryItem = mCategoryListViewItems[pageIdx];
		            if (isCurrent)
		                categoryItem.mIsBold = propPage.mHasChanges;
		            hasChanges |= propPage.mHasChanges;
		        }
		    }

		    if (hasChanges)
		        mApplyButton.mDisabled = false;
		    else
		        mApplyButton.mDisabled = true;
		}

		protected void ChangeProperty(PropEntry[] propEntries, Variant value)
		{
			for (var propEntry in propEntries)
			{
				let curVariantType = propEntry.mCurValue.VariantType;
				if (propEntry.mCurValue.VariantType.IsNullable)
				{
					let genericType = (SpecializedGenericType)curVariantType;
					let elementType = genericType.GetGenericArg(0);

					propEntry.mCurValue.[Friend]mData = 0;
					*((bool*)&propEntry.mCurValue.[Friend]mData + elementType.[Friend]mSize) = value.HasValue;
					if (value.HasValue)
					{
						void* valueData = propEntry.mCurValue.GetValueData();
						var copyValue = value;
						Internal.MemCpy(valueData, copyValue.GetValueData(), elementType.[Friend]mSize);
					}
				}
				else
				{
					PropEntry.DisposeVariant(ref propEntry.mCurValue);
					propEntry.mCurValue = value;
				}
			}	
			UpdatePropertyValue(propEntries);
		}

        void HandleCancel(EditEvent theEvent)
        {
            mCancelingEdit = true;
            HandleEditLostFocus((EditWidget)theEvent.mSender);
        }

        protected void HandleSubmit(EditEvent theEvent)
        {
            HandleEditLostFocus((EditWidget)theEvent.mSender);
        }

        protected void MoveEditingItem(int32 subValueIdx, int32 dir)
        {
            ListViewItem item = mEditingListViewItem;
			var itemParent = item.mParentItem;
            String editText = scope String();
            mPropEditWidget.GetText(editText);
            PropEntry propEntry = mEditingProps[0];

			var propEntries = new PropEntry[mEditingProps.Count];
			defer delete propEntries;
			mEditingProps.CopyTo(propEntries);

			var curStringList = propEntry.mCurValue.Get<List<String>>();
            List<String> stringList = new List<String>();
			for (var str in curStringList)
			{
				stringList.Add(new String(str));
			}

            mCancelingEdit = true;
            HandleEditLostFocus(mPropEditWidget);

            if (subValueIdx >= stringList.Count)
                stringList.Add(editText);

            String swap = stringList[subValueIdx];
            stringList[subValueIdx] = stringList[subValueIdx + dir];
            stringList[subValueIdx + dir] = swap;
			PropEntry.DisposeVariant(ref propEntry.mCurValue);
            propEntry.mCurValue = Variant.Create(stringList, true);
			for (int otherIdx = 1; otherIdx < propEntries.Count; otherIdx++)
			{
				var otherProps = propEntries[otherIdx];
				PropEntry.DisposeVariant(ref otherProps.mCurValue);
				otherProps.mCurValue = DuplicateVariant(propEntry.mCurValue);
			}
            UpdatePropertyValue(propEntries);

            ListViewItem newItem = itemParent.GetChildAtIndex(subValueIdx + dir);
            EditValue(newItem, propEntries, subValueIdx + dir);
            mPropEditWidget.SetText(editText);
        }

        protected void EditValue(ListViewItem item, PropEntry[] propEntries, int32 subValueIdx = -1)
        {
            DarkListViewItem editItem = (DarkListViewItem)item.GetSubItem(1);

			mPreviousFocus = null;
			if ((mWidgetWindow.mFocusWidget == mPropPage.mPropertiesListView) && (mPropPage.mPropertiesListView.GetRoot().FindFocusedItem() != null))
				mPreviousFocus = mWidgetWindow.mFocusWidget;
            
            mCancelingEdit = false;

			var propEntry = propEntries[0];

            var valueItem = propEntry.mListViewItem.GetSubItem(1);
			let type = propEntry.mCurValue.VariantType;

            DarkEditWidget editWidget;
			if (propEntry.mIsTypeWildcard)
				editWidget = new TypeWildcardEditWidget();
			else if (propEntry.mRelPath != null)
			{
				var pathEditWidget = new PathEditWidget();
				pathEditWidget.mRelPath = new String(propEntry.mRelPath);
				editWidget = pathEditWidget;
			}
			else if (type == typeof(List<KeyState>))
			{
				editWidget = new KeysEditWidget();
				editWidget.Content.mIsReadOnly = true;
			}
			else
				editWidget = new DarkEditWidget();
			editWidget.mScrollContentInsets.Set(GS!(3), GS!(3), GS!(1), GS!(3));
			editWidget.Content.mTextInsets.Set(GS!(-3), GS!(2), 0, GS!(2));
			//editWidget.RehupSize();
            if (subValueIdx != -1)
            {
                List<String> stringList = propEntry.mCurValue.Get<List<String>>();
                if (subValueIdx < stringList.Count)
                    editWidget.SetText(stringList[subValueIdx]);

                MoveItemWidget moveItemWidget;
                if (subValueIdx > 0)
                {
                    moveItemWidget = new MoveItemWidget();
                    editWidget.AddWidget(moveItemWidget);
                    moveItemWidget.Resize(6, editWidget.mY - GS!(16), GS!(20), GS!(20));
                    moveItemWidget.mArrowDir = -1;
                    moveItemWidget.mOnMouseDown.Add(new (evt) => { MoveEditingItem(subValueIdx, -1); });
                    editWidget.mOnKeyDown.Add(new (evt) => { if (evt.mKeyCode == KeyCode.Up) MoveEditingItem(subValueIdx, -1); });
                }

                if (subValueIdx < stringList.Count - 1)
                {
                    moveItemWidget = new MoveItemWidget();
                    editWidget.AddWidget(moveItemWidget);
                    moveItemWidget.Resize(6, editWidget.mY + GS!(16), GS!(20), GS!(20));
                    moveItemWidget.mArrowDir = 1;
                    moveItemWidget.mOnMouseDown.Add(new (evt) => { MoveEditingItem(subValueIdx, 1); });
                    editWidget.mOnKeyDown.Add(new (evt) => { if (evt.mKeyCode == KeyCode.Down) MoveEditingItem(subValueIdx, 1); });
                }
            }
            else
            {
				String label = valueItem.mLabel;
				if ((propEntry.mNotSetString != null) && (label == propEntry.mNotSetString))
					label = "";
				/*var curVariantType = mEditingProp.mCurValue.VariantType;
				if (curVariantType.IsNullable)
				{
					if (label == "Not Set")
						label = "";
				}*/
				if (editWidget is KeysEditWidget)
					editWidget.SetText("< Press Key >");
				else
					editWidget.SetText(label);
			}

            editWidget.Content.SelectAll();

            editItem.mListView.AddWidget(editWidget);
            
            editWidget.mOnLostFocus.Add(new => HandleEditLostFocus);
            editWidget.mOnSubmit.Add(new => HandleSubmit);
            editWidget.mOnCancel.Add(new => HandleCancel);
            editWidget.SetFocus();
			mWidgetWindow.mOnMouseWheel.Add(new => HandleMouseWheel);
			mWidgetWindow.mOnWindowMoved.Add(new => HandleWindowMoved);

			editWidget.mEditWidgetContent.ClearUndoData();

            mEditingListViewItem = item;
            mPropEditWidget = editWidget;

			mEditingProps = new PropEntry[propEntries.Count];
			for (int i < propEntries.Count)
				mEditingProps[i] = propEntries[i];

            ResizeComponents();
        }

		void HandleMouseWheel(MouseEvent evt)
		{
		    mCancelingEdit = true;
			HandleEditLostFocus(mPropEditWidget);
		}

		void HandleWindowMoved(BFWindow window)
		{
			mCancelingEdit = true;
			HandleEditLostFocus(mPropEditWidget);
		}

        protected virtual void UpdatePropertyValue(PropEntry[] propEntries)
        {
			var propEntry = propEntries[0];
			if (propEntry.mListViewItem == null)
				return;
			bool areDifferent = false;
			for (int checkIdx = 1; checkIdx < propEntries.Count; checkIdx++)
			{
				var checkProp = propEntries[checkIdx];
				if (!PropEntry.IsVariantEqual(checkProp.mCurValue, propEntry.mCurValue))
					areDifferent = true;
			}

			bool hasChanged = false;
			for (var checkPropEntry in propEntries)
			{
				if (checkPropEntry.HasChanged())
					hasChanged = true;
			}

            if (propEntry.mFieldInfo == default(FieldInfo))
                return;

			var curVariantType = propEntry.mCurValue.VariantType;

			bool handled = false;
			if (propEntry.mOnUpdate.HasListeners)
			{
				handled |= propEntry.mOnUpdate();
			}
			bool isNotSet = false;
            var valueItem = (DarkListViewItem)propEntry.mListViewItem.GetSubItem(1);
			if (curVariantType.IsNullable)
			{
				propEntry.mNotSetString = "Not Set";

				let genericType = (SpecializedGenericType)curVariantType;
				let elementType = genericType.GetGenericArg(0);

				bool hasValue = *((bool*)&propEntry.mCurValue.[Friend]mData + elementType.[Friend]mSize);
				curVariantType = elementType;
				if (!hasValue)
				{
					valueItem.Label = "Not Set";
					handled = true;
					isNotSet = true;
				}
			}

			if (handled)
			{
				if (curVariantType == typeof(String))
				{
					var str = propEntry.mCurValue.Get<String>();
					if ((propEntry.mNotSetString != null) &&
						((propEntry.mNotSetString == str) || (str.IsEmpty)))
					{
						isNotSet = true;
						str = propEntry.mNotSetString;
					}
					valueItem.Label = str;
				}
			}
            else if (propEntry.mOptionValues != null)
            {
                int32 idx = 0;
                if (curVariantType == typeof(bool))
				{
                    if (propEntry.mCurValue.Get<bool>())
                        idx = 1;
				}
				else if (curVariantType.IsEnum)
    			{
					//idx = propEntry.mCurValue.Get<int32>();
					int64 val = 0;
					Internal.MemCpy(&val, propEntry.mCurValue.GetValueData(), curVariantType.Size);
					idx = (int32)val;
				}
                valueItem.Label = propEntry.mOptionValues[idx];
            }
			else if (curVariantType == typeof(List<KeyState>))
			{
				var keyList = propEntry.mCurValue.Get<List<KeyState>>();

				var str = scope String();
				KeyState.ToString(keyList, str);
				valueItem.Label = str;
			}
            else if (curVariantType.IsGenericType)
            {
                String allValues = scope String();
                List<String> strVals;
				if (areDifferent)
					strVals = scope:: .();
				else
					strVals = propEntry.mCurValue.Get<List<String>>();
                for (int32 i = 0; i <= strVals.Count; i++)
                {
                    String curValue = "<Add New...>";
					if (i < strVals.Count)
                    {
                        if (i > 0)
                            allValues.Append(";");
                        curValue = strVals[i];
                        allValues.Append(curValue);
                    }

                    DarkListViewItem childItem;
                    DarkListViewItem childSubItem;
                    if (i >= propEntry.mListViewItem.GetChildCount())
                    {
                        childItem = (DarkListViewItem)propEntry.mListViewItem.CreateChildItem();
                        childSubItem = (DarkListViewItem)childItem.CreateSubItem(1);
                        int32 curIdx = i;

						PropEntry[] propEntriesCopy = new PropEntry[propEntries.Count];
						propEntries.CopyTo(propEntriesCopy);

                        childSubItem.mOnMouseDown.Add(new (evt) =>
							{
								if (areDifferent)
								{
									var strList = propEntry.mCurValue.Get<List<String>>();
									ClearAndDeleteItems(strList);
								}
								ArrayPropValueClicked(propEntriesCopy, curIdx);
							}
							~
							{
								delete propEntriesCopy;
							});
                    }
                    else
                    {
                        childItem = (DarkListViewItem)propEntry.mListViewItem.GetChildAtIndex(i);
                        childSubItem = (DarkListViewItem)childItem.GetSubItem(1);
                    }
                    if (i < strVals.Count)
                    {
                        childItem.Label = StackStringFormat!("#{0}", i + 1);
                        childSubItem.mTextColor = Color.White;
                    }
                    else
                    {
                        childItem.Label = "";
                        childSubItem.mTextColor = 0xFFC0C0C0;
                    }
                    childSubItem.Label = curValue;
                }

                while (propEntry.mListViewItem.GetChildCount() > strVals.Count + 1)
                    propEntry.mListViewItem.RemoveChildItem(propEntry.mListViewItem.GetChildAtIndex(propEntry.mListViewItem.GetChildCount() - 1));

                valueItem.Label = allValues;
            }
            else if (propEntry.mCheckBox != null)
            {
                propEntry.mCheckBox.Checked = propEntry.mCurValue.Get<bool>();
            }
            else if (curVariantType.IsEnum)
            {
				var enumStr = scope String();

				Debug.Assert(curVariantType.Size <= 8);
				int64 val = 0;
				Internal.MemCpy(&val, propEntry.mCurValue.GetValueData(), curVariantType.Size);
				Enum.EnumToString(curVariantType, enumStr, val);
				GetEnumDisp(enumStr);
                valueItem.Label = enumStr;
            }
            else if (curVariantType == typeof(String))
			{
				valueItem.Label = propEntry.mCurValue.Get<String>();
			}
			else if (curVariantType == typeof(int32))
			{
				let valStr = scope String();
				int32 intVal = propEntry.mCurValue.Get<int32>();
				intVal.ToString(valStr);
				valueItem.Label = valStr;
			}
			else if (curVariantType == typeof(Color))
			{
				let valStr = scope String();
				int32 intVal = propEntry.mCurValue.Get<int32>();
				(intVal & 0x00FFFFFF).ToString(valStr, "X6", null);
				valueItem.Label = valStr;
			}
			else if (curVariantType == typeof(float))
			{
				let valStr = scope String();
				float floatVal = propEntry.mCurValue.Get<float>();
				floatVal.ToString(valStr);
				valueItem.Label = valStr;
			}
			else
				ThrowUnimplemented();
                //valueItem.Label = ToStackString!(propEntry.mCurValue);

			if (hasChanged)
                valueItem.mIsBold = true;
            else
                valueItem.mIsBold = false;

			if (areDifferent)
			{
				valueItem.Label = "<Multiple Values>";
				valueItem.mTextColor = 0xFFC0C0C0;
			}
			else if (propEntry.mColorOverride.HasValue)
				valueItem.mTextColor = propEntry.mColorOverride.Value;
			else if (isNotSet)
				valueItem.mTextColor = 0xFFC0C0C0;
			else
				valueItem.mTextColor = 0xFFFFFFFF;
        }

        void GetEnumDisp(String enumDisp)
        {
            int32 firstUpperChar = -1;            

            for (int32 i = 0; i < enumDisp.Length; i++)
            {
                char8 c = enumDisp[i];
                if (c.IsUpper)
                {                    
                    if (firstUpperChar == -1)
                        firstUpperChar = i;
                }
                
                if (c.IsLower)
                {
                    if (firstUpperChar > 0)
                    {
						enumDisp.Insert(firstUpperChar, " ");
                        i++;
                    }

                    firstUpperChar = -1;                    
                }
            }

			if (firstUpperChar > 0)
			{
				enumDisp.Insert(firstUpperChar, " ");
			}
        }

        protected void PopulateComboBox(Menu menu, PropEntry[] propEntries)
        {
			var propEntry = propEntries[0];
            var valueType = propEntry.mCurValue.VariantType;

			if (valueType.IsNullable)
			{
				let genericType = (SpecializedGenericType)valueType;
				let elementType = genericType.GetGenericArg(0);

				var menuItem = menu.AddItem("Not Set");
				Variant newVariant = Variant();
				menuItem.mOnMenuItemSelected.Add(new (evt) => { ChangeProperty(propEntries, newVariant); });
				valueType = elementType;
			}

            if (propEntry.mOptionValues != null)
            {
                for (int32 i = 0; i < propEntry.mOptionValues.Count; i++)
                {
                    var menuItem = menu.AddItem(propEntry.mOptionValues[i]);

                    Variant newVariant = Variant();
                    if (valueType == typeof(bool))
						newVariant = Variant.Create(i != 0);
					else if (valueType.IsEnum)
					{
						newVariant = Variant.Create(valueType, &i);
					}
                        //newVariant = Variant.Create(!propEntry.mCurValue.Get<bool>());
                    menuItem.mOnMenuItemSelected.Add(new (evt) => { ChangeProperty(propEntries, newVariant); });
                }
            }
            else
            {
                //Array enumValues = ;
                for (var enumValue in ((TypeInstance)valueType).GetFields())
                {
					var enumStr = scope String();
					enumStr.Append(enumValue.Name);
					GetEnumDisp(enumStr);
                    var menuItem = menu.AddItem(enumStr);
					Variant newVariant = enumValue.GetValue();
                    menuItem.mOnMenuItemSelected.Add(new (evt) => { ChangeProperty(propEntries, newVariant); });
                }
            }
        }

        protected float GetValueEditWidth(ListViewItem subItem)
        {
            float width = subItem.mWidth;
            float transX;
            float transY;
            subItem.SelfToOtherTranslate(subItem.mListView, 0, 0, out transX, out transY);
            width = Math.Max(subItem.mListView.mWidth - transX - GS!(18), GS!(80));
            return width;
        }

		static Variant DuplicateVariant(Variant prevVariant)
		{
			if (!prevVariant.HasValue)
				return Variant();

			var type = prevVariant.VariantType;
			if (prevVariant.IsObject)
			{
				if (type == typeof(List<String>))
				{					
					var newStrList = new List<String>();
					for (var str in prevVariant.Get<List<String>>())
						newStrList.Add(new String(str));
					return Variant.Create(newStrList, true);
				}
				if (type == typeof(List<KeyState>))
				{					
					var newList = new List<KeyState>();
					for (var keyState in prevVariant.Get<List<KeyState>>())
						newList.Add(keyState.Clone());
					return Variant.Create(newList, true);
				}
				if (type == typeof(String))
				{
					var str = new String(prevVariant.Get<String>());
					return Variant.Create(str, true);
				}
			}
			else
			{
				//Variant.Create();

				var data = prevVariant.[Friend]mData;
				return Variant.Create(prevVariant.VariantType, &data);

				/*if (type == typeof(bool))
					return Variant.Create(prevVariant.Get<bool>());
				if (type == typeof(int32))
					return Variant.Create(prevVariant.Get<int32>());
				if (type.IsEnum)
					return Variant.Create(prevVariant.VariantType, prevVariant.Get<int32>());*/
			}

			ThrowUnimplemented();
		}

		protected void UpdateFromTarget(Dictionary<Object, Object> targetDict)
		{
			for (let propKV in mPropPage.mPropEntries)
			{
#unwarn
				var listViewItem = propKV.key;
				var propEntries = propKV.value;

				for (var propEntry in propEntries)
				{
					Object target;
					if (!targetDict.TryGetValue(propEntry.mTarget, out target))
					{
						//Debug.FatalError();
						continue;
					}

					String usePropName = scope:: String(propEntry.mPropertyName);
					
					while (true)
					{
					    int dotIdx = usePropName.IndexOf('.');
					    if (dotIdx == -1)
					        break;

					    String newTargetName = scope String(usePropName, 0, (int)dotIdx);
					    usePropName.Remove(0, dotIdx + 1);

						int idx = -1;
						if (newTargetName.EndsWith("]"))
						{
							int bracketPos = newTargetName.IndexOf("[");
							String idxStr = scope String(newTargetName, bracketPos + 1, (int)newTargetName.Length - bracketPos - 2);
							idx = int32.Parse(idxStr).GetValueOrDefault();
							newTargetName.RemoveToEnd(bracketPos);
						}

						Debug.Assert(target != null);
					    var targetFieldInfo = target.GetType().GetField(newTargetName);
					    targetFieldInfo.Value.GetValue(target, out target);

						if (idx != -1)
						{
							if (var iList = target as IList)
							{
								var vari = iList[idx];
								Debug.Assert(vari.IsObject);
								target = vari.Get<Object>();
							}
						}

					    Debug.Assert(target != null);
					}

					var fieldInfo = target.GetType().GetField(usePropName).Value;
					var curValue = fieldInfo.GetValue(target).GetValueOrDefault();
					PropEntry.DisposeVariant(ref propEntry.mCurValue);
					propEntry.mCurValue = DuplicateVariant(curValue);
				}
				UpdatePropertyValue(propEntries);
			}
		}

		void BrowseForFile(String outPath)
		{
#if !CLI		
			var fileDialog = scope OpenFileDialog();
			fileDialog.Title = "Select File";
			fileDialog.SetFilter("All files (*.*)|*.*");

			if (!outPath.IsWhiteSpace)
			{
				String initialDir = scope .();
				Path.GetDirectoryPath(outPath, initialDir);
				fileDialog.InitialDirectory = initialDir;
			}

			if (fileDialog.ShowDialog(mWidgetWindow).GetValueOrDefault() == .OK)
			{
				for (String openFileName in fileDialog.FileNames)
				{
					outPath.Set(openFileName);
					return;
				}
			}
#endif			
		}

		void BrowseForFolder(String outPath)
		{
#if !CLI		
			var folderDialog = scope FolderBrowserDialog();
			if (!outPath.IsWhiteSpace)
			{
				folderDialog.SelectedPath = outPath;
			}
			if (folderDialog.ShowDialog(mWidgetWindow).GetValueOrDefault() == .OK)
			{
				outPath.Set(folderDialog.SelectedPath);
			}
#endif			
		}

		protected PropEntry SetupPropertiesItem(DarkListViewItem item, String name, Object propName = null, String[] optionValues = null, PropEntry.Flags flags = .None)
		{
			if ((propName == null) || (propName == ""))
				return null;

			DarkListViewItem subItem = null;
			if (item.GetSubItemCount() >= 2)
				subItem = (DarkListViewItem)item.GetSubItem(1);
			else
		    	subItem = (DarkListViewItem)item.CreateSubItem(1);

			if (mCurPropertiesTargets == null)
			{
				mCurPropertiesTargets = scope:: Object[1];
				mCurPropertiesTargets[0] = mCurPropertiesTarget;
				defer:: { mCurPropertiesTargets = null; }
			}
			//
			//var comboBox = new DarkComboBox();
			//Action act = new () => { comboBox.Resize(0, 0, GetValueEditWidth(subItem), subItem.mHeight + 1); };

			//

			PropEntry[] propEntries = new PropEntry[mCurPropertiesTargets.Count];
			for (int propIdx < (int)propEntries.Count)
			PropBlock:
			{
				Object target = mCurPropertiesTargets[propIdx];

				String usePropName = propName as String;
				if (usePropName != null)
				{
					usePropName = scope:PropBlock String(usePropName);
				}
				else if (var propNames = propName as String[])
				{
					usePropName = scope:PropBlock String(propNames[propIdx]);
				}

				String[] useOptionValues = optionValues;

			    while (true)
			    {
			        int dotIdx = usePropName.IndexOf('.');
			        if (dotIdx == -1)
			            break;

			        String newTargetName = scope String(usePropName, 0, (int)dotIdx);
			        usePropName.Remove(0, dotIdx + 1);

					int idx = -1;
					if (newTargetName.EndsWith("]"))
					{
						int bracketPos = newTargetName.IndexOf("[");
						String idxStr = scope String(newTargetName, bracketPos + 1, (int)newTargetName.Length - bracketPos - 2);
						idx = int32.Parse(idxStr).GetValueOrDefault();
						newTargetName.RemoveToEnd(bracketPos);
					}

					Debug.Assert(target != null);
			        var targetFieldInfo = target.GetType().GetField(newTargetName);
			        targetFieldInfo.Value.GetValue(target, out target);

					if (idx != -1)
					{
						if (var iList = target as IList)
						{
							var vari = iList[idx];
							Debug.Assert(vari.IsObject);
							target = vari.Get<Object>();
						}
					}

			        Debug.Assert(target != null);
			    }

			    PropEntry propEntry = new PropEntry();
				propEntry.mFlags = flags;
			    propEntry.mPropertyName = new String(usePropName);
			    propEntry.mTarget = target;
			    propEntry.mListViewItem = item;

				propEntries[propIdx] = propEntry;

				//TODO:
			    var fieldInfo = target.GetType().GetField(usePropName).Value;
			    propEntry.mFieldInfo = fieldInfo;

				var curValue = fieldInfo.GetValue(target).GetValueOrDefault();
			    propEntry.mOrigValue = curValue;
			    propEntry.mCurValue = DuplicateVariant(curValue);
				//TODO: Duplicate optionValues
				
				if (useOptionValues != null)
				{
					// Duplicate onto heap
					var prevOptionValues = useOptionValues;
					useOptionValues = new String[useOptionValues.Count];
					for (int32 i = 0; i < prevOptionValues.Count; i++)
						useOptionValues[i] = new String(prevOptionValues[i]);
				}

			    propEntry.mOptionValues = useOptionValues;
				bool handledMouseDown = false;

				var fieldType = fieldInfo.FieldType;

			    if ((fieldType == typeof(bool)) && (useOptionValues == null))
			    {
			        propEntry.mOptionValues = new String[] { new String("No"), new String("Yes") };
			    }

				if (fieldType.IsNullable)
				{
					fieldType = ((SpecializedGenericType)fieldType).GetGenericArg(0);
				}

			    if ((fieldType.IsEnum) || (fieldType == typeof(bool)))
			    {
					if (propIdx == 0)
					{
				        var comboBox = new DarkComboBox();
				        comboBox.mFrameless = true;
				        comboBox.mPopulateMenuAction.Add(new (menu) => { PopulateComboBox(menu, propEntries); });
				        subItem.AddWidget(comboBox);
				        subItem.mOnResized.Add(new (evt) => { comboBox.Resize(0, 0, GetValueEditWidth(subItem), subItem.mHeight + 1); });
				        propEntry.mComboBoxes = new List<DarkComboBox>();
				        propEntry.mComboBoxes.Add(comboBox);
						subItem.mOnMouseDown.Add(new (evt) =>
							{
								comboBox.MouseDown(-1, -1, 0, 1);
								//comboBox.ShowDropdown();
							});
					}
					handledMouseDown = true;
			    }
			    else if (fieldType == typeof(List<String>)) // List<T>
			    {
			        item.MakeParent();
			    }

				if (!handledMouseDown)
				{
					if (propIdx == 0)
			    		subItem.mOnMouseDown.Add(new => PropValueClicked);
				}

				if ((flags.HasFlag(.BrowseForFile)) | (flags.HasFlag(.BrowseForFolder)))
				{
					propEntry.mEditInsets = new .();
					
					DarkButton browseButton = new DarkButton();
					browseButton.Label = "...";
					browseButton.mOnMouseClick.Add(new (evt) =>
						{
							let path = propEntry.mCurValue.Get<String>();
							if (propEntry.mFlags.HasFlag(.BrowseForFile))
							{
								BrowseForFile(path);
							}
							else
							{
								BrowseForFolder(path);
							}
							propEntry.mListViewItem.GetSubItem(1).Label = path;
							CheckForChanges();
						});
					subItem.mOnResized.Add(new (evt) =>
						{
							let width = GetValueEditWidth(subItem);
							propEntry.mEditInsets.mRight = GS!(22);
							browseButton.mLabelYOfs = GS!(-2);
							browseButton.Resize(width - GS!(21), GS!(1), GS!(20), subItem.mHeight - 2);
							
						});
					subItem.AddWidget(browseButton);
				}
			}

		    mPropPage.mPropEntries[item] = propEntries;

		    UpdatePropertyValue(propEntries);

			return propEntries[0];
		}

        protected (DarkListViewItem, PropEntry) AddPropertiesItem(DarkListViewItem parent, String name, String propName = null, String[] optionValues = null, PropEntry.Flags flags = .None)
        {
            var item = (DarkListViewItem)parent.CreateChildItem();
            item.Label = name;
            item.mFocusColor = 0xFFA0A0A0;
			item.mOnMouseDown.Add(new => PropValueClicked);
            let propEntry = SetupPropertiesItem(item, name, propName, optionValues, flags);
            return (item, propEntry);
        }

        protected void PropValueClicked(MouseEvent theEvent)
        {
            DarkListViewItem clickedItem = (DarkListViewItem)theEvent.mSender;
			if (clickedItem.mColumnIdx == 0)
			{
				clickedItem.mListView.SetFocus();
				clickedItem.mListView.GetRoot().SelectItemExclusively(clickedItem);
				return;
			}

			if (theEvent.mX != -1)
			{
				clickedItem.mListView.GetRoot().SelectItemExclusively(null);
			}

            DarkListViewItem item = (DarkListViewItem)clickedItem.GetSubItem(0);

            PropEntry[] propertyEntries = mPropPage.mPropEntries[item];
			if (propertyEntries[0].mDisabled)
				return;
            EditValue(item, propertyEntries);
        }

        protected void ArrayPropValueClicked(PropEntry[] propEntries, int32 idx)
        {
			var propEntry = propEntries[0];
            DarkListViewItem parentItem = propEntry.mListViewItem;
            DarkListViewItem clickedItem = (DarkListViewItem)parentItem.GetChildAtIndex(idx);
            DarkListViewItem item = (DarkListViewItem)clickedItem.GetSubItem(0);
            EditValue(item, propEntries, idx);
        }

        public override void DrawAll(Graphics g)
        {
            base.DrawAll(g);

			if (mCategorySelector.mVisible)
            	IDEUtils.DrawOutline(g, mCategorySelector);

            IDEUtils.DrawOutline(g, mPropPage.mPropertiesListView, 0, 1);

			if (mSearchEdit != null)
			{
				g.Draw(DarkTheme.sDarkTheme.GetImage(.Search), mSearchEdit.mX + GS!(2), mSearchEdit.mY + GS!(1));
			}
        }

		public override void Update()
		{
			base.Update();

			if (mSearchEdit != null)
			{
				int curVersion = mSearchEdit.mEditWidgetContent.mData.mCurTextVersionId;
				if (curVersion != mSearchEdit.mSearchVersion)
				{
					UpdateSearch();
					mSearchEdit.mSearchVersion = curVersion;
				}
			}
		}
    }
}
