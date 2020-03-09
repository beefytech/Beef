// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Text;
using System.Diagnostics;

namespace System.IO
{
	public static class Path
	{
#if BF_PLATFORM_WINDOWS
		public const char8 DirectorySeparatorChar = '\\';
#else
		public const char8 DirectorySeparatorChar = '/';
#endif //BF_PLATFORM_WINDOWS
		
		// Platform specific alternate directory separator char8acter.  
		// This is backslash ('\') on Unix, and slash ('/') on Windows 
		// and MacOS.
		// 
#if BF_PLATFORM_WINDOWS
		public const char8 AltDirectorySeparatorChar = '/';
#else
		public const char8 AltDirectorySeparatorChar = '\\';
#endif //BF_PLATFORM_WINDOWS
	
		// Platform specific volume separator char8acter.  This is colon (':')
		// on Windows and MacOS, and slash ('/') on Unix.  This is mostly
		// useful for parsing paths like "c:\windows" or "MacVolume:System Folder".  
		// 
#if BF_PLATFORM_WINDOWS
		public const char8 VolumeSeparatorChar = ':';
#else
		public const char8 VolumeSeparatorChar = '/';
#endif //BF_PLATFORM_WINDOWS

		// Make this public sometime.
		// The max total path is 260, and the max individual component length is 255. 
		// For example, D:\<256 char8 file name> isn't legal, even though it's under 260 char8s.
		protected const int32 MaxPath = 260;
		private const int32 MaxDirectoryLength = 255;

		public static void GetFullPath(String inPartialPath, String outFullPath)
		{
			Platform.GetStrHelper(outFullPath, scope (outPtr, outSize, outResult) =>
				{
					Platform.BfpFile_GetFullPath(inPartialPath, outPtr, outSize, (Platform.BfpFileResult*)outResult);
				});
		}

		protected static void CheckInvalidPathChars(StringView path, bool checkAdditional = false)
		{

		}

		public static void GetFileName(StringView inPath, String outFileName)
		{
			if (inPath.IsEmpty)
				return;
			
			CheckInvalidPathChars(inPath);

			int length = inPath.Length;
			for (int i = length; --i >= 0; )
			{
				char8 ch = inPath[i];
				if (ch == DirectorySeparatorChar || ch == AltDirectorySeparatorChar || ch == VolumeSeparatorChar)
				{
					outFileName.Append(inPath, i + 1, length - i - 1);
					return;
				}
			}
			outFileName.Append(inPath);
		}

		static String NormalizePath(String path, bool fullCheck) 
        {
		    return NormalizePath(path, fullCheck, MaxPath);
		}

		
		static String NormalizePath(String path, bool fullCheck, bool expandShortPaths)
		{
		    return NormalizePath(path, fullCheck, MaxPath, expandShortPaths);
		}

		
		static String NormalizePath(String path, bool fullCheck, int32 maxPathLength) 
        {
		    return NormalizePath(path, fullCheck, maxPathLength, true);
		}

		static String NormalizePath(String path, bool fullCheck, int32 maxPathLength, bool expandShortPaths)
		{
			//TODO: Implement
			return path;
		}

		static String RemoveLongPathPrefix(String path)
		{
			//TODO: Implement
			return path;
		}

		public static Result<void> GetDirectoryPath(StringView path, String outDir)
		{
			String usePath = scope String(Math.Min(MaxPath, path.Length));
			usePath.Append(path);

			//String origPath = path;
			//if (usePath != null)
			{
				CheckInvalidPathChars(usePath);
				String normalizedPath = NormalizePath(usePath, false);

				// If there are no permissions for PathDiscovery to this path, we should NOT expand the short paths
				// as this would leak information about paths to which the user would not have access to.
				if (usePath.Length > 0)
				{
					// If we were passed in a path with \\?\ we need to remove it as FileIOPermission does not like it.
					String tempPath = Path.RemoveLongPathPrefix(usePath);

					// FileIOPermission cannot handle paths that contain ? or *
					// So we only pass to FileIOPermission the text up to them.
					int32 pos = 0;
					while (pos < tempPath.Length && (tempPath[pos] != '?' && tempPath[pos] != '*'))
						pos++;
					// GetFullPath will Demand that we have the PathDiscovery FileIOPermission and thus throw 
					// SecurityException if we don't. 
					// While we don't use the result of this call we are using it as a consistent way of 
					// doing the security checks.
					if (pos > 0)
					{
						//Path.GetFullPath(tempPath.Substring(0, pos));
						String newPath = scope String(usePath.Length - pos);
						newPath.Append(usePath, pos);
						usePath = newPath;
					}
					/*}
					catch (SecurityException) {
						// If the user did not have permissions to the path, make sure that we don't leak expanded short paths
						// Only re-normalize if the original path had a ~ in it.
						if (path.IndexOf("~", StringComparison.Ordinal) != -1)
						{
							normalizedPath = NormalizePath(path, /*fullCheck*/ false, /*expandShortPaths*/ false);
						}
					}
					catch (PathTooLongException) { }
					catch (NotSupportedException) { }  // Security can throw this on "c:\foo:"
					catch (IOException) { }
					catch (ArgumentException) { } // The normalizePath with fullCheck will throw this for file: and http:*/
				}

				usePath = normalizedPath;

				int root = GetRootLength(usePath);
				int i = usePath.Length;
				if (i > root)
				{
					i = usePath.Length;
					if (i == root) return .Err;
					while (i > root && usePath[--i] != DirectorySeparatorChar && usePath[i] != AltDirectorySeparatorChar) {}
						outDir.Append(usePath, 0, i);
					return .Ok;
				}
			}
			return .Err;
		}

		// Gets the length of the root DirectoryInfo or whatever DirectoryInfo markers
				// are specified for the first part of the DirectoryInfo name.
				// 
        static int GetRootLength(String path)
		{
			CheckInvalidPathChars(path);

			int i = 0;
			int length = path.Length;

#if BF_PLATFORM_WINDOWS
			if (length >= 1 && (IsDirectorySeparator(path[0])))
			{
				// handles UNC names and directories off current drive's root.
				i = 1;
				if (length >= 2 && (IsDirectorySeparator(path[1])))
				{
					i = 2;
					int32 n = 2;
					while (i < length && ((path[i] != DirectorySeparatorChar && path[i] != AltDirectorySeparatorChar) || --n > 0)) i++;
				}
			}
			else if (length >= 2 && path[1] == VolumeSeparatorChar)
			{
				// handles A:\foo.
				i = 2;
				if (length >= 3 && (IsDirectorySeparator(path[2]))) i++;
			}
			return i;
#else    
			if (length >= 1 && (IsDirectorySeparator(path[0]))) {
				i = 1;
			}
			return i;
#endif //BF_PLATFORM_WINDOWS
        }

		static bool IsDirectorySeparator(char8 c) 
        {
		    return (c==DirectorySeparatorChar || c == AltDirectorySeparatorChar);
		}

		/*
		public static char8[] GetInvalidPathChars()
		{
			return RealInvalidPathChars;
		}

		public static char8[] GetInvalidFileNameChars()
		{
			return (char8[]) InvalidFileNameChars.Clone();
		} */

		public static void GetFileNameWithoutExtension(String inPath, String outFileName)
		{
			int lastSlash = Math.Max(inPath.LastIndexOf('\\'), inPath.LastIndexOf('/'));

			int i;
			if ((i = inPath.LastIndexOf('.')) != -1)
			{
				int len = i - lastSlash - 1;
				if (len > 0)
					outFileName.Append(inPath, lastSlash + 1, i - lastSlash - 1);
			}
			else
				outFileName.Append(inPath, lastSlash + 1);
		}

		public static Result<void> GetExtension(StringView inPath, String outExt)
		{
			int i;
			if ((i = inPath.LastIndexOf('.')) != -1)
				outExt.Append(inPath, i);
			return .Ok;
		}

		public static Result<void> GetTempPath(String outPath)
		{
			Platform.GetStrHelper(outPath, scope (outPtr, outSize, outResult) =>
				{
					Platform.BfpFile_GetTempPath(outPtr, outSize, (Platform.BfpFileResult*)outResult);
				});

			return .Ok;
		}

		public static Result<void> GetTempFileName(String outPath)
		{
			Platform.GetStrHelper(outPath, scope (outPtr, outSize, outResult) =>
				{
					Platform.BfpFile_GetTempFileName(outPtr, outSize, (Platform.BfpFileResult*)outResult);
				});

			return .Ok;
		}

		public static void InternalCombine(String target, params String[] components)
		{
			for (var component in components)
			{
				if ((target.Length > 0) && (!target.EndsWith("\\")) && (!target.EndsWith("/")))
					target.Append(Path.DirectorySeparatorChar);
				target.Append(component);
			}
		}

		public static void GetActualPathName(StringView inPath, String outPath)
		{
			Platform.GetStrHelper(outPath, scope (outPtr, outSize, outResult) =>
				{
					Platform.BfpFile_GetActualPath(scope String()..Append(inPath), outPtr, outSize, (Platform.BfpFileResult*)outResult);
				});
		}

		public static bool Equals(StringView filePathA, StringView filePathB)
		{
		    Debug.Assert(!filePathA.Contains(Path.AltDirectorySeparatorChar));
		    Debug.Assert(!filePathB.Contains(Path.AltDirectorySeparatorChar));
		    return filePathA.Equals(filePathB, !Environment.IsFileSystemCaseSensitive);
		}

		static void GetDriveStringTo(String outDrive, String path)
		{
		    if ((path.Length >= 2) && (path[1] == ':'))
		        outDrive.Append(path, 0, 2);
		}

		/// Tests if the given path contains a root. A path is considered rooted
		/// if it starts with a backslash ("\") or a drive letter and a colon (":").
		public static bool IsPathRooted(StringView path)
		{
	        CheckInvalidPathChars(path);
	        int length = path.Length;
	        if ((length >= 1 && (path[0] == DirectorySeparatorChar || path[0] == AltDirectorySeparatorChar)) || (length >= 2 && path[1] == VolumeSeparatorChar))
	            return true;
		    return false;
		}

		public static void GetRelativePath(StringView fullPath, StringView curDir, String outRelPath)
		{
		    String curPath1 = scope String(curDir);
		    String curPath2 = scope String(fullPath);

		    if (curPath1.Contains(Path.AltDirectorySeparatorChar))
		        curPath1.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);
		    if (curPath2.Contains(Path.AltDirectorySeparatorChar))
		        curPath1.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

		    String driveString1 = scope String();
		    GetDriveStringTo(driveString1, curPath1);
		    String driveString2 = scope String();
		    GetDriveStringTo(driveString2, curPath2);

		    StringComparison compareType = Environment.IsFileSystemCaseSensitive ? StringComparison.Ordinal : StringComparison.OrdinalIgnoreCase;

			// On seperate drives?
		    if (!String.Equals(driveString1, driveString2, compareType))
			{
				outRelPath.Set(fullPath);
		        return;
			}

		    if (driveString1.Length > 0)
		        curPath1.Remove(0, Math.Min(driveString1.Length + 1, curPath1.Length));
		    if (driveString2.Length > 0)
		        curPath2.Remove(0, Math.Min(driveString2.Length + 1, curPath2.Length));

		    while ((curPath1.Length > 0) && (curPath2.Length > 0))
		    {
		        int slashPos1 = curPath1.IndexOf(Path.DirectorySeparatorChar);
		        if (slashPos1 == -1)
		            slashPos1 = curPath1.Length;
		        int slashPos2 = curPath2.IndexOf(Path.DirectorySeparatorChar);
		        if (slashPos2 == -1)
		            slashPos2 = curPath2.Length;

		        String section1 = scope String();
		        section1.Append(curPath1, 0, slashPos1);
		        String section2 = scope String();
		        section2.Append(curPath2, 0, slashPos2);

		        if (!String.Equals(section1, section2, compareType))
		        {
					// a/b/c
					// d/e/f

		            while (curPath1.Length > 0)
		            {
		                slashPos1 = curPath1.IndexOf(Path.DirectorySeparatorChar);
		                if (slashPos1 == -1)
		                    slashPos1 = curPath1.Length;

		                if (slashPos1 + 1 >= curPath1.Length)
		                    curPath1.Clear();
		                else
		                    curPath1.Remove(0, slashPos1 + 1);
						if (Path.DirectorySeparatorChar == '\\')
		                	curPath2.Insert(0, "..\\");
						else
							curPath2.Insert(0, "../");
		            }
		        }
		        else
		        {
		            if (slashPos1 + 1 >= curPath1.Length)
		                curPath1.Clear();
		            else
		                curPath1.Remove(0, slashPos1 + 1);

		            if (slashPos2 + 2 >= curPath2.Length)
		                curPath1 = "";
		            else
		                curPath2.Remove(0, slashPos2 + 1);
		        }
		    }

		    outRelPath.Set(curPath2);
		}

		public static void GetAbsolutePath(StringView relPath, StringView relToAbsPath, String outAbsPath)
		{
		    String driveString = null;

			var relPath;
			if (relPath == outAbsPath)
				relPath = scope:: String(relPath);

			if ((relPath.Length >= 2) && (relPath[1] == ':'))
			{
				outAbsPath.Append(relPath);
			    return;
			}

			if ((relPath.Length > 1) &&
				(relPath[0] == '/') || (relPath[0] == '\\'))
			{
				outAbsPath.Append(relPath);
				return;
			}

			int startLen = outAbsPath.Length;
			outAbsPath.Append(relToAbsPath);
			//outAbsPath.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

		    char8 slashChar = Path.DirectorySeparatorChar;

		    if ((outAbsPath.Length >= 2) && (outAbsPath[1] == ':'))
		    {
				driveString = scope:: String();
		        driveString.Append(outAbsPath, 0, 2);
		        outAbsPath.Remove(0, 2);
		    }

			// Append a trailing slash if necessary
		    if ((outAbsPath.Length > 0) && (outAbsPath[outAbsPath.Length - 1] != '\\') && (outAbsPath[outAbsPath.Length - 1] != '/'))
		        outAbsPath.Append(slashChar);

		    int32 relIdx = 0;
		    for (; ;)
		    {
		        if (outAbsPath.Length == 0)
		            break;

		        int32 firstSlash = -1;
				for (int32 i = relIdx; i < relPath.Length; i++)
					if ((relPath[i] == '\\') || (relPath[i] == '/'))
					{
						firstSlash = i;
						break;
					}
		        if (firstSlash == -1)
		            break;

		        String chDir = scope String();
				chDir.Append(relPath, relIdx, firstSlash - relIdx);

				//relPath.Substring(relIdx, firstSlash - relIdx, chDir);
				//chDir.Append(relPath.Ptr + relIdx, firstSlash - relIdx);

		        relIdx = firstSlash + 1;

		        if (chDir == "..")
		        {
		            int32 lastDirStart = (int32)outAbsPath.Length - 1;
		            while ((lastDirStart > 0) && (outAbsPath[lastDirStart - 1] != '\\') && (outAbsPath[lastDirStart - 1] != '/'))
		                lastDirStart--;

		            String lastDir = scope String();
		            lastDir.Append(outAbsPath, lastDirStart, outAbsPath.Length - lastDirStart - 1);
		            if (lastDir == "..")
		            {
		                outAbsPath.Append("..");
		                outAbsPath.Append(Path.DirectorySeparatorChar);
		            }
		            else
		            {
						//newPath.erase(newPath.begin() + lastDirStart, newPath.end());
		                outAbsPath.Remove(lastDirStart, outAbsPath.Length - lastDirStart);
		            }
		        }
		        else if (chDir == "")
		        {
		            outAbsPath.Append(Path.DirectorySeparatorChar);
		            break;
		        }
		        else if (chDir != ".")
		        {
					//newPath += chDir + slashChar;
		            outAbsPath.Append(chDir);
		            outAbsPath.Append(Path.DirectorySeparatorChar);
		            break;
		        }
		    }

		    if (driveString != null)
		        outAbsPath.Insert(0, driveString);
			//relPath.Substring(relIdx, outAbsPath);
			outAbsPath.Append(relPath, relIdx);
			//outAbsPath.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);
			for (int i = startLen; i < outAbsPath.Length; i++)
			{
				if (outAbsPath[i] == Path.AltDirectorySeparatorChar)
					outAbsPath[i] = Path.DirectorySeparatorChar;
			}
		}

		public static bool WildcareCompare(StringView path, StringView wildcard)
		{
			bool matches = true;
			char8* afterLastWild = null; // The location after the last '*', if we’ve encountered one
			char8* afterLastPath = null; // The location in the path string, from which we started after last wildcard
			char8 t, w;

			char8* pathPtr = path.Ptr;
			char8* pathEnd = path.EndPtr;
			char8* wildPtr = wildcard.Ptr;
			char8* wildEnd = wildcard.EndPtr;

			// Walk the text strings one character at a time.
			while (true)
			{
				// How do you match a unique text string?
				if (pathPtr == pathEnd)
				{
					// Easy: unique up on it!
					if (wildPtr == wildEnd)
					{
						break; // "x" matches "x"
					}
					w = *wildPtr;
					if (w == '*')
					{
						wildPtr++;
						continue;// "x*" matches "x" or "xy"
					}
					else if (afterLastPath != null)
					{
						if (afterLastPath == pathEnd)
						{
							matches = false;
							break;
						}
						pathPtr = afterLastPath++;
						wildPtr = afterLastWild;
						continue;
					}

					matches = false;
					break; // "x" doesn't match "xy"
				}
				else
				{
					t = *pathPtr;
					w = *wildPtr;

					if (!Environment.IsFileSystemCaseSensitive)
					{
						t = t.ToUpper;
						w = w.ToUpper;
					}
		 
					// How do you match a tame text string?
					if (t != w)
					{
						// The tame way: unique up on it!
						if (w == '*')
						{
							afterLastWild = ++wildPtr;
							afterLastPath = pathPtr;
							if (wildPtr == wildEnd)
							{
								break; // "*" matches "x"
							}
							w = *wildPtr;
							continue; // "*y" matches "xy"
						}
						else if (afterLastWild != null)
						{
							if (afterLastWild != wildPtr)
							{
								wildPtr = afterLastWild;
								w = *wildPtr;

								if (!Environment.IsFileSystemCaseSensitive)
									w = w.ToUpper;

								if (t == w)
								{
									wildPtr++;
								}
							}
							pathPtr++;
							continue; // "*sip*" matches "mississippi"
						}
						else
						{
							matches = false;
							break; // "x" doesn't match "y"
						}
					}
				}

				pathPtr++;
				wildPtr++;
			}

			return matches;
		}
	}
}
