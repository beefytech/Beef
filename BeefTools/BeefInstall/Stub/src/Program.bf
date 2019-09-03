using System;
using IDE.Util;
using System.IO;
using System.Windows;

namespace BIStub
{
	class Program
	{
		bool mFailed;

		void Fail(StringView str)
		{
			if (mFailed)
				return;
			mFailed = true;
			Windows.MessageBoxA(default, scope String..AppendF("ERROR: {}", str), "FATAL ERROR", Windows.MB_ICONHAND);
		}

		bool HandleCommandLineParam(String key, String value)
		{
			return false;
		}

		void UnhandledCommandLine(String key, String value)
		{

		}

		void ParseCommandLine(String[] args)
		{
			for (var str in args)
			{
				int eqPos = str.IndexOf('=');
				if (eqPos == -1)
				{
					if (!HandleCommandLineParam(str, null))
						UnhandledCommandLine(str, null);
				}	
				else
				{
					var cmd = scope String(str, 0, eqPos);
					var param = scope String(str, eqPos + 1);
					if (!HandleCommandLineParam(cmd, param))
						UnhandledCommandLine(cmd, param);
				}
			}
		}

		Result<void> ExtractTo(ZipFile zipFile, StringView destDir, StringView subStr)
		{
			String fileName = scope .();
			String destPath = scope .();

			for (int i < zipFile.GetNumFiles())
			{
				ZipFile.Entry entry = scope .();
				if (zipFile.SelectEntry(i, entry) case .Err)
					continue;

				fileName.Clear();
				entry.GetFileName(fileName);

				if (!fileName.StartsWith(subStr))
					continue;

				destPath.Clear();
				destPath.Append(destDir);
				destPath.Append('/');
				destPath.Append(fileName);

				if (entry.IsDirectory)
				{
					if (Directory.CreateDirectory(destPath) case .Err)
						return .Err;
				}
				else
				{
					if (entry.ExtractToFile(destPath) case .Err)
						return .Err;
				}
			}

			return .Ok;
		}

		void CheckPE()
		{
			let module = Windows.GetModuleHandleW(null);
			uint8* moduleData = (uint8*)(int)module;
			PEFile.PEHeader* header = (.)moduleData;

			PEFile.PE_NTHeaders64* hdr64 = (.)(moduleData + header.e_lfanew);
			if (hdr64.mFileHeader.mMachine == PEFile.PE_MACHINE_X64)
			{
				int fileEnd = 0;

				for (int sectIdx < hdr64.mFileHeader.mNumberOfSections)
				{
					PEFile.PESectionHeader* sectHdrHead = (.)((uint8*)(hdr64 + 1)) + sectIdx;
					fileEnd = Math.Max(fileEnd, sectHdrHead.mPointerToRawData + sectHdrHead.mSizeOfRawData);
				}


			}
		}

		public function void InstallFunc(StringView dest, StringView filter);
		public function int ProgressFunc();
		public function void CancelFunc();

		public function void StartFunc(InstallFunc installFunc, ProgressFunc progressFunc, CancelFunc cancelFunc);

		static void UI_Install(StringView dest, StringView filter)
		{

		}

		static int UI_GetProgress()
		{
			return 0;
		}

		static void UI_Cancel()
		{

		}

		void StartUI(StringView dir)
		{
			String destLib = scope .();
			destLib.Append(dir);
			destLib.Append("/../dist/StubUI_d.dll");

			var lib = Windows.LoadLibraryW(destLib.ToScopedNativeWChar!());
			if (lib.IsInvalid)
			{
				Fail(scope String()..AppendF("Failed to load installer UI '{}'", destLib));
				return;
			}

			StartFunc startFunc = (.)Windows.GetProcAddress(lib, "Start");
			if (startFunc == null)
			{
				Fail(scope String()..AppendF("Failed to initialize installer UI '{}'", destLib));
				return;
			}

			startFunc(=> UI_Install, => UI_GetProgress, => UI_Cancel);

			Windows.FreeLibrary(lib);
		}

		void Run()
		{
			String cwd = scope .();
			Directory.GetCurrentDirectory(cwd);
			StartUI(cwd);

			/*CheckPE();

			ZipFile zipFile = scope .();
			zipFile.Open(@"c:\\temp\\build_1827.zip");
			ExtractTo(zipFile, @"c:\temp\unzip", .());

			CabFile cabFile = scope .();
			cabFile.Init();
			cabFile.Copy();*/
		}

		static int Main(String[] args)
		{
			
			Program pg = new Program();
			pg.ParseCommandLine(args);
			pg.Run();
			delete pg;
			return 0;
		}
	}
}
