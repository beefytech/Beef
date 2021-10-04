using System;

namespace Beefy.theme
{
	public class ThemeColors
	{
		public enum Types
		{
			// Generated via code
			Colors,
			Widget

			// Template

			// Add new theme.toml headings here
		}

		// And add them here as Types. There should be 1 for each Enum below in the same order as specified in the Enum above

		static Type[?] mTypes = .( typeof(Colors), typeof(Widget) );

		// Generated via code
		public enum Colors
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

		// Generated via code
		public enum Widget
		{
			case placeHolder;

			public uint32 Color
			{
				get
				{
					return sColors[(int)Types.Widget][(int)this];
				}
				set
				{
					sColors[(int)Types.Widget][(int)this] = value;
				}
			}
		}

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


		static uint32[][] sColors ~ DeleteContainerAndItems!(_);
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
			}
		}

		public static void SetDefaults()
		{
			// Generated via code

			// Colors
			Colors.Text.Color = 0xFFFFFFFF;
			Colors.Window.Color = 0xFF44444D;
			Colors.Background.Color = 0xFF1C1C24;
			Colors.SelectedOutline.Color = 0xFFCFAE11;
			Colors.MenuFocused.Color = 0xFFE5A910;
			Colors.MenuSelected.Color = 0xFFCB9B80;
			Colors.AutoCompleteSubText.Color = 0xFFB0B0B0;
			Colors.AutoCompleteDocText.Color = 0xFFC0C0C0;
			Colors.AutoCompleteActiveText.Color = 0xFFB0B0FF;
			Colors.WorkspaceDisabledText.Color = 0xFFA0A0A0;
			Colors.WorkspaceFailedText.Color = 0xFFE04040;
			Colors.WorkspaceManualIncludeText.Color = 0xFFE0E0FF;
			Colors.WorkspaceIgnoredText.Color = 0xFF909090;

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

			//Widget

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
			int i = Enum.Parse(t,name);
			return i;
		}

	}
}
