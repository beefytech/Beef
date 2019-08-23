using System;
using IDE.Util;
using System.Diagnostics;

namespace BeefBuild
{
	class Program
	{
		static void TestZip()
		{
			String str = "Good morning Dr. Chandra. This is Hal. I am ready for my first lesson.Good morning Dr. Chandra. This is Hal. I am ready for my first lesson.Good morning Dr. Chandra. This is Hal. I am ready for my first lesson.Good morning Dr. Chandra. This is Hal. I am ready for my first lesson.Good morning Dr. Chandra. This is Hal. I am ready for my first lesson.Good morning Dr. Chandra. This is Hal. I am ready for my first lesson.Good morning Dr. Chandra. This is Hal. I am ready for my first lesson.";
			int src_len = str.Length;
			int uncomp_len = (uint32)src_len;
			int cmp_len = MiniZ.CompressBound(src_len);

			var pCmp = new uint8[cmp_len]*;
#unwarn
			var pUncomp = new uint8[src_len]*;

#unwarn
			//var cmp_status = MiniZ.compress(pCmp, ref cmp_len, (uint8*)(char8*)str, src_len);

			var cmp_status = MiniZ.Compress(pCmp, ref cmp_len, (uint8*)(char8*)str, src_len, .BEST_COMPRESSION);

			cmp_status = MiniZ.Uncompress(pUncomp, ref uncomp_len, pCmp, cmp_len);
		}

		static void TestZip2()
		{
			MiniZ.ZipArchive zipArchive = default;

			if (!MiniZ.ZipReaderInitFile(&zipArchive, "c:\\temp\\zip\\test.zip", default))
				return;

			for (int32 i = 0; i < (int)MiniZ.ZipReaderGetNumFiles(&zipArchive); i++)
			{
				MiniZ.ZipArchiveFileStat file_stat;
				if (!MiniZ.ZipReaderFileStat(&zipArchive, i, &file_stat))
				{			
					MiniZ.ZipReaderEnd(&zipArchive);
					return;
				}

				//printf("Filename: \"%s\", Comment: \"%s\", Uncompressed size: %u, Compressed size: %u, Is Dir: %u\n", file_stat.m_filename, file_stat.m_comment, 
					//(uint)file_stat.m_uncomp_size, (uint)file_stat.m_comp_size, mz_zip_reader_is_file_a_directory(&zipArchive, i));

				/*if (!strcmp(file_stat.m_filename, "directory/"))
				{
					if (!mz_zip_reader_is_file_a_directory(&zipArchive, i))
					{
						printf("mz_zip_reader_is_file_a_directory() didn't return the expected results!\n");
						mz_zip_reader_end(&zipArchive);
					}
				}*/

				var str = scope String();
				str.AppendF("c:\\temp\\file.{0}", i);
				MiniZ.ZipReaderExtractToFile(&zipArchive, i, str, default);
			}

			// Close the archive, freeing any resources it was using
			MiniZ.ZipReaderEnd(&zipArchive);
		}

		/*[StdCall]
		static int32 fetch_progress(Git.git_transfer_progress* stats, void* payload)
		{
			return 0;
		}

		[StdCall]
		static void checkout_progress(char8* path, int cur, int tot, void* payload)
		{
			
		}

		static void TestGit()
		{
			Git.git_libgit2_init();
			Git.git_repository* repo = null;


			Git.git_clone_options cloneOptions = default;
			cloneOptions.version = 1;
			cloneOptions.checkout_opts.version = 1;
			cloneOptions.checkout_opts.checkout_strategy = 1;
			//cloneOptions.checkout_opts.perfdata_cb = => checkout_progress;
			cloneOptions.checkout_opts.progress_cb = => checkout_progress;
			cloneOptions.fetch_opts.version = 1;
			cloneOptions.fetch_opts.callbacks.version = 1;
			cloneOptions.fetch_opts.update_fetchhead = 1;
			cloneOptions.fetch_opts.proxy_opts.version = 1;
			cloneOptions.fetch_opts.callbacks.transfer_progress = => fetch_progress;
			//cloneOptions.

			var result = Git.git_clone(&repo, "https://github.com/ponylang/pony-stable.git", "c:/temp/pony-stable", &cloneOptions);
			Git.git_repository_free(repo);
			Git.git_libgit2_shutdown();
		}*/

		public static int32 Main(String[] args)		
		{
			//TestGit();

			for (let arg in args)
			{
				if (arg != "-help")
					continue;
				Console.WriteLine(
					"""
					BeefBuild [args]
					  If no arguments are specified, a build will occur using current working directory as the workspace.
					    -config=<config>        Sets the config (defaults to Debug)
					    -minidump=<path>        Opens windows minidup file
					    -new                    Creates a new workspace and project
					    -platform=<platform>    Sets the platform (defaults to system platform)
					    -test=<path>            Executes test script
					    -verbosity=<verbosity>  Set verbosity level to: quiet/minimal/normal/detailed/diagnostics
					    -workspace=<path>       Sets workspace path (defaults to current working directory)
					""");
				return 0;
			}

			//TestZip2();
			String commandLine = scope String();
			commandLine.Join(" ", params args);

			BuildApp mApp = new BuildApp();	
			mApp.ParseCommandLine(commandLine);
			if (mApp.mFailed)
			{
				Console.Error.WriteLine("  Run with \"-help\" for a list of command-line arguments");
			}
			else
			{
				mApp.Init();
				mApp.Run();
			}
			mApp.Shutdown();
			int32 result = mApp.mFailed ? 1 : 0;

			delete mApp;

			return result;
		}
	}
}
