using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.gfx;
using System.IO;

namespace IDE.ui
{
    public class OutputWidgetContent : SourceEditWidgetContent
    {
		public bool mRemapToHighestCompileIdx;
		public List<Widget> mActionWidgets = new .() ~ delete _;

        public this()
        {
            mIsReadOnly = true;
        }

		public Event<delegate bool(int, int)> mGotoReferenceEvent ~ _.Dispose();
        public bool GotoRefrenceAtLine(int line, int lineOfs = 0)
        {
            //bool selectLine = true;
            //Match match = null;
            int lineStart = 0;
            int lineEnd = 0;

            int lineCheck = Math.Max(0, line + lineOfs);            
            GetLinePosition(lineCheck, out lineStart, out lineEnd);
            
            mSelection = EditSelection(lineStart, lineEnd);
			var selectionText = scope String();
            GetSelectionText(selectionText);

			if (selectionText.Length > 1024) // Remove middle
				selectionText.Remove(1024/2, selectionText.Length - 1024);

			int32 errLine = 0;
			int32 errLineChar = 0;
			String filePath = null;

			bool success = false;
			for (var dlg in mGotoReferenceEvent)
				if (dlg(line, lineOfs))
				{
					success = true;
					break;
				}	

			int32 inTextPos = -1;

			for (int32 i = 1; i < selectionText.Length; i++)
			{
				if (success)
					break;

				if (selectionText[i] == ':')
				{
					if (selectionText[i - 1] == ')')
					{
						int32 startIdx = i;
						while ((startIdx > 0) && (selectionText[startIdx - 1] != '('))
							startIdx--;

						String lineStr = scope String(selectionText, startIdx, i - startIdx - 1);
						var lineResult = Int32.Parse(lineStr);
						if (lineResult case .Ok(out errLine))
						{
							//errLine = lineResult.Value;
							filePath = scope:: String(selectionText, 0, startIdx - 1);
							filePath.Trim();
							if ((filePath.Contains('\\')) || (filePath.Contains('/')))
								break;
						}
						else
						{
							// filePath:(line,col)
							int commaPos = lineStr.IndexOf(',');
							if (commaPos != -1)
							{
								if ((Int32.Parse(lineStr.Substring(0, commaPos)) case .Ok(out errLine)) &&
									(Int32.Parse(lineStr.Substring(commaPos + 1)) case .Ok(out errLineChar)))
								{
									filePath = scope:: String(selectionText, 0, startIdx - 1);
									filePath.Trim();
									if ((filePath.Contains('\\')) || (filePath.Contains('/')))
										break;
								}
							}
						}
					}
					else
					{
						int32 startIdx = i;
						while ((startIdx > 0) && (selectionText[startIdx - 1].IsNumber))
							startIdx--;
	
						if ((startIdx > 0) && (selectionText[startIdx - 1] == ':'))
						{ 
                            // :num:
							int32 columnStartIdx = startIdx;						
							startIdx--;
							while ((startIdx > 0) && (selectionText[startIdx - 1].IsNumber))
								startIdx--;

							String lineStr = scope String(selectionText, startIdx, columnStartIdx - startIdx - 1);
							String columnStr = scope String(selectionText, columnStartIdx, i - columnStartIdx);

							if ((startIdx > 0) && (selectionText[startIdx - 1] == ':'))
							{
                                // fileName:line:column:
								var lineResult = Int32.Parse(lineStr);
								var columnResult = Int32.Parse(columnStr);
	
								if ((lineResult case .Ok(out errLine)) && (columnResult case .Ok(out errLineChar)))
								{
									//errLine = lineResult.Value;
									//errLineChar = columnResult.Value;
									filePath = scope:: String(selectionText, 0, startIdx - 1);
									filePath.Trim();
								}			
	
								//lineResult.Dispose();
								//columnResult.Dispose();
							}
							continue;
						}

						int32 endIdx = i;
						while ((endIdx < selectionText.Length - 1) && (selectionText[endIdx + 1].IsNumber))
							endIdx++;
	
						String lineStr = scope String(selectionText, startIdx, i - startIdx);
						String columnStr = scope String(selectionText, i + 1, endIdx - i);
	
						var lineResult = Int32.Parse(lineStr);
						var columnResult = Int32.Parse(columnStr);

						if ((lineResult case .Ok) && (columnResult case .Ok))
						{
							errLine = lineResult;
							errLineChar = columnResult;
						}

						/*if (lineResult.HasValue && columnResult.HasValue)
						{
							errLine = lineResult.Value;
							errLineChar = columnResult.Value;
						}
	
						lineResult.Dispose();
						columnResult.Dispose();*/
					}
				}

				// Match " in "
				if ((errLine > 0) && (i > 3) && (errLine != -1) && (selectionText[i] == ' ') && (selectionText[i - 1] == 'n') && (selectionText[i - 2] == 'i') && (selectionText[i - 3] == ' '))
				{
					inTextPos = i + 1;

					//filePath = stack String(selectionText, i + 1);
					//break;
				}
			}

			if (inTextPos != -1)
			{
				filePath = scope:: String(selectionText, inTextPos);
			}

			if (filePath == null)
			{
				if (selectionText.StartsWith("ERROR: "))
				{
					int linePos = selectionText.IndexOf(" at line ");
					if (linePos != -1)
					{
						int lineStartIdx = linePos + " at line ".Length;
						int spacePos = selectionText.IndexOf(" in ", lineStartIdx);
						if (spacePos != -1)
						{
							int nameStartPos = spacePos + " in ".Length;
							if (int32.Parse(.(selectionText, lineStartIdx, spacePos - lineStartIdx)) case .Ok(out errLine))
							{
								filePath = scope:: String(selectionText, nameStartPos);
							}
						}
					}
				}

				if (selectionText.StartsWith(" "))
				{
					int colonPos = selectionText.IndexOf(": ");
					if (colonPos > 0)
					{
						String testPath = scope String(selectionText, 1, colonPos - 1);
						if (File.Exists(testPath))
						{
							filePath = scope:: String(testPath);
						}
					}
				}
			}

			if ((errLine != -1) && (errLineChar != -1) && (filePath != null))
			{
				if (filePath.StartsWith("$Emit"))
				{
					// Is good
				}
				else if ((!filePath.Contains('\\')) && (!filePath.Contains('/')) && (!filePath.Contains('.')))
					return false;
				IDEApp.sApp.CheckProjectRelativePath(filePath);

				IDEUtils.FixFilePath(filePath);
				IDEApp.sApp.ShowSourceFileLocation(filePath, -1, mRemapToHighestCompileIdx ? IDEApp.sApp.mWorkspace.GetHighestCompileIdx() : -1, errLine - 1, errLineChar - 1, LocatorType.Always);
				success = true;
			}

			if (success)
			{
				bool selectLine = lineOfs == 0;
				if (!selectLine)
				    mSelection = null;
				CursorTextPos = lineStart;
				EnsureCursorVisible();
				return true;
			}

            return false;
        }

		public override void MouseClicked(float x, float y, float origX, float origY, int32 btn)
		{
			base.MouseClicked(x, y, origX, origY, btn);

			if (btn == 1)
			{
				float useX = x;
				float useY = y;
				ClampMenuCoords(ref useX, ref useY);

				Menu menu = new Menu();

				var menuItem = menu.AddItem("Clear All");
				var panel = mParent.mParent.mParent as TextPanel;
				menuItem.mOnMenuItemSelected.Add(new (evt) =>
			    {
					if (panel != null)
						panel.Clear();
					else
			        	ClearText();
			    });

				MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);
				menuWidget.Init(this, useX, useY);
			}
		}

        public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
        {            
            if ((btn == 0) && (btnCount > 1))
            {
                for (int32 lineOfs = 0; lineOfs >= -1; lineOfs--)
                    if (GotoRefrenceAtLine(CursorLineAndColumn.mLine, lineOfs))
                        return;
            }

            base.MouseDown(x, y, btn, btnCount);
        }

		public override void KeyDown(KeyCode keyCode, bool isRepeat)
		{
			base.KeyDown(keyCode, isRepeat);

			if (keyCode == .Apps)
			{
				GetTextCoordAtCursor(var x, var y);
				MouseClicked(x, y, x, y, 1);
			}
		}

		public override void TextChanged()
		{
			base.TextChanged();

			for (var widget in mActionWidgets)
			{
				widget.RemoveSelf();
				delete widget;
			}
			mActionWidgets.Clear();
		}
    }

    public class OutputWidget : SourceEditWidget
    {
        public this()
            : base(null, new OutputWidgetContent())
        {            
        }

		public override void DrawAll(Beefy.gfx.Graphics g)
		{
			base.DrawAll(g);

			/*for (int i < 10)
			{
				var testStr = scope String();

				for (int j < 10)
				{
					Font.StrEncodeColor(0xfffebd57, testStr);
					testStr.Append('A' + j);
					Font.StrEncodePopColor(testStr);
				}

				g.DrawString(testStr, 20, 300 + i * 20);
			}*/
		}
    }
}
