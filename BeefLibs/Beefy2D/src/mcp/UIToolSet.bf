using System;
using System.Collections;
using System.Diagnostics;
using Beefy.utils;
using Beefy.widgets;
using Beefy.geom;
using Beefy.theme.dark;

namespace Beefy.mcp
{
	// Tools for seeing and driving any Beefy application's UI: the window list, the widget tree
	// with each widget's state (via Widget.Describe), screenshots, synthetic mouse and keyboard
	// input, and the popup surfaces -- tooltips, dialogs and menus -- that a test most often needs
	// to read or dismiss. Nothing here knows about the IDE; IDE tool sets build on it.
	//
	// Coordinates are in a window's widget space, which equals client pixels for every window that
	// does not scale its content. Widget ids come from get_ui_tree and find_widgets and stay valid
	// until the widget is destroyed; window ids likewise. Input goes through the same WidgetWindow
	// entry points the OS uses, so hover, focus, drag and keyboard-shortcut handling all run for
	// real.
	public class UIToolSet : MCPToolSet
	{
		// ---------------------------------------------------------------------------------------
		// Lookup
		// ---------------------------------------------------------------------------------------

		public static WidgetWindow FindWindow(int32 id)
		{
			for (var window in BFApp.sApp.mWindows)
			{
				if (window.mId != id)
					continue;
				return window as WidgetWindow;
			}
			return null;
		}

		public static WidgetWindow MainWindow()
		{
			WidgetWindow first = null;
			for (var window in BFApp.sApp.mWindows)
			{
				var widgetWindow = window as WidgetWindow;
				if (widgetWindow == null)
					continue;
				if (widgetWindow.mIsMainWindow)
					return widgetWindow;
				if ((first == null) && (widgetWindow.mParent == null))
					first = widgetWindow;
			}
			return first;
		}

		// The widgets logically under a widget: its tracked children plus, for list view items, the
		// sub-item cells and child rows, which ListView attaches untracked (not in mChildWidgets)
		public static void GetChildren(Widget widget, List<Widget> outChildren)
		{
			if (widget.mChildWidgets != null)
			{
				for (var child in widget.mChildWidgets)
					outChildren.Add(child);
			}
			if (var listViewItem = widget as ListViewItem)
			{
				if (listViewItem.mSubItems != null)
				{
					// Sub-items and child items may also be tracked in mChildWidgets; list each once
					for (var subItem in listViewItem.mSubItems)
					{
						if ((subItem != null) && (subItem != widget) && (subItem.mParent == widget) && (!outChildren.Contains(subItem)))
							outChildren.Add(subItem);
					}
				}
				if (listViewItem.mChildItems != null)
				{
					for (var childItem in listViewItem.mChildItems)
					{
						if ((childItem.mParent == widget) && (!outChildren.Contains(childItem)))
							outChildren.Add(childItem);
					}
				}
			}
		}

		static Widget FindWidgetIn(Widget widget, int32 id)
		{
			if (widget.mWidgetId == id)
				return widget;
			List<Widget> children = scope .();
			GetChildren(widget, children);
			for (var child in children)
			{
				var found = FindWidgetIn(child, id);
				if (found != null)
					return found;
			}
			return null;
		}

		public static Widget FindWidget(int32 id)
		{
			for (var window in BFApp.sApp.mWindows)
			{
				var widgetWindow = window as WidgetWindow;
				if ((widgetWindow == null) || (widgetWindow.mRootWidget == null))
					continue;
				var found = FindWidgetIn(widgetWindow.mRootWidget, id);
				if (found != null)
					return found;
			}
			return null;
		}

		// The topmost window whose root widget is of type T (dialogs and menus are their own windows)
		static WidgetWindow FindTopmostWindowWithRoot<T>() where T : Widget
		{
			for (int idx = BFApp.sApp.mWindows.Count - 1; idx >= 0; idx--)
			{
				var widgetWindow = BFApp.sApp.mWindows[idx] as WidgetWindow;
				if ((widgetWindow == null) || (!widgetWindow.mVisible))
					continue;
				if (widgetWindow.mRootWidget is T)
					return widgetWindow;
			}
			return null;
		}

		static void GetTypeName(Object obj, String outName)
		{
			obj.GetType().GetName(outName);
		}

		// Resolves the window and point a tool acts on. A widget id targets that widget (x/y are
		// then offsets from its top-left, defaulting to its center); otherwise x/y are window
		// coordinates in the given or main window, defaulting to the current mouse position.
		bool ResolveTarget(MCPCall call, bool requirePos, out WidgetWindow window, out float x, out float y)
		{
			window = null;
			x = 0;
			y = 0;

			if (call.HasArg("widget"))
			{
				var widget = FindWidget((int32)call.GetInt("widget"));
				if (widget == null)
				{
					call.Error("No widget with that id. Ids come from get_ui_tree or find_widgets and go stale when the UI is rebuilt; call get_ui_tree again.");
					return false;
				}
				window = widget.mWidgetWindow;
				if (window == null)
				{
					call.Error("That widget is not attached to a window");
					return false;
				}
				float localX = call.HasArg("x") ? (float)call.GetFloat("x") : widget.mWidth / 2;
				float localY = call.HasArg("y") ? (float)call.GetFloat("y") : widget.mHeight / 2;
				widget.SelfToRootTranslate(localX, localY, out x, out y);
				return true;
			}

			if (call.HasArg("window"))
			{
				window = FindWindow((int32)call.GetInt("window"));
				if (window == null)
				{
					call.Error("No window with that id. Call get_windows for the current list.");
					return false;
				}
			}
			else
			{
				window = MainWindow();
				if (window == null)
				{
					call.Error("No window is open");
					return false;
				}
			}

			if ((requirePos) && ((!call.HasArg("x")) || (!call.HasArg("y"))))
			{
				call.Error("Pass x and y (window coordinates), or a widget id");
				return false;
			}
			x = (float)call.GetFloat("x", window.mMouseX);
			y = (float)call.GetFloat("y", window.mMouseY);
			return true;
		}

		// ---------------------------------------------------------------------------------------
		// Describing
		// ---------------------------------------------------------------------------------------

		static void AppendWindowInfo(StructuredData sd, WidgetWindow window)
		{
			sd.Add("id", window.mId);
			sd.Add("title", window.mTitle ?? "");
			if (window.mIsMainWindow)
				sd.Add("main", true);
			if (!window.mVisible)
				sd.Add("hidden", true);
			if (window.mHasFocus)
				sd.Add("focus", true);
			if (window.mParent != null)
				sd.Add("parent", window.mParent.mId);
			if (window.mRootWidget != null)
			{
				String rootType = scope .();
				GetTypeName(window.mRootWidget, rootType);
				sd.Add("kind", rootType);
				sd.Add("rootWidget", window.mRootWidget.mWidgetId);
			}
			if (window.mWindowFlags.HasFlag(.Tooltip))
				sd.Add("tooltip", true);
			if (window.mWindowFlags.HasFlag(.Modal))
				sd.Add("modal", true);

			sd.Add("x", window.mClientX);
			sd.Add("y", window.mClientY);
			sd.Add("w", window.mClientWidth);
			sd.Add("h", window.mClientHeight);
			if (window.mScaleMatrix.a != 1.0f)
				sd.Add("scale", window.mScaleMatrix.a);

			sd.Add("mouseX", (int)window.mMouseX);
			sd.Add("mouseY", (int)window.mMouseY);
			if (window.mHasMouseInside)
				sd.Add("mouseInside", true);
			if (window.mFocusWidget != null)
				sd.Add("focusWidget", window.mFocusWidget.mWidgetId);
			if (window.mOverWidget != null)
				sd.Add("overWidget", window.mOverWidget.mWidgetId);
			if (window.mCaptureWidget != null)
				sd.Add("captureWidget", window.mCaptureWidget.mWidgetId);
		}

		class TreeWalk
		{
			public int mNodeCount;
			public int mMaxNodes = 1500;
			public bool mIncludeHidden;
			public bool mTruncated;
		}

		static void AppendWidgetNode(StructuredData sd, Widget widget, int depth, TreeWalk walk)
		{
			walk.mNodeCount++;

			sd.Add("id", widget.mWidgetId);
			String typeName = scope .();
			GetTypeName(widget, typeName);
			sd.Add("type", typeName);
			if (widget.mIdStr != null)
				sd.Add("name", widget.mIdStr);

			widget.SelfToRootTranslate(0, 0, var rootX, var rootY);
			sd.Add("x", (int)Math.Round(rootX));
			sd.Add("y", (int)Math.Round(rootY));
			sd.Add("w", (int)Math.Round(widget.mWidth));
			sd.Add("h", (int)Math.Round(widget.mHeight));
			if (!widget.mVisible)
				sd.Add("hidden", true);
			if (widget.mHasFocus)
				sd.Add("focus", true);
			if (widget.mMouseOver)
				sd.Add("hover", true);

			widget.Describe(sd);

			if (depth <= 0)
				return;

			List<Widget> children = scope .();
			GetChildren(widget, children);
			bool hasChild = false;
			for (var child in children)
			{
				if ((walk.mIncludeHidden) || (child.mVisible))
				{
					hasChild = true;
					break;
				}
			}
			if (!hasChild)
				return;

			using (sd.CreateArray("children"))
			{
				for (var child in children)
				{
					if ((!walk.mIncludeHidden) && (!child.mVisible))
						continue;
					if (walk.mNodeCount >= walk.mMaxNodes)
					{
						walk.mTruncated = true;
						break;
					}
					using (sd.CreateObject())
						AppendWidgetNode(sd, child, depth - 1, walk);
				}
			}
		}

		// Everything Describe says about a widget, flattened to text, for substring matching
		static void GetSearchText(Widget widget, String outText)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			widget.Describe(sd);
			if (widget.mIdStr != null)
				sd.Add("name", widget.mIdStr);
			sd.ToJSON(outText);
		}

		// ---------------------------------------------------------------------------------------
		// Input helpers
		// ---------------------------------------------------------------------------------------

		static bool ParseModifier(StringView token, out KeyCode keyCode)
		{
			keyCode = default;
			if ((token.Equals("ctrl", true)) || (token.Equals("control", true)))
				keyCode = .Control;
			else if (token.Equals("shift", true))
				keyCode = .Shift;
			else if ((token.Equals("alt", true)) || (token.Equals("menu", true)))
				keyCode = .Alt;
			else if ((token.Equals("win", true)) || (token.Equals("cmd", true)) || (token.Equals("meta", true)))
				keyCode = .LWin;
			else
				return false;
			return true;
		}

		// "Ctrl+Shift+B", "F5", "Enter", "a". Fills the key, the modifiers to hold, and the character
		// the key would produce (0 when it produces none).
		static bool ParseKeyChord(StringView chord, List<KeyCode> modifiers, out KeyCode key, out char32 keyChar, String error)
		{
			key = default;
			keyChar = (char32)0;
			bool hasKey = false;

			for (var token in chord.Split('+'))
			{
				var trimmed = token;
				trimmed.Trim();
				if (trimmed.IsEmpty)
					continue;

				if (ParseModifier(trimmed, var modifier))
				{
					modifiers.Add(modifier);
					continue;
				}

				if (hasKey)
				{
					error.AppendF("More than one non-modifier key in '{}'", chord);
					return false;
				}
				hasKey = true;

				if (trimmed.Length == 1)
				{
					char8 c = trimmed[0];
					if (c.IsLetter)
					{
						key = (KeyCode)(uint8)c.ToUpper;
						keyChar = modifiers.Contains(.Shift) ? c.ToUpper : c.ToLower;
					}
					else if (c.IsDigit)
					{
						key = (KeyCode)(uint8)c;
						keyChar = c;
						if (modifiers.Contains(.Shift))
							keyChar = ")!@#$%^&*("[(int)(c - '0')];
					}
					else
					{
						switch (c)
						{
						case ' ': key = .Space;
						case '-': key = .Minus;
						case '=': key = (KeyCode)0xBB; // KeyCode.Equals, spelled numerically to avoid the Object.Equals overload
						case ',': key = .Comma;
						case '.': key = .Period;
						case '/': key = .Slash;
						case ';': key = .Semicolon;
						case '[': key = .LBracket;
						case ']': key = .RBracket;
						case '\\': key = .Backslash;
						case '\'': key = .Apostrophe;
						case '`': key = .Tilde;
						default:
							error.AppendF("Cannot map '{}' to a key; use type_text for arbitrary characters", c);
							return false;
						}
						keyChar = c;
					}
					continue;
				}

				// The OS delivers a character for these as well as the key down; edit widgets act on the
				// character (Enter inserts a line, Backspace deletes, Tab indents, Escape is ignored)
				if ((trimmed.Equals("enter", true)) || (trimmed.Equals("return", true)))
				{
					key = .Return;
					keyChar = (char32)'\r';
				}
				else if ((trimmed.Equals("esc", true)) || (trimmed.Equals("escape", true)))
				{
					key = .Escape;
					keyChar = (char32)'\x1B';
				}
				else if ((trimmed.Equals("backspace", true)) || (trimmed.Equals("bksp", true)))
				{
					key = .Backspace;
					keyChar = (char32)'\b';
				}
				else if ((trimmed.Equals("del", true)) || (trimmed.Equals("delete", true)))
					key = .Delete;
				else if ((trimmed.Equals("ins", true)) || (trimmed.Equals("insert", true)))
					key = .Insert;
				else if ((trimmed.Equals("pgup", true)) || (trimmed.Equals("pageup", true)))
					key = .PageUp;
				else if ((trimmed.Equals("pgdn", true)) || (trimmed.Equals("pgdown", true)) || (trimmed.Equals("pagedown", true)))
					key = .PageDown;
				else if (trimmed.Equals("space", true))
				{
					key = .Space;
					keyChar = ' ';
				}
				else if (trimmed.Equals("tab", true))
				{
					key = .Tab;
					keyChar = (char32)'\t';
				}
				else if (Enum.Parse<KeyCode>(trimmed, true) case .Ok(let parsed))
					key = parsed;
				else
				{
					error.AppendF("Unknown key '{}'", trimmed);
					return false;
				}
			}

			// With Ctrl, Alt or Win held the OS types no character, except Ctrl+Backspace which arrives as DEL
			bool hasCtrl = modifiers.Contains(.Control);
			bool hasAltOrWin = (modifiers.Contains(.Alt)) || (modifiers.Contains(.LWin));
			if ((hasCtrl) && (!hasAltOrWin) && (key == .Backspace))
				keyChar = (char32)'\x7F';
			else if ((hasCtrl) || (hasAltOrWin))
				keyChar = (char32)0;

			if (!hasKey)
			{
				error.Append("No key given");
				return false;
			}
			return true;
		}

		static void EnsureFocus(WidgetWindow window)
		{
			if (!window.mHasFocus)
				window.GotFocus();
		}

		static void SetModifiers(WidgetWindow window, List<KeyCode> modifiers, bool down)
		{
			if (down)
			{
				for (var modifier in modifiers)
					window.KeyDown((int32)modifier, 0);
			}
			else
			{
				for (int idx = modifiers.Count - 1; idx >= 0; idx--)
					window.KeyUp((int32)modifiers[idx]);
			}
		}

		// The sequence the OS delivers for a key press: key down (where shortcuts fire), the
		// character it types unless a shortcut consumed it or Ctrl/Alt is held, then key up
		static void SendKey(WidgetWindow window, List<KeyCode> modifiers, KeyCode key, char32 keyChar)
		{
			EnsureFocus(window);
			SetModifiers(window, modifiers, true);
			window.KeyDown((int32)key, 0);
			if (keyChar != (char32)0)
				window.KeyChar(keyChar); // WidgetWindow drops this itself if the key down was handled
			window.KeyUp((int32)key);
			SetModifiers(window, modifiers, false);
		}

		static void DoClick(WidgetWindow window, int32 deviceX, int32 deviceY, int32 button, int32 count, List<KeyCode> modifiers)
		{
			SetModifiers(window, modifiers, true);
			window.MouseMove(deviceX, deviceY);
			for (int32 clickIdx < count)
			{
				window.MouseDown(deviceX, deviceY, button, clickIdx + 1);
				window.MouseUp(deviceX, deviceY, button);
			}
			SetModifiers(window, modifiers, false);
		}

		void ReadModifiers(MCPCall call, List<KeyCode> modifiers)
		{
			String modifiersStr = scope .();
			call.GetString("modifiers", modifiersStr);
			for (var token in modifiersStr.Split('+'))
			{
				var trimmed = token;
				trimmed.Trim();
				if (trimmed.IsEmpty)
					continue;
				if (ParseModifier(trimmed, var modifier))
					modifiers.Add(modifier);
			}
		}

		// Reports where the pointer ended up, and what is under it, after an input tool runs
		static void AppendPointerResult(StructuredData sd, WidgetWindow window)
		{
			sd.Add("window", window.mId);
			sd.Add("x", (int)window.mMouseX);
			sd.Add("y", (int)window.mMouseY);
			var over = window.mCaptureWidget ?? window.mOverWidget;
			if (over != null)
			{
				sd.Add("overWidget", over.mWidgetId);
				String typeName = scope .();
				GetTypeName(over, typeName);
				sd.Add("overType", typeName);
			}
			if (window.mFocusWidget != null)
				sd.Add("focusWidget", window.mFocusWidget.mWidgetId);
		}

		static void Finish(MCPCall call, StructuredData sd)
		{
			String json = scope .();
			sd.ToJSON(json);
			call.Result(json);
		}

		// ---------------------------------------------------------------------------------------
		// Tools: seeing
		// ---------------------------------------------------------------------------------------

		[MCPTool("get_windows", "List the application's windows: the main window plus any dialogs, popup menus, tooltips and floating panels, each with its id, title, kind (root widget type), screen rect, focus, and where the mouse is in it. Window ids are what the other UI tools take.")]
		void GetWindows(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			var mainWindow = MainWindow();
			if (mainWindow != null)
				sd.Add("mainWindow", mainWindow.mId);
			using (sd.CreateArray("windows"))
			{
				for (var window in BFApp.sApp.mWindows)
				{
					var widgetWindow = window as WidgetWindow;
					if (widgetWindow == null)
						continue;
					using (sd.CreateObject())
						AppendWindowInfo(sd, widgetWindow);
				}
			}
			Finish(call, sd);
		}

		[MCPTool("get_ui_tree", "The widget tree of a window (or of one widget), with each widget's id, type, rect in window coordinates, visibility, focus, hover, and its own state: labels, text, checked, selected, open, scroll position and so on. This is how to find out what is on screen and what to click. Large trees are cut off at max_nodes; narrow with root and depth.",
			"{'type':'object','properties':{'window':{'type':'integer','description':'Window id from get_windows. Default: every window.'},'root':{'type':'integer','description':'Widget id to start from instead of a window root.'},'depth':{'type':'integer','description':'How many levels below the root to include. Default 6.'},'hidden':{'type':'boolean','description':'Include invisible widgets. Default false.'},'max_nodes':{'type':'integer','description':'Cap on widgets returned. Default 1500.'}}}")]
		void GetUITree(MCPCall call)
		{
			var walk = scope TreeWalk();
			walk.mIncludeHidden = call.GetBool("hidden", false);
			walk.mMaxNodes = (int)call.GetInt("max_nodes", 1500);
			int depth = (int)call.GetInt("depth", 6);

			var sd = scope StructuredData();
			sd.CreateNew();

			if (call.HasArg("root"))
			{
				var widget = FindWidget((int32)call.GetInt("root"));
				if (widget == null)
				{
					call.Error("No widget with that id. Call get_ui_tree without root, or find_widgets, to get current ids.");
					return;
				}
				if (widget.mWidgetWindow != null)
					sd.Add("window", widget.mWidgetWindow.mId);
				using (sd.CreateObject("root"))
					AppendWidgetNode(sd, widget, depth, walk);
			}
			else
			{
				using (sd.CreateArray("windows"))
				{
					for (var window in BFApp.sApp.mWindows)
					{
						var widgetWindow = window as WidgetWindow;
						if ((widgetWindow == null) || (widgetWindow.mRootWidget == null))
							continue;
						if ((call.HasArg("window")) && (widgetWindow.mId != (int32)call.GetInt("window")))
							continue;
						if ((!walk.mIncludeHidden) && (!widgetWindow.mVisible))
							continue;
						using (sd.CreateObject())
						{
							sd.Add("id", widgetWindow.mId);
							sd.Add("title", widgetWindow.mTitle ?? "");
							using (sd.CreateObject("root"))
								AppendWidgetNode(sd, widgetWindow.mRootWidget, depth, walk);
						}
					}
				}
			}

			sd.Add("nodes", walk.mNodeCount);
			if (walk.mTruncated)
				sd.Add("truncated", true);
			Finish(call, sd);
		}

		[MCPTool("find_widgets", "Find widgets by the text they show (label, title, contents) and/or their type name, across all windows or one. Returns each match with its id, type, rect and state, plus its parent id. Use it to locate a button, tab, tree item or field before clicking it.",
			"{'type':'object','properties':{'text':{'type':'string','description':'Case-insensitive substring to look for in the widget state (label, text, title...).'},'type':{'type':'string','description':'Case-insensitive substring of the widget type name, e.g. Button, TabButton, ListViewItem, EditWidget.'},'window':{'type':'integer','description':'Restrict to one window id.'},'hidden':{'type':'boolean','description':'Include invisible widgets. Default false.'},'max':{'type':'integer','description':'Maximum matches. Default 50.'}}}")]
		void FindWidgets(MCPCall call)
		{
			String textFilter = scope .();
			call.GetString("text", textFilter);
			textFilter.ToLower();
			String typeFilter = scope .();
			call.GetString("type", typeFilter);
			typeFilter.ToLower();
			if ((textFilter.IsEmpty) && (typeFilter.IsEmpty))
			{
				call.Error("Pass text and/or type");
				return;
			}
			bool includeHidden = call.GetBool("hidden", false);
			int maxResults = (int)call.GetInt("max", 50);
			int32 windowFilter = call.HasArg("window") ? (int32)call.GetInt("window") : -1;

			var sd = scope StructuredData();
			sd.CreateNew();
			int matchCount = 0;
			int returned = 0;

			void Visit(Widget widget)
			{
				if ((!includeHidden) && (!widget.mVisible))
					return;

				bool matches = true;
				if (!typeFilter.IsEmpty)
				{
					String typeName = scope .();
					GetTypeName(widget, typeName);
					typeName.ToLower();
					matches = typeName.Contains(typeFilter);
				}
				if ((matches) && (!textFilter.IsEmpty))
				{
					String searchText = scope .();
					GetSearchText(widget, searchText);
					searchText.ToLower();
					matches = searchText.Contains(textFilter);
				}

				if (matches)
				{
					matchCount++;
					if (returned < maxResults)
					{
						returned++;
						using (sd.CreateObject())
						{
							var walk = scope TreeWalk();
							AppendWidgetNode(sd, widget, 0, walk);
							if (widget.mParent != null)
								sd.Add("parent", widget.mParent.mWidgetId);
							if (widget.mWidgetWindow != null)
								sd.Add("window", widget.mWidgetWindow.mId);
						}
					}
				}

				List<Widget> children = scope .();
				GetChildren(widget, children);
				for (var child in children)
					Visit(child);
			}

			using (sd.CreateArray("matches"))
			{
				for (var window in BFApp.sApp.mWindows)
				{
					var widgetWindow = window as WidgetWindow;
					if ((widgetWindow == null) || (widgetWindow.mRootWidget == null))
						continue;
					if ((windowFilter != -1) && (widgetWindow.mId != windowFilter))
						continue;
					if ((!includeHidden) && (!widgetWindow.mVisible))
						continue;
					Visit(widgetWindow.mRootWidget);
				}
			}
			sd.Add("total", matchCount);
			Finish(call, sd);
		}

		[MCPTool("screenshot", "Capture the UI as a PNG image you can look at. By default the main window's client area is rendered offscreen (works even if the window is covered or minimized); pass all=true to composite every window at its screen position, so popup menus, tooltips and dialogs appear where they really are; or widget to capture one widget cropped to its rect. Large images are scaled down to max_size on the longest side; pixel coordinates in the reply are before scaling.",
			"{'type':'object','properties':{'window':{'type':'integer','description':'Window id from get_windows. Default: the main window.'},'widget':{'type':'integer','description':'Widget id to capture instead of a window.'},'all':{'type':'boolean','description':'Composite every visible window into one image. Default false.'},'screen':{'type':'boolean','description':'Read the pixels the OS is actually showing for the window instead of rendering offscreen. Needs the window on screen. Default false.'},'scale':{'type':'number','description':'Explicit downscale factor, 0 < scale <= 1. Overrides max_size.'},'max_size':{'type':'integer','description':'Longest side in pixels before the image is scaled down. Default 1600.'},'save_path':{'type':'string','description':'Also write the (unscaled) PNG to this file path.'}}}")]
		void Screenshot(MCPCall call)
		{
			UICapture.Bitmap bitmap = null;
			int32 originX = 0;
			int32 originY = 0;
			WidgetWindow window = null;
			Widget widget = null;

			if (call.HasArg("widget"))
			{
				widget = FindWidget((int32)call.GetInt("widget"));
				if (widget == null)
				{
					call.Error("No widget with that id");
					return;
				}
				window = widget.mWidgetWindow;
				bitmap = UICapture.CaptureWidget(widget);
				if ((bitmap != null) && (window != null))
				{
					widget.SelfToRootTranslate(0, 0, var rootX, var rootY);
					UICapture.ToDevice(window, rootX, rootY, var deviceX, var deviceY);
					originX = window.mClientX + deviceX;
					originY = window.mClientY + deviceY;
				}
			}
			else if (call.GetBool("all", false))
			{
				bitmap = UICapture.CaptureAll(out originX, out originY);
			}
			else
			{
				if (!ResolveTarget(call, false, out window, var x, var y))
					return;
				bitmap = call.GetBool("screen", false) ? UICapture.CaptureScreen(window) : UICapture.CaptureWindow(window);
				originX = window.mClientX;
				originY = window.mClientY;
			}

			if (bitmap == null)
			{
				call.Error("Nothing to capture: the target has no size, or the window is not on screen (for screen=true)");
				return;
			}
			defer delete bitmap;

			String savePath = scope .();
			call.GetString("save_path", savePath);
			bool saved = false;
			if (!savePath.IsEmpty)
				saved = UICapture.WritePNG(bitmap, savePath);

			int32 fullWidth = bitmap.mWidth;
			int32 fullHeight = bitmap.mHeight;
			float scale = (float)call.GetFloat("scale", 0);
			if ((scale <= 0) || (scale > 1))
			{
				int32 maxSize = (int32)call.GetInt("max_size", 1600);
				int32 longest = Math.Max(bitmap.mWidth, bitmap.mHeight);
				scale = (longest > maxSize) ? (maxSize / (float)longest) : 1.0f;
			}
			if (scale < 1)
				UICapture.Downscale(bitmap, scale);

			List<uint8> pngData = scope .();
			if (!UICapture.EncodePNG(bitmap, pngData))
			{
				call.Error("PNG encoding failed");
				return;
			}

			var sd = scope StructuredData();
			sd.CreateNew();
			if (window != null)
				sd.Add("window", window.mId);
			if (widget != null)
				sd.Add("widget", widget.mWidgetId);
			sd.Add("width", fullWidth);
			sd.Add("height", fullHeight);
			sd.Add("imageWidth", bitmap.mWidth);
			sd.Add("imageHeight", bitmap.mHeight);
			sd.Add("scale", scale);
			sd.Add("screenX", originX);
			sd.Add("screenY", originY);
			if (!savePath.IsEmpty)
				sd.Add("saved", saved);
			Finish(call, sd);
			call.AddImage(Span<uint8>(pngData.Ptr, pngData.Count));
		}

		[MCPTool("pixel_probe", "The color at one point, rendered the same way screenshot renders. For exact checks -- is this row highlighted, did the error underline appear -- without reading a whole image. Coordinates are window coordinates, or offsets within a widget when widget is given.",
			"{'type':'object','properties':{'window':{'type':'integer','description':'Window id. Default: the main window.'},'widget':{'type':'integer','description':'Widget id; x and y are then offsets from its top-left, default its center.'},'x':{'type':'number'},'y':{'type':'number'}}}")]
		void PixelProbe(MCPCall call)
		{
			if (!ResolveTarget(call, false, var window, var x, var y))
				return;
			var bitmap = UICapture.CaptureWindow(window);
			if (bitmap == null)
			{
				call.Error("Window has no size");
				return;
			}
			defer delete bitmap;

			UICapture.ToDevice(window, x, y, var deviceX, var deviceY);
			uint32 pixel = bitmap.GetPixel(deviceX, deviceY);
			uint32 r = pixel & 0xFF;
			uint32 g = (pixel >> 8) & 0xFF;
			uint32 b = (pixel >> 16) & 0xFF;

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("window", window.mId);
			sd.Add("x", deviceX);
			sd.Add("y", deviceY);
			sd.Add("r", r);
			sd.Add("g", g);
			sd.Add("b", b);
			sd.Add("hex", scope $"#{r:X2}{g:X2}{b:X2}");
			Finish(call, sd);
		}

		// ---------------------------------------------------------------------------------------
		// Tools: input
		// ---------------------------------------------------------------------------------------

		[MCPTool("mouse_move", "Move the mouse to a point, or over a widget, without clicking. Hover effects, tooltips and mouseover popups follow from this; call wait_frames afterwards to let them appear.",
			"{'type':'object','properties':{'window':{'type':'integer','description':'Window id. Default: the main window.'},'widget':{'type':'integer','description':'Widget id to move over; x and y become offsets from its top-left, default its center.'},'x':{'type':'number','description':'Window x coordinate.'},'y':{'type':'number','description':'Window y coordinate.'}}}")]
		void MouseMove(MCPCall call)
		{
			if (!ResolveTarget(call, true, var window, var x, var y))
				return;
			UICapture.ToDevice(window, x, y, var deviceX, var deviceY);
			window.MouseMove(deviceX, deviceY);

			var sd = scope StructuredData();
			sd.CreateNew();
			AppendPointerResult(sd, window);
			Finish(call, sd);
		}

		[MCPTool("click", "Click at a point or on a widget: mouse move, button down, button up, with optional modifier keys held. count=2 double-clicks. Reports what is under the pointer and what has focus afterwards.",
			"{'type':'object','properties':{'window':{'type':'integer','description':'Window id. Default: the main window.'},'widget':{'type':'integer','description':'Widget id to click; x and y become offsets from its top-left, default its center.'},'x':{'type':'number'},'y':{'type':'number'},'button':{'type':'integer','description':'0 left (default), 1 right, 2 middle.'},'count':{'type':'integer','description':'1 for a click (default), 2 for a double-click.'},'modifiers':{'type':'string','description':'Keys to hold, e.g. Ctrl, Shift, Ctrl+Shift.'}}}")]
		void Click(MCPCall call)
		{
			if (!ResolveTarget(call, true, var window, var x, var y))
				return;
			List<KeyCode> modifiers = scope .();
			ReadModifiers(call, modifiers);
			UICapture.ToDevice(window, x, y, var deviceX, var deviceY);
			DoClick(window, deviceX, deviceY, (int32)call.GetInt("button", 0), (int32)Math.Max(call.GetInt("count", 1), 1), modifiers);

			var sd = scope StructuredData();
			sd.CreateNew();
			AppendPointerResult(sd, window);
			Finish(call, sd);
		}

		[MCPTool("drag", "Press at one point, move to another over several frames, and release -- for dragging tabs, splitters, tree items and selections. Coordinates are window coordinates, or offsets within widget if given.",
			"{'type':'object','properties':{'window':{'type':'integer','description':'Window id. Default: the main window.'},'widget':{'type':'integer','description':'Widget id the coordinates are relative to.'},'x':{'type':'number'},'y':{'type':'number'},'to_x':{'type':'number'},'to_y':{'type':'number'},'button':{'type':'integer','description':'0 left (default), 1 right, 2 middle.'},'steps':{'type':'integer','description':'Intermediate move events, one per frame. Default 8.'},'modifiers':{'type':'string','description':'Keys to hold, e.g. Ctrl or Shift.'}},'required':['x','y','to_x','to_y']}")]
		void Drag(MCPCall call)
		{
			if (!ResolveTarget(call, true, var window, var startX, var startY))
				return;
			if ((!call.HasArg("to_x")) || (!call.HasArg("to_y")))
			{
				call.Error("to_x and to_y are required");
				return;
			}

			float endX = (float)call.GetFloat("to_x");
			float endY = (float)call.GetFloat("to_y");
			if (call.HasArg("widget"))
			{
				// Same frame of reference as the start point
				var widget = FindWidget((int32)call.GetInt("widget"));
				widget.SelfToRootTranslate(endX, endY, out endX, out endY);
			}

			var drag = new DragState();
			ReadModifiers(call, drag.mModifiers);
			drag.mWindowId = window.mId;
			drag.mButton = (int32)call.GetInt("button", 0);
			drag.mSteps = (int32)Math.Max(call.GetInt("steps", 8), 1);
			drag.mStartX = startX;
			drag.mStartY = startY;
			drag.mEndX = endX;
			drag.mEndY = endY;

			call.Defer(30000, new (pollCall) =>
				{
					bool done = drag.Poll(pollCall);
					if (done)
						delete drag;
					return done;
				});
		}

		// One drag in progress: press, a move per frame, release. Held by the deferred call's poll.
		class DragState
		{
			public List<KeyCode> mModifiers = new List<KeyCode>() ~ delete _;
			public int32 mWindowId;
			public int32 mButton;
			public int32 mSteps;
			public int32 mStep;
			public float mStartX;
			public float mStartY;
			public float mEndX;
			public float mEndY;

			public bool Poll(MCPCall call)
			{
				var window = FindWindow(mWindowId);
				if ((window == null) || (call.mTimedOut))
				{
					call.Error("The window went away during the drag");
					return true;
				}

				if (mStep == 0)
				{
					SetModifiers(window, mModifiers, true);
					UICapture.ToDevice(window, mStartX, mStartY, var deviceX, var deviceY);
					window.MouseMove(deviceX, deviceY);
					window.MouseDown(deviceX, deviceY, mButton, 1);
				}
				else if (mStep <= mSteps)
				{
					float pct = mStep / (float)mSteps;
					UICapture.ToDevice(window, mStartX + (mEndX - mStartX) * pct, mStartY + (mEndY - mStartY) * pct, var deviceX, var deviceY);
					window.MouseMove(deviceX, deviceY);
				}
				else
				{
					UICapture.ToDevice(window, mEndX, mEndY, var deviceX, var deviceY);
					window.MouseMove(deviceX, deviceY);
					window.MouseUp(deviceX, deviceY, mButton);
					SetModifiers(window, mModifiers, false);

					var sd = scope StructuredData();
					sd.CreateNew();
					AppendPointerResult(sd, window);
					Finish(call, sd);
					return true;
				}
				mStep++;
				return false;
			}
		}

		[MCPTool("wheel", "Scroll the mouse wheel at a point or over a widget. Positive delta scrolls up (or left with horizontal=true), one unit per notch.",
			"{'type':'object','properties':{'window':{'type':'integer','description':'Window id. Default: the main window.'},'widget':{'type':'integer','description':'Widget id to scroll over.'},'x':{'type':'number'},'y':{'type':'number'},'delta':{'type':'number','description':'Notches. Default 1.'},'horizontal':{'type':'boolean','description':'Scroll horizontally. Default false.'}}}")]
		void Wheel(MCPCall call)
		{
			if (!ResolveTarget(call, false, var window, var x, var y))
				return;
			float delta = (float)call.GetFloat("delta", 1);
			bool horizontal = call.GetBool("horizontal", false);
			UICapture.ToDevice(window, x, y, var deviceX, var deviceY);
			window.MouseMove(deviceX, deviceY);
			window.MouseWheel(deviceX, deviceY, horizontal ? delta : 0, horizontal ? 0 : delta);

			var sd = scope StructuredData();
			sd.CreateNew();
			AppendPointerResult(sd, window);
			Finish(call, sd);
		}

		[MCPTool("key", "Press a key or key chord in a window, e.g. F5, Enter, Escape, Down, Ctrl+S, Ctrl+Shift+B, Alt+F4, a. Goes through the same path as a real key press, so keyboard shortcuts and menu accelerators fire and edit fields receive the typed character. For entering text, use type_text.",
			"{'type':'object','properties':{'keys':{'type':'string','description':'The chord: optional modifiers Ctrl, Shift, Alt, Win joined with + and one key name or single character.'},'window':{'type':'integer','description':'Window id to send to. Default: the focused window, else the main window.'},'repeat':{'type':'integer','description':'Press this many times. Default 1.'}},'required':['keys']}")]
		void Key(MCPCall call)
		{
			String keys = scope .();
			call.GetString("keys", keys);
			var window = FindKeyWindow(call);
			if (window == null)
			{
				call.Error("No window to send keys to");
				return;
			}

			List<KeyCode> modifiers = scope .();
			String error = scope .();
			if (!ParseKeyChord(keys, modifiers, var key, var keyChar, error))
			{
				call.Error(error);
				return;
			}

			int repeatCount = (int)Math.Max(call.GetInt("repeat", 1), 1);
			for (int idx < repeatCount)
				SendKey(window, modifiers, key, keyChar);

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("window", window.mId);
			if (window.mFocusWidget != null)
			{
				sd.Add("focusWidget", window.mFocusWidget.mWidgetId);
				String typeName = scope .();
				GetTypeName(window.mFocusWidget, typeName);
				sd.Add("focusType", typeName);
			}
			Finish(call, sd);
		}

		WidgetWindow FindKeyWindow(MCPCall call)
		{
			if (call.HasArg("window"))
				return FindWindow((int32)call.GetInt("window"));
			for (var window in BFApp.sApp.mWindows)
			{
				var widgetWindow = window as WidgetWindow;
				if ((widgetWindow != null) && (widgetWindow.mHasFocus))
					return widgetWindow;
			}
			return MainWindow();
		}

		[MCPTool("type_text", "Type a string into whatever has keyboard focus, character by character, as a user would. Newlines press Enter and tabs press Tab. Click on a field first to focus it.",
			"{'type':'object','properties':{'text':{'type':'string'},'window':{'type':'integer','description':'Window id to type into. Default: the focused window, else the main window.'}},'required':['text']}")]
		void TypeText(MCPCall call)
		{
			String text = scope .();
			call.GetString("text", text);
			var window = FindKeyWindow(call);
			if (window == null)
			{
				call.Error("No window to type into");
				return;
			}

			List<KeyCode> modifiers = scope .();
			for (char32 c in text.DecodedChars)
			{
				modifiers.Clear();
				if (c == '\r')
					continue;
				if (c == '\n')
				{
					SendKey(window, modifiers, .Return, (char32)'\r');
					continue;
				}
				if (c == '\t')
				{
					SendKey(window, modifiers, .Tab, (char32)'\t');
					continue;
				}

				if ((c < (char32)0x80) && (((char8)c).IsLetterOrDigit))
				{
					char8 c8 = (char8)c;
					if (c8.IsUpper)
						modifiers.Add(.Shift);
					SendKey(window, modifiers, (KeyCode)(uint8)c8.ToUpper, c);
				}
				else if (c == ' ')
					SendKey(window, modifiers, .Space, c);
				else
				{
					// No simple key for this character; deliver just the character, as an IME would
					EnsureFocus(window);
					window.KeyChar(c);
				}
			}

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("window", window.mId);
			if (window.mFocusWidget != null)
				sd.Add("focusWidget", window.mFocusWidget.mWidgetId);
			Finish(call, sd);
		}

		[MCPTool("focus_window", "Bring a window to the front and give it keyboard focus. Needed before key or type_text when another application, or another of this application's windows, has focus.",
			"{'type':'object','properties':{'window':{'type':'integer','description':'Window id. Default: the main window.'}}}")]
		void FocusWindow(MCPCall call)
		{
			var window = call.HasArg("window") ? FindWindow((int32)call.GetInt("window")) : MainWindow();
			if (window == null)
			{
				call.Error("No such window");
				return;
			}
			window.SetForeground();
			EnsureFocus(window);

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("window", window.mId);
			sd.Add("focus", window.mHasFocus);
			Finish(call, sd);
		}

		[MCPTool("activate_tab", "Bring a tab to the front of its tab group, by the tab widget id from get_ui_tree or find_widgets (type TabButton).",
			"{'type':'object','properties':{'id':{'type':'integer','description':'Tab button widget id.'},'focus':{'type':'boolean','description':'Also give the tab content keyboard focus. Default true.'}},'required':['id']}")]
		void ActivateTab(MCPCall call)
		{
			var tab = FindWidget((int32)call.GetInt("id")) as TabbedView.TabButton;
			if (tab == null)
			{
				call.Error("No tab with that widget id. Look for TabButton widgets in get_ui_tree or find_widgets.");
				return;
			}
			tab.Activate(call.GetBool("focus", true));

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("tab", tab.mWidgetId);
			sd.Add("label", tab.mLabel ?? "");
			if (tab.mContent != null)
				sd.Add("contentWidget", tab.mContent.mWidgetId);
			Finish(call, sd);
		}

		// ---------------------------------------------------------------------------------------
		// Tools: popup surfaces
		// ---------------------------------------------------------------------------------------

		[MCPTool("get_tooltip", "The tooltip currently shown, if any: its text, the widget it belongs to, and its window. Tooltips appear a number of frames after the mouse stops over a widget, so mouse_move, then wait_frames 30 or so, then this.")]
		void GetTooltip(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			var tooltip = DarkTooltipManager.sTooltip;
			sd.Add("shown", tooltip != null);
			sd.Add("mouseStillTicks", DarkTooltipManager.sMouseStillTicks);
			if (tooltip != null)
			{
				sd.Add("text", tooltip.mText ?? "");
				if (tooltip.mRelWidget != null)
					sd.Add("forWidget", tooltip.mRelWidget.mWidgetId);
				if (tooltip.mWidgetWindow != null)
				{
					sd.Add("window", tooltip.mWidgetWindow.mId);
					sd.Add("screenX", tooltip.mWidgetWindow.mClientX);
					sd.Add("screenY", tooltip.mWidgetWindow.mClientY);
					sd.Add("w", tooltip.mWidgetWindow.mClientWidth);
					sd.Add("h", tooltip.mWidgetWindow.mClientHeight);
				}
			}
			Finish(call, sd);
		}

		[MCPTool("get_dialogs", "The dialogs currently open, topmost last: window id, title, message text, and the buttons with their labels and widget ids. An open dialog blocks most other actions, so check this when something did not respond.")]
		void GetDialogs(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			using (sd.CreateArray("dialogs"))
			{
				for (var window in BFApp.sApp.mWindows)
				{
					var widgetWindow = window as WidgetWindow;
					if ((widgetWindow == null) || (!widgetWindow.mVisible))
						continue;
					var dialog = widgetWindow.mRootWidget as Dialog;
					if (dialog == null)
						continue;

					using (sd.CreateObject())
					{
						sd.Add("window", widgetWindow.mId);
						sd.Add("dialog", dialog.mWidgetId);
						String typeName = scope .();
						GetTypeName(dialog, typeName);
						sd.Add("type", typeName);
						sd.Add("title", dialog.mTitle ?? "");
						sd.Add("text", dialog.mText ?? "");
						if (widgetWindow.mHasFocus)
							sd.Add("focus", true);
						using (sd.CreateArray("buttons"))
						{
							for (var button in dialog.mButtons)
							{
								using (sd.CreateObject())
								{
									sd.Add("id", button.mWidgetId);
									if (var darkButton = button as DarkButton)
										sd.Add("label", darkButton.mLabel ?? "");
									if (button == dialog.mDefaultButton)
										sd.Add("default", true);
									if (button == dialog.mEscButton)
										sd.Add("escape", true);
									if (button.mDisabled)
										sd.Add("disabled", true);
								}
							}
						}
					}
				}
			}
			Finish(call, sd);
		}

		[MCPTool("click_dialog_button", "Click a button on an open dialog by its label (case-insensitive, & ignored), for example Yes, No, OK, Cancel, Save. Defaults to the topmost dialog.",
			"{'type':'object','properties':{'label':{'type':'string'},'window':{'type':'integer','description':'The dialog window id from get_dialogs. Default: the topmost dialog.'}},'required':['label']}")]
		void ClickDialogButton(MCPCall call)
		{
			String label = scope .();
			call.GetString("label", label);
			label.Replace("&", "");

			WidgetWindow window = call.HasArg("window") ? FindWindow((int32)call.GetInt("window")) : FindTopmostWindowWithRoot<Dialog>();
			var dialog = window?.mRootWidget as Dialog;
			if (dialog == null)
			{
				call.Error("No dialog is open");
				return;
			}

			ButtonWidget match = null;
			for (var button in dialog.mButtons)
			{
				var darkButton = button as DarkButton;
				if ((darkButton == null) || (darkButton.mLabel == null))
					continue;
				String buttonLabel = scope String(darkButton.mLabel);
				buttonLabel.Replace("&", "");
				if (String.Compare(buttonLabel, label, true) == 0)
				{
					match = button;
					break;
				}
			}
			if (match == null)
			{
				String labels = scope .();
				for (var button in dialog.mButtons)
				{
					if (var darkButton = button as DarkButton)
						labels.AppendF("{}{}", labels.IsEmpty ? "" : ", ", darkButton.mLabel ?? "");
				}
				call.Error(scope $"No button labeled '{label}'. Buttons: {labels}");
				return;
			}

			// The click may close the dialog, so take what we report beforehand
			int32 dialogId = dialog.mWidgetId;
			int32 buttonId = match.mWidgetId;
			match.SelfToRootTranslate(match.mWidth / 2, match.mHeight / 2, var x, var y);
			UICapture.ToDevice(window, x, y, var deviceX, var deviceY);
			List<KeyCode> modifiers = scope .();
			DoClick(window, deviceX, deviceY, 0, 1, modifiers);

			// A closed dialog's window lingers a frame before deletion, so ask the dialog itself
			var dialogNow = FindWidget(dialogId) as Dialog;
			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("clicked", buttonId);
			sd.Add("dialogStillOpen", (dialogNow != null) && (!dialogNow.mClosed));
			Finish(call, sd);
		}

		static MenuWidget FindMenuWidget(Widget widget)
		{
			if (var menuWidget = widget as MenuWidget)
				return menuWidget;
			if (widget.mChildWidgets != null)
			{
				for (var child in widget.mChildWidgets)
				{
					var found = FindMenuWidget(child);
					if (found != null)
						return found;
				}
			}
			return null;
		}

		[MCPTool("get_popup_menus", "The popup (context or dropdown) menus currently open, outermost first, each with its items: widget id, label, disabled, whether it opens a submenu, and which is highlighted. Right-click something, then call this to see what the menu offers.")]
		void GetPopupMenus(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			using (sd.CreateArray("menus"))
			{
				for (var window in BFApp.sApp.mWindows)
				{
					var widgetWindow = window as WidgetWindow;
					if ((widgetWindow == null) || (!widgetWindow.mVisible) || (widgetWindow.mRootWidget == null))
						continue;
					if (!(widgetWindow.mRootWidget is MenuContainer))
						continue;
					var menuWidget = FindMenuWidget(widgetWindow.mRootWidget);
					if (menuWidget == null)
						continue;

					using (sd.CreateObject())
					{
						sd.Add("window", widgetWindow.mId);
						sd.Add("menuWidget", menuWidget.mWidgetId);
						sd.Add("screenX", widgetWindow.mClientX);
						sd.Add("screenY", widgetWindow.mClientY);
						using (sd.CreateArray("items"))
						{
							for (var itemWidget in menuWidget.mItemWidgets)
							{
								using (sd.CreateObject())
								{
									sd.Add("id", itemWidget.mWidgetId);
									itemWidget.Describe(sd);
								}
							}
						}
					}
				}
			}
			Finish(call, sd);
		}

		[MCPTool("select_popup_item", "Activate an item in an open popup menu, by label (case-insensitive, & ignored) or widget id. An item with a submenu opens it; call get_popup_menus again to see the submenu's items.",
			"{'type':'object','properties':{'label':{'type':'string'},'id':{'type':'integer','description':'Item widget id from get_popup_menus.'}}}")]
		void SelectPopupItem(MCPCall call)
		{
			String label = scope .();
			call.GetString("label", label);
			label.Replace("&", "");
			int32 wantId = call.HasArg("id") ? (int32)call.GetInt("id") : -1;
			if ((label.IsEmpty) && (wantId == -1))
			{
				call.Error("Pass label or id");
				return;
			}

			MenuItemWidget match = null;
			// Innermost (most recently opened) menu first
			for (int idx = BFApp.sApp.mWindows.Count - 1; (idx >= 0) && (match == null); idx--)
			{
				var widgetWindow = BFApp.sApp.mWindows[idx] as WidgetWindow;
				if ((widgetWindow == null) || (!widgetWindow.mVisible) || (widgetWindow.mRootWidget == null))
					continue;
				if (!(widgetWindow.mRootWidget is MenuContainer))
					continue;
				var menuWidget = FindMenuWidget(widgetWindow.mRootWidget);
				if (menuWidget == null)
					continue;
				for (var itemWidget in menuWidget.mItemWidgets)
				{
					if (wantId != -1)
					{
						if (itemWidget.mWidgetId == wantId)
						{
							match = itemWidget;
							break;
						}
						continue;
					}
					if (itemWidget.mMenuItem?.mLabel == null)
						continue;
					String itemLabel = scope String(itemWidget.mMenuItem.mLabel);
					itemLabel.Replace("&", "");
					if (String.Compare(itemLabel, label, true) == 0)
					{
						match = itemWidget;
						break;
					}
				}
			}

			if (match == null)
			{
				call.Error("No open popup menu has that item. Call get_popup_menus to see what is open.");
				return;
			}
			if (match.mMenuItem.mDisabled)
			{
				call.Error("That item is disabled");
				return;
			}

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("item", match.mWidgetId);
			if (match.mMenuItem.IsParent)
			{
				if (var darkMenuItem = match as DarkMenuItem)
				{
					darkMenuItem.OpenSubMenu(true);
					sd.Add("openedSubmenu", true);
				}
				else
				{
					call.Error("Cannot open that submenu");
					return;
				}
			}
			else
			{
				match.Submit();
				sd.Add("activated", true);
			}
			Finish(call, sd);
		}
	}
}
