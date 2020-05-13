using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Globalization;
using System.Reflection;
using Beefy;
using Beefy.widgets;
using Beefy.theme;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.events;
using Beefy.utils;
using IDE.Debugger;

namespace IDE.ui
{
    public class MemoryVarListView : DarkListView
    {
        MemoryPanel mMemoryPanel;

        public this(MemoryPanel panel)
        {
            mMemoryPanel = panel;
            //mHeaderImageIdx = DarkTheme.ImageIdx.COUNT;
            //mHeaderLabelYOfs = 2 - panel.mVarListViewStartY;
        }

        public override void Draw(Graphics g)
        {
            g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.EditBox), 0, GS!(mMemoryPanel.mBinaryDataWidget.mColumnHeaderHeight) + GS!(mHeaderLabelYOfs) - GS!(1), mWidth, mHeight - GS!(mMemoryPanel.mBinaryDataWidget.mColumnHeaderHeight));

            base.Draw(g);
        }
    }

    public class MemoryRepListView : DarkListView
    {
		[AllowDuplicates]
        public enum RepType
        {
            // integer types (dual signed/unsigned reps)

			Address,

            Int8,
            Int16,
            Int32,
            Int64,

            Float,
            Double,
			AddrSymbol,
			ValSymbol,

            COUNT,
            INTCOUNT = (int32)RepType.Float
        }

        MemoryPanel mMemoryPanel;
        DarkEditWidget mEditWidget;
        ListViewItem mEditingItem;
        RepType mEditingRepType;
        bool mEditRepTypeIsAlt;
        bool mEditSubmitting;
        DebugManager.IntDisplayType[] mIntRepTypeDisplayTypes = new DebugManager.IntDisplayType[(int32)RepType.INTCOUNT] ~ delete _;
        DebugManager.IntDisplayType[] mIntRepTypeDisplayTypesAlt = new DebugManager.IntDisplayType[(int32)RepType.INTCOUNT] ~ delete _;
        DebugManager.IntDisplayType mDefaultDisplayType = DebugManager.IntDisplayType.Default;

        const int32 kLockSize = 256; // are you really going to be typing strings longer than this in here? Yeah I didn't think so.

        public static void GetRepTypeDescription(RepType repType, String outRepType)
        {
            /*FieldInfo fi = repType.GetType().GetField(repType.ToString());

            DescriptionAttribute[] attributes = (DescriptionAttribute[])fi.GetCustomAttributes(typeof(DescriptionAttribute), false);

            if ((attributes != null) && (attributes.Length > 0))
                return attributes[0].Description;
        
            return repType.ToString();*/
			repType.ToString(outRepType);
        }

        public this(MemoryPanel panel)
        {
            mMemoryPanel = panel;
            //mShowColumnGrid = true;
            //mShowLineGrid = true;
            mLabelX = GS!(6);

            for (int32 i=0; i<mIntRepTypeDisplayTypes.Count; ++i)
            {
                mIntRepTypeDisplayTypes[i] = DebugManager.IntDisplayType.Default;
                mIntRepTypeDisplayTypesAlt[i] = DebugManager.IntDisplayType.Default;
            }
        }
    
        /*protected override void DrawColumnGridColumn(Graphics g, float x, float y, float height, uint32 color)
        {
            float yBase = 0;
            var baseItem = GetRoot().GetChildAtIndex(0);
            if (baseItem != null)
            {
                float xBase;
                baseItem.SelfToOtherTranslate(this, 0, 0, out xBase, out yBase);
            }

            float yLimit = yBase + (int32)RepType.INTCOUNT * mFont.GetLineSpacing();
			float useHeight = height;
            if ((y + useHeight) > yLimit)
                useHeight = yLimit - y;

            base.DrawColumnGridColumn(g, x, y, useHeight, color);
        }*/

        public override void Draw(Graphics g)
        {
            //g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Window), 0, mMemoryPanel.mBinaryDataWidget.mColumnHeaderHeight + mHeaderLabelYOfs - 1, mWidth, mHeight - mMemoryPanel.mBinaryDataWidget.mColumnHeaderHeight);

            base.Draw(g);
        }

        public void InitRows()
        {
            VertScrollTo(0);
            GetRoot().Clear();

            for (int32 i=0; i<(int32)RepType.COUNT; ++i)
            {
                RepType repType = (RepType)i;

                var listViewItem = GetRoot().CreateChildItem();
                listViewItem.mOnMouseDown.Add(new (theEvent) => OnRepListValueClicked(theEvent, repType, 0));
				String repName = scope String();
				GetRepTypeDescription(repType, repName);
                listViewItem.Label = repName;

                var subItem = listViewItem.CreateSubItem(1);
                subItem.Label = "";
                subItem.mOnMouseDown.Add(new (theEvent) => OnRepListValueClicked(theEvent, repType, 1));

                if (i<(int32)RepType.INTCOUNT)
                {
                    subItem = listViewItem.CreateSubItem(2);
                    subItem.Label = "";
                    subItem.mOnMouseDown.Add(new (theEvent) => OnRepListValueClicked(theEvent, repType, 2));
                }
            }
        }

        public void ZomgRandomFloatTest()
        {
            /*Random rng = new Random((int)DateTime.Now.ToBinary());
            byte[] srcFltBytes = new byte[4];

            for (int i=0; i<100000; ++i)
            {
                int exponent;
                do
                {
                    rng.NextBytes(srcFltBytes);

                    //srcFltBytes[3] &= 0x80; srcFltBytes[2] &= 0x7f; // denormal test

                    exponent = ((srcFltBytes[3] & 0x7f) << 1) | ((srcFltBytes[2] >> 7) & 1);
                    //System.Diagnostics.Trace.Assert(exponent == 0); // denormal test

                } while (/*exponent == 0 || */exponent == 0xFF); // skip zero/denormal/inf/nan for now

                float srcF = System.BitConverter.ToSingle(srcFltBytes, 0);
                string srcS = srcF.ToString("G10");
                if (!srcS.Contains('.'))
                    srcS += ".0";

                string evalResultStr = MemoryPanel.EvalExpression(srcS);
                if (evalResultStr == null)
                    System.Diagnostics.Trace.Assert(false, "Could not eval expression");
                float dstF;
                if (!float.TryParse(evalResultStr, out dstF))
                    System.Diagnostics.Trace.Assert(false, "Could not parse float");
                string dstS = dstF.ToString("G10");
                if (!dstS.Contains('.'))
                    dstS += ".0";
                System.Diagnostics.Trace.Assert(srcS == dstS);
                System.Diagnostics.Trace.Assert(srcF == dstF);
            }*/
        }

        public void ZomgRandomDoubleTest()
        {
            /*Random rng = new Random((int)DateTime.Now.ToBinary());
            byte[] srcFltBytes = new byte[8];

            for (int i=0; i<100000; ++i)
            {
                int exponent;
                do
                {
                    rng.NextBytes(srcFltBytes);

                    //srcFltBytes[7] &= 0x80; srcFltBytes[6] &= 0x0f; // denormal test

                    exponent = ((int)(srcFltBytes[7] & 0x7f) << 4) | ((srcFltBytes[6] >> 4) & 0x0f);
                    //System.Diagnostics.Trace.Assert(exponent == 0); // denormal test

                } while (exponent == 0 || exponent == 0x7FF); // skip zero/denormal/inf/nan for now

                double srcF = System.BitConverter.ToDouble(srcFltBytes, 0);
                string srcS = srcF.ToString("G18");
                if (!srcS.Contains('.'))
                    srcS += ".0";

                string evalResultStr = MemoryPanel.EvalExpression(srcS);
                if (evalResultStr == null)
                    System.Diagnostics.Trace.Assert(false, "Could not eval expression");
                double dstF;
                if (!double.TryParse(evalResultStr, out dstF))
                    System.Diagnostics.Trace.Assert(false, "Could not parse float");
                string dstS = dstF.ToString("G18");
                if (!dstS.Contains('.'))
                    dstS += ".0";
                System.Diagnostics.Trace.Assert(srcS == dstS);
                System.Diagnostics.Trace.Assert(srcF == dstF);
            }*/
        }

        public void UpdateRepStrings()
        {
            //ZomgRandomFloatTest();
            //ZomgRandomDoubleTest();

            var lockRange = mMemoryPanel.mBinaryDataWidget.GetLockedRangeAtCursor(kLockSize);
            if (lockRange == null)
            {
                for (int32 i=0; i<GetRoot().GetChildCount(); ++i)
                {
                    GetRoot().GetChildAtIndex(i).GetSubItem(1).Label = "";
                    if (i<(int32)RepType.INTCOUNT)
                        GetRoot().GetChildAtIndex(i).GetSubItem(2).Label = "";
                }
            }
            else
            {
				defer delete lockRange;
                
                {
                    for (int32 i=0; i<GetRoot().GetChildCount(); ++i)
                    {
						String s = scope String();
						String altS = scope String();
						bool hasAltS = false;

                        DebugManager.IntDisplayType intDisplayType = DebugManager.IntDisplayType.Default;
                        DebugManager.IntDisplayType altIntDisplayType = DebugManager.IntDisplayType.Default;
                        if (i < (int32)RepType.INTCOUNT)
                        {
                            intDisplayType = mIntRepTypeDisplayTypes[i];
                            altIntDisplayType = mIntRepTypeDisplayTypesAlt[i];
                        }
                        if (intDisplayType == DebugManager.IntDisplayType.Default)
                            intDisplayType = mDefaultDisplayType;
                        if (altIntDisplayType == DebugManager.IntDisplayType.Default)
                            altIntDisplayType = mDefaultDisplayType;
                        int32 intBase, altIntBase;
                        String intBasePrefix = null, altIntBasePrefix = null;
                        switch(intDisplayType)
                        {
                            case DebugManager.IntDisplayType.Hexadecimal: intBase = 16; intBasePrefix = "0x"; break;
                            case DebugManager.IntDisplayType.Binary: intBase = 2; intBasePrefix = "0b"; break;
                            default: intBase = 10; break;
                        }
                        switch(altIntDisplayType)
                        {
                            case DebugManager.IntDisplayType.Hexadecimal: altIntBase = 16; altIntBasePrefix = "0x"; break;
                            case DebugManager.IntDisplayType.Binary: altIntBase = 2; altIntBasePrefix = "0b"; break;
                            default: altIntBase = 10; break;
                        }

						
                        switch((RepType)i)
                        {
						case RepType.Address:
							((uint64)lockRange.mBaseOffset).ToString(s, "P", null);
                        case RepType.Int8:
							hasAltS = true;
							(*(int8*)lockRange.mData.CArray()).ToString(s, (altIntBase == 10) ? "" : "X2", null);
							if (altIntBase == 10)
								((UInt64)*(uint8*)lockRange.mData.CArray()).ToString(altS, "", null);
                        case RepType.Int16:
							hasAltS = true;
							(*(int16*)lockRange.mData.CArray()).ToString(s, (altIntBase == 10) ? "" : "X4", null);
							if (altIntBase == 10)
								((UInt64)*(uint16*)lockRange.mData.CArray()).ToString(altS, "", null);
                        case RepType.Int32:
							hasAltS = true;
							(*(int32*)lockRange.mData.CArray()).ToString(s, (altIntBase == 10) ? "" : "X8", null);
							if (altIntBase == 10)
								((UInt64)*(uint32*)lockRange.mData.CArray()).ToString(altS, "", null);
                        case RepType.Int64:
							hasAltS = true;
							(*(int64*)lockRange.mData.CArray()).ToString(s, (altIntBase == 10) ? "" : "X16", null);
							if (altIntBase == 0x10)
								s.Insert(8, '\'');
							if (altIntBase == 10)
								((UInt64)*(uint64*)lockRange.mData.CArray()).ToString(altS, "", null);
                        case RepType.Float:
							(*(float*)lockRange.mData.CArray()).ToString(s);
                        case RepType.Double:
							(*(double*)lockRange.mData.CArray()).ToString(s);
						case RepType.AddrSymbol:
							gApp.mDebugger.GetAddressSymbolName(lockRange.mBaseOffset, true, s);
						case RepType.ValSymbol:
							int addr = 0;
							if (gApp.mDebugger.GetAddrSize() == 8)
								addr = (.)*(uint64*)lockRange.mData.CArray();
							else
								addr = (.)*(uint32*)lockRange.mData.CArray();
							gApp.mDebugger.GetAddressSymbolName(addr, true, s);
                        /*case RepType.StringA:
							fallthrough;
                        case RepType.StringW:
                            {
                                {
                                    uint8* buf = lockRange.mData.CArray();
									int bufOfs = 0;
                                    {
                                        if ((RepType)i == RepType.StringA)
                                        {
											while (bufOfs < kLockSize)
											{
												char8 c = *(char8*)(buf + bufOfs);
												if (c == 0)
													break;
												s.Append(c);
												bufOfs++;
											}
										}    
                                        else
                                        {
											while (bufOfs < kLockSize)
											{
												char16 c = *(char16*)(buf + bufOfs);
												if (c == 0)
													break;
												s.Append(c);
												bufOfs++;
											}
										}    
                                    }
                                }
                            }
                            break;*/
                        default: break;
                        }
                        if (i < (int)RepType.INTCOUNT)
                        {
                            if (intBasePrefix != null)
                                s.Insert(0, intBasePrefix);
                            if (!altS.IsEmpty && altIntBasePrefix != null)
                                altS.Insert(0, altIntBasePrefix);
                        }
                        GetRoot().GetChildAtIndex(i).GetSubItem(1).Label = s;
                        if (hasAltS)
                        	GetRoot().GetChildAtIndex(i).GetSubItem(2).Label = altS;
                    }
                }
            }
        }

        public void OnRepListValueClicked(MouseEvent theEvent, RepType repType, int32 column)
        {
            ListViewItem clickedItem = (ListViewItem)theEvent.mSender;
            ListViewItem item = (ListViewItem)clickedItem.GetSubItem(0);

            GetRoot().SelectItemExclusively(item);
            SetFocus();
        
            if ((theEvent.mBtn == 0) && (theEvent.mBtnCount > 1))
            {
                if ((column == 1) || (column == 2))
                {
                    EditItem(clickedItem, repType, column);
                }
            }
            else if (theEvent.mBtn == 1)
            {
                ShowRightClickMenu(theEvent, repType, column);
            }
        }

        private void EditItem(ListViewItem item, RepType repType, int32 column)
        {
            ListViewItem subItem = (ListViewItem)item.GetSubItem(column);
            mEditWidget = new DarkEditWidget();
            String editVal = subItem.mLabel;
            mEditWidget.SetText(editVal);
            mEditWidget.Content.SelectAll();
			mEditSubmitting = true; // Default to submitting on lost focus

            mEditingItem = item;
            mEditingRepType = repType;
            mEditRepTypeIsAlt = (column == 2);
        
            float x;
            float y;
            subItem.SelfToOtherTranslate(this, 0, 0, out x, out y);
            x = subItem.LabelX - 4;

            float width = Math.Max(mWidth - x - 20, 50);
            mEditWidget.Resize(x, y, width, 20);
            AddWidget(mEditWidget);
        
            mEditWidget.mOnLostFocus.Add(new => HandleEditLostFocus);
            mEditWidget.mOnSubmit.Add(new => HandleRenameSubmit);
            mEditWidget.mOnCancel.Add(new => HandleRenameCancel);
            WidgetWindow.sOnMouseWheel.Add(new => HandleMouseWheel);
            mEditWidget.SetFocus();
        }

        void ShowRightClickMenu(MouseEvent theEvent, RepType repType, int32 column)
        {
            if ((int32)repType >= (int32)RepType.INTCOUNT)
                return;
            
            bool isAlt = (column == 2);

            Menu menu = new Menu();

            delegate void(String, bool) addSubMenu = scope (label, useRepType) => {
                
                Menu subMenu = menu.AddItem(label);

                for (DebugManager.IntDisplayType i = default; i < DebugManager.IntDisplayType.COUNT; i++)
                {
					var toType = i;

                    Menu subMenuItem = subMenu.AddItem(ToStackString!(i));
                    DebugManager.IntDisplayType curDisplayType = (useRepType) ? (isAlt ? mIntRepTypeDisplayTypesAlt[(int32)repType] : mIntRepTypeDisplayTypes[(int32)repType]) : mDefaultDisplayType;
                    if (curDisplayType == i)
                        subMenuItem.mIconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Check);
					
                    subMenuItem.mOnMenuItemSelected.Add(new (iMenu) =>
                    	{
                        	if (useRepType)
	                        {
	                            if (isAlt)
	                                mIntRepTypeDisplayTypesAlt[(int)repType] = toType;
	                            else
	                                mIntRepTypeDisplayTypes[(int)repType] = toType;
	                        }
	                        else
	                            mDefaultDisplayType = toType;
	
	                        UpdateRepStrings();
	                    });
                }
            };
            
            addSubMenu("Item Display", true);
            addSubMenu("Default Display", false);

            float x, y;
            theEvent.GetRootCoords(out x, out y);
            MenuWidget menuWidget = ThemeFactory.mDefault.CreateMenuWidget(menu);
            menuWidget.Init(mWidgetWindow.mRootWidget, x, y);
        }

        void HandleMouseWheel(MouseEvent evt)
        {
            HandleEditLostFocus(mEditWidget);
        }
    
        void HandleEditLostFocus(Widget widget)
        {
            if (mEditWidget == null)
                return;

            if (mEditSubmitting)
            {
				String editText = scope String();
				mEditWidget.GetText(editText);
                String expr = scope String();
				editText.Append(", d");
                if (!MemoryPanel.EvalExpression(editText, expr))
                    expr = editText;

                bool markDirty = false;
                var lockRange = mMemoryPanel.mBinaryDataWidget.GetLockedRangeAtCursor(kLockSize);
                if (lockRange != null)
                {
					defer delete lockRange;

					
                    switch(mEditingRepType)
                    {
					case RepType.Address:
						if (uint64.Parse(expr, .HexNumber) case .Ok(let val))
						{
							mMemoryPanel.mBinaryDataWidget.SelectRange((int)val, 1);
						}
                    case RepType.Int8:
                        // Check mEditRepTypeIsAlt?
                        if (int32.Parse(expr) case .Ok(let val))
						{
							*((int8*)lockRange.mData.CArray()) = (.)val;
						}
                    case RepType.Int16:
						if (int32.Parse(expr) case .Ok(let val))
						{
							*((int16*)lockRange.mData.CArray()) = (.)val;
						}
                    case RepType.Int32:
						if (int32.Parse(expr) case .Ok(let val))
						{
							*((int32*)lockRange.mData.CArray()) = (.)val;
						}
                    case RepType.Int64:
						if (int64.Parse(expr) case .Ok(let val))
						{
							*((int64*)lockRange.mData.CArray()) = (.)val;
						}
                    case RepType.Float:
						if (float.Parse(expr) case .Ok(let val))
						{
							*((float*)lockRange.mData.CArray()) = (.)val;
						}
                    case RepType.Double:
						if (double.Parse(expr) case .Ok(let val))
						{
							*((double*)lockRange.mData.CArray()) = (.)val;
						}
                    /*case RepType.StringA:
                    case RepType.StringW:
                        {
                            unsafe
                            {
                                //fixed (byte* buf = lockRange.mData)
                                {
                                    IntPtr ptr;
                                    int len;
                                    if (mEditingRepType == RepType.StringA)
                                    {
                                        ptr = (IntPtr)System.Runtime.InteropServices.Marshal.StringToHGlobalAnsi(expr);
                                        len = expr.Length + 1;
                                    }
                                    else
                                    {
                                        ptr = (IntPtr)System.Runtime.InteropServices.Marshal.StringToHGlobalUni(expr);
                                        len = (expr.Length + 1) * 2;
                                    }

                                    len = Math.Min(len, lockRange.mData.Length);

                                    repBytes = new byte[len];
                                    System.Runtime.InteropServices.Marshal.Copy(ptr, repBytes, 0, len);

                                    System.Runtime.InteropServices.Marshal.FreeHGlobal(ptr);
                                }
                            }
                        }
                        break;*/
                    default: break;
                    }

                    lockRange.mModified = true;
                    markDirty = true;
                }

                if (markDirty)
                {
                    mMemoryPanel.MarkViewDirty();
                    UpdateRepStrings();
                }
            }

            EditWidget editWidget = (EditWidget)widget;
            editWidget.mOnLostFocus.Remove(scope => HandleEditLostFocus, true);
            editWidget.mOnSubmit.Remove(scope => HandleRenameSubmit, true);
            editWidget.mOnCancel.Remove(scope => HandleRenameCancel, true);
            WidgetWindow.sOnMouseWheel.Remove(scope => HandleMouseWheel, true);
            
            editWidget.RemoveSelf();
			gApp.DeferDelete(editWidget);
            mEditWidget = null;

            if (mWidgetWindow.mFocusWidget == null)
                SetFocus();
            //if ((mHasFocus) && (mEditingItem.mParent != null))
                //  GetRoot().SelectItemExclusively(mEditingItem);
            mEditingItem = null;
            mEditingRepType = RepType.COUNT;
            mEditRepTypeIsAlt = false;
            mEditSubmitting = false;
        }

        void HandleRenameCancel(EditEvent theEvent)
        {
			mEditSubmitting = false;
            HandleEditLostFocus((EditWidget)theEvent.mSender);
        }

        delegate bool TryParseHandler<T>(String expr, out T val);
        delegate uint8[] GetBytesHandler<T>(T val);
        uint8[] ParseType<T>(String expr, TryParseHandler<T> parseHandler, GetBytesHandler<T> getBytesHandler)
        {
            T val;
            if (parseHandler(expr, out val))
                return getBytesHandler(val);
            return null;
        }

        void HandleRenameSubmit(EditEvent theEvent)
        {
            mEditSubmitting = true;

            HandleEditLostFocus((EditWidget)theEvent.mSender);
        }

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);
		}
    }

    public class MemoryContentProvider : IBinaryDataContentProvider
    {
        public bool IsReadOnly { get { return false; } }

        public uint8[] ReadBinaryData(int offset, int32 size)
        {
            uint8[] result = new uint8[(int32)size];
            IDEApp.sApp.mDebugger.ReadMemory(offset, (int)size, result);
            return result;
        }
        public void WriteBinaryData(uint8[] data, int offset)
        {
            IDEApp.sApp.mDebugger.WriteMemory(offset, (int)data.Count, data);
        }
    }

    public class MemoryPanel : Panel
    {
        public BinaryDataWidget mBinaryDataWidget;
        //public DarkEditWidget mAddressEditWidget;
        //public WatchRefreshButton mWatchRefreshButton;
        public MemoryVarListView mVarListView;
        public MemoryRepListView mRepListView;
        public DockingProxy mVarListViewDockingProxy;
        public DockingProxy mRepListViewDockingProxy;
        public DarkDockingFrame mDockingFrame;
        //public TabbedView mTabbedView;

        public MemoryContentProvider mProvider ~ delete _;
        public String mLastAddressExpr = new String() ~ delete _;
        public String mLastAddressValue = new String() ~ delete _;
        public float mVarListViewStartY = 0;//5;
        public bool mMemoryViewDirty = true;
        public bool mAutoExprRefresh = true;

        public bool mDisabled;
        public int32 mDisabledTicks;
        //public int mCallStackUpdateCnt;

        public Dictionary<ListViewItem, BinaryDataWidget.TrackedExpression> mListViewItemToTrackedExprMap = new Dictionary<ListViewItem,BinaryDataWidget.TrackedExpression>() ~ delete _;

        public this()
        {
            mProvider = new MemoryContentProvider();

            mBinaryDataWidget = new BinaryDataWidget(mProvider);
            mBinaryDataWidget.InitScrollbar();
            mBinaryDataWidget.mScrollbarInsets.mTop -= 1;
            mBinaryDataWidget.UpdateScrollbar();
            mBinaryDataWidget.TrackedExpressionsUpdated.Add(new => OnTrackedExpressionsUpdated);
            mBinaryDataWidget.OnKeyCursorUpdated.Add(new => OnBinaryDataKeyCursorUpdated);
            mBinaryDataWidget.OnActiveWidthChanged.Add(new => OnBinaryDataActiveWidthChanged);
			mBinaryDataWidget.mOnGotoAddress.Add(new => GotoAddress);

            /*mAddressEditWidget = new DarkEditWidget();
            mAddressEditWidget.SetText(mLastAddressExpr);
            mAddressEditWidget.Resize(20, 0, 80, 20);

            mAddressEditWidget.mLostFocusHandler.Add(new => OnAddressEditLostFocus);
            mAddressEditWidget.mSubmitHandler.Add(new => OnAddressEditSubmit);
            mAddressEditWidget.mCancelHandler.Add(new => OnAddressEditCancel);
            mAddressEditWidget.mContentChangedHandler.Add(new => OnAddressEditContentChanged);
            
            mWatchRefreshButton = new WatchRefreshButton();
            mWatchRefreshButton.Resize(0, 0, 20, 20);
            mWatchRefreshButton.mMouseDownHandler.Add(new (evt) => ToggleAutoExprRefresh());*/

            mVarListView = new MemoryVarListView(this);
            //mVarListView.Resize(mBinaryDataWidget.GetActiveWidth(), mVarListViewStartY, 200, 10);
            //mVarListView.mIconX = 4;
            //mVarListView.mOpenButtonX = 4;
            //mVarListView.mLabelX = 26;
            //mVarListView.mChildIndent = 16;
            //mVarListView.mHiliteOffset = -2;
            mVarListView.InitScrollbars(true, true);
            mVarListView.mHorzScrollbar.mPageSize = 100;
            mVarListView.mHorzScrollbar.mContentSize = 500;
            mVarListView.mVertScrollbar.mPageSize = 100;
            mVarListView.mVertScrollbar.mContentSize = 500;
            mVarListView.UpdateScrollbars();
			mVarListView.mOnMouseDown.Add(new (evt) => { mVarListView.GetRoot().SelectItem(null); mBinaryDataWidget.DeselectRange(); });
            
            ListViewColumn column = mVarListView.AddColumn(GS!(200), "Name");
            column.mMinWidth = 100;            
            column = mVarListView.AddColumn(80, "Pinned");
            //column = mVarListView.AddColumn(200, "Type");

            mVarListViewDockingProxy = new DockingProxy(mVarListView);
            mVarListViewDockingProxy.SetRequestedSize(GS!(300), GS!(300));

            mRepListView = new MemoryRepListView(this);
            //mRepListView.Resize(mBinaryDataWidget.GetActiveWidth(), mVarListViewStartY, 200, 10);
            //mVarListView.mIconX = 4;
            //mVarListView.mOpenButtonX = 4;
            //mVarListView.mLabelX = 26;
            //mVarListView.mChildIndent = 16;
            //mVarListView.mHiliteOffset = -2;
            mRepListView.InitScrollbars(true, true);
            mRepListView.mHorzScrollbar.mPageSize = 100;
            mRepListView.mHorzScrollbar.mContentSize = 500;
            mRepListView.mVertScrollbar.mPageSize = 100;
            mRepListView.mVertScrollbar.mContentSize = 500;
            mRepListView.UpdateScrollbars();
            mRepListView.InitRows();
			mRepListView.mOnMouseDown.Add(new (evt) => { mRepListView.GetRoot().SelectItem(null); });
            
            column = mRepListView.AddColumn(GS!(80), "Type");
            column.mMinWidth = GS!(25);
            column = mRepListView.AddColumn(GS!(100), "Value");
            column.mMinWidth = GS!(50);
            column = mRepListView.AddColumn(GS!(100), "Value (Unsigned)");
            column.mMinWidth = GS!(50);

            mRepListViewDockingProxy = new DockingProxy(mRepListView);
            mRepListViewDockingProxy.SetRequestedSize(GS!(300), GS!(300));

            mDockingFrame = (DarkDockingFrame)ThemeFactory.mDefault.CreateDockingFrame();
			mDockingFrame.mDrawBkg = false;
            mDockingFrame.Resize(mBinaryDataWidget.GetActiveWidth() + GS!(200), mVarListViewStartY, GS!(200), GS!(10));

            //mTabbedView = ThemeFactory.mDefault.CreateTabbedView();
            //mTabbedView.SetRequestedSize(150, 150);

            //AddWidget(mBinaryDataWidget);
            //AddWidget(mAddressEditWidget);
            //AddWidget(mWatchRefreshButton);
            //AddWidget(mVarListView);
            AddWidget(mDockingFrame);
            //mDockingFrame.AddDockedWidget(mTabbedView, null, DockingFrame.WidgetAlign.Bottom, false);

			mBinaryDataWidget.mIsFillWidget = true;
            mDockingFrame.AddDockedWidget(mVarListViewDockingProxy, null, DockingFrame.WidgetAlign./*Left*/Top);
            mDockingFrame.AddDockedWidget(mRepListViewDockingProxy, null, DockingFrame.WidgetAlign./*Left*/Top);
			mDockingFrame.AddDockedWidget(mBinaryDataWidget, null, .Left);
			

            //RebuildUI();
        }

        void ToggleAutoExprRefresh()
        {
            mAutoExprRefresh = !mAutoExprRefresh;

            AddressExprUpdated();
            
            //mAddressEditWidget.SetText(mAutoExprRefresh ? mLastAddressExpr : mLastAddressValue);
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);
            /*if (mAutoExprRefresh)
            {
                using (g.PushColor(0x80FFFF00))
                {
                    g.FillRect(mWatchRefreshButton.mX, mWatchRefreshButton.mY, mWatchRefreshButton.mWidth, mWatchRefreshButton.mHeight);
                }
            }*/
        }

		public void GotoAddress()
		{
			GoToAddressDialog aDialog = new GoToAddressDialog();
			aDialog.Init(this);
			aDialog.PopupWindow(mWidgetWindow);
		}

        static bool EvalRawIntegerExpression(String expr, out int position)
        {
            position = 0;
            String parseStr = expr;
            bool parsed = false;
            if (parseStr.StartsWith("0x"))
            {
                //parseStr = scope String(parseStr, 2);
				
                switch (UInt64.Parse(parseStr, NumberStyles.HexNumber, CultureInfo.CurrentCulture))
				{
				case .Ok(let value):
					position = (.)value;
					parsed = true;
				case .Err:
				}
            }
            else
            {
				//TODO: ThrowUnimplemented();
                //parsed = ulong.TryParse(parseStr, out position);
            }
            
            return parsed;
        }

        public static bool EvalExpression(String expr, String result)
        {
            String evalResultStr = scope String();
            IDEApp.sApp.mDebugger.Evaluate(expr, evalResultStr, -1, -1, DebugManager.EvalExpressionFlags.FullPrecision);
            if (evalResultStr.StartsWith("!"))
            {
                // even if evaluation fails, we can still use a raw value
                int position = 0;
                if (EvalRawIntegerExpression(expr, out position))
                {
					result.Set(expr);
                    return true;
                }
                else
                {
                    //CDH TODO indicate error somehow? maybe turn box red?
                }
            }
            else
            {
                var lines = scope List<StringView>(evalResultStr.Split('\n'));
                if (lines.Count > 0)
                {
                    String valStr = scope String(lines[0]);
                    if (valStr.Contains(' '))
                    {
                        var tempSplit = scope List<StringView>(valStr.Split( ' '));
                        valStr.Set(tempSplit[0]);
                    }

                    result.Set(valStr);
					return true;
                }
            }
        
            return false;
        }

        void AddressExprUpdated()
        {
            int position = 0;

            if (mAutoExprRefresh && (mLastAddressExpr.Length > 0))
            {
                String evalResultStr = scope String();
                if (EvalExpression(mLastAddressExpr, evalResultStr))
                    mLastAddressValue.Set(evalResultStr);
            }

            if (EvalRawIntegerExpression(mLastAddressValue, out position))
            {
                mBinaryDataWidget.ResetPosition(position);
            }
            else
            {
                String evalResultStr = scope String();
                if (EvalExpression(mLastAddressValue, evalResultStr))
                    mLastAddressValue.Set(evalResultStr);
                if (EvalRawIntegerExpression(mLastAddressValue, out position))
                    mBinaryDataWidget.ResetPosition(position);
            }
        }

        /*void OnAddressEditLostFocus(Widget widget)
        {
        }
        void OnAddressEditSubmit(EditEvent theEvent)
        {
            if (mAutoExprRefresh)
            {
                mLastAddressExpr.Clear();
                mAddressEditWidget.GetText(mLastAddressExpr);
            }
            else
            {
                mLastAddressValue.Clear();
                mAddressEditWidget.GetText(mLastAddressValue);
            }

            AddressExprUpdated();

            mAddressEditWidget.SetText(mAutoExprRefresh ? mLastAddressExpr : mLastAddressValue);
        }
        void OnAddressEditCancel(EditEvent theEvent)
        {
            AddressExprUpdated();
        
            mAddressEditWidget.SetText(mAutoExprRefresh ? mLastAddressExpr : mLastAddressValue);
        }
        void OnAddressEditContentChanged(EditEvent theEvent)
        {
        }*/

        void OnTrackedExpressionsUpdated(List<BinaryDataWidget.TrackedExpression> trackedExpressions)
        {
            mVarListView.VertScrollTo(0);
            mVarListView.GetRoot().Clear();
            //IDEApp.sApp.mDebugger.CheckCallStack();

            mListViewItemToTrackedExprMap.Clear();

            //var sortedExprs = trackedExpressions.OrderBy(te => te.mMemStart).ToList();
			List<BinaryDataWidget.TrackedExpression> sortedExprs = scope .(trackedExpressions.GetEnumerator());
			sortedExprs.Sort(scope (lhs, rhs) => lhs.mMemStart <=> rhs.mMemStart);

            for (var te in sortedExprs)
            {
                if (te.mIsReg)
                    continue; // this list is just for variables for now (regs are their own thing)
                if (!te.mIsVisible)
                    continue;

                var listViewItem = mVarListView.GetRoot().CreateChildItem();
                listViewItem.mOnMouseDown.Add(new (theEvent) => OnVarListValueClicked(theEvent, 0));
                listViewItem.Label = te.mDisplayName;
                listViewItem.IconImageColor = te.mDisplayColor;
                listViewItem.IconImage = DarkTheme.sDarkTheme.GetImage(.UIBoneJoint);

                var subItem = listViewItem.CreateSubItem(1);
                subItem.Label = mBinaryDataWidget.mPinnedTrackedExprs.Contains(te.mDisplayName) ? "Yes" : "No";//stackSize.ToString() ;
                subItem.mOnMouseDown.Add(new (theEvent) => OnVarListValueClicked(theEvent, 1));
                //subItem.mMouseDownHandler += mMemoryPanel.ValueClicked;

                //subItem = listViewItem.CreateSubItem(2);
                //subItem.mLabel = "TBD";//"C++";
                //subItem.mMouseDownHandler += mMemoryPanel.ValueClicked;

                mListViewItemToTrackedExprMap[listViewItem] = te;
            }

#unwarn
            mVarListView.Resized();
            mVarListView.UpdateListSize();
            //mCallStackUpdateCnt = 0;

            //for (int childIdx = 0; childIdx < mVarListView.GetRoot().GetChildCount(); childIdx++)
            //{
                //var checkListViewItem = mVarListView.GetRoot().GetChildAtIndex(childIdx);
            //}
            //mVarListView.GetRoot().SelectItemExclusively(listViewItem);
            //SetFocus();

            //listViewItem.Update();

            //mMemoryViewDirty = false;
            UpdateIcons();
        }

        public void OnBinaryDataKeyCursorUpdated()
        {
            mRepListView.UpdateRepStrings();
        }

        public void OnBinaryDataActiveWidthChanged()
        {
            //ResizeChildren();
        }

        public void OnVarListValueClicked(MouseEvent theEvent, int32 column)
        {
            ListViewItem clickedItem = (ListViewItem)theEvent.mSender;
            ListViewItem item = (ListViewItem)clickedItem.GetSubItem(0);

            mVarListView.GetRoot().SelectItemExclusively(item);
            SetFocus();

			var result = mListViewItemToTrackedExprMap.GetValue(item);
			BinaryDataWidget.TrackedExpression te;
			if (result case .Ok(out te))
            {
				mBinaryDataWidget.SelectRange(te.mCurValue, te.mSize);
				
				if ((theEvent.mBtn == 0) && (theEvent.mBtnCount > 1))
				{
				    if (column == 1)
				    {
				        ListViewItem subItem = (ListViewItem)clickedItem.GetSubItem(1);
						String* entryPtr;
						if (mBinaryDataWidget.mPinnedTrackedExprs.Add(te.mDisplayName, out entryPtr))
						{
							*entryPtr = new String(te.mDisplayName);
							subItem.Label = "Yes";
						}
						else
						{
							String oldStr = *entryPtr;
							mBinaryDataWidget.mPinnedTrackedExprs.Remove(te.mDisplayName);
							delete oldStr;
							subItem.Label = "No";
						}
				    }

				    /*
				    for (int childIdx = 1; childIdx < mVarListView.GetRoot().GetChildCount(); childIdx++)
				    {
				        var checkListViewItem = mVarListView.GetRoot().GetChildAtIndex(childIdx);
				        //checkListViewItem.IconImage = null;
				    }
				    */
				    
				    //int selectedIdx = item.mVirtualIdx;
				    //IDEApp.sApp.ShowPCLocation(selectedIdx);
				    //IDEApp.sApp.mDebugger.mSelectedCallStackIdx = selectedIdx;
				    //IDEApp.sApp.mWatchPanel.MarkWatchesDirty();
				}
			}

            UpdateIcons();
        }

        void UpdateIcons()
        {
            int itemCount = mVarListView.GetRoot().GetChildCount();
            for (int callStackIdx = 0; callStackIdx < itemCount; callStackIdx++)
            {
                //var listViewItem = mVarListView.GetRoot().GetChildAtIndex(callStackIdx);
                /*
                if (callStackIdx == 0)
                {                    
                    if (callStackIdx == IDEApp.sApp.mDebugger.mSelectedCallStackIdx)
                        listViewItem.IconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.LinePointer);
                    else if (IDEApp.sApp.mDebugger.GetRunState() == DebugManager.RunState.Breakpoint)
                        listViewItem.IconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.RedDot);
                    else
                        listViewItem.IconImage = null;
                }
                else if (callStackIdx == IDEApp.sApp.mDebugger.mSelectedCallStackIdx)
                    listViewItem.IconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.EditCircle);
                */
            }
        }

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);

            data.Add("Type", "MemoryPanel");

			if (mBinaryDataWidget.mAutoResizeType == .Manual)
				data.Add("Columns", mBinaryDataWidget.mBytesPerDisplayLine);
			else
				data.Add("AutoResize", mBinaryDataWidget.mAutoResizeType);

			data.Add("RequestedWidth", mDockingFrame.mDockedWidgets[1].mRequestedWidth);
        }

        public override bool Deserialize(StructuredData data)
        {
            if (!base.Deserialize(data))
				return false;

			mBinaryDataWidget.mAutoResizeType = data.GetEnum<BinaryDataWidget.AutoResizeType>("AutoResize");
			if (mBinaryDataWidget.mAutoResizeType == .Manual)
			{
				mBinaryDataWidget.mBytesPerDisplayLine = data.GetInt("Columns");
				if (mBinaryDataWidget.mBytesPerDisplayLine == 0)
				{
					mBinaryDataWidget.mAutoResizeType = .Auto_Mul8;
					mBinaryDataWidget.mBytesPerDisplayLine = 16;
				}
			}


			float requestedSize = data.GetFloat("RequestedWidth");
			if (requestedSize != 0)
				mDockingFrame.mDockedWidgets[1].mRequestedWidth = requestedSize;

			return true;
        }

        void ResizeChildren()
        {
            //var varListStartX = mBinaryDataWidget.GetActiveWidth() + GS!(20);
            //mBinaryDataWidget.Resize(0, 0, varListStartX, mHeight);
            //mDockingFrame.Resize(varListStartX, GS!(mVarListViewStartY), Math.Max(GS!(50), mWidth - varListStartX), mHeight - GS!(mVarListViewStartY));

			mDockingFrame.Resize(0, 0, mWidth, mHeight);
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            ResizeChildren();
        }

        public void MarkViewDirty()
        {
			mListViewItemToTrackedExprMap.Clear();
            mBinaryDataWidget.ClearData(true);
            
            if (mAutoExprRefresh)
                AddressExprUpdated();

            mMemoryViewDirty = true;
        }

        public override void LostFocus()
        {
            base.LostFocus();
            mVarListView.GetRoot().SelectItemExclusively(null);
        }

        public override void Update()
        {
            base.Update();

            if (!IDEApp.sApp.mDebugger.mIsRunning)
            {
                mVarListView.GetRoot().Clear();
                mBinaryDataWidget.ClearData(false);
				mListViewItemToTrackedExprMap.Clear();
            }
            else if ((IDEApp.sApp.mExecutionPaused) && (mMemoryViewDirty) && (!mDisabled))
            {
                //mBinaryDataWidget.ClearData();
                //mMemoryViewDirty = false;

                /*																					
                mVarListView.VertScrollTo(0);
                mVarListView.GetRoot().Clear();
                //IDEApp.sApp.mDebugger.CheckCallStack();

                var listViewItem = (VirtualListViewItem)mVarListView.GetRoot().CreateChildItem();
                listViewItem.mVirtualHeadItem = listViewItem;
                listViewItem.mVirtualCount = 5;//IDEApp.sApp.mDebugger.GetCallStackCount();
                mVarListView.PopulateVirtualItem(listViewItem);
                mVarListView.Resized();
                mVarListView.UpdateListSize();
                //mCallStackUpdateCnt = 0;

                //for (int childIdx = 0; childIdx < mVarListView.GetRoot().GetChildCount(); childIdx++)
                //{
                    //var checkListViewItem = mVarListView.GetRoot().GetChildAtIndex(childIdx);
                //}
                //mVarListView.GetRoot().SelectItemExclusively(listViewItem);
                //SetFocus();

                listViewItem.Update();

                mMemoryViewDirty = false;
                UpdateIcons();
                */
            }
            else if (!mDisabled)
            {
                mBinaryDataWidget.ClearData(true);
                /*
                if ((mListView.GetRoot().GetChildCount() > 0) && (IDEApp.sApp.mUpdateBatchStart))
                {
                    mCallStackUpdateCnt++;
                    var listViewItem = (VirtualListViewItem)mListView.GetRoot().GetChildAtIndex(0);
                    IDEApp.sApp.mDebugger.CheckCallStack();
                    listViewItem.mVirtualCount = IDEApp.sApp.mDebugger.GetCallStackCount();
                    if (mCallStackUpdateCnt <= 2)
                        UpdateIcons();
                }
                */
            }
            else            
            {
                mDisabledTicks++;
				if (mDisabledTicks == 40)
				{
					mVarListView.GetRoot().Clear();
					//mBinaryDataWidget.ClearData(false);
					mBinaryDataWidget.ClearTrackedExprs(true);
					mListViewItemToTrackedExprMap.Clear();
					MarkDirty();
				}

				// This animates memory while we're paused
                /*if ((mDisabledTicks >= 40) && (mDisabledTicks % 30 == 0))
                {
                    mBinaryDataWidget.ClearData(false);
					MarkDirty();
                }*/
            }            
        }

        public void SetDisabled(bool disabled)
        {
            mDisabled = disabled;
			mBinaryDataWidget.mMouseVisible = !disabled;
			mBinaryDataWidget.mGotoButton.mDisabled = disabled;
			mRepListView.mMouseVisible = !disabled;
			if (disabled)
			{
				if (!gApp.mDebugger.mIsRunning)
					mBinaryDataWidget.mCurPosition = 0;
				mRepListView.GetRoot().SelectItem(null);
				mRepListView.UpdateRepStrings();
			}
            //mMouseVisible = !disabled;
            mDisabledTicks = 0;
        }

        public override void DrawAll(Graphics g)
        {
            base.DrawAll(g);         
        }

        public override void MouseClicked(float x, float y, float origX, float origY, int32 btn)
        {
            base.MouseClicked(x, y, origX, origY, btn);

            if (btn == 1) //
            {
                if (mBinaryDataWidget != null)
                    mBinaryDataWidget.ShowMenu(x, y);
            }
        }
    }
}
