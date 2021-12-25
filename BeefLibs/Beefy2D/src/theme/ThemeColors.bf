using System;

namespace Beefy.theme
{
	public class ThemeColors
	{
		public enum Types
		{
			// Generated via code
			Colors,
			Theme,
			ArgBuilder,
			AutoComplete,
			BinaryDataContent,
			CharData,
			CompositionDef,
			ConfigDataGroup,
			DarkTooltipManager,
			DistinctOptionBuilder,
			GraphicsBase,
			HiliteZone,
			HoverResolveTask,
			IDEUtils,
			JumpEntry,
			PendingWatch,
			PropEntry,
			ReplaceSymbolData,
			SourceEditBatchHelper,
			TrackedTextElement,
			WatchEntry,
			Widget,
			Panel

			// Template

			// Add new theme.toml headings here. Automatically processed by UISettings.Apply.LoadTheme
		}

		// And add them here as Types. There should be 1 for each Enum below in the same order as specified in the Enum
		// above

		static Type[] mTypes = new .(typeof(Colors), typeof(Theme), typeof(ArgBuilder), typeof(AutoComplete), typeof(BinaryDataContent), typeof(CharData), typeof(CompositionDef), typeof(ConfigDataGroup), typeof(DarkTooltipManager), typeof(DistinctOptionBuilder), typeof(GraphicsBase), typeof(HiliteZone), typeof(HoverResolveTask), typeof(IDEUtils), typeof(JumpEntry), typeof(PendingWatch), typeof(PropEntry), typeof(ReplaceSymbolData), typeof(SourceEditBatchHelper), typeof(TrackedTextElement), typeof(WatchEntry), typeof(Widget), typeof(Panel)) ~ delete _;

		// Generated via code
		public enum Colors
		{
			// Note: Do NOT change the order of these entries

			case Code;
			case Keyword;
			case Literal;
			case Identifier;
			case Comment;
			case Method;
			case Type;
			case PrimitiveType;
			case Struct;
			case GenericParam;
			case RefType;
			case Interface;
			case Namespace;
			case DisassemblyText;
			case DisassemblyFileName;
			case Error;
			case BuildError;
			case BuildWarning;
			case VisibleWhiteSpace;

			public uint32 Color
			{
				get
				{
					return sColors[(int)Types.Colors][(int)this];
				}
				set
				{
					sColors[(int)Types.Colors][(int)this] = value;
				}
			}
		}

		public enum Theme
		{
			case Text;
			case Window;
			case Background;
			case SelectedOutline;
			case MenuFocused;
			case MenuSelected;
			case AutoCompleteSubText;
			case AutoCompleteDocText;
			case AutoCompleteActiveText;
			case WorkspaceDisabledText;
			case WorkspaceFailedText;
			case WorkspaceManualIncludeText;
			case WorkspaceIgnoredText;

			public uint32 Color
			{
				get
				{
					return sColors[(int)Types.Theme][(int)this];
				}
				set
				{
					sColors[(int)Types.Theme][(int)this] = value;
				}
			}
		}

		// Generated via code

		public enum ArgBuilder
		{
			case ArgBuilder001;
			case ArgBuilder002;
			case ArgBuilder003;
			case ArgBuilder004;
			case ArgBuilder005;
			case ArgBuilder006;
			case ArgBuilder007;
			case ArgBuilder008;
			case ArgBuilder009;
			case ArgBuilder010;
			case ArgBuilder011;
			public uint32 Color { get { return sColors[(int)Types.ArgBuilder][(int)this]; } set { sColors[(int)Types.ArgBuilder][(int)this] = value; } } }


		public enum AutoComplete
		{
			case AutoComplete001;
			case AutoComplete002;
			case AutoComplete003;
			case AutoComplete004;
			case AutoComplete005;
			public uint32 Color { get { return sColors[(int)Types.AutoComplete][(int)this]; } set { sColors[(int)Types.AutoComplete][(int)this] = value; } } }


		public enum BinaryDataContent
		{
			case BinaryDataContent001;
			case BinaryDataContent002;
			case BinaryDataContent003;
			case BinaryDataContent004;
			case BinaryDataContent005;
			case BinaryDataContent006;
			case BinaryDataContent007;
			case BinaryDataContent008;
			case BinaryDataContent009;
			public uint32 Color { get { return sColors[(int)Types.BinaryDataContent][(int)this]; } set { sColors[(int)Types.BinaryDataContent][(int)this] = value; } } }


		public enum CharData
		{
			case CharData001;
			public uint32 Color { get { return sColors[(int)Types.CharData][(int)this]; } set { sColors[(int)Types.CharData][(int)this] = value; } } }


		public enum CompositionDef
		{
			case CompositionDef001;
			public uint32 Color { get { return sColors[(int)Types.CompositionDef][(int)this]; } set { sColors[(int)Types.CompositionDef][(int)this] = value; } } }


		public enum ConfigDataGroup
		{
			case ConfigDataGroup001;
			case ConfigDataGroup002;
			public uint32 Color { get { return sColors[(int)Types.ConfigDataGroup][(int)this]; } set { sColors[(int)Types.ConfigDataGroup][(int)this] = value; } } }


		public enum DarkTooltipManager
		{
			case DarkTooltipManager001;
			case DarkTooltipManager002;
			public uint32 Color { get { return sColors[(int)Types.DarkTooltipManager][(int)this]; } set { sColors[(int)Types.DarkTooltipManager][(int)this] = value; } } }


		public enum DistinctOptionBuilder
		{
			case DistinctOptionBuilder001;
			case DistinctOptionBuilder002;
			case DistinctOptionBuilder003;
			public uint32 Color { get { return sColors[(int)Types.DistinctOptionBuilder][(int)this]; } set { sColors[(int)Types.DistinctOptionBuilder][(int)this] = value; } } }


		public enum GraphicsBase
		{
			case GraphicsBase001;
			case GraphicsBase002;
			case GraphicsBase003;
			case GraphicsBase004;
			public uint32 Color { get { return sColors[(int)Types.GraphicsBase][(int)this]; } set { sColors[(int)Types.GraphicsBase][(int)this] = value; } } }


		public enum HiliteZone
		{
			case HiliteZone001;
			case HiliteZone002;
			case HiliteZone003;
			case HiliteZone004;
			case HiliteZone005;
			case HiliteZone006;
			case HiliteZone007;
			case HiliteZone008;
			case HiliteZone009;
			case HiliteZone010;
			case HiliteZone011;
			case HiliteZone012;
			case HiliteZone013;
			case HiliteZone014;
			case HiliteZone015;
			case HiliteZone016;
			public uint32 Color { get { return sColors[(int)Types.HiliteZone][(int)this]; } set { sColors[(int)Types.HiliteZone][(int)this] = value; } } }


		public enum HoverResolveTask
		{
			case HoverResolveTask001;
			case HoverResolveTask002;
			case HoverResolveTask003;
			case HoverResolveTask004;
			case HoverResolveTask005;
			case HoverResolveTask006;
			case HoverResolveTask007;
			case HoverResolveTask008;
			case HoverResolveTask009;
			case HoverResolveTask010;
			case HoverResolveTask011;
			public uint32 Color { get { return sColors[(int)Types.HoverResolveTask][(int)this]; } set { sColors[(int)Types.HoverResolveTask][(int)this] = value; } } }


		public enum IDEUtils
		{
			case IDEUtils001;
			case IDEUtils002;
			case IDEUtils003;
			case IDEUtils004;
			case IDEUtils005;
			case IDEUtils006;
			public uint32 Color { get { return sColors[(int)Types.IDEUtils][(int)this]; } set { sColors[(int)Types.IDEUtils][(int)this] = value; } } }


		public enum JumpEntry
		{
			case JumpEntry001;
			case JumpEntry002;
			case JumpEntry003;
			case JumpEntry004;
			case JumpEntry005;
			public uint32 Color { get { return sColors[(int)Types.JumpEntry][(int)this]; } set { sColors[(int)Types.JumpEntry][(int)this] = value; } } }


		public enum PendingWatch
		{
			case PendingWatch001;
			case PendingWatch002;
			case PendingWatch003;
			case PendingWatch004;
			case PendingWatch005;
			case PendingWatch006;
			public uint32 Color { get { return sColors[(int)Types.PendingWatch][(int)this]; } set { sColors[(int)Types.PendingWatch][(int)this] = value; } } }


		public enum PropEntry
		{
			case PropEntry001;
			case PropEntry002;
			case PropEntry003;
			case PropEntry004;
			case PropEntry005;
			case PropEntry006;
			case PropEntry007;
			case PropEntry008;
			public uint32 Color { get { return sColors[(int)Types.PropEntry][(int)this]; } set { sColors[(int)Types.PropEntry][(int)this] = value; } } }


		public enum ReplaceSymbolData
		{
			case ReplaceSymbolData001;
			case ReplaceSymbolData002;
			case ReplaceSymbolData003;
			case ReplaceSymbolData004;
			case ReplaceSymbolData005;
			public uint32 Color { get { return sColors[(int)Types.ReplaceSymbolData][(int)this]; } set { sColors[(int)Types.ReplaceSymbolData][(int)this] = value; } } }


		public enum SourceEditBatchHelper
		{
			/*
			case SourceEditBatchHelper001;
			case SourceEditBatchHelper002;
			case SourceEditBatchHelper003;
			case SourceEditBatchHelper004;
			case SourceEditBatchHelper005;
			case SourceEditBatchHelper006;
			case SourceEditBatchHelper007;
			case SourceEditBatchHelper008;
			case SourceEditBatchHelper009;
			case SourceEditBatchHelper011;
			case SourceEditBatchHelper012;
			case SourceEditBatchHelper013;
			*/
			case SourceEditBatchHelper010;
			case SourceEditBatchHelper014;
			case SourceEditBatchHelper015;
			case SourceEditBatchHelper016;
			case SourceEditBatchHelper017;
			case SourceEditBatchHelper018;
			case SourceEditBatchHelper019;
			case SourceEditBatchHelper020;
			public uint32 Color { get { return sColors[(int)Types.SourceEditBatchHelper][(int)this]; } set { sColors[(int)Types.SourceEditBatchHelper][(int)this] = value; } } }


		public enum TrackedTextElement
		{
			case Breakpoint001;
			case Breakpoint002;
			public uint32 Color { get { return sColors[(int)Types.TrackedTextElement][(int)this]; } set { sColors[(int)Types.TrackedTextElement][(int)this] = value; } } }


		public enum WatchEntry
		{
			case WatchEntry001;
			case WatchEntry002;
			case WatchEntry003;
			case WatchEntry004;
			case WatchEntry005;
			case WatchEntry006;
			case WatchEntry007;
			case WatchEntry008;
			case WatchEntry009;
			public uint32 Color { get { return sColors[(int)Types.WatchEntry][(int)this]; } set { sColors[(int)Types.WatchEntry][(int)this] = value; } } }


		public enum Widget
		{
			case DarkSmartEdit001;
			case DarkButton002;
			case DarkButton003;
			case OutputActionButton004;
			case OutputActionButton005;
			case DarkTabButtonClose006;
			case DarkTabButtonClose007;
			case DarkTabButtonClose008;
			case DarkTabButtonClose009;
			case DarkTreeOpenButton010;
			case DarkTreeOpenButton011;
			case DarkTreeOpenButton012;
			case DarkTreeOpenButton013;
			case DarkTreeOpenButton014;
			case DarkTreeOpenButton015;
			case DarkComboBox016;
			case SessionComboBox017;
			case OpenFileInSolutionDialog018;
			case OpenFileInSolutionDialog019;
			case DarkDockingFrame020;
			case CommentArea021;
			case TypeArea022;
			case TypeArea023;
			case DarkEditWidgetContent024;
			case DarkEditWidgetContent025;
			case DarkEditWidgetContent026;
			case OutputWidgetContent027;
			case ImmediateWidgetContent028;
			case ImmediateWidgetContent029;
			case DarkInfiniteScrollbar030;
			case FindListViewItem031;
			case FindListViewItem032;
			case ProfileListViewItem033;
			case ErrorsListViewItem034;
			case DarkMenuItem035;
			case DarkMenuItem036;
			case MemoryRepListView037;
			case Board038;
			case Board039;
			case Board040;
			case Board041;
			case StatusBar042;
			case StatusBar043;
			case StatusBar044;
			case StatusBar045;
			case StatusBar046;
			case StatusBar047;
			public uint32 Color { get { return sColors[(int)Types.Widget][(int)this]; } set { sColors[(int)Types.Widget][(int)this] = value; } } }


		public enum Panel
		{
			case WorkspaceProperties001;
			case WorkspaceProperties002;
			case WorkspaceProperties003;
			case WelcomePanel004;
			case WelcomePanel005;
			case WelcomePanel006;
			case WelcomePanel007;
			case TypeWildcardEditWidget008;
			case ProjectProperties009;
			case PanelPopup010;
			case PanelPopup011;
			case DiagnosticsPanel012;
			case DiagnosticsPanel013;
			case DiagnosticsPanel014;
			case DiagnosticsPanel015;
			case DiagnosticsPanel016;
			case DiagnosticsPanel017;
			case DiagnosticsPanel018;
			case DiagnosticsPanel019;
			public uint32 Color { get { return sColors[(int)Types.Panel][(int)this]; } set { sColors[(int)Types.Panel][(int)this] = value; } } }


							/* Template
							public enum Template
							{
								case placeHolder;
								//etc

								public uint32 Color
								{
									get
									{
										return sColors[(int)Types.Template][(int)this];
									}
									set
									{
										sColors[(int)Types.Template][(int)this] = value;
									}
								}
							}
							*/


		public static uint32[][] sColors ~ DeleteContainerAndItems!(_);
		public uint32[][] mColors ~ DeleteContainerAndItems!(_);

		static this()
		{
			sColors = new uint32[Enum.Count<Types>()][];

			for (int i = 0; i < Enum.Count<Types>(); i++)
			{
				Type type = GetType((Types)i);
				sColors[i] = new uint32[Enum.Count(type)];
			}
			SetDefaults();
		}

		public this()
		{
			mColors = new uint32[Enum.Count<Types>()][];

			for (int i = 0; i < Enum.Count<Types>(); i++)
			{
				Type type = GetType((Types)i);
				mColors[i] = new uint32[Enum.Count(type)];
				for (int j = 0; j < mColors[i].Count; j++)
				{
					mColors[i][j] = sColors[i][j];
				}
			}
		}

		public static void SetDefaults()
		{
			// Generated via code

			// Colors

			Colors.Code.Color = 0xFFFFFFFF;
			Colors.Keyword.Color = 0xFFE1AE9A;
			Colors.Literal.Color = 0XFFC8A0FF;
			Colors.Identifier.Color = 0xFFFFFFFF;
			Colors.Comment.Color = 0xFF75715E;
			Colors.Method.Color = 0xFFA6E22A;
			Colors.Type.Color = 0xFF66D9EF;
			Colors.PrimitiveType.Color = 0xFF66D9EF;
			Colors.Struct.Color = 0xFF66D9EF;
			Colors.GenericParam.Color = 0xFF66D9EF;
			Colors.RefType.Color = 0xFF66A0EF;
			Colors.Interface.Color = 0xFF9A9EEB;
			Colors.Namespace.Color = 0xFF7BEEB7;
			Colors.DisassemblyText.Color = 0xFFB0B0B0;
			Colors.DisassemblyFileName.Color = 0XFFFF0000;
			Colors.Error.Color = 0xFFFF0000;
			Colors.BuildError.Color = 0xFFFF8080;
			Colors.BuildWarning.Color = 0xFFFFFF80;
			Colors.VisibleWhiteSpace.Color = 0xFF9090C0;

			//Theme

			Theme.Text.Color = 0xFFFFFFFF;
			Theme.Window.Color = 0xFF44444D;
			Theme.Background.Color = 0xFF1C1C24;
			Theme.SelectedOutline.Color = 0xFFCFAE11;
			Theme.MenuFocused.Color = 0xFFE5A910;
			Theme.MenuSelected.Color = 0xFFCB9B80;
			Theme.AutoCompleteSubText.Color = 0xFFB0B0B0;
			Theme.AutoCompleteDocText.Color = 0xFFC0C0C0;
			Theme.AutoCompleteActiveText.Color = 0xFFB0B0FF;
			Theme.WorkspaceDisabledText.Color = 0xFFA0A0A0;
			Theme.WorkspaceFailedText.Color = 0xFFE04040;
			Theme.WorkspaceManualIncludeText.Color = 0xFFE0E0FF;
			Theme.WorkspaceIgnoredText.Color = 0xFF909090;


// ArgBuilder
			ArgBuilder.ArgBuilder001.Color = 0xFF404040;
			ArgBuilder.ArgBuilder002.Color = 0xFF202020;
			ArgBuilder.ArgBuilder003.Color = 0xFFFEF860;
			ArgBuilder.ArgBuilder004.Color = 0xFFFF8080;
			ArgBuilder.ArgBuilder005.Color = 0xFFF0F0F0;
			ArgBuilder.ArgBuilder006.Color = 0xFFFFFFFF;
			ArgBuilder.ArgBuilder007.Color = 0x80FFFFFF;
			ArgBuilder.ArgBuilder008.Color = 0xFFFFFFFF;
			ArgBuilder.ArgBuilder009.Color = 0xFF384858;
			ArgBuilder.ArgBuilder010.Color = 0x80384858;
			ArgBuilder.ArgBuilder011.Color = 0xFFC8C8C8;


// AutoComplete
			AutoComplete.AutoComplete001.Color = 0x80000000;
			AutoComplete.AutoComplete002.Color = 0xFFFFFFFF;
			AutoComplete.AutoComplete003.Color = 0x80FF0000;
			AutoComplete.AutoComplete004.Color = 0xFFC0C0C0;
			AutoComplete.AutoComplete005.Color = 0x20FF0000;


// BinaryDataContent
			BinaryDataContent.BinaryDataContent001.Color = 0xFFFFFFFF;
			BinaryDataContent.BinaryDataContent002.Color = 0x40808000;
			BinaryDataContent.BinaryDataContent003.Color = 0xFF60C0D0;
			BinaryDataContent.BinaryDataContent004.Color = 0xFFFFA000;
			BinaryDataContent.BinaryDataContent005.Color = 0xFFFFFFFF;
			BinaryDataContent.BinaryDataContent006.Color = 0xFFC0C0C0;
			BinaryDataContent.BinaryDataContent007.Color = 0x80A0A0A0;
			BinaryDataContent.BinaryDataContent008.Color = 0xFF2F5C88;
			BinaryDataContent.BinaryDataContent009.Color = 0x8000C000;


// CharData
			CharData.CharData001.Color = 0x7F7F7F7F;


// CompositionDef
			CompositionDef.CompositionDef001.Color = 0xFFFF0000;


// ConfigDataGroup
			ConfigDataGroup.ConfigDataGroup001.Color = 0x60FFFFFF;
			ConfigDataGroup.ConfigDataGroup002.Color = 0xFFFFFFFF;


// DarkTooltipManager
			DarkTooltipManager.DarkTooltipManager001.Color = 0x80000000;
			DarkTooltipManager.DarkTooltipManager002.Color = 0xFFFFFFFF;


// DistinctOptionBuilder
			DistinctOptionBuilder.DistinctOptionBuilder001.Color = 0xFFC0C0C0;
			DistinctOptionBuilder.DistinctOptionBuilder002.Color = 0xFFFFFFFF;
			DistinctOptionBuilder.DistinctOptionBuilder003.Color = 0xFFFF8080;


// GraphicsBase
			GraphicsBase.GraphicsBase001.Color = 0xFFFFFFFF;
			GraphicsBase.GraphicsBase002.Color = 0xFF00FF00;
			GraphicsBase.GraphicsBase003.Color = 0x00FF0000;
			GraphicsBase.GraphicsBase004.Color = 0x000000FF;


// HiliteZone
			HiliteZone.HiliteZone001.Color = 0x10FFFFFF;
			HiliteZone.HiliteZone002.Color = 0xA000FF00;
			HiliteZone.HiliteZone003.Color = 0xFF000000;
			HiliteZone.HiliteZone004.Color = 0x00FFFFFF;
			HiliteZone.HiliteZone005.Color = 0xE0000000;
			HiliteZone.HiliteZone006.Color = 0xFF00A000;
			HiliteZone.HiliteZone007.Color = 0x80A0A0A0;
			HiliteZone.HiliteZone008.Color = 0x80FFFFFF;
			HiliteZone.HiliteZone009.Color = 0xFFA0A0A0;
			HiliteZone.HiliteZone010.Color = 0xFFFFFFFF;
			HiliteZone.HiliteZone011.Color = 0xFF303030;
			HiliteZone.HiliteZone012.Color = 0x60A0A0A0;
			HiliteZone.HiliteZone013.Color = 0x60606060;
			HiliteZone.HiliteZone014.Color = 0x80404080;
			HiliteZone.HiliteZone015.Color = 0xFF202020;
			HiliteZone.HiliteZone016.Color = 0x00202020;


// HoverResolveTask
			HoverResolveTask.HoverResolveTask001.Color = 0xFFFF8080;
			HoverResolveTask.HoverResolveTask002.Color = 0xFF595959;
			HoverResolveTask.HoverResolveTask003.Color = 0xFFFFFFFF;
			HoverResolveTask.HoverResolveTask004.Color = 0xFFC0C0C0;
			HoverResolveTask.HoverResolveTask005.Color = 0x60FFFFFF;
			HoverResolveTask.HoverResolveTask006.Color = 0x80FFFFFF;
			HoverResolveTask.HoverResolveTask007.Color = 0x80FF0000;
			HoverResolveTask.HoverResolveTask008.Color = 0x50FFFFFF;
			HoverResolveTask.HoverResolveTask009.Color = 0x50D0D0D0;
			HoverResolveTask.HoverResolveTask010.Color = 0xFFFFFFFF;
			HoverResolveTask.HoverResolveTask011.Color = 0x8080FFFF;


// IDEUtils
			IDEUtils.IDEUtils001.Color = 0x00FF0000;
			IDEUtils.IDEUtils002.Color = 0xFFFFFFFF;
			IDEUtils.IDEUtils003.Color = 0x80000000;
			IDEUtils.IDEUtils004.Color = 0x7F7F7F7F;
			IDEUtils.IDEUtils005.Color = 0xFFA0A0A0;
			IDEUtils.IDEUtils006.Color = 0xFF80A080;


// JumpEntry
			JumpEntry.JumpEntry001.Color = 0xFFFFEAFE;
			JumpEntry.JumpEntry002.Color = 0xFF8A7489;
			JumpEntry.JumpEntry003.Color = 0xFFEAF5FF;
			JumpEntry.JumpEntry004.Color = 0xFF72828F;
			JumpEntry.JumpEntry005.Color = 0x80FFFFFF;


// PendingWatch
			PendingWatch.PendingWatch001.Color = 0xFFB0B0FF;
			PendingWatch.PendingWatch002.Color = 0xFFE0E0FF;
			PendingWatch.PendingWatch003.Color = 0x80000000;
			PendingWatch.PendingWatch004.Color = 0xFFFFFFFF;
			PendingWatch.PendingWatch005.Color = 0x40FF0000;
			PendingWatch.PendingWatch006.Color = 0x20000000;


// PropEntry
			PropEntry.PropEntry001.Color = 0xFFE8E8E8;
			PropEntry.PropEntry002.Color = 0xFFFFFFFF;
			PropEntry.PropEntry003.Color = 0x80FFFFFF;
			PropEntry.PropEntry004.Color = 0x20FFFFFF;
			PropEntry.PropEntry005.Color = 0xFFA0A0A0;
			PropEntry.PropEntry006.Color = 0xFF000000;
			PropEntry.PropEntry007.Color = 0xFFC0C0C0;
			PropEntry.PropEntry008.Color = 0x00FFFFFF;


// ReplaceSymbolData
			ReplaceSymbolData.ReplaceSymbolData001.Color = 0xFFFFFFFF;
			ReplaceSymbolData.ReplaceSymbolData002.Color = 0xFFF0B0B0;
			ReplaceSymbolData.ReplaceSymbolData003.Color = 0xFFE0E0E0;
			ReplaceSymbolData.ReplaceSymbolData004.Color = 0xFFFF7070;
			ReplaceSymbolData.ReplaceSymbolData005.Color = 0xFFFFB0B0;


// SourceEditBatchHelper
			/*
			SourceEditBatchHelper.SourceEditBatchHelper001.Color = 0xFFFFFFFF;
			SourceEditBatchHelper.SourceEditBatchHelper002.Color = 0xFFE1AE9A;
			SourceEditBatchHelper.SourceEditBatchHelper003.Color = 0xFF75715E;
			SourceEditBatchHelper.SourceEditBatchHelper004.Color = 0xFFA6E22A;
			SourceEditBatchHelper.SourceEditBatchHelper005.Color = 0xFF66D9EF;
			SourceEditBatchHelper.SourceEditBatchHelper006.Color = 0xFF66A0EF;
			SourceEditBatchHelper.SourceEditBatchHelper007.Color = 0xFF9A9EEB;
			SourceEditBatchHelper.SourceEditBatchHelper008.Color = 0xFF7BEEB7;
			SourceEditBatchHelper.SourceEditBatchHelper009.Color = 0xFFB0B0B0;
			SourceEditBatchHelper.SourceEditBatchHelper011.Color = 0xFFFF8080;
			SourceEditBatchHelper.SourceEditBatchHelper012.Color = 0xFFFFFF80;
			SourceEditBatchHelper.SourceEditBatchHelper013.Color = 0xFF9090C0;
			*/
			SourceEditBatchHelper.SourceEditBatchHelper010.Color = 0xFFFF0000;
			SourceEditBatchHelper.SourceEditBatchHelper014.Color = 0x28FFFFFF;
			SourceEditBatchHelper.SourceEditBatchHelper015.Color = 0x18FFFFFF;
			SourceEditBatchHelper.SourceEditBatchHelper016.Color = 0x80FFE0B0;
			SourceEditBatchHelper.SourceEditBatchHelper017.Color = 0x50D0C090;
			SourceEditBatchHelper.SourceEditBatchHelper018.Color = 0xFFFFD200;
			SourceEditBatchHelper.SourceEditBatchHelper019.Color = 0x80FFD200;
			SourceEditBatchHelper.SourceEditBatchHelper020.Color = 0x50FFE0B0;


// TrackedTextElement
			TrackedTextElement.Breakpoint001.Color = 0xFFFFFFFF;
			TrackedTextElement.Breakpoint002.Color = 0x8080FFFF;


// WatchEntry
			WatchEntry.WatchEntry001.Color = 0xFFFF4040;
			WatchEntry.WatchEntry002.Color = 0x60404040;
			WatchEntry.WatchEntry003.Color = 0xFF384858;
			WatchEntry.WatchEntry004.Color = 0x80384858;
			WatchEntry.WatchEntry005.Color = 0x50FFE0B0;
			WatchEntry.WatchEntry006.Color = 0x34FFE0B0;
			WatchEntry.WatchEntry007.Color = 0xD0FFFFFF;
			WatchEntry.WatchEntry008.Color = 0xFFFFFFFF;
			WatchEntry.WatchEntry009.Color = 0x80FFFFFF;


// Widget
			Widget.DarkSmartEdit001.Color = 0xFFCC9600;
			Widget.DarkButton002.Color = 0xFFFFFFFF;
			Widget.DarkButton003.Color = 0x80FFFFFF;
			Widget.OutputActionButton004.Color = 0x00FFFFFF;
			Widget.OutputActionButton005.Color = 0x60000000;
			Widget.DarkTabButtonClose006.Color = 0xFFFF0000;
			Widget.DarkTabButtonClose007.Color = 0x60FFFFFF;
			Widget.DarkTabButtonClose008.Color = 0xFFF7A900;
			Widget.DarkTabButtonClose009.Color = 0x80FF0000;
			Widget.DarkTreeOpenButton010.Color = 0x400000FF;
			Widget.DarkTreeOpenButton011.Color = 0xFF6F9761;
			Widget.DarkTreeOpenButton012.Color = 0xFF95A68F;
			Widget.DarkTreeOpenButton013.Color = 0x0CFFFFFF;
			Widget.DarkTreeOpenButton014.Color = 0xFF707070;
			Widget.DarkTreeOpenButton015.Color = 0xFF888888;
			Widget.DarkComboBox016.Color = 0x1FFF0000;
			Widget.SessionComboBox017.Color = 0xFFB0B0B0;
			Widget.OpenFileInSolutionDialog018.Color = 0xFF404040;
			Widget.OpenFileInSolutionDialog019.Color = 0xFF202020;
			Widget.DarkDockingFrame020.Color = 0x30FFFFFF;
			Widget.CommentArea021.Color = 0xFFC0C0C0;
			Widget.TypeArea022.Color = 0xFFFFFFFF;
			Widget.TypeArea023.Color = 0x60505050;
			Widget.DarkEditWidgetContent024.Color = 0xFF2F5C88;
			Widget.DarkEditWidgetContent025.Color = 0x4000FF00;
			Widget.DarkEditWidgetContent026.Color = 0x40FF0000;
			Widget.OutputWidgetContent027.Color = 0xFFFEBD57;
			Widget.ImmediateWidgetContent028.Color = 0xFFFF4040;
			Widget.ImmediateWidgetContent029.Color = 0xFF000000;
			Widget.DarkInfiniteScrollbar030.Color = 0x40000000;
			Widget.FindListViewItem031.Color = 0xFF808080;
			Widget.FindListViewItem032.Color = 0xFFFF8080;
			Widget.ProfileListViewItem033.Color = 0xA0595959;
			Widget.ErrorsListViewItem034.Color = 0xFFFFFF80;
			Widget.DarkMenuItem035.Color = 0xFFA8A8A8;
			Widget.DarkMenuItem036.Color = 0x80000000;
			Widget.MemoryRepListView037.Color = 0x80FFFF00;
			Widget.Board038.Color = 0xFF000000;
			Widget.Board039.Color = 0xD080F080;
			Widget.Board040.Color = 0xFF00FF00;
			Widget.Board041.Color = 0xFF0000FF;
			Widget.StatusBar042.Color = 0xFF800000;
			Widget.StatusBar043.Color = 0xFF35306A;
			Widget.StatusBar044.Color = 0xFFB65D08;
			Widget.StatusBar045.Color = 0xFF562143;
			Widget.StatusBar046.Color = 0xFF101010;
			Widget.StatusBar047.Color = 0x40202080;

// Panel
			Panel.WorkspaceProperties001.Color = 0xFFE8E8E8;
			Panel.WorkspaceProperties002.Color = 0xFFFFFFFF;
			Panel.WorkspaceProperties003.Color = 0xFFC0C0C0;
			Panel.WelcomePanel004.Color = 0x80FFFFFF;
			Panel.WelcomePanel005.Color = 0x40000000;
			Panel.WelcomePanel006.Color = 0xFFE0E0FF;
			Panel.WelcomePanel007.Color = 0xFFA0A0A0;
			Panel.TypeWildcardEditWidget008.Color = 0xFFFF8080;
			Panel.ProjectProperties009.Color = 0xFFFF6060;
			Panel.PanelPopup010.Color = 0x80FF0000;
			Panel.PanelPopup011.Color = 0x80000000;
			Panel.DiagnosticsPanel012.Color = 0x40FF0000;
			Panel.DiagnosticsPanel013.Color = 0xA0000000;
			Panel.DiagnosticsPanel014.Color = 0x40FFFFFF;
			Panel.DiagnosticsPanel015.Color = 0xFF00FF00;
			Panel.DiagnosticsPanel016.Color = 0x70FFFFFF;
			Panel.DiagnosticsPanel017.Color = 0xA0FFFFFF;
			Panel.DiagnosticsPanel018.Color = 0x60FFFFFF;
			Panel.DiagnosticsPanel019.Color = 0x20FFFFFF;


			//Template

			// Add new Default values here
		}

		public static void GetNames(ref StringView[] names)
		{
			Enum.GetNames<Types>(ref names);
		}

		public static void GetNames(StringView type, ref StringView[] names)
		{
			Types types = Enum.Parse<Types>(type);
			Enum.GetNames(GetType(types), ref names);
		}

		public static void GetNames(Types type, ref StringView[] names)
		{
			Enum.GetNames(GetType(type), ref names);
		}

		public void SetColor(Types type, int ix, uint32 color)
		{
			mColors[(int)type][ix] = color;
		}

		public void SetColor(Types types, StringView name, uint32 color)
		{
			int t = GetEnumValue(types, name);
			mColors[(int)types][t] = color;
		}

		public void SetColor(StringView type, int ix, uint32 color)
		{
			Types types = Enum.Parse<Types>(type);
			mColors[(int)types][ix] = color;
		}

		public void SetColor(int type, int ix, uint32 color)
		{
			mColors[type][ix] = color;
		}

		public void SetColors(Types types)
		{
			SetColors((int)types);
		}

		public void SetColors(StringView type)
		{
			Types types = Enum.Parse<Types>(type);
			SetColors((int)types);
		}

		public void SetColors(int type)
		{
			uint32[] colorFrom = mColors[type];
			uint32[] colorTo = sColors[type];

			for (int i = 0; i < colorFrom.Count; i++)
			{
				colorTo[i] = colorFrom[i];
			}
			//delete mColors[type];
		}

		public static void GetColor(StringView type, StringView name, ref uint32 color)
		{
			Types types = Enum.Parse<Types>(type);
			GetColor(types, name, ref color);
		}

		public static void GetColor(Types types, StringView name, ref uint32 color)
		{
			int i = GetEnumValue(types, name);
			color = sColors[(int)types][i];
		}

		public static Type GetType(Types types)
		{
			return mTypes[(int)types];
		}

		public static int GetEnumValue(Types types, StringView name)
		{
			Type t = GetType(types);
			int i = Enum.Parse(t, name);
			return i;
		}

	}
}
