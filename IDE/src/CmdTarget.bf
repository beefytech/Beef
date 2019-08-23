using System;
using System.Collections.Generic;

namespace IDE
{
	class CmdTarget
	{
		List<String> mQueue = new .() ~ DeleteContainerAndItems!(_);

		public void Queue(StringView cmd)
		{
			mQueue.Add(new String(cmd));
		}

		public Result<void> Execute(StringView cmd)
		{
			return .Ok;
		}
	}
}
