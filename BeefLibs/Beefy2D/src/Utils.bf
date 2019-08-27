using System;
using System.Collections.Generic;
using System.Text;
using System.Reflection;
using System.Threading;
using System.IO;
using System.Diagnostics;
using System.Security.Cryptography;

namespace Beefy
{
    public static class Utils
    {
        static Random mRandom = new Random() ~ delete _;

        [StdCall, CLink]
        static extern int32 BF_TickCount();

        [StdCall, CLink]
        static extern int32 BF_TickCountMicroFast();

        public static float Deg2Rad = Math.PI_f / 180.0f;

        

        public static int32 Rand()
        {
            return mRandom.NextI32();
        }

		public static float RandFloat()
		{
			return (Rand() & 0xFFFFFF) / (float)0xFFFFFF;
		}

		public static float Interpolate(float left, float right, float pct)
		{
			return left + (right - left) * pct;
		}

        public static float Clamp(float val, float min, float max)
        {
            return Math.Max(min, Math.Min(max, val));
        }

        public static float Lerp(float val1, float val2, float pct)
        {
            return val1 + (val2 - val1) * pct;
        }

        public static float EaseInAndOut(float pct)
        {
            return ((-Math.Cos(pct * Math.PI_f) + 1.0f) / 2.0f);
        }

        public static float Distance(float dX, float dY)
        {
            return Math.Sqrt(dX * dX + dY * dY);
        }

        public static char8 CtrlKeyChar(char32 theChar)
        {
            char8 aChar = (char8)(theChar + (int)'A' - (char8)1);
            if (aChar < (char8)0)
                return (char8)0;
            return aChar;
        }

        public static uint32 GetTickCount()
        {
            return (uint32)BF_TickCount();
        }

        public static uint64 GetTickCountMicro()
        {
            return (uint32)BF_TickCountMicroFast();
        }

        public static Object DefaultConstruct(Type theType)
        {
			ThrowUnimplemented();

            /*ConstructorInfo constructor = theType.GetConstructors()[0];

            ParameterInfo[] paramInfos = constructor.GetParameters();
            object[] aParams = new object[paramInfos.Length];

            for (int paramIdx = 0; paramIdx < aParams.Length; paramIdx++)
                aParams[paramIdx] = paramInfos[paramIdx].DefaultValue;

            object newObject = constructor.Invoke(aParams);
            return newObject;*/
        }

        /*public static int StrToInt(string theString)
        {
            if (theString.StartsWith("0X", StringComparison.OrdinalIgnoreCase))
                return Convert.ToInt32(theString.Substring(2), 16);
            return Convert.ToInt32(theString);
        }

        // WaitForEvent differs from theEvent.WaitOne in that it doesn't pump the Windows
        //  message loop under STAThread
        public static bool WaitForEvent(EventWaitHandle theEvent, int timeMS)
        {
            return WaitForSingleObject(theEvent.SafeWaitHandle, timeMS) == 0;
        }*/

        
        public static void GetDirWithSlash(String dir)
        {
			if (dir.IsEmpty)
				return;
            char8 endChar = dir[dir.Length - 1];
            if ((endChar != Path.DirectorySeparatorChar) && (endChar != Path.AltDirectorySeparatorChar))
                dir.Append(Path.DirectorySeparatorChar);
        }

        public static Result<void> DelTree(String path, Predicate<String> fileFilter = null)
        {
			if (path.Length <= 2)
				return .Err;
			if ((path[0] != '/') && (path[0] != '\\'))
			{
				if (path[1] == ':')
				{
					if (path.Length < 3)
						return .Err;
				}
				else
					return .Err;
			}

            for (var fileEntry in Directory.EnumerateDirectories(path))
            {
				let fileName = scope String();
				fileEntry.GetFilePath(fileName);
                Try!(DelTree(fileName, fileFilter));
            }

            for (var fileEntry in Directory.EnumerateFiles(path))
            {
				let fileName = scope String();
				fileEntry.GetFilePath(fileName);

				if (fileFilter != null)
					if (!fileFilter(fileName))
						continue;

                Try!(File.SetAttributes(fileName, FileAttributes.Archive));
                Try!(File.Delete(fileName));
            }

			// Allow failure for the directory, this can often be locked for various reasons
			//  but we only consider a file failure to be an "actual" failure
            Directory.Delete(path).IgnoreError();
			return .Ok;
        }

        public static Result<void, FileError> LoadTextFile(String fileName, String outBuffer, bool autoRetry = true, delegate void() onPreFilter = null)
        {
			// Retry for a while if the other side is still writing out the file
			for (int i = 0; i < 100; i++)
			{
				if (File.ReadAllText(fileName, outBuffer, true) case .Err(let err))
				{
					bool retry = false;
					if ((autoRetry) && (err case .FileOpenError(let fileOpenErr)))
					{
						if (fileOpenErr == .SharingViolation)
							retry = true;
					}
					if (!retry)
                    	return .Err(err);
				}
				else
					break;
				Thread.Sleep(20);
			}

			if (onPreFilter != null)
				onPreFilter();

			/*if (hashPtr != null)
				*hashPtr = MD5.Hash(Span<uint8>((uint8*)outBuffer.Ptr, outBuffer.Length));*/

			bool isAscii = false;

			int outIdx = 0;
            for (int32 i = 0; i < outBuffer.Length; i++)
            {
                char8 c = outBuffer[i];
				if (c >= '\x80')
				{
					switch (UTF8.TryDecode(outBuffer.Ptr + i, outBuffer.Length - i))
					{
					case .Ok((?, let len)):
						if (len > 1)
						{
							for (int cnt < len)
							{
								char8 cPart = outBuffer[i++];
								outBuffer[outIdx++] = cPart;
							}
							i--;
							continue;
						}
					case .Err: isAscii = true;
					}
				}

                if (c != '\r')
                {
					if (outIdx == i)
					{
						outIdx++;
                        continue;
					}
					outBuffer[outIdx++] = c;
				}
            }
			outBuffer.RemoveToEnd(outIdx);

			if (isAscii)
			{
				String prevBuffer = scope String();
				outBuffer.MoveTo(prevBuffer);

				for (var c in prevBuffer.RawChars)
				{
					outBuffer.Append((char32)c, 1);
				}
			}

            return .Ok;
        }

        public static bool FileTextEquals(String textA, String textB)
        {
            int32 posA = 0;
            int32 posB = 0;

            while (true)
            {
                char8 char8A = (char8)0;
                char8 char8B = (char8)0;

                while (posA < textA.Length)
                {
                    char8A = textA[posA++];
                    if (char8A != '\r')
                        break;
                    char8A = (char8)0;
                }

                while (posB < textB.Length)
                {
                    char8B = textB[posB++];
                    if (char8B != '\r')
                        break;
                    char8B = (char8)0;
                }

                if ((char8A == (char8)0) && (char8B == (char8)0))
                    return true;
                if (char8A != char8B)                
                    return false;                
            }
        }

        public static Result<void> WriteTextFile(StringView path, StringView text)
        {
			var stream = scope FileStream();
			if (stream.Create(path) case .Err)
			{
				return .Err;
			}
            if (stream.WriteStrUnsized(text) case .Err)
				return .Err;
            
            return .Ok;
        } 

        public static int LevenshteinDistance(String s, String t)
        {
            int n = s.Length;
            int m = t.Length;
            int32[,] d = new int32[n + 1, m + 1];
			defer delete d;
            if (n == 0)
            {
                return m;
            }
            if (m == 0)
            {
                return n;
            }
            for (int32 i = 0; i <= n; d[i, 0] = i++)
                {}
            for (int32 j = 0; j <= m; d[0, j] = j++)
				{}
            for (int32 i = 1; i <= n; i++)
            {
                for (int32 j = 1; j <= m; j++)
                {
                    int32 cost = (t[j - 1] == s[i - 1]) ? 0 : 1;
                    d[i, j] = Math.Min(
                        Math.Min(d[i - 1, j] + 1, d[i, j - 1] + 1),
                        d[i - 1, j - 1] + cost);
                }
            }
            return d[n, m];
        }

        /*public static List<TSource> ToSortedList<TSource>(this IEnumerable<TSource> source)
        {
            var list = source.ToList();
            list.Sort();
            return list;
        }*/

		public static int64 DecodeInt64(ref uint8* ptr)
		{
			int64 value = 0;
			int32 shift = 0;
			int64 curByte;
			repeat
			{
				curByte = *(ptr++);
				value |= ((curByte & 0x7f) << shift);
				shift += 7;
			
			} while (curByte >= 128);
			// Sign extend negative numbers.
			if (((curByte & 0x40) != 0) && (shift < 64))
				value |= -1 << shift;
			return value;
		}

        public static int32 DecodeInt(uint8[] buf, ref int idx)
        {
            int32 value = 0;
            int32 Shift = 0;
            int32 curByte;

            repeat
            {
                curByte = buf[idx++];
                value |= ((curByte & 0x7f) << Shift);
                Shift += 7;

            } while (curByte >= 128);
            // Sign extend negative numbers.
            if ((curByte & 0x40) != 0)
                value |= -1 << Shift;
            return value;
        }

        public static void EncodeInt(uint8[] buf, ref int idx, int value)
        {
			int curValue = value;

            bool hasMore;
            repeat
            {
                uint8 curByte = (uint8)(curValue & 0x7f);    
                curValue >>= 7;
                hasMore = !((((curValue == 0) && ((curByte & 0x40) == 0)) ||
                    ((curValue == -1) && ((curByte & 0x40) != 0))));
                if (hasMore)
                    curByte |= 0x80;
                buf[idx++] = curByte;                
            }
            while (hasMore);        
        }

		public static bool Contains<T>(IEnumerator<T> itr, T value)
		{
			for (var check in itr)
				if (check == value)
					return true;
			return false;
		}

		public static void QuoteString(String str, String strOut)
		{
			strOut.Append('"');
			for (int i < str.Length)
			{
				char8 c = str[i];
				strOut.Append(c);
			}
			strOut.Append('"');
		}

		public static void ParseSpaceSep(String str, ref int idx, String subStr)
		{
			while (idx < str.Length)
			{
				char8 c = str[idx];
				if (c != ' ')
					break;
				idx++;
			}

			if (idx >= str.Length)
				return;

			// Quoted
			if (str[idx] == '"')
			{
				idx++;
				while (idx < str.Length)
				{
					char8 c = str[idx++];
					if (c == '"')
						break;
					subStr.Append(c);
				}
				return;
			}

			// Unquoted
			while (idx < str.Length)
			{
				char8 c = str[idx++];
				if (c == ' ')
					break;
				subStr.Append(c);
			}
		}

		public static void SnapScale(ref float val, float scale)
		{
			val *= scale;
			float frac = val - (int)val;
			if ((frac <= 0.001f) || (frac >= 0.999f))
				val = (float)Math.Round(val);
		}

		public static void RoundScale(ref float val, float scale)
		{
			val = (float)Math.Round(val * scale);
		}
    }        
}
