#if !CLI
using System;
using System.Collections;
using Beefy.mcp;
using Beefy.utils;
using Beefy.widgets;
using IDE.ui;

namespace IDE
{
	// Changing documents: replacing text, inserting at the cursor, undo/redo and saving. Edits go
	// through the edit widget's own operations, so they land in the undo history, mark the document
	// changed, and trigger the same re-parse a keystroke would.
	class IDEEditToolSet : MCPToolSet
	{
		static void Finish(MCPCall call, StructuredData sd)
		{
			String json = scope .();
			sd.ToJSON(json);
			call.Result(json);
		}

		static void FinishWithDocument(MCPCall call, SourceViewPanel panel, StructuredData sd)
		{
			IDEEditorToolSet.AppendDocumentInfo(sd, panel, gApp.GetTab(panel));
			Finish(call, sd);
		}

		[MCPTool("edit_document", "Replace a range of a document with new text, or insert text at a position when no end is given. Positions are 0-based line and character column, as get_document reports them. The change is undoable and marks the document as modified; it is not saved until save_file.")]
		void EditDocument(MCPCall call,
			[MCPParam("Replacement text. Empty to delete the range.", true)] String text,
			[MCPParam("Start line.", true)] int line,
			[MCPParam("Start column. Default 0.")] int column,
			[MCPParam("End line of the range to replace. Omit with to_column to insert.")] int to_line,
			[MCPParam("End column (exclusive).")] int to_column,
			[MCPParam("Path of an open document. Default: the active document.")] String file)
		{
			var panel = IDEEditorToolSet.GetDocument(call);
			if (panel == null)
				return;

			var content = panel.mEditWidget.mEditWidgetContent;
			int startIdx = content.GetTextIdx(line, column);
			int endIdx = startIdx;
			if ((call.HasArg("to_line")) && (call.HasArg("to_column")))
				endIdx = content.GetTextIdx(to_line, to_column);
			if (endIdx < startIdx)
				Swap!(startIdx, endIdx);

			content.CurSelection = null;
			content.CursorTextPos = startIdx;
			if (endIdx > startIdx)
			{
				content.CurSelection = EditSelection(startIdx, endIdx);
				content.DeleteSelection();
			}
			if (!text.IsEmpty)
				content.InsertAtCursor(text);
			content.EnsureCursorVisible();

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("replacedChars", endIdx - startIdx);
			sd.Add("insertedChars", text.Length);
			FinishWithDocument(call, panel, sd);
		}

		[MCPTool("insert_text", "Insert text at the cursor of a document, replacing the selection if there is one, as typing would but without keystroke side effects such as autocomplete.")]
		void InsertText(MCPCall call,
			[MCPParam("Text to insert.", true)] String text,
			[MCPParam("Path of an open document. Default: the active document.")] String file)
		{
			var panel = IDEEditorToolSet.GetDocument(call);
			if (panel == null)
				return;

			var content = panel.mEditWidget.mEditWidgetContent;
			if (content.CurSelection != null)
				content.DeleteSelection();
			content.InsertAtCursor(text);
			content.EnsureCursorVisible();

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("insertedChars", text.Length);
			FinishWithDocument(call, panel, sd);
		}

		[MCPTool("undo", "Undo the last edits in a document, one undo step per count.")]
		void Undo(MCPCall call,
			[MCPParam("How many steps. Default 1.")] int count,
			[MCPParam("Path of an open document. Default: the active document.")] String file)
		{
			var panel = IDEEditorToolSet.GetDocument(call);
			if (panel == null)
				return;

			var content = panel.mEditWidget.mEditWidgetContent;
			int done = 0;
			for (int idx < Math.Max(count, 1))
			{
				if (!content.mData.mUndoManager.Undo())
					break;
				done++;
			}
			content.EnsureCursorVisible();

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("undone", done);
			FinishWithDocument(call, panel, sd);
		}

		[MCPTool("redo", "Redo previously undone edits in a document, one step per count.")]
		void Redo(MCPCall call,
			[MCPParam("How many steps. Default 1.")] int count,
			[MCPParam("Path of an open document. Default: the active document.")] String file)
		{
			var panel = IDEEditorToolSet.GetDocument(call);
			if (panel == null)
				return;

			var content = panel.mEditWidget.mEditWidgetContent;
			int done = 0;
			for (int idx < Math.Max(count, 1))
			{
				if (!content.mData.mUndoManager.Redo())
					break;
				done++;
			}
			content.EnsureCursorVisible();

			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("redone", done);
			FinishWithDocument(call, panel, sd);
		}

		[MCPTool("save_file", "Save a document to disk. Fails if the IDE could not write it (the reason is in get_output or a dialog).")]
		void SaveFile(MCPCall call,
			[MCPParam("Path of an open document. Default: the active document.")] String file)
		{
			var panel = IDEEditorToolSet.GetDocument(call);
			if (panel == null)
				return;

			bool saved = gApp.SaveFile(panel);
			var sd = scope StructuredData();
			sd.CreateNew();
			sd.Add("saved", saved);
			FinishWithDocument(call, panel, sd);
		}

		[MCPTool("save_all", "Save every modified document, plus the workspace and project files if they changed.")]
		void SaveAll(MCPCall call)
		{
			bool saved = gApp.SaveAll();
			call.Result(scope $"{{\"saved\":{saved ? "true" : "false"}}}");
		}
	}
}
#endif
