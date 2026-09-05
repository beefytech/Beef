#if !CLI
using System;
using System.Collections;
using Beefy.mcp;
using Beefy.utils;
using Beefy.widgets;
using Beefy.sys;
using IDE.ui;

namespace IDE
{
	// The workspace and the IDE's own command surface: projects and their files, project and
	// workspace configuration, the named commands every menu item and shortcut maps to, the menu
	// bar itself, and the docked panels.
	class IDEWorkspaceToolSet : MCPToolSet
	{
		// Runs a named IDE command; false if there is no such command
		public static bool RunCommand(StringView name)
		{
			IDECommand command;
			if (!gApp.mCommands.mCommandMap.TryGetValue(scope String(name), out command))
				return false;
			command.mAction();
			return true;
		}

		static void Finish(MCPCall call, StructuredData sd)
		{
			String json = scope .();
			sd.ToJSON(json);
			call.Result(json);
		}

		// ---------------------------------------------------------------------------------------
		// Workspace and projects
		// ---------------------------------------------------------------------------------------

		static void AppendProjectItems(StructuredData sd, ProjectFolder folder, int depth, ref int count, int maxCount)
		{
			using (sd.CreateArray("items"))
			{
				for (var item in folder.mChildItems)
				{
					if (count >= maxCount)
						return;
					count++;
					using (sd.CreateObject())
					{
						sd.Add("name", item.mName);
						if (var childFolder = item as ProjectFolder)
						{
							sd.Add("kind", "folder");
							String path = scope .();
							childFolder.GetFullImportPath(path);
							sd.Add("path", path);
							if (item.mIncludeKind != .Auto)
								sd.Add("include", item.mIncludeKind);
							if (depth > 0)
								AppendProjectItems(sd, childFolder, depth - 1, ref count, maxCount);
							else if (!childFolder.mChildItems.IsEmpty)
								sd.Add("itemCount", childFolder.mChildItems.Count);
						}
						else if (var fileItem = item as ProjectFileItem)
						{
							sd.Add("kind", (item is ProjectSource) ? "source" : "file");
							String path = scope .();
							fileItem.GetFullImportPath(path);
							sd.Add("path", path);
							if (item.mIncludeKind != .Auto)
								sd.Add("include", item.mIncludeKind);
						}
						else
							sd.Add("kind", "composition");
					}
				}
			}
		}

		[MCPTool("get_workspace_tree", "The projects in the workspace and their folders and files, as the Project panel shows them, with full paths. Narrow to one project and limit depth for large workspaces.")]
		void GetWorkspaceTree(MCPCall call,
			[MCPParam("Only this project. Default: all.")] String project,
			[MCPParam("Folder depth to descend. Default 8.")] int depth,
			[MCPParam("Maximum items. Default 5000.")] int max)
		{
			int maxCount = (max > 0) ? max : 5000;
			int useDepth = call.HasArg("depth") ? depth : 8;

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("name", gApp.mWorkspace.mName ?? "");
			sd.Add("dir", gApp.mWorkspace.mDir ?? "");
			sd.Add("initialized", gApp.mWorkspace.IsInitialized);
			int count = 0;
			using (sd.CreateArray("projects"))
			{
				for (var checkProject in gApp.mWorkspace.mProjects)
				{
					if ((!project.IsEmpty) && (String.Compare(checkProject.mProjectName, project, true) != 0))
						continue;
					using (sd.CreateObject())
					{
						sd.Add("name", checkProject.mProjectName);
						sd.Add("dir", checkProject.mProjectDir);
						sd.Add("file", checkProject.mProjectPath);
						if (checkProject == gApp.mWorkspace.mStartupProject)
							sd.Add("startup", true);
						if (checkProject.mHasChanged)
							sd.Add("hasChanges", true);
						if (checkProject.mRootFolder != null)
							AppendProjectItems(sd, checkProject.mRootFolder, useDepth, ref count, maxCount);
					}
				}
			}
			if (count >= maxCount)
				sd.Add("truncated", true);
			Finish(call, sd);
		}

		[MCPTool("get_project", "A project's full configuration as the IDE would save it to BeefProj.toml, as JSON: general settings, dependencies, and every config/platform's build, debug and Beef options.")]
		void GetProject(MCPCall call,
			[MCPParam("Project name from get_workspace_tree.", true)] String name)
		{
			var project = gApp.mWorkspace.FindProject(name);
			if (project == null)
			{
				call.Error(scope $"No project named '{name}'. Call get_workspace_tree for the list.");
				return;
			}

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("name", project.mProjectName);
			sd.Add("dir", project.mProjectDir);
			sd.Add("file", project.mProjectPath);
			sd.Add("hasChanges", project.mHasChanged);
			using (sd.CreateObject("config"))
				project.Serialize(sd);
			Finish(call, sd);
		}

		[MCPTool("get_workspace", "The workspace configuration as the IDE would save it to BeefSpace.toml, as JSON, plus the active config and platform and the startup project.")]
		void GetWorkspace(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("name", gApp.mWorkspace.mName ?? "");
			sd.Add("dir", gApp.mWorkspace.mDir ?? "");
			sd.Add("initialized", gApp.mWorkspace.IsInitialized);
			sd.Add("config", gApp.mConfigName);
			sd.Add("platform", gApp.mPlatformName);
			if (gApp.mWorkspace.mStartupProject != null)
				sd.Add("startupProject", gApp.mWorkspace.mStartupProject.mProjectName);
			sd.Add("hasChanges", gApp.mWorkspace.mHasChanged);
			if (gApp.mWorkspace.IsInitialized)
			{
				using (sd.CreateObject("config"))
					gApp.mWorkspace.Serialize(sd);
			}
			Finish(call, sd);
		}

		// ---------------------------------------------------------------------------------------
		// Commands and menus
		// ---------------------------------------------------------------------------------------

		[MCPTool("list_commands", "The IDE's named commands, which every menu item and keyboard shortcut maps to, with their current shortcut. Pass any of these names to execute_command. Filter by a substring of the name.")]
		void ListCommands(MCPCall call,
			[MCPParam("Case-insensitive substring of the command name.")] String filter)
		{
			String filterLower = scope String(filter);
			filterLower.ToLower();

			List<String> names = scope .();
			for (var name in gApp.mCommands.mCommandMap.Keys)
				names.Add(name);
			names.Sort(scope (lhs, rhs) => String.Compare(lhs, rhs, true));

			var sd = scope StructuredData();
			sd.CreateNew();
			using (sd.CreateArray("commands"))
			{
				for (var name in names)
				{
					if (!filterLower.IsEmpty)
					{
						String nameLower = scope String(name);
						nameLower.ToLower();
						if (!nameLower.Contains(filterLower))
							continue;
					}
					var command = gApp.mCommands.mCommandMap[name];
					using (sd.CreateObject())
					{
						sd.Add("name", name);
						String shortcut = scope .();
						command.ToString(shortcut);
						if (!shortcut.IsEmpty)
							sd.Add("shortcut", shortcut);
						if (command.mContextFlags.HasFlag(.Editor))
							sd.Add("editorOnly", true);
						if (command.mMenuItem != null)
							sd.Add("inMenu", true);
					}
				}
			}
			Finish(call, sd);
		}

		[MCPTool("execute_command", "Run one of the IDE's named commands, the same thing its menu item or shortcut does: Build Workspace, Cancel Build, Save All, Goto Definition, Find in Files, Autocomplete, Comment Toggle, Start Debugging, Step Over, and so on (see list_commands). Commands that open dialogs return immediately; check get_dialogs afterwards. Commands that start work return immediately too; follow with wait_idle.")]
		void ExecuteCommand(MCPCall call,
			[MCPParam("Command name exactly as list_commands reports it.", true)] String name)
		{
			IDECommand command;
			if (!gApp.mCommands.mCommandMap.TryGetValue(name, out command))
			{
				call.Error(scope $"No command named '{name}'. Call list_commands to see the names.");
				return;
			}
			command.mAction();
			call.Result("{\"ok\":true}");
		}

		static SysMenu GetMenuRoot()
		{
#if BF_PLATFORM_WINDOWS
			return gApp.mMainWindow?.mSysMenu;
#else
			return gApp.[Friend]mMainFrame.mMenuBar.mSysMenuRoot;
#endif
		}

		static void GetMenuLabel(SysMenu menu, String outLabel)
		{
			if (menu.mText == null)
				return;
			outLabel.Append(menu.mText);
			outLabel.Replace("&", "");
		}

		// Walks a path of labels like File/Open/Open Workspace..., case-insensitive, & ignored
		static SysMenu FindMenu(SysMenu root, StringView path, String outError)
		{
			var menu = root;
			for (var part in path.Split('/'))
			{
				var wantLabel = part;
				wantLabel.Trim();
				if (wantLabel.IsEmpty)
					continue;
				if (menu.mChildren == null)
				{
					outError.AppendF("'{}' has no submenu", GetMenuLabel(menu, .. scope .()));
					return null;
				}
				// Refresh enabled/checked state the way opening the menu would
				menu.UpdateChildItems();

				SysMenu found = null;
				for (var child in menu.mChildren)
				{
					String label = GetMenuLabel(child, .. scope .());
					if (String.Compare(label, scope String(wantLabel), true) == 0)
					{
						found = child;
						break;
					}
				}
				if (found == null)
				{
					outError.AppendF("No menu item '{}' under '{}'", wantLabel, GetMenuLabel(menu, .. scope .()));
					return null;
				}
				menu = found;
			}
			return menu;
		}

		static void AppendMenu(StructuredData sd, SysMenu menu, int depth, Dictionary<SysMenu, String> commandNames)
		{
			String label = GetMenuLabel(menu, .. scope .());
			if (menu.mText == null)
			{
				sd.Add("separator", true);
				return;
			}
			sd.Add("label", label);
			if (menu.mHotKey != null)
				sd.Add("shortcut", menu.mHotKey);
			if (!menu.mEnabled)
				sd.Add("disabled", true);
			if (menu.mCheckState == 1)
				sd.Add("checked", true);
			if (commandNames.TryGetValue(menu, var commandName))
				sd.Add("command", commandName);
			if (menu.mChildren != null)
			{
				if (depth <= 0)
				{
					sd.Add("itemCount", menu.mChildren.Count);
					return;
				}
				menu.UpdateChildItems();
				using (sd.CreateArray("items"))
				{
					for (var child in menu.mChildren)
					{
						using (sd.CreateObject())
							AppendMenu(sd, child, depth - 1, commandNames);
					}
				}
			}
		}

		[MCPTool("get_menu_bar", "The main menu bar: menus and their items with label, shortcut, enabled and checked state, and the command each item runs. Enabled state is refreshed the way opening the menu would. Pass a path like File or Debug/Windows to start below the top level.")]
		void GetMenuBar(MCPCall call,
			[MCPParam("Slash-separated labels of the menu to start from. Default: the whole bar.")] String path,
			[MCPParam("How many levels of submenu to include. Default 2.")] int depth)
		{
			var root = GetMenuRoot();
			if (root == null)
			{
				call.Error("No menu bar");
				return;
			}
			int useDepth = call.HasArg("depth") ? depth : 2;

			var menu = root;
			if (!path.IsEmpty)
			{
				String error = scope .();
				menu = FindMenu(root, path, error);
				if (menu == null)
				{
					call.Error(error);
					return;
				}
			}

			Dictionary<SysMenu, String> commandNames = scope .();
			for (var kv in gApp.mCommands.mCommandMap)
			{
				if (kv.value.mMenuItem != null)
					commandNames[kv.value.mMenuItem] = kv.key;
			}

			var sd = scope StructuredData();
			sd.CreateNew();
			if (menu == root)
			{
				if (root.mChildren != null)
				{
					using (sd.CreateArray("menus"))
					{
						for (var child in root.mChildren)
						{
							using (sd.CreateObject())
								AppendMenu(sd, child, useDepth - 1, commandNames);
						}
					}
				}
			}
			else
			{
				using (sd.CreateObject("menu"))
					AppendMenu(sd, menu, useDepth, commandNames);
			}
			Finish(call, sd);
		}

		[MCPTool("select_menu", "Activate a menu bar item by its path of labels, e.g. File/Save All or Build/Build Workspace, as clicking it would. Fails if the item is disabled or is a submenu. Items that open dialogs return immediately; check get_dialogs.")]
		void SelectMenu(MCPCall call,
			[MCPParam("Slash-separated labels from the top-level menu down to the item.", true)] String path)
		{
			var root = GetMenuRoot();
			if (root == null)
			{
				call.Error("No menu bar");
				return;
			}
			String error = scope .();
			var menu = FindMenu(root, path, error);
			if (menu == null)
			{
				call.Error(error);
				return;
			}
			if (menu.mChildren != null)
			{
				call.Error("That is a submenu, not an item. Call get_menu_bar with this path to see its items.");
				return;
			}
			if (!menu.mEnabled)
			{
				call.Error("That menu item is disabled right now");
				return;
			}
			menu.Selected();
			call.Result("{\"ok\":true}");
		}

		// ---------------------------------------------------------------------------------------
		// Panels
		// ---------------------------------------------------------------------------------------

		[MCPTool("get_panels", "Every tab in every docked or floating tab group: panels (Project, Output, Errors, Watch, Call Stack...) and documents, with the tab's widget id, its content type, whether it is the active tab of its group, and its window. Use activate_tab or show_panel to bring one forward.")]
		void GetPanels(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			using (sd.CreateArray("tabs"))
			{
				gApp.WithTabs(scope (tab) =>
					{
						using (sd.CreateObject())
						{
							sd.Add("tabWidget", tab.mWidgetId);
							sd.Add("label", tab.mLabel ?? "");
							if (tab.mContent != null)
							{
								String typeName = scope .();
								tab.mContent.GetType().GetName(typeName);
								sd.Add("type", typeName);
								sd.Add("contentWidget", tab.mContent.mWidgetId);
								if (var sourceViewPanel = tab.mContent as SourceViewPanel)
									sd.Add("file", sourceViewPanel.mFilePath ?? "");
							}
							if (tab.mIsActive)
								sd.Add("active", true);
							if (tab.mWidgetWindow != null)
								sd.Add("window", tab.mWidgetWindow.mId);
							if (tab.mTabbedView != null)
								sd.Add("tabGroup", tab.mTabbedView.mWidgetId);
						}
					});
			}
			Finish(call, sd);
		}

		[MCPTool("show_panel", "Show one of the IDE's panels by type name, opening it if closed and bringing its tab forward: ProjectPanel, OutputPanel, ErrorsPanel, FindResultsPanel, WatchPanel, AutoWatchPanel, CallStackPanel, BreakpointPanel, ThreadPanel, ModulePanel, MemoryPanel, ImmediatePanel, ClassViewPanel, BookmarksPanel, DiagnosticsPanel, TerminalPanel, ConsolePanel, ProfilePanel, AutoCompletePanel.")]
		void ShowPanel(MCPCall call,
			[MCPParam("Panel type name, e.g. OutputPanel.", true)] String name,
			[MCPParam("Give the panel keyboard focus. Default true.")] bool focus)
		{
			var panel = gApp.GetPanel(name);
			if (panel == null)
			{
				call.Error(scope $"No panel named '{name}'");
				return;
			}
			bool setFocus = call.HasArg("focus") ? focus : true;
			gApp.[Friend]ShowPanel(panel, setFocus);

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("panelWidget", panel.mWidgetId);
			var tab = gApp.GetTab(panel);
			if (tab != null)
			{
				sd.Add("tabWidget", tab.mWidgetId);
				if (tab.mWidgetWindow != null)
					sd.Add("window", tab.mWidgetWindow.mId);
			}
			Finish(call, sd);
		}
	}
}
#endif
