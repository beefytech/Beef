using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using Beefy.widgets;
using Beefy.utils;
using Beefy;
using Beefy.gfx;
using Beefy.theme.dark;
using IDE.ui;

namespace IDE
{
    public static class IDEUtils
    {
		public const char8 cNativeSlash = Path.DirectorySeparatorChar;
		public const char8 cOtherSlash = Path.AltDirectorySeparatorChar;        

		public static void AppendWithOptionalQuotes(String targetStr, String srcFileName)
		{
		    if (!srcFileName.Contains(' '))
			    targetStr.Append(srcFileName);
			else
				targetStr.Append("\"", srcFileName, "\"");
		}

        public static bool FixFilePath(String filePath, char8 wantSlash, char8 otherSlash)
        {
            if (filePath.Length == 0)
                return false;

            if (filePath[0] == '<')
                return false;

            if ((filePath.Length > 1) && (filePath[1] == ':'))
				filePath[0] = filePath[0].ToLower;

			bool prevWasSlash = false;
            for (int i = 0; i < filePath.Length; i++)
            {
				if (filePath[i] == otherSlash)
					filePath[i] = wantSlash;

				if (filePath[i] == wantSlash)
				{
					if ((prevWasSlash) && (i > 1))
					{
						filePath.Remove(i);
						i--;
						continue;
					}

					prevWasSlash = true;
				}
				else
					prevWasSlash = false;

                if ((i >= 4) && (filePath[i - 3] == wantSlash) && (filePath[i - 2] == '.') && (filePath[i - 1] == '.') && (filePath[i] == wantSlash))
                {
                    int prevSlash = filePath.LastIndexOf(wantSlash, i - 4);
                    if (prevSlash != -1)
                    {
						if ((i - prevSlash != 6) || (filePath[prevSlash + 1] != '.') || (filePath[prevSlash + 2] != '.'))
						{
                            filePath.Remove(prevSlash, i - prevSlash);
                        	i = prevSlash;
						}
                    }
                }
            }
			return true;
        }

		public static bool IsDirectoryEmpty(StringView dirPath)
		{
			for (let entry in Directory.Enumerate(scope String()..AppendF("{}/*.*", dirPath), .Directories | .Files))
			{
				return false;
			}
			return true;
		}

		public static bool FixFilePath(String filePath)
		{
			return FixFilePath(filePath, cNativeSlash, cOtherSlash);
		}

		public static bool MakeComparableFilePath(String filePath)
		{
			bool success = FixFilePath(filePath, cNativeSlash, cOtherSlash);
			if (!Environment.IsFileSystemCaseSensitive)
				filePath.ToUpper();
			return success;
		}

		public static bool CanonicalizeFilePath(String filePath)
		{
			return FixFilePath(filePath, '/', '\\');
		}

		public static void AddSortedEntries(List<String> stringList, IEnumerator<String> strs)
		{
			for (var str in strs)
				stringList.Add(str);
			//TODO: Sort
		}

        public static bool IsHeaderFile(String fileName)
        {
            return fileName.EndsWith(".h", StringComparison.OrdinalIgnoreCase) || fileName.EndsWith(".hpp", StringComparison.OrdinalIgnoreCase);
        }

        public static int64 GetLastModifiedTime(String filePath)
        {
            DateTime dt = File.GetLastWriteTime(filePath);
            //return dt.ToFileTime();

			//ThrowUnimplemented();
			return dt.ToFileTime();
        }

        public static void SelectNextChildWidget(Widget parentWidget, bool doBackwards)
        {
            Widget firstTabWidget = null;
            Widget lastTabWidget = null;
            bool prevWasSelected = false;

            for (var childWidget in parentWidget.mChildWidgets)
            {
                if ((!doBackwards) && (prevWasSelected))
                {
                    childWidget.SetFocus();
                    return;
                }

                if ((childWidget.mHasFocus) && (lastTabWidget != null))
                {
                    lastTabWidget.SetFocus();
                    return;
                }

                prevWasSelected = childWidget.mHasFocus;

                if (firstTabWidget == null)
                    firstTabWidget = childWidget;
                lastTabWidget = childWidget;
            }
            
            if ((!doBackwards) && (firstTabWidget != null))
                firstTabWidget.SetFocus();

            if ((doBackwards) && (lastTabWidget != null))
                lastTabWidget.SetFocus();
        }

        public static void SerializeListViewState(StructuredData data, ListView listView)
        {
            using (data.CreateArray("Columns", true))
            {
                for (var column in listView.mColumns)
                {
                    using (data.CreateObject())
                    {                        
                        data.Add("Width", column.mWidth);
                    }
                }
            }            
        }

        public static void DeserializeListViewState(StructuredData data, ListView listView)
        {
            //using (data.Open("Columns"))
			int columnIdx = 0;
			for (var _t in data.Enumerate("Columns"))
            {
                //for (int32 columnIdx = 0; columnIdx < data.Count; columnIdx++)
                {
                    //using (data.Open(@columnKV))
                    {
                        listView.mColumns[columnIdx].mWidth = data.GetFloat("Width");
                    }

					++columnIdx;
                }
            }
        }

        public static void DrawWait(Graphics g, float x, float y, int updateCnt)
        {
            var image = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.WaitSegment);
            image.mPixelSnapping = PixelSnapping.Never;

            float colorPct = updateCnt / 100.0f;
            colorPct -= (int32)colorPct;

            for (int32 i = 0; i < 8; i++)
            {
                float brightness = 1.0f;
                float wheelPct = i / 8.0f;

                float dist;
                if (wheelPct > colorPct)                
                    dist = (1.0f - wheelPct) + colorPct;                
                else                
                    dist = colorPct - wheelPct;                
                
                brightness -= dist * 1.0f;

                Matrix mat = Matrix.IdentityMatrix;
                mat.Translate(-9.5f, -14);
                mat.Rotate(Math.PI_f * i / 4);
                mat.Translate(x, y);
                using (g.PushColor(Color.Get(brightness)))
                    using (g.PushMatrix(mat))
                        g.Draw(image);
            }
        }

		public static void DrawOutline(Graphics g, Widget widget, int inflateX, int inflateY, uint32 color)
		{
		    using (g.PushColor(color))
		    {
		        g.OutlineRect(widget.mX - inflateX, widget.mY - inflateY, widget.mWidth + inflateX * 2, widget.mHeight + inflateY * 2);
		    }
		}

		public static void DrawOutline(Graphics g, Widget widget, int inflateX = 0, int32 inflateY = 0)
		{
		    DrawOutline(g, widget, inflateX, inflateY, IDEApp.cDialogOutlineLightColor);
			DrawOutline(g, widget, inflateX - 1, inflateY - 1, IDEApp.cDialogOutlineDarkColor);
		}

		public static void ClampMenuCoords(ref float x, ref float y, ScrollableWidget scrollableWidget, Insets insets = null)
		{
			var insets;
			if (insets == null)
				insets = scope:: .();

			var visibleContentRange = scrollableWidget.GetVisibleContentRange();
			if (y > visibleContentRange.Bottom)
				y = visibleContentRange.Bottom - insets.mBottom;
			if (y < visibleContentRange.Top)
				y = visibleContentRange.Top + insets.mTop;
			if (x > visibleContentRange.Right)
				x = visibleContentRange.Right - insets.mRight;
			if (x < visibleContentRange.Left)
				x = visibleContentRange.Left + insets.mLeft;
		}

		public static void DrawLock(Graphics g, float x, float y, bool isLocked, float pct)
		{
			if (pct > 0)
			{
				int32 circleCount = 2;
				for (int32 i = 0; i < circleCount; i++)
				{
				    float sepPct = 0.3f;
				    float maxSep = (circleCount - 2) * sepPct;
				    float circlePct = (pct - maxSep + (i * 0.3f)) / (1.0f - maxSep);
				    if ((circlePct < 0.0f) || (circlePct > 1.0f))
				        continue;

				    float scale = (float)Math.Sin(circlePct * Math.PI_f / 2) * 0.8f;
				    float alpha = Math.Min(1.0f, (1.0f - (float)Math.Sin(circlePct * Math.PI_f / 2)) * 1.0f);

					using (g.PushTranslate(x - GS!(22), y - GS!(22)))
					{
					    using (g.PushColor(Color.Get(0x00FF0000, alpha)))
					    {
					        using (g.PushScale(scale, scale, GS!(32), GS!(32)))
					            g.Draw(IDEApp.sApp.mCircleImage);
					    }                
					}
				}
			}

			using (g.PushColor(isLocked ? 0xFFFFFFFF : 0x80000000))
			{
				var x;
				var y;
				if (pct > 0)
				{
					let rand = scope Random((int32)(Math.Pow(pct, 0.6f) * 20));
					x += (float)rand.NextDoubleSigned() * (1.5f - pct);
					y += (float)rand.NextDoubleSigned() * (1.5f - pct);
				}

				g.Draw(DarkTheme.sDarkTheme.GetImage(.LockIcon), x, y);
			}
		}

		public static void InsertColorChange(String str, int idx, uint32 color)
		{
			char8* insertChars = scope char8[5]*;
			insertChars[0] = (char8)1;
			*(uint32*)(insertChars + 1) = (color >> 1) & 0x7F7F7F7F;
			str.Insert(idx, scope String(insertChars, 5));
		}

		public static void ModifyColorChange(String str, int idx, uint32 color)
		{
			char8* insertPos = str.Ptr + idx;
			*(uint32*)(insertPos + 1) = (color >> 1) & 0x7F7F7F7F;
		}

		public enum CodeKind
		{
			Callstack,
			Method,
			Field,
			Type
		}

		public static void ColorizeCodeString(String label, CodeKind codeKind)
		{
			int prevTypeColor = -1;
			int prevStart = -1;
			bool foundOpenParen = codeKind != .Callstack;
			// Check to see if this is just a Mixin name, don't mistake for the bang that separates module name 
			bool awaitingBang = label.Contains('!') && !label.EndsWith("!"); 
			bool awaitingParamName = false;
			int chevronCount = 0;
			int parenCount = 0;

			int lastTopStart = -1;
			int lastTopEnd = -1;

			bool inSubtype = false;

			bool IsIdentifierChar(char8 c)
			{
				return (c.IsLetterOrDigit) || (c == '_') || (c == '$') || (c == '@');
			}

			bool IsIdentifierStart(char8 c)
			{
				return (c.IsLetter) || (c == '_');
			}

			for (int32 i = 0; i < label.Length; i++)
			{
				char8 c = label[i];
				if ((c == '0') && (i == 0))
					break; // Don't colorize addresses

				if ((c == '<') && (i == 0))
				{
					uint32 color = 0xFFA0A0A0;//SourceEditWidgetContent.sTextColors[(int)SourceElementType.Comment];
					InsertColorChange(label, 0, color);
					label.Append('\x02');
					break;
				}

				if (awaitingBang)
				{
					if ((c == ':') || (c == '<'))
					{
		                awaitingBang = false;
						i = -1;
						continue;
					}

					if (c == '!')
					{
						bool endNow = false;

						if (i + 1 < label.Length)
						{
		                    char16 nextC = label[i + 1];
							if ((nextC == '(') || (nextC == '='))
							{
								awaitingBang = false;
								i = -1;
								continue;
							}
							else if ((nextC == '0') || (nextC == '<'))
							{
								endNow = true; // Just a raw string
							}
						}

						uint32 color = 0xFFA0A0A0;//SourceEditWidgetContent.sTextColors[(int)SourceElementType.Comment];
						InsertColorChange(label, 0, color);
						awaitingBang = false;

						i += 5;
						label.Insert(i, '\x02');

						if (endNow)
						{
							InsertColorChange(label, i + 2, SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Method]);
		                    break;
						}
					}
				}
				else if (c == '$')
				{
					uint32 color = 0xFF80A080;//SourceEditWidgetContent.sTextColors[(int)SourceElementType.Comment];
					InsertColorChange(label, i, color);
					i += 5;
				}
				else if (IsIdentifierChar(c))
				{					
					if ((prevStart == -1) && (!awaitingParamName))
						prevStart = i;
				}
				else
				{
					int setNamespaceIdx = -1;

					if ((!inSubtype) && (prevTypeColor != -1))
					{
						setNamespaceIdx = prevTypeColor;
					}

					bool isSubtypeSplit = false;
					char8 nextC = (i < label.Length - 1) ? label[i + 1] : 0;

					// Check for internal '+' subtype encoding
					if ((c == '+') && (IsIdentifierStart(nextC)))
					{
						isSubtypeSplit = true;
						inSubtype = true;
						label[i] = '.';
					}

					if (prevStart != -1)
					{
						//label.Insert(prevStart, SourceEditWidgetContent.sTextColors[(int)SourceElementType.TypeRef]);
						/*uint32 color = SourceEditWidgetContent.sTextColors[
							inSubtype ? (int32)SourceElementType.TypeRef : (int32)SourceElementType.Namespace
							];*/

						uint32 color = SourceEditWidgetContent.sTextColors[(int32)SourceElementType.TypeRef];

						/*if ((c == '+') || (c == '('))
						{
							foundOpenParen = true;
							color = SourceEditWidgetContent.sTextColors[(int)SourceElementType.Method];
						}*/

						if (chevronCount == 0)
						{
							lastTopStart = prevStart;
							lastTopEnd = i;
						}

						prevTypeColor = prevStart;
						InsertColorChange(label, prevStart, color);
						i += 5;

						label.Insert(i, '\x02');
						prevStart = -1;
						awaitingParamName = false;

						i++;
					}

					

					if (c == ',')
						awaitingParamName = false;

					if ((c == ')') && (parenCount > 0))
						parenCount--;

					if ((c != '+') && (c != '.'))
					{
						inSubtype = false;
						prevTypeColor = -1;
					}

					if ((c == '(') || ((c == '+') && (!isSubtypeSplit)))
						setNamespaceIdx = -1;

					if (setNamespaceIdx != -1)
						ModifyColorChange(label, setNamespaceIdx, SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Namespace]);

					if (isSubtypeSplit)
					{
						// Handled
					}
					else if ((c == '(') && ((i == 0) || (chevronCount > 0)))
					{
						parenCount++;
					}
					else if ((c == '(') || (c == '+'))
					{
						foundOpenParen = true;
						if (lastTopStart != -1)
						{
							char8* insertChars = label.CStr() + lastTopStart;
							uint32 color = SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Method];
							*(uint32*)(insertChars + 1) = (color >> 1) & 0x7F7F7F7F;
						}
						else
						{
							int checkIdx = i - 1;
							while (checkIdx > 0)
							{
								char8 checkC = label[checkIdx];
								if (checkC == ':')
								{
									checkIdx++;
		                            break;
								}
								checkIdx--;
							}
							if (checkIdx >= 0)
							{
		                        InsertColorChange(label, checkIdx, SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Method]);
								i += 5;
							}
						}
					}

					if ((foundOpenParen) && (!awaitingParamName) && (chevronCount == 0))
					{
						if (c == ' ')
						{							
							bool nextIsName = true;
							int32 spaceCount = 0;
							for (int32 checkIdx = i + 1; checkIdx < label.Length; checkIdx++)
							{
								char8 checkC = label[checkIdx];
								if (checkC == ' ')
								{
									spaceCount++;
									if (spaceCount > 1)
										nextIsName = false;
								}
								if ((checkC == '<') || (checkC == '*') || (checkC == '['))
									nextIsName = false;
								if ((checkC == ',') || (checkC == ')'))
								{
									if (spaceCount > 0)
										nextIsName = false;
									break;
								}
							}

							if (nextIsName)
								awaitingParamName = true;
						}						
					}
				}

				if (c == '<')
					chevronCount++;
				else if (c == '>')
					chevronCount--;
			}

			if ((prevStart != -1) && (codeKind == .Type))
			{
				InsertColorChange(label, prevStart, SourceEditWidgetContent.sTextColors[(int32)SourceElementType.Type]);
			}
		}
    }
}

static
{
	public static mixin Set(String val1, String val2)
	{
		val1.Set(val2);
	}

	public static mixin Set(List<String> val1, List<String> val2)
	{
		ClearAndDeleteItems(val1);
		for (var str in val2)
			val1.Add(new String(str));
	}

	public static mixin Set(var val1, var val2)
	{
		val1 = val2;
	}
}
