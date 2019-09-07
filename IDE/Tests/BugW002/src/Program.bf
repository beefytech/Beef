#pragma warning disable 168

using System;

namespace Bug
{
	class Program
	{
		static void Main()
		{
			/*Main_OpenReg
			Windows.HKey key = 0;
			Windows.RegOpenKeyExA(Windows.HKEY_LOCAL_MACHINE, @"SYSTEM\CurrentControlSet\Control\Session Manager\Environment", 0, Windows.KEY_QUERY_VALUE, out key);
			if (key.IsInvalid)
				Runtime.FatalError();
			*/
			
			/*Main_GetValue
			String path = scope .();
			if (key.GetValue("path", path) case .Err)
				Runtime.FatalError();
			*/
		}
	}
}
