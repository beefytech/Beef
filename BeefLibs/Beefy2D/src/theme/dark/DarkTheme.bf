
using System;
using System.Collections;
using System.Text;
using Beefy.widgets;
using Beefy.gfx;

namespace Beefy.theme.dark
{    
    public class DarkTheme : ThemeFactory
    {
        public enum ImageIdx
        {
            Bkg,
            Window,
            Dots,
            RadioOn,
            RadioOff,
            MainBtnUp,
            MainBtnDown,
            BtnUp,
            BtnOver,
            BtnDown,
            Seperator,
            TabActive,
            TabActiveOver,
            TabInactive,
            TabInactiveOver,            
            EditBox,
            Checkbox,
            CheckboxOver,
            CheckboxDown,
            Check,
            Close,
            CloseOver,
            DownArrow,
            GlowDot,
            ArrowRight,
            WhiteCircle,
            DropMenuButton,
            ListViewHeader,
            ListViewSortArrow,
            Outline,
            Scrollbar,
            ScrollbarThumbOver,
            ScrollbarThumb,
            ScrollbarArrow,
            ShortButton,
            ShortButtonDown,
            VertScrollbar,
            VertScrollbarThumbOver,
            VertScrollbarThumb,
            VertScrollbarArrow,
            VertShortButton,
            VertShortButtonDown,
            Grabber,
            DropShadow,
            Menu,
            MenuSepVert,
            MenuSepHorz,
            MenuSelect,
            TreeArrow,

            UIPointer,
            UIImage,
            UIComposition,
            UILabel,
            UIButton,
            UIEdit,
            UICombobox,
            UICheckbox,
            UIRadioButton,
            UIListView,
            UITabView,

            EditCorners,
            EditCircle,
            EditPathNode,
            EditPathNodeSelected,
            EditAnchor,
            
            UIBone,
            UIBoneJoint,

            VisibleIcon,
            LockIcon,
            LeftArrow,
            KeyframeMakeOff,
            RightArrow,
            LeftArrowDisabled,
            KeyframeMakeOn,
            RightArrowDisabled,
            TimelineSelector,
            TimelineBracket,
            KeyframeOff,
            KeyframeOn,

            LinkedIcon,
            CheckboxLarge,
            ComboBox,
            ComboEnd,
            ComboSelectedIcon,

            LinePointer,
            RedDot,
            Document,
            ReturnPointer,
            RefreshArrows,
            MoveDownArrow,

            IconObject,
            IconObjectDeleted,
            IconObjectAppend,
            IconObjectStack,
            IconValue,
            IconPointer,
            IconType,
            IconError,
            IconBookmark,

            ProjectFolder,
            Project,
            ArrowMoveDown,
            Workspace,
            MemoryArrowSingle,
            MemoryArrowDoubleTop,
            MemoryArrowDoubleBottom,
            MemoryArrowTripleTop,
            MemoryArrowTripleMiddle,
            MemoryArrowTripleBottom,
            MemoryArrowRainbow,
            Namespace,
            ResizeGrabber,
            AsmArrow,
            AsmArrowRev,
            AsmArrowShadow,
            MenuNonFocusSelect,
            StepFilter,
            WaitSegment,
            FindCaseSensitive,
            FindWholeWord,

            RedDotUnbound,
            MoreInfo,

            Interface,
            Property,
            Field,
            Method,
            Variable,
            Constant,

            Type_ValueType,
            Type_Class,

			LinePointer_Prev,
			LinePointer_Opt,
			RedDotEx,
			RedDotExUnbound,
			RedDotDisabled,
			RedDotExDisabled,
			RedDotRunToCursor,

			GotoButton,
			YesJmp,
			NoJmp,
			WhiteBox,
			UpDownArrows,
			EventInfo,
			WaitBar,
			HiliteOutline,
			HiliteOutlineThin,

			IconPayloadEnum,
			StepFilteredDefault,

			ThreadBreakpointMatch,
			ThreadBreakpointNoMatch,
			ThreadBreakpointUnbound,
			Search,
			CheckIndeterminate,
			CodeError,
			CodeWarning,
			ComboBoxFrameless,

            COUNT
        };

        public static uint32 COLOR_WINDOW              = 0xFF595959;
        public static uint32 COLOR_BKG                 = 0xFF262626;
        public static uint32 COLOR_SELECTED_OUTLINE    = 0xFFE6A800;
        public static uint32 COLOR_MENU_FOCUSED        = 0xFFFFA000;
        public static uint32 COLOR_MENU_SELECTED       = 0xFFD0A070;

		public static float sScale = 1.0f;
		public static int32 sSrcImgScale = 1;
		public static int32 sSrcImgUnitSize = 20;
		public static int32 sUnitSize = 20;

        public static DarkTheme sDarkTheme ~ delete _;
        Image mThemeImage ~ delete _;
        public Image[] mImages = new Image[(int32) ImageIdx.COUNT] ~ delete _;

        public Font mHeaderFont ~ delete _;
        public Font mSmallFont ~ delete _;
        public Font mSmallBoldFont ~ delete _;
        public Image mTreeArrow ~ delete _;
		public Image mWindowTopImage ~ delete _;
        public Image mIconWarning ~ delete _;
        public Image mIconError ~ delete _;

        public static DesignToolboxEntry[] GetDesignToolboxEntries()
        {
            Get();

            DesignToolboxEntry [] entries = new DesignToolboxEntry [] 
            {
                new DesignToolboxEntry("ButtonWidget", typeof(DarkButton), sDarkTheme.mImages[(int32)ImageIdx.UIButton]),
                new DesignToolboxEntry("LabelWidget", null, sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.UILabel]),                
                new DesignToolboxEntry("EditWidget", typeof(DarkEditWidget), sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.UIEdit]),
                new DesignToolboxEntry("ComboBox", null, sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.UICombobox]),
                new DesignToolboxEntry("CheckBox", typeof(DarkCheckBox), sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.UICheckbox]),
                new DesignToolboxEntry("RadioButton", null, sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.UIRadioButton]),
                new DesignToolboxEntry("ListView", typeof(DarkListView), sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.UIListView]),
                new DesignToolboxEntry("TabView", typeof(DarkTabbedView), sDarkTheme.mImages[(int32)DarkTheme.ImageIdx.UITabView])
            };

            for (DesignToolboxEntry entry in entries)
                entry.mGroupName = "DarkTheme";
            return entries;
        }

        public static DarkTheme Get()
        {
            if (sDarkTheme != null)
                return sDarkTheme;

            sDarkTheme = new DarkTheme();
            sDarkTheme.Init();
            return sDarkTheme;
        }

		public static int GetScaled(int val)
		{
			return (int)(val * sScale);
		}

		public static float GetScaled(float val)
		{
			return (val * sScale);
		}

		public static void SetScale(float scale)
		{
			sScale = scale;
			sSrcImgScale = (int32)Math.Clamp((int)Math.Ceiling(scale), 1, 4);
			if (sSrcImgScale == 3)
				sSrcImgScale = 4;
			sSrcImgUnitSize = (int32)(20.0f * sSrcImgScale);
			sUnitSize = (int32)(sScale * 20);
			if (sDarkTheme != null)
				sDarkTheme.Rehup();
		}

		Image LoadSizedImage(StringView baseName)
		{
			var fileName = scope String();
			fileName.Append(BFApp.sApp.mInstallDir, "images/");
			fileName.Append(baseName);
			if (sSrcImgScale > 1)
				fileName.AppendF("_{0}", sSrcImgScale);
			fileName.Append(".png");
			var image = Image.LoadFromFile(fileName);
			image.Scale(GS!(48) / image.mWidth);
			return image;
		}

        public override void Init()
        {
            sDarkTheme = this;

			//SetScale(2);
			//String tempStr = scope String();

            /*mIconError = Image.LoadFromFile(StringAppend!(tempStr, BFApp.sApp.mInstallDir, "images/IconError.png"));
            mIconWarning = Image.LoadFromFile(StringAppend!(tempStr, BFApp.sApp.mInstallDir, "images/IconWarning.png"));*/

			for (int32 i = 0; i < (int32)ImageIdx.COUNT; i++)
			{
				mImages[i] = new Image();
			}

			mHeaderFont = new Font();
			mSmallFont = new Font();
			mSmallBoldFont = new Font();
			//SetScale(2.2f);
			SetScale(sScale);
        }

		public void Rehup()
		{
			String tempStr = scope String();

			if (mThemeImage != null)
			{
				delete mIconError;
				delete mIconWarning;
				delete mThemeImage;
				delete mTreeArrow;
				delete mWindowTopImage;
			}

			Image themeImg = Image.LoadFromFile(scope String()..Append(tempStr, BFApp.sApp.mInstallDir, "images/DarkTheme.png"), .AllowRead);
			defer delete themeImg;
			uint32[5] bits = ?;
			themeImg.GetBits(0, 0, bits.Count, 1, bits.Count, &bits);
			COLOR_WINDOW = Color.FromNative(bits[0]);
			COLOR_BKG = Color.FromNative(bits[1]);
			COLOR_SELECTED_OUTLINE = Color.FromNative(bits[2]);
			COLOR_MENU_FOCUSED = Color.FromNative(bits[3]);
			COLOR_MENU_SELECTED = Color.FromNative(bits[4]);

			String uiFileName = null;
			switch (sSrcImgScale)
			{
			case 1:
				uiFileName = "DarkUI.png";
			case 2:
				uiFileName = "DarkUI_2.png";
			case 4:
				uiFileName = "DarkUI_4.png";
			default:
				Runtime.FatalError("Invalid scale");
			}

			mIconError = LoadSizedImage("IconError");
			mIconWarning = LoadSizedImage("IconWarning");
			mThemeImage = Image.LoadFromFile(scope String()..Append(tempStr, BFApp.sApp.mInstallDir, "images/", uiFileName));

			for (int32 i = 0; i < (int32)ImageIdx.COUNT; i++)
			{
				var image = mImages[i];
				image.CreateImageSegment(mThemeImage, (i % 20) * sSrcImgUnitSize, (i / 20) * sSrcImgUnitSize, sSrcImgUnitSize, sSrcImgUnitSize);
				image.SetDrawSize(sUnitSize, sUnitSize);
			}

			// Trim off outside pixels
			mTreeArrow = mImages[(int32) ImageIdx.TreeArrow].CreateImageSegment(1, 1, DarkTheme.sSrcImgUnitSize - 2, DarkTheme.sSrcImgUnitSize - 2);

			mWindowTopImage = mImages[(int32)ImageIdx.Window].CreateImageSegment(2 * DarkTheme.sSrcImgScale, 2 * DarkTheme.sSrcImgScale, 16 * DarkTheme.sSrcImgScale, (int)Math.Ceiling(sScale));

			//mIconError.Scale(sScale);
			//mIconWarning.Scale(sScale);
			mTreeArrow.SetDrawSize((int)(18 * sScale), (int)(18 * sScale));

			mHeaderFont.Dispose(true);
			/*mHeaderFont.Load(StringAppend!(tempStr, BFApp.sApp.mInstallDir, "fonts/segoeui.ttf"), 11.7f * sScale); //8.8
			mHeaderFont.AddAlternate(scope String(BFApp.sApp.mInstallDir, "fonts/segoeui.ttf"), 11.7f * sScale);
			mHeaderFont.AddAlternate(scope String(BFApp.sApp.mInstallDir, "fonts/seguisym.ttf"), 11.7f * sScale);
			mHeaderFont.AddAlternate(scope String(BFApp.sApp.mInstallDir, "fonts/seguihis.ttf"), 11.7f * sScale);*/

			mHeaderFont.Load("Segoe UI", 11.7f * sScale); //8.8
			mHeaderFont.AddAlternate("Segoe UI Symbol", 11.7f * sScale).IgnoreError();
			mHeaderFont.AddAlternate("Segoe UI Historic", 11.7f * sScale).IgnoreError();
			mHeaderFont.AddAlternate("Segoe UI Emoji", 11.7f * sScale).IgnoreError();

			mSmallFont.Dispose(true);
			mSmallFont.Load("Segoe UI", 12.8f * sScale); // 10.0
			mSmallFont.AddAlternate("Segoe UI Symbol", 12.8f * sScale).IgnoreError();
			mSmallFont.AddAlternate("Segoe UI Historic", 12.8f * sScale).IgnoreError();
			mSmallFont.AddAlternate("Segoe UI Emoji", 12.8f * sScale).IgnoreError();

			mSmallBoldFont.Dispose(true);
			mSmallBoldFont.Dispose(true);
			mSmallBoldFont.Load("Segoe UI Bold", 12.8f * sScale); // 10.0
			mSmallBoldFont.AddAlternate("Segoe UI Symbol", 12.8f * sScale).IgnoreError();
			mSmallBoldFont.AddAlternate("Segoe UI Historic", 12.8f * sScale).IgnoreError();
			mSmallBoldFont.AddAlternate("Segoe UI Emoji", 12.8f * sScale).IgnoreError();
			/*mSmallBoldFont.Load(StringAppend!(tempStr, BFApp.sApp.mInstallDir, "fonts/segoeuib.ttf"), 12.8f * sScale); // 10.0
			mSmallBoldFont.AddAlternate(scope String(BFApp.sApp.mInstallDir, "fonts/segoeui.ttf"), 12.8f * sScale);
			mSmallBoldFont.AddAlternate(scope String(BFApp.sApp.mInstallDir, "fonts/seguisym.ttf"), 12.8f * sScale);
			mSmallBoldFont.AddAlternate(scope String(BFApp.sApp.mInstallDir, "fonts/seguihis.ttf"), 12.8f * sScale);*/
		}

		public override void Update()
		{
			base.Update();
			DarkTooltipManager.UpdateTooltip();
			DarkTooltipManager.UpdateMouseover();
		}

        public ~this()
        {
            for (var image in mImages)
                delete image;
        }

        public Image GetImage(ImageIdx idx)
        {
            return mImages[(int32)idx];
        }

        public override ButtonWidget CreateButton(Widget parent, String label, float x, float y, float width, float height)
        {
            DarkButton button = new DarkButton();
            button.Resize(x, y, width, height);
            button.Label = label;
            if (parent != null)
                parent.AddWidget(button);
            return button;
        }

        public override EditWidget CreateEditWidget(Widget parent, float x = 0, float y = 0, float width = 0, float height = 0)
        {
            DarkEditWidget editWidget = new DarkEditWidget();
            editWidget.Resize(x, y, width, height);            
            if (parent != null)
                parent.AddWidget(editWidget);
            return editWidget;
        }

        public override TabbedView CreateTabbedView(TabbedView.SharedData sharedData, Widget parent, float x, float y, float width, float height)
        {
            DarkTabbedView tabbedView = new DarkTabbedView(sharedData);
            tabbedView.Resize(x, y, width, height);
            if (parent != null)
                parent.AddWidget(tabbedView);
            return tabbedView;
        }

        public override DockingFrame CreateDockingFrame(DockingFrame parent)
        {
            DarkDockingFrame dockingFrame = new DarkDockingFrame();
            if (parent == null)
                dockingFrame.mWindowMargin = GS!(1);
			else if (var darkParent = parent as DarkDockingFrame)
			{
				dockingFrame.mDrawBkg = darkParent.mDrawBkg;
			}
            return dockingFrame;
        }

        public override ListView CreateListView()
        {
            return new DarkListView();
        }

        public override Scrollbar CreateScrollbar(Scrollbar.Orientation orientation)
        {
            DarkScrollbar scrollbar = new DarkScrollbar();
            scrollbar.mOrientation = orientation;
            return scrollbar;
        }

        public override InfiniteScrollbar CreateInfiniteScrollbar()
        {
            return new DarkInfiniteScrollbar();
        }

        public override CheckBox CreateCheckbox(Widget parent, float x = 0, float y = 0, float width = 0, float height = 0)
        {
            DarkCheckBox checkbox = new DarkCheckBox();
            checkbox.Resize(x, y, width, height);
            if (parent != null)
                parent.AddWidget(checkbox);
            return checkbox;
        }

        public override MenuWidget CreateMenuWidget(Menu menu)
        {
            return new DarkMenuWidget(menu);
        }

        public override Dialog CreateDialog(String title = null, String text = null, Image icon = null)
        {
            return new DarkDialog(title, text, icon);
        }

		public static bool CheckUnderlineKeyCode(StringView label, KeyCode keyCode)
		{
			int underlinePos = label.IndexOf('&');
			if (underlinePos == -1)
				return false;
			char32 underlineC = label.GetChar32(underlinePos + 1).0;
			underlineC = underlineC.ToUpper;
			return ((char32)keyCode == underlineC);
		}

		public static void DrawUnderlined(Graphics g, StringView str, float x, float y, FontAlign alignment = FontAlign.Left, float width = 0, Beefy.gfx.FontOverflowMode overflowMode = .Overflow)
		{
			int underlinePos = str.IndexOf('&');
			if ((underlinePos != -1) && (underlinePos < str.Length - 1))
			{
				String label = scope String();
				label.Append(str, 0, underlinePos);
				float underlineX = g.mFont.GetWidth(label);

				char32 underlineC = str.GetChar32(underlinePos + 1).0;
				float underlineWidth = g.mFont.GetWidth(underlineC);

				FontMetrics fm = .();
				label.Append(str, underlinePos + 1);
				g.DrawString(label, x, y, alignment, width, overflowMode, &fm);

				float drawX;
				switch (alignment)
				{
				case .Centered:
					drawX = x + underlineX + (width - fm.mMaxWidth) / 2;
				default:
					drawX = x + underlineX;
				}

				g.FillRect(drawX, y + g.mFont.GetAscent() + GS!(1), underlineWidth, (int)GS!(1.2f));
			}
			else
			{
				g.DrawString(str, x, y, alignment, width, overflowMode);
			}
		}
    }

	static
	{
		public static mixin GS(int32 val)
		{
			(int32)(val * DarkTheme.sScale)
		}

		public static mixin GS(int64 val)
		{
			(int64)(val * DarkTheme.sScale)
		}

		public static mixin GS(float val)
		{
			float fVal = val * DarkTheme.sScale;
			fVal
		}
	}
}
