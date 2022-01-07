using System;
using System.Collections;

namespace IDE.util
{
	class DefinesSet
	{
		public List<String> mDefines = new List<String>() ~ DeleteContainerAndItems!(_);
		public HashSet<String> mDefinesSet = new HashSet<String>() ~ delete _;

		public void Add(StringView str)
		{
			if (str.StartsWith("!"))
			{
				String removeKey = scope .(str, 1);
				if (mDefinesSet.Remove(removeKey))
					mDefines.Remove(removeKey);
				return;
			}

			if (!mDefinesSet.Contains(scope .(str)))
			{
				var strCopy = new String(str);
				mDefines.Add(strCopy);
				mDefinesSet.Add(strCopy);
			}
			
		}
	}
}
