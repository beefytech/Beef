using System;
using System.Collections;
using System.Text;
using System.IO;
using Beefy.geom;
using Beefy.utils;
using System.Diagnostics;
using System.Threading;

namespace Beefy.gfx
{
    public enum FontOverflowMode
    {            
        Overflow,
        Clip,
        Truncate,
        Wrap,
        Ellipsis
    }
																													  
    public enum FontAlign
    {
        Left = -1,
        Centered,
        Right
    }

    public struct FontMetrics
    {
        public int32 mLineCount;
        public float mMinX;
        public float mMinY;
        public float mMaxX;
        public float mMaxY;

        public float mMaxWidth;
    }

    public class Font
    {
		[CallingConvention(.Stdcall), CLink]
		static extern FTFont* FTFont_Load(char8* fileName, float pointSize);

		[CallingConvention(.Stdcall), CLink]
		static extern void FTFont_Delete(FTFont* ftFont, bool cacheRetain);

		[CallingConvention(.Stdcall), CLink]
		static extern void FTFont_ClearCache();

		[CallingConvention(.Stdcall), CLink]
		static extern FTGlyph* FTFont_AllocGlyph(FTFont* ftFont, int32 char8Code, bool allowDefault);

		[CallingConvention(.Stdcall), CLink]
		static extern int32 FTFont_GetKerning(FTFont* font, int32 char8CodeA, int32 char8CodeB);

		static Dictionary<String, String> sFontNameMap ~ DeleteDictionaryAndKeysAndValues!(_);
		static Dictionary<String, String> sFontFailMap ~ DeleteDictionaryAndKeysAndValues!(_);
		static Monitor sMonitor = new .() ~ delete _;

		struct FTFont
		{
			public int32 mHeight;
			public int32 mAscent;
			public int32 mDescent;
			public int32 mMaxAdvance;
		}

		struct FTGlyph
		{
			public void* mPage;
			public void* mTextureSegment;
			public int32 mX;
			public int32 mY;
			public int32 mWidth;
			public int32 mHeight;
			public int32 mXOffset;
			public int32 mYOffset;
			public int32 mXAdvance;
		}

        public class CharData
        {
            public Image mImageSegment ~ delete _;
            public int32 mX;
            public int32 mY;
            public int32 mWidth;
            public int32 mHeight;
            public int32 mXOffset;
            public int32 mYOffset;
            public int32 mXAdvance;
			public bool mIsCombiningMark;
        }

        public class Page
        {
            public Image mImage ~ delete _;
            public bool mIsCorrectedImage = true;
        }

		public struct AltFont
		{
			public Font mFont;
			public bool mOwned;
		}

		class MarkRefData
		{
			public float mTop;
			public float mBottom;
		}

		enum MarkPosition
		{
			AboveC, // Center
			AboveR, // Left edge of mark aligned on center of char
			AboveE, // Center of mark aligned on right edge of char
			BelowC,
			BelowR,
			OverC,
			OverE,
			TopR, // Center of edge aligned to top of char
		}

        const int32 LOW_CHAR_COUNT = 128;

        Dictionary<char32, CharData> mCharData;
        CharData[] mLowCharData;
		FTFont* mFTFont;
		String mPath;
		List<AltFont> mAlternates;
		MarkRefData mMarkRefData ~ delete _;
		float[] mLoKerningTable;
		public StringView mEllipsis = "...";

        public this()
        {
        }

		public ~this()
		{
			Dispose();
		}

		public static ~this()
		{
			FTFont_ClearCache();
		}

		static void BuildFontNameCache()
		{
#if BF_PLATFORM_WINDOWS
			using (sMonitor.Enter())
			{
				sFontNameMap = new .();

				for (int pass < 2)
				{
					Windows.HKey hkey;

					if (pass == 0)
					{
						if (Windows.RegOpenKeyExA(Windows.HKEY_LOCAL_MACHINE, @"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts", 0,
							Windows.KEY_QUERY_VALUE | Windows.KEY_WOW64_32KEY | Windows.KEY_ENUMERATE_SUB_KEYS, out hkey) != Windows.S_OK)
							continue;
					}
					else
					{
						if (Windows.RegOpenKeyExA(Windows.HKEY_CURRENT_USER, @"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts", 0,
							Windows.KEY_QUERY_VALUE | Windows.KEY_WOW64_32KEY | Windows.KEY_ENUMERATE_SUB_KEYS, out hkey) != Windows.S_OK)
							continue;
					}

					defer Windows.RegCloseKey(hkey);

					for (int32 i = 0; true; i++)
					{
						char16[256] fontNameArr;
						uint32 nameLen = 255;
						uint32 valType = 0;

						char16[256] data;
						uint32 dataLen = 256 * 2;
						int32 result = Windows.RegEnumValueW(hkey, i, &fontNameArr, &nameLen, null, &valType, &data, &dataLen);
						if (result == 0)
						{
							if (valType == 1)
							{
								String fontName = scope String(&fontNameArr);
								int parenPos = fontName.IndexOf(" (");
								if (parenPos != -1)
									fontName.RemoveToEnd(parenPos);
								fontName.ToUpper();
								String fontPath = scope String(&data);
								if ((!fontPath.EndsWith(".TTF", .OrdinalIgnoreCase)) && (!fontPath.EndsWith(".TTC", .OrdinalIgnoreCase)))
									continue;

								if (fontName.Contains('&'))
								{
									int collectionIdx = 0;
									for (var namePart in fontName.Split('&', .RemoveEmptyEntries))
									{
										namePart.Trim();
										if (sFontNameMap.TryAddAlt(namePart, var keyPtr, var valuePtr))
										{
											*keyPtr = new String(namePart);
											*valuePtr = new $"{fontPath}@{collectionIdx}";
											collectionIdx++;
										}
									}
								}
								else if (sFontNameMap.TryAdd(fontName, var keyPtr, var valuePtr))
								{
									*keyPtr = new String(fontName);
									*valuePtr = new String(fontPath);
								}
							}
						}
						else
						{
							if (result == Windows.ERROR_MORE_DATA)
								continue;

							break;
						}
					}
				}
			}
#endif
		}

		public static void ClearFontNameCache()
		{
			using (sMonitor.Enter())
			{
				DeleteDictionaryAndKeysAndValues!(sFontNameMap);
				sFontNameMap = null;
			}
		}

		public static void AddFontFailEntry(StringView mapFrom, StringView mapTo)
		{
			using (sMonitor.Enter())
			{
				if (sFontFailMap == null)
					sFontFailMap = new .();

				String str = new String(mapFrom);
				str.ToUpper();
				bool added = sFontFailMap.TryAdd(str, var keyPtr, var valuePtr);
				if (added)
				{
					*keyPtr = str;
					*valuePtr = new String(mapTo);
				}
				else
				{
					delete str;
					(*valuePtr).Set(mapTo);
				}
			}
		}

		public void Dispose(bool cacheRetain)
		{
			if (mFTFont != null)
			{
				FTFont_Delete(mFTFont, cacheRetain);
				mFTFont = null;
			}

			if (mLowCharData != null)
			{
				for (var charData in mLowCharData)
					delete charData;
				DeleteAndNullify!(mLowCharData);
			}

			if (mCharData != null)
			{
				for (var charData in mCharData.Values)
					delete charData;
				DeleteAndNullify!(mCharData);
			}

			if (mAlternates != null)
			{
				for (var altFont in mAlternates)
					if (altFont.mOwned)
						delete altFont.mFont;
				DeleteAndNullify!(mAlternates);
			}

			DeleteAndNullify!(mPath);
			DeleteAndNullify!(mLoKerningTable);
		}

		public void Dispose()
		{
			Dispose(false);
		}

		public static void ClearCache()
		{
			FTFont_ClearCache();
		}

		public void AddAlternate(Font altFont)
		{
			AltFont altFontEntry;
			altFontEntry.mFont = altFont;
			altFontEntry.mOwned = false;
			mAlternates.Add(altFontEntry);
		}

		public Result<void> AddAlternate(String path, float pointSize = -1)
		{
			Font altFont = Try!(LoadFromFile(path, pointSize));
			AltFont altFontEntry;
			altFontEntry.mFont = altFont;
			altFontEntry.mOwned = true;
			mAlternates.Add(altFontEntry);
			return .Ok;
		}
		
        public bool GetVal(String outVal, List<String> cmds, String find)
        {            
            for (String cmd in cmds)
            {
                if (cmd.StartsWith(find) && (cmd[find.Length] == '='))
				{
                    outVal.Append(cmd, find.Length + 1);
					return true;
				}
            }

            return false;
        }

        public int32 GetValInt(List<String> cmds, String find)
        {
			String strVal = scope String();
			if (!GetVal(strVal, cmds, find))
				return 0;
            return int32.Parse(strVal);
        }

		public void CalcKerning()
		{
			for (char8 c0 = ' '; c0 < '\x80'; c0++)
			{
				for (char8 c1 = ' '; c1 < '\x80'; c1++)
				{
					float kernVal = FTFont_GetKerning(mFTFont, (int32)c0, (int32)c1);
					if (kernVal != 0)
					{
						if (mLoKerningTable == null)
							mLoKerningTable = new float[128*128];
						mLoKerningTable[(int32)c0 + ((int32)c1)*128] = kernVal;
					}
				}
			}
		}

		public float GetKerning(char32 c0, char32 c1)
		{
			if (mLoKerningTable == null)
				return 0;

			if ((c0 < '\x80') && (c1 < '\x80'))
			{
				return mLoKerningTable[(int32)c0 + ((int32)c1)*128];
			}

			return FTFont_GetKerning(mFTFont, (int32)c0, (int32)c1);
		}

		void GetFontPath(StringView fontName, String path)
		{
			if (fontName.Contains('.'))
			{
				Path.GetAbsolutePath(fontName, BFApp.sApp.mInstallDir, path);
			}
			else
			{
				using (sMonitor.Enter())
				{
					if (sFontNameMap == null)
						BuildFontNameCache();
					String pathStr;
					let lookupStr = scope String(fontName)..ToUpper();
#if BF_PLATFORM_WINDOWS
					if (sFontNameMap.TryGetValue(lookupStr, out pathStr))
					{
						if (!pathStr.Contains(':'))
						{
							char8[256] windowsDir;
							Windows.GetWindowsDirectoryA(&windowsDir, 256);
							path.Append(&windowsDir);
							path.Append(@"\Fonts\");
						}

						path.Append(pathStr);
						return;
					}
#endif
					if ((sFontFailMap != null) && (sFontFailMap.TryGetValue(lookupStr, out pathStr)))
					{
						path.Append(pathStr);
						return;
					}
				}
			}
		}

        public bool Load(StringView fontName, float pointSize = -1)
        {
			Dispose();

			mCharData = new Dictionary<char32, CharData>();
			mLowCharData = new CharData[LOW_CHAR_COUNT];
			mAlternates = new List<AltFont>();

			float usePointSize = pointSize;
			mPath = new String();
			GetFontPath(fontName, mPath);

			if (mPath.IsEmpty)
				return false;

			String fontPath = scope String(mPath);
			if (pointSize == -1)
			{
				fontPath.Set(BFApp.sApp.mInstallDir);
				fontPath.Append("fonts/SourceCodePro-Regular.ttf");
				usePointSize = 9;
			}
			mFTFont = FTFont_Load(fontPath, usePointSize);
			if (mFTFont == null)
				return false;
			
			CalcKerning();
            return true;
        }

        public static Result<Font> LoadFromFile(String path, float pointSize = -1)
        {
			scope AutoBeefPerf("Font.LoadFromFile");

            Font font = new Font();
            if (!font.Load(path, pointSize))
			{
				delete font;
                return .Err; //TODO: Make proper error
			}
            return font;
        }

        public bool HasChar(char32 theChar)
        {
			return true;
            /*if ((theChar >= (char8)0) && (theChar < (char8)LOW_CHAR_COUNT))
                return mLowCharData[(int)theChar] != null;
            return mCharData.ContainsKey(theChar);*/
        }

        public float GetWidth(StringView theString)
        {
            float curX = 0;

			int len = theString.Length;
			if (len == 0)
			{
				return 0;
			}

            char32 prevChar = (char8)0;
			for (var c in theString.DecodedChars)
			{
				int idx = @c.NextIndex;
				if (c == (char32)'\x01')
				{
					@c.NextIndex = idx + 4;
					continue;
				}
				else if (c == (char32)'\x02')
				{
					continue;
				}
                CharData charData = GetCharData((char32)c);
				if ((charData != null) && (!charData.mIsCombiningMark))
				{
	                curX += charData.mXAdvance;
	                if (prevChar != (char8)0)
	                {
	                    float kernAmount = GetKerning(prevChar, c);
	                    curX += kernAmount;
	                }
				}

                prevChar = c;

				if (idx >= len)
					break;
            }

            return curX;
        }

		static MarkPosition[] sMarkPositionsLow = new MarkPosition[0x70]
		//       0        1        2        3            4        5        6        7             8        9        A        B             C        D        E        F
       /*0*/(.AboveC, .AboveC, .AboveC, .AboveC, /**/ .AboveC, .AboveC, .AboveC, .AboveC, /**/ .AboveC, .AboveC, .AboveC, .AboveC, /**/ .AboveC, .AboveC, .AboveC, .AboveC,
	   /*1*/ .AboveC, .AboveC, .AboveC, .AboveC, /**/ .AboveC, .AboveE, .AboveC, .AboveC, /**/ .BelowC, .BelowC, .AboveE, .TopR,   /**/ .BelowC, .BelowC, .BelowC, .BelowC,
	   /*2*/ .BelowC, .BelowC, .BelowC, .BelowC, /**/ .BelowC, .BelowC, .BelowC, .BelowC, /**/ .BelowC, .BelowC, .BelowC, .BelowC, /**/ .BelowC, .BelowC, .BelowC, .BelowC,
       /*3*/ .BelowC, .BelowC, .BelowC, .BelowC, /**/ .OverC , .OverC , .OverC , .OverC , /**/ .OverC , .BelowC, .BelowC, .BelowC, /**/ .AboveC, .AboveC, .AboveC, .AboveC,
       /*4*/ .AboveC, .AboveC, .AboveC, .AboveC, /**/ .AboveC, .BelowC, .AboveC, .BelowC, /**/ .BelowC, .BelowC, .AboveC, .AboveC, /**/ .AboveC, .BelowC, .BelowC, .OverC,
       /*5*/ .AboveC, .AboveC, .AboveC, .BelowC, /**/ .BelowC, .BelowC, .BelowC, .AboveC, /**/ .AboveE, .BelowC, .AboveC, .AboveC, /**/ .BelowR, .AboveR, .AboveR, .BelowR,
       /*6*/ .AboveR, .AboveR, .BelowR, .AboveC, /**/ .AboveC, .AboveC, .AboveC, .AboveC, /**/ .AboveC, .AboveC, .AboveC, .AboveC, /**/ .AboveC, .AboveC, .AboveC, .AboveC,
			) ~ delete _;

		MarkPosition GetMarkPosition(char32 checkChar)
		{
			if ((checkChar >= '\u{0300}') && (checkChar <= '\u{036F}'))
			{
				return sMarkPositionsLow[(int)(checkChar - '\u{0300}')];
			}
			return .OverC;
		}

        CharData GetCharData(char32 checkChar)
        {
            CharData charData;
            if ((checkChar >= (char8)0) && (checkChar < (char32)LOW_CHAR_COUNT))
                charData = mLowCharData[(int)checkChar];
            else
                mCharData.TryGetValue(checkChar, out charData);
            if (charData == null)
			{
				for (int fontIdx = -1; fontIdx < mAlternates.Count; fontIdx++)
				{
					FTFont* ftFont;
					if (fontIdx == -1)
						ftFont = mFTFont;
					else
						ftFont = mAlternates[fontIdx].mFont.mFTFont;
					if (ftFont == null)
						continue;

					var ftGlyph = FTFont_AllocGlyph(ftFont, (int32)checkChar, fontIdx == mAlternates.Count - 1);
					if (ftGlyph == null)
						continue;

					charData = new CharData();
					charData.mX = ftGlyph.mX;
					charData.mY = ftGlyph.mY;
					charData.mWidth = ftGlyph.mWidth;
					charData.mHeight = ftGlyph.mHeight;
					charData.mXAdvance = ftGlyph.mXAdvance;
					charData.mXOffset = ftGlyph.mXOffset;
					charData.mYOffset = ftGlyph.mYOffset;
					charData.mImageSegment = new Image();
					charData.mImageSegment.mNativeTextureSegment = ftGlyph.mTextureSegment;
					charData.mImageSegment.mX = ftGlyph.mX;
					charData.mImageSegment.mY = ftGlyph.mY;
					charData.mImageSegment.mWidth = ftGlyph.mWidth;
					charData.mImageSegment.mHeight = ftGlyph.mHeight;
					charData.mImageSegment.mSrcWidth = ftGlyph.mWidth;
					charData.mImageSegment.mSrcHeight = ftGlyph.mHeight;
					charData.mIsCombiningMark = ((checkChar >= '\u{0300}') && (checkChar <= '\u{036F}')) || ((checkChar >= '\u{1DC0}') && (checkChar <= '\u{1DFF}'));
					if (charData.mIsCombiningMark)
						charData.mXAdvance = 0;
					if ((checkChar >= (char32)0) && (checkChar < (char32)LOW_CHAR_COUNT))
						mLowCharData[(int)checkChar] = charData;
					else
						mCharData[checkChar] = charData;
	                return charData;
				}
				
				if (checkChar == (char32)'?')
					return null;
				return GetCharData((char32)'?');
			}
            return charData;
        }

		MarkRefData GetMarkRefData()
		{
			if (mMarkRefData == null)
			{
				mMarkRefData = new MarkRefData();
				var charData = GetCharData('o');
				mMarkRefData.mTop = charData.mYOffset;
				mMarkRefData.mBottom = charData.mYOffset + charData.mHeight;
			}
			return mMarkRefData;
		}

        public float GetWidth(char32 theChar)
        {
            CharData charData = GetCharData(theChar);
            return charData.mXAdvance;
        }

        public int GetCharCountToLength(StringView theString, float maxLength, bool* hitTabStop = null)
        {
            float curX = 0;

			int startIdx = 0;
            char32 prevChar = (char8)0;
            for (var c in theString.DecodedChars)
            {
				int idx = @c.NextIndex;
				if (c == (char32)'\x01')
				{
					@c.NextIndex = idx + 4;
					//startIdx = idx;
					continue;
				}
				else if (c == (char32)'\x02')
				{
					//startIdx = idx;
					continue;
				}
				else if (c == (char32)'\t')
				{
					if (hitTabStop != null)
					{
						*hitTabStop = true;
						return startIdx;
					}
				}

                CharData charData = GetCharData(c);
                curX += charData.mXAdvance;
                if (prevChar != (char32)0)
                {
                    float kernAmount = GetKerning(prevChar, c);
					curX += kernAmount;
                }

                if (curX > maxLength)
                    return startIdx;

                prevChar = c;
				startIdx = idx;
            }

            return (int32)theString.Length;
        }

        public float GetLineSpacing()
        {
			if (mFTFont == null)
				return 0;
            return mFTFont.mHeight;
        }

		public float GetHeight()
		{
			if (mFTFont == null)
				return 0;
		    return mFTFont.mHeight;
		}

		public float GetAscent()
		{
			if (mFTFont == null)
				return 0;
		    return mFTFont.mAscent;
		}

		public float GetDescent()
		{
			if (mFTFont == null)
				return 0;
		    return mFTFont.mDescent;
		}

        public float GetWrapHeight(StringView theString, float width)
        {
            return Draw(null, theString, 0, 0, -1, width, FontOverflowMode.Wrap);
        }

		public static void StrEncodeColor(uint32 color, String outString)
		{
			uint32 colorVal = (color >> 1) & 0x7F7F7F7F;
			outString.Append('\x01');
			outString.Append((char8*)&colorVal, 4);
		}

		public static void StrEncodePopColor(String outString)
		{
			outString.Append('\x02');
		}

		public static char8[5] EncodeColor(uint32 color)
		{
			char8[5] val;
			val[0] = '\x01';
			*((uint32*)&val[1]) = (color >> 1) & 0x7F7F7F7F;
			return val;
		}

		public static char8 EncodePopColor()
		{
			return '\x02';
		}

		public static void StrRemoveColors(StringView theString, String str)
		{
			int len = theString.Length;
			if (len == 0)
			{
				return;
			}

			for (var c in theString.DecodedChars)
			{
				int idx = @c.NextIndex;
				if (c == (char32)'\x01')
				{
					@c.NextIndex = idx + 4;
					continue;
				}
				else if (c == (char32)'\x02')
				{
					continue;
				}
		        str.Append(c);

				if (idx >= len)
					break;
		    }
		}

        public void Draw(Graphics g, StringView theString, FontMetrics* fontMetrics = null)
        {
			if (mFTFont == null)
				return;

            float curX = 0;
            float curY = 0;

            Matrix newMatrix = Matrix();
			bool hasClipRect = g.mClipRect.HasValue;
            Rect clipRect = g.mClipRect.GetValueOrDefault();

			uint32 color = g.mColor;

			g.PushTextRenderState();

#unwarn
			float markScale = mFTFont.mHeight / 8.0f;
			float markTopOfs = 0;
			float markBotOfs = 0;

			CharData lastCharData = null;
			float lastCharX = 0;
            char32 prevChar = (char32)0;			
            
			for (var c in theString.DecodedChars)
            {
				int idx = @c.NextIndex;

				if (c == (char32)'\x01') // Set new color
				{
					if (idx <= theString.Length - 4)
					{
						uint32 newColor = *(uint32*)(theString.Ptr + idx) << 1;
						color = Color.Mult(newColor, g.mColor);
						@c.NextIndex = idx + 4;
						continue;
					}
				}
				else if (c == (char32)'\x02') // Restore color
				{
					color = g.mColor;
					continue;
				}

                CharData charData = GetCharData(c);
				float drawX = curX + charData.mXOffset;
				float drawY = curY + charData.mYOffset;
				
				if ((charData.mIsCombiningMark) && (lastCharData != null))
				{
					var markRefData = GetMarkRefData();
					var markPos = GetMarkPosition(c);

					if (markPos == .TopR)
					{
						drawX = lastCharX + lastCharData.mXOffset + lastCharData.mWidth - charData.mWidth / 2;
						drawY = curY + lastCharData.mYOffset - charData.mHeight / 2;
					}
					else if ((markPos == .AboveE) || (markPos == .OverE))
					{
						drawX = lastCharX + lastCharData.mXOffset + lastCharData.mWidth - charData.mWidth / 2;
					}
					else
					{
						drawX = lastCharX + lastCharData.mXOffset + (lastCharData.mWidth / 2);
						if ((markPos == .AboveC) || (markPos == .BelowC) || (markPos == .OverC))
							drawX -= charData.mWidth / 2;
					}

					if ((markPos == .AboveC) || (markPos == .AboveR))
					{
                        drawY += lastCharData.mYOffset - markRefData.mTop - markTopOfs;
						markTopOfs += charData.mHeight;
					}
					else if ((markPos == .BelowC) || (markPos == .BelowR))
					{
						drawY += (lastCharData.mYOffset + lastCharData.mHeight) - markRefData.mBottom + markBotOfs;
						markBotOfs += charData.mHeight;
					}
				}
				else
				{
					lastCharX = curX;
					markTopOfs = 0;
					markBotOfs = 0;

					if (prevChar != (char8)0)
					{
					    float kernAmount = GetKerning(prevChar, c);
						curX += kernAmount;
						drawX += kernAmount;
					}
					lastCharData = charData;
				}

                newMatrix.SetMultiplied(drawX, drawY, ref g.mMatrix);
				bool isFullyClipped = false;
				if (hasClipRect)
				{
					if (newMatrix.tx < clipRect.mX -charData.mImageSegment.mWidth)
					{
						isFullyClipped = true;
					}
					else if (newMatrix.tx > clipRect.mX + clipRect.mWidth)
					{
						isFullyClipped = true;
						if ((newMatrix.a > 0) && (fontMetrics == null)) // Forward? If so, all future chars will clip
							break;
					}
				}

				if (!isFullyClipped)
                    charData.mImageSegment.Draw(newMatrix, g.ZDepth, color);

                curX += charData.mXAdvance;

                prevChar = c;
            }

			g.PopRenderState();
        }

        public float Draw(Graphics g, StringView theString, float x, float y, int32 justification = -1, float width = 0, FontOverflowMode stringEndMode = FontOverflowMode.Overflow, FontMetrics* fontMetrics = null)
        {
            float drawHeight = 0;
			float useX = x;
			float useY = y;

			StringView workingStr = theString;
			String tempStr = null;

			void PopulateTempStr()
			{
				if (tempStr.Length != workingStr.Length)
				{
					tempStr.Clear();
					tempStr.Append(workingStr);
				}	
			}

            while (true)
            {
                int32 crPos = (int32)workingStr.IndexOf('\n');
                if (crPos == -1)
                    break;
				
                float sectionHeight = Draw(g, StringView(workingStr, 0, crPos), useX, useY, justification, width, stringEndMode, fontMetrics);
                drawHeight += sectionHeight;
                useY += sectionHeight;
				workingStr = .(workingStr, crPos + 1, workingStr.Length - (crPos + 1));
            }

            if ((justification != -1) || (stringEndMode != FontOverflowMode.Overflow))
            {
                float aWidth;
                while (true)
                {
                    aWidth = GetWidth(workingStr);
                    if (aWidth <= width)
                        break;

                    if (stringEndMode == FontOverflowMode.Ellipsis)
                    {
                        float ellipsisLen = GetWidth(mEllipsis);
                        if (width < ellipsisLen)
                            return 0; // Don't draw at all if we don't even have enough space for the ellipsis
                        
						int strLen = GetCharCountToLength(workingStr, width - ellipsisLen);
						tempStr = scope:: String(Math.Min(strLen, 128));
						tempStr.Append(theString, 0, strLen);
						tempStr.Append(mEllipsis);
						workingStr = tempStr;
                        aWidth = GetWidth(workingStr);
                        break;
                    }
                    else if (stringEndMode == FontOverflowMode.Truncate)
                    {
						int strLen = GetCharCountToLength(workingStr, width);
						tempStr = scope:: String(Math.Min(strLen, 128));
						tempStr.Append(theString, 0, strLen);
						workingStr = tempStr;
						aWidth = GetWidth(workingStr);
                        break;
                    }
                    else if (stringEndMode == FontOverflowMode.Wrap)
                    {
                        int32 maxChars = (int32)GetCharCountToLength(workingStr, width);
						if (maxChars == 0)
						{
							if (workingStr.IsEmpty)
								break;
							maxChars = 1;
						}

                        int32 checkIdx = maxChars;
						if (checkIdx < workingStr.Length)
						{
	                        while ((checkIdx > 0) && (!workingStr[checkIdx].IsWhiteSpace))
	                            checkIdx--;
						}

                        if (checkIdx == 0)
                        {
                            // Can't break on whitespace
                            checkIdx = maxChars;
                        }

                        if (checkIdx > 0)
                        {
                            if (fontMetrics != null)
                                fontMetrics.mLineCount++;

                            drawHeight += Draw(g, StringView(workingStr, 0, checkIdx), useX, useY, justification, width, .Truncate, fontMetrics);
                            useY += GetLineSpacing();                            
                            workingStr.Adjust(checkIdx);
                        }
                    }
                    else
                        break;
                }

                if (fontMetrics != null)
                    fontMetrics.mMaxWidth = Math.Max(fontMetrics.mMaxWidth, aWidth);

                if (justification == 0)
				{
					// This strange-looking construct is so that odd-length lines and even-length lines do not 'jitter'
					// relative to each other as we're resizing a window
                    useX += (((int)(width)&~1) - (int)aWidth) / 2;
				}
                else if (justification == 1)
                    useX += width - aWidth;
            }

            if (g != null)
            {
                using (g.PushTranslate(useX, useY))
                    Draw(g, workingStr, fontMetrics);
            }
            else
            {
                if (fontMetrics != null)
                    fontMetrics.mMaxWidth = Math.Max(fontMetrics.mMaxWidth, GetWidth(workingStr));
            }
            drawHeight += GetLineSpacing();

            if (fontMetrics != null)
                fontMetrics.mLineCount++;

            return drawHeight;
        }
    }
}
