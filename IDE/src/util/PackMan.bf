using System;
using IDE.Util;

#if BF_PLATFORM_WINDOWS
using static Git.GitApi;
#define SUPPORT_GIT
#endif

namespace IDE.util
{
	class PackMan
	{
		class GitHelper
		{
			static bool sInitialized;

			public this()
			{
				if (!sInitialized)
				{
#if SUPPORT_GIT
#unwarn
					var result = git_libgit2_init();
					sInitialized = true;
#endif
				}
			}
		}

		public bool CheckLock(StringView projectName, String outPath)
		{
			return false;
		}
	}
}
