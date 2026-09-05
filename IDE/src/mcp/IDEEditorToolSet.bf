#if !CLI
using System;
using System.Collections;
using System.IO;
using Beefy.mcp;
using Beefy.utils;
using Beefy.widgets;
using Beefy.theme.dark;
using IDE.ui;
using IDE.Compiler;

namespace IDE
{
	// Reading and navigating source documents: what is open, their unsaved text, the cursor, the
	// errors the background compiler reports, the autocomplete popup, and hover information. Line
	// numbers are 0-based throughout, as the IDE itself counts them; columns are character offsets
	// within the line.
	class IDEEditorToolSet : MCPToolSet
	{
		// ---------------------------------------------------------------------------------------
		// Helpers
		// ---------------------------------------------------------------------------------------

		// Absolute, separator-normalized path; relative paths are taken from the workspace directory
		public static void ResolvePath(StringView path, String outPath)
		{
			if (Path.IsPathRooted(path))
				outPath.Append(path);
			else if (gApp.mWorkspace.mDir != null)
				Path.GetAbsolutePath(path, gApp.mWorkspace.mDir, outPath);
			else
				Path.GetFullPath(path, outPath);
			IDEUtils.FixFilePath(outPath);
		}

		public static bool PathsEqual(StringView lhs, StringView rhs)
		{
			String fixedLhs = scope String(lhs);
			IDEUtils.FixFilePath(fixedLhs);
			String fixedRhs = scope String(rhs);
			IDEUtils.FixFilePath(fixedRhs);
			return String.Compare(fixedLhs, fixedRhs, true) == 0;
		}

		public static void WithSourceViewPanels(delegate void(SourceViewPanel panel, TabbedView.TabButton tab) func)
		{
			gApp.WithTabs(scope (tab) =>
				{
					if (var sourceViewPanel = tab.mContent as SourceViewPanel)
						func(sourceViewPanel, tab);
				});
		}

		public static SourceViewPanel FindOpenDocument(StringView path)
		{
			String absPath = scope .();
			ResolvePath(path, absPath);
			SourceViewPanel found = null;
			WithSourceViewPanels(scope [&] (panel, tab) =>
				{
					if ((found == null) && (panel.mFilePath != null) && (PathsEqual(panel.mFilePath, absPath)))
						found = panel;
				});
			return found;
		}

		// The document a tool acts on: the 'file' argument if given (must be open), else the active one
		public static SourceViewPanel GetDocument(MCPCall call)
		{
			if (call.HasArg("file"))
			{
				String file = scope .();
				call.GetString("file", file);
				var panel = FindOpenDocument(file);
				if (panel == null)
					call.Error(scope $"'{file}' is not open. Call open_file first, or get_open_documents to see what is.");
				return panel;
			}

			var panel = gApp.GetActiveSourceViewPanel(true);
			if (panel == null)
				call.Error("No document is active. Pass file, or open one with open_file.");
			return panel;
		}

		public static void AppendDocumentInfo(StructuredData sd, SourceViewPanel panel, TabbedView.TabButton tab)
		{
			sd.Add("file", panel.mFilePath ?? "");
			if (panel.mProjectSource != null)
				sd.Add("project", panel.mProjectSource.mProject.mProjectName);
			if (panel.HasUnsavedChanges())
				sd.Add("hasChanges", true);
			if (panel.mIsBeefSource)
				sd.Add("beef", true);
			var activePanel = gApp.GetActiveDocumentPanel();
			if ((activePanel == panel) || ((activePanel == null) && (gApp.mLastActiveSourceViewPanel == panel)))
				sd.Add("active", true);
			sd.Add("panelWidget", panel.mWidgetId);
			if (tab != null)
			{
				sd.Add("tabWidget", tab.mWidgetId);
				if (tab.mWidgetWindow != null)
					sd.Add("window", tab.mWidgetWindow.mId);
			}

			var content = panel.mEditWidget.mEditWidgetContent;
			var lineAndColumn = content.CursorLineAndColumn;
			content.GetLineCharAtIdx(content.CursorTextPos, var cursorLine, var cursorLineChar);
			sd.Add("line", cursorLine);
			sd.Add("column", cursorLineChar);
			sd.Add("visualColumn", lineAndColumn.mColumn);
			if (content.CurSelection != null)
			{
				var selection = content.CurSelection.Value;
				content.GetLineCharAtIdx(selection.mStartPos, var startLine, var startChar);
				content.GetLineCharAtIdx(selection.mEndPos, var endLine, var endChar);
				using (sd.CreateObject("selection"))
				{
					sd.Add("startLine", startLine);
					sd.Add("startColumn", startChar);
					sd.Add("endLine", endLine);
					sd.Add("endColumn", endChar);
				}
			}
		}

		static void Finish(MCPCall call, StructuredData sd)
		{
			String json = scope .();
			sd.ToJSON(json);
			call.Result(json);
		}

		// ---------------------------------------------------------------------------------------
		// Documents
		// ---------------------------------------------------------------------------------------

		[MCPTool("get_open_documents", "Every source document open in the editor, with its path, project, whether it has unsaved changes, which one is active, its cursor position, and the widget ids of its panel and tab.")]
		void GetOpenDocuments(MCPCall call)
		{
			var sd = scope StructuredData();
			sd.CreateNew();
			using (sd.CreateArray("documents"))
			{
				WithSourceViewPanels(scope (panel, tab) =>
					{
						using (sd.CreateObject())
							AppendDocumentInfo(sd, panel, tab);
					});
			}
			Finish(call, sd);
		}

		[MCPTool("open_file", "Open a source file in the editor (or bring it to front if already open), optionally placing the cursor on a line and column and highlighting the location. Paths may be relative to the workspace directory.")]
		void OpenFile(MCPCall call,
			[MCPParam("Path of the file, absolute or relative to the workspace.", true)] String path,
			[MCPParam("0-based line to go to. Omit to keep the current position.")] int line,
			[MCPParam("0-based character column on that line. Default 0.")] int column)
		{
			String absPath = scope .();
			ResolvePath(path, absPath);
			if (!File.Exists(absPath))
			{
				call.Error(scope $"File not found: {absPath}");
				return;
			}

			SourceViewPanel panel = null;
			if (call.HasArg("line"))
				panel = gApp.ShowSourceFileLocation(absPath, -1, -1, line, column, .Always);
			else
				panel = gApp.ShowSourceFile(absPath).panel;

			if (panel == null)
			{
				call.Error(scope $"Could not open {absPath}");
				return;
			}

			var sd = scope StructuredData();
			sd.CreateNew();
			AppendDocumentInfo(sd, panel, gApp.GetTab(panel));
			Finish(call, sd);
		}

		[MCPTool("get_document", "The text of an open document as the editor holds it, unsaved edits included, with 0-based line numbers prefixed so positions can be quoted back to other tools. Also the cursor and selection. Read a window of lines with start_line and line_count for big files.")]
		void GetDocument(MCPCall call,
			[MCPParam("Path of an open document. Default: the active document.")] String file,
			[MCPParam("First 0-based line to return. Default 0.")] int start_line,
			[MCPParam("How many lines to return. Default 400, maximum 2000.")] int line_count,
			[MCPParam("Prefix each line with its number. Default true.")] bool numbered)
		{
			var panel = GetDocument(call);
			if (panel == null)
				return;

			String text = scope .();
			panel.mEditWidget.GetText(text);

			List<StringView> lines = scope .();
			for (var lineView in text.Split('\n'))
			{
				var trimmed = lineView;
				if (trimmed.EndsWith("\r"))
					trimmed.RemoveFromEnd(1);
				lines.Add(trimmed);
			}

			int startLine = Math.Clamp(start_line, 0, lines.Count);
			int count = call.HasArg("line_count") ? Math.Clamp(line_count, 1, 2000) : 400;
			int endLine = Math.Min(startLine + count, lines.Count);
			bool addNumbers = call.HasArg("numbered") ? numbered : true;

			String outText = scope .();
			for (int lineIdx = startLine; lineIdx < endLine; lineIdx++)
			{
				if (addNumbers)
					outText.AppendF("{}: ", lineIdx);
				outText.Append(lines[lineIdx]);
				outText.Append('\n');
			}

			var sd = scope StructuredData();
			sd.CreateNew();
			AppendDocumentInfo(sd, panel, gApp.GetTab(panel));
			sd.Add("lineCount", lines.Count);
			sd.Add("startLine", startLine);
			sd.Add("endLine", endLine);
			if (endLine < lines.Count)
				sd.Add("truncated", true);
			sd.Add("text", outText);
			Finish(call, sd);
		}

		[MCPTool("set_cursor", "Move the cursor in a document to a 0-based line and character column, scrolling it into view, and optionally select from there to a second position.")]
		void SetCursor(MCPCall call,
			[MCPParam("0-based line.", true)] int line,
			[MCPParam("0-based character column. Default 0.")] int column,
			[MCPParam("Path of an open document. Default: the active document.")] String file,
			[MCPParam("If given with to_column, select from the cursor to this 0-based line.")] int to_line,
			[MCPParam("End column of the selection.")] int to_column)
		{
			var panel = GetDocument(call);
			if (panel == null)
				return;

			var content = panel.mEditWidget.mEditWidgetContent;
			int textIdx = content.GetTextIdx(line, column);
			if ((call.HasArg("to_line")) && (call.HasArg("to_column")))
			{
				int endIdx = content.GetTextIdx(to_line, to_column);
				content.CurSelection = EditSelection(textIdx, endIdx);
				content.CursorTextPos = endIdx;
			}
			else
			{
				content.CurSelection = null;
				content.CursorTextPos = textIdx;
			}
			content.EnsureCursorVisible();

			var sd = scope StructuredData();
			sd.CreateNew();
			AppendDocumentInfo(sd, panel, gApp.GetTab(panel));
			Finish(call, sd);
		}

		// ---------------------------------------------------------------------------------------
		// Errors
		// ---------------------------------------------------------------------------------------

		[MCPTool("get_errors", "The errors and warnings the background compiler currently reports, as shown in the Errors panel: file, 0-based line and column, code, message, project. These update as you type; a full build reports through get_output instead.")]
		void GetErrors(MCPCall call,
			[MCPParam("Only errors in this file, absolute or workspace-relative.")] String file,
			[MCPParam("Maximum entries. Default 200.")] int max)
		{
			String filterPath = scope .();
			if (call.HasArg("file"))
				ResolvePath(file, filterPath);
			int maxCount = (max > 0) ? max : 200;

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("errorCount", gApp.mErrorsPanel.mErrorCount);
			sd.Add("warningCount", gApp.mErrorsPanel.mWarningCount);

			int returned = 0;
			int total = 0;
			void AddError(BfPassInstance.BfError error, StringView source)
			{
				if ((!filterPath.IsEmpty) && ((error.mFilePath == null) || (!PathsEqual(error.mFilePath, filterPath))))
					return;
				total++;
				if (returned >= maxCount)
					return;
				returned++;

				using (sd.CreateObject())
				{
					sd.Add("file", error.mFilePath ?? "");
					sd.Add("line", error.mLine);
					sd.Add("column", error.mColumn);
					sd.Add("warning", error.mIsWarning);
					if (error.mCode != 0)
						sd.Add("code", error.mCode);
					sd.Add("message", error.mError ?? "");
					if (error.mProject != null)
						sd.Add("project", error.mProject);
					sd.Add("source", source);
					if ((error.mMoreInfo != null) && (!error.mMoreInfo.IsEmpty))
					{
						using (sd.CreateArray("moreInfo"))
						{
							for (var info in error.mMoreInfo)
							{
								using (sd.CreateObject())
								{
									sd.Add("file", info.mFilePath ?? "");
									sd.Add("line", info.mLine);
									sd.Add("column", info.mColumn);
									sd.Add("message", info.mError ?? "");
								}
							}
						}
					}
				}
			}

			using (sd.CreateArray("errors"))
			{
				for (var kv in gApp.mErrorsPanel.mParseErrors)
				{
					for (var error in kv.value)
						AddError(error, "parse");
				}
				for (var error in gApp.mErrorsPanel.mResolveErrors)
					AddError(error, "resolve");
			}
			sd.Add("total", total);
			if (returned < total)
				sd.Add("truncated", true);
			Finish(call, sd);
		}

		// ---------------------------------------------------------------------------------------
		// Autocomplete
		// ---------------------------------------------------------------------------------------

		static AutoComplete GetAutoComplete(SourceViewPanel panel)
		{
			var sourceEditWidgetContent = panel.mEditWidget.mEditWidgetContent as SourceEditWidgetContent;
			if (sourceEditWidgetContent == null)
				return null;
			var autoComplete = sourceEditWidgetContent.mAutoComplete;
			if ((autoComplete == null) || (!autoComplete.IsShowing()))
				return null;
			return autoComplete;
		}

		[MCPTool("get_autocomplete", "The autocomplete popup and parameter-info popup for a document, if showing: the entries (index, display text, kind), which is selected, the selected entry's documentation, and the method signatures with the highlighted argument. Autocomplete opens as you type or via execute_command Autocomplete; it lives in the document, so pass file or make the document active.")]
		void GetAutocomplete(MCPCall call,
			[MCPParam("Path of an open document. Default: the active document.")] String file,
			[MCPParam("Maximum entries to list. Default 200.")] int max)
		{
			var panel = GetDocument(call);
			if (panel == null)
				return;

			var sd = scope StructuredData();
			sd.CreateNew();
			var autoComplete = GetAutoComplete(panel);
			sd.Add("shown", autoComplete != null);
			if (autoComplete == null)
			{
				Finish(call, sd);
				return;
			}

			int maxCount = (max > 0) ? max : 200;
			if (autoComplete.mInsertStartIdx != null)
				sd.Add("insertStart", autoComplete.mInsertStartIdx.mTextPos);
			sd.Add("insertEnd", autoComplete.mInsertEndIdx);
			if (autoComplete.mIsDocumentationPass)
				sd.Add("documentationPass", true);

			var listWidget = autoComplete.mAutoCompleteListWidget;
			if (listWidget != null)
			{
				sd.Add("listWidget", listWidget.mWidgetId);
				sd.Add("selectedIndex", listWidget.mSelectIdx);
				sd.Add("entryCount", listWidget.mEntryList.Count);
				if ((listWidget.mSelectIdx >= 0) && (listWidget.mSelectIdx < listWidget.mEntryList.Count))
				{
					var selected = listWidget.mEntryList[listWidget.mSelectIdx];
					if (selected.mDocumentation != null)
						sd.Add("documentation", selected.mDocumentation);
				}
				using (sd.CreateArray("entries"))
				{
					for (var entry in listWidget.mEntryList)
					{
						if (@entry.Index >= maxCount)
							break;
						using (sd.CreateObject())
						{
							sd.Add("index", @entry.Index);
							sd.Add("display", entry.mEntryDisplay ?? "");
							if (entry.mEntryType != null)
								sd.Add("kind", entry.mEntryType);
							if ((entry.mEntryInsert != null) && (entry.mEntryInsert != entry.mEntryDisplay))
								sd.Add("insert", entry.mEntryInsert);
							if (@entry.Index == listWidget.mSelectIdx)
								sd.Add("selected", true);
						}
					}
				}
			}

			var invokeWidget = autoComplete.mInvokeWidget;
			if (invokeWidget != null)
			{
				using (sd.CreateObject("invoke"))
				{
					sd.Add("widget", invokeWidget.mWidgetId);
					sd.Add("selectedIndex", invokeWidget.mSelectIdx);
					sd.Add("leftParenIdx", invokeWidget.mLeftParenIdx);
					using (sd.CreateArray("signatures"))
					{
						for (var entry in invokeWidget.mEntryList)
						{
							using (sd.CreateObject())
							{
								sd.Add("text", entry.mText ?? "");
								sd.Add("argIndex", entry.mArgMatchCount);
								if (entry.mDocumentation != null)
									sd.Add("documentation", entry.mDocumentation);
							}
						}
					}
				}
			}
			Finish(call, sd);
		}

		[MCPTool("select_autocomplete", "Accept an entry from the open autocomplete popup, by display text or index, inserting it into the document as pressing Enter on it would.")]
		void SelectAutocomplete(MCPCall call,
			[MCPParam("Display text of the entry, matched case-insensitively.")] String text,
			[MCPParam("Index of the entry from get_autocomplete. Default: the currently selected one.")] int index,
			[MCPParam("Path of an open document. Default: the active document.")] String file)
		{
			var panel = GetDocument(call);
			if (panel == null)
				return;
			var autoComplete = GetAutoComplete(panel);
			var listWidget = autoComplete?.mAutoCompleteListWidget;
			if ((listWidget == null) || (listWidget.mEntryList.IsEmpty))
			{
				call.Error("No autocomplete list is showing");
				return;
			}

			int selectIdx = listWidget.mSelectIdx;
			if (call.HasArg("index"))
				selectIdx = index;
			else if (!text.IsEmpty)
			{
				selectIdx = -1;
				for (var entry in listWidget.mEntryList)
				{
					if ((entry.mEntryDisplay != null) && (String.Compare(entry.mEntryDisplay, text, true) == 0))
					{
						selectIdx = @entry.Index;
						break;
					}
				}
				if (selectIdx == -1)
				{
					call.Error(scope $"No autocomplete entry '{text}'. Call get_autocomplete to see the list.");
					return;
				}
			}
			if ((selectIdx < 0) || (selectIdx >= listWidget.mEntryList.Count))
			{
				call.Error("Index out of range");
				return;
			}

			String inserted = scope String(listWidget.mEntryList[selectIdx].mEntryDisplay ?? "");
			listWidget.mSelectIdx = (int32)selectIdx;
			autoComplete.InsertSelection((char32)0);

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("inserted", inserted);
			AppendDocumentInfo(sd, panel, gApp.GetTab(panel));
			Finish(call, sd);
		}

		[MCPTool("close_autocomplete", "Dismiss the autocomplete and parameter-info popups without inserting anything.")]
		void CloseAutocomplete(MCPCall call,
			[MCPParam("Path of an open document. Default: the active document.")] String file)
		{
			var panel = GetDocument(call);
			if (panel == null)
				return;
			var autoComplete = GetAutoComplete(panel);
			if (autoComplete != null)
				autoComplete.Close();
			call.Result(scope $"{{\"closed\":{(autoComplete != null) ? "true" : "false"}}}");
		}

		// ---------------------------------------------------------------------------------------
		// Hover
		// ---------------------------------------------------------------------------------------

		static void AppendHoverItems(StructuredData sd, ListViewItem parentItem, int depth)
		{
			if ((parentItem.mChildItems == null) || (depth <= 0))
				return;
			using (sd.CreateArray("rows"))
			{
				for (var childItem in parentItem.mChildItems)
				{
					using (sd.CreateObject())
					{
						sd.Add("name", childItem.Label);
						if ((childItem.mSubItems != null) && (childItem.mSubItems.Count > 1))
							sd.Add("value", childItem.GetSubItem(1).Label);
						if (var watchItem = childItem as WatchListViewItem)
						{
							var watchEntry = watchItem.mWatchEntry;
							if (watchEntry != null)
							{
								if (watchEntry.mResultTypeStr != null)
									sd.Add("type", watchEntry.mResultTypeStr);
								if (watchEntry.mEvalStr != null)
									sd.Add("expression", watchEntry.mEvalStr);
							}
						}
						if (childItem.mChildItems != null)
						{
							sd.Add("expandable", true);
							sd.Add("open", childItem.IsOpen);
							AppendHoverItems(sd, childItem, depth - 1);
						}
					}
				}
			}
		}

		static void AppendHover(StructuredData sd, SourceViewPanel panel)
		{
			var hoverWatch = panel.mHoverWatch;
			bool shown = (hoverWatch != null) && (hoverWatch.mIsShown);
			sd.Add("shown", shown);
			if (!shown)
				return;
			sd.Add("widget", hoverWatch.mWidgetId);
			sd.Add("display", hoverWatch.mDisplayString);
			if (!hoverWatch.mEvalString.IsEmpty)
				sd.Add("expression", hoverWatch.mEvalString);
			if (hoverWatch.mLastError != null)
				sd.Add("error", hoverWatch.mLastError);
			if (hoverWatch.mListView != null)
				AppendHoverItems(sd, hoverWatch.mListView.GetRoot(), 4);
		}

		[MCPTool("get_hover", "The hover popup currently shown over a document, if any: the text (type information, documentation) or, while debugging, the watch value tree for the expression under the mouse.")]
		void GetHover(MCPCall call,
			[MCPParam("Path of an open document. Default: the active document.")] String file)
		{
			var panel = GetDocument(call);
			if (panel == null)
				return;
			var sd = scope StructuredData();
			sd.CreateNew();
			AppendHover(sd, panel);
			Finish(call, sd);
		}

		[MCPTool("hover_at", "Hover the mouse over a 0-based line and column in a document and wait for the hover popup, then return it: type information and documentation for a symbol, or its value while debugging. Moves the cursor there first so the position is on screen. Returns shown=false if nothing appeared within the timeout, which is also what happens over whitespace.")]
		void HoverAt(MCPCall call,
			[MCPParam("0-based line.", true)] int line,
			[MCPParam("0-based character column.", true)] int column,
			[MCPParam("Path of an open document. Default: the active document.")] String file,
			[MCPParam("Timeout in milliseconds. Default 3000.")] int timeoutMS)
		{
			var panel = GetDocument(call);
			if (panel == null)
				return;
			var window = panel.mWidgetWindow;
			if (window == null)
			{
				call.Error("The document is not in a window");
				return;
			}

			// A hover still showing from an earlier position would be reported as this one's
			if ((panel.mHoverWatch != null) && (panel.mHoverWatch.mIsShown))
				panel.mHoverWatch.Close();

			var content = panel.mEditWidget.mEditWidgetContent;
			content.CurSelection = null;
			content.CursorTextPos = content.GetTextIdx(line, column);
			content.EnsureCursorVisible();

			// Under the glyph, a few pixels in, the way a pointer resting on a word would sit
			content.GetTextCoordAtLineChar(line, column, var textX, var textY);
			content.SelfToRootTranslate(textX + 3, textY + 7, var rootX, var rootY);
			UICapture.ToDevice(window, rootX, rootY, var deviceX, var deviceY);
			window.MouseMove(deviceX, deviceY);

			int32 panelWidgetId = panel.mWidgetId;
			call.Defer((timeoutMS > 0) ? timeoutMS : 3000, new (pollCall) =>
				{
					var hoverPanel = UIToolSet.FindWidget(panelWidgetId) as SourceViewPanel;
					if (hoverPanel == null)
					{
						pollCall.Error("The document closed while waiting for the hover");
						return true;
					}
					bool shown = (hoverPanel.mHoverWatch != null) && (hoverPanel.mHoverWatch.mIsShown);
					if ((!shown) && (!pollCall.mTimedOut))
						return false;

					var sd = scope StructuredData();
					sd.CreateNew();
					AppendHover(sd, hoverPanel);
					if (!shown)
						sd.Add("note", "No hover appeared. Hovers need a symbol under the pointer, the window to have focus (see focus_window), and the resolve compiler to be idle.");
					Finish(pollCall, sd);
					return true;
				});
		}
	}
}
#endif
