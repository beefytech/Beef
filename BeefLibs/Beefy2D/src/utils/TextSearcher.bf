using System;
using System.Collections;

namespace Beefy.utils
{
	class TextSearcher
	{
		enum CmdKind
		{
			Contains,
			NotContains,
			Equals,
		}

		struct CmdElement
		{
			public CmdKind mKind;
			public String mString;
		}

		List<CmdElement> mCmds = new List<CmdElement>() ~
		{
			for (var cmdElement in mCmds)
			{
				delete cmdElement.mString;
			}
			delete _;
		};

		Result<void> EndCmd(String findStr, ref char8 startChar, String outError, OperatorDelegate operatorHandler)
		{
			if (findStr.IsEmpty)
				return .Ok;
			
			CmdElement cmdElement;

			if ((findStr.StartsWith(":")) && (operatorHandler != null))
			{
				int opIdx = -1;
				for (int i < findStr.Length)
				{
					char8 c = findStr[i];
					if ((c == '=') || (c == '>') || (c == '<'))
					{
						if (opIdx != -1)
						{
							outError.Append("Multiple operators cannot be defined in the same text segment");
							return .Err;
						}
						opIdx = i;
					}
				}

				if (opIdx != -1)
				{
					if (startChar != 0)
					{
						outError.Append("Multiple operators cannot be defined in the same text segment");
						return .Err;
					}

					if ((operatorHandler == null) || (!operatorHandler(scope String(findStr, 0, opIdx), findStr[opIdx], scope String(findStr, opIdx + 1))))
					{
						outError.AppendF("Invalid expression: {0}", findStr);
						return .Err;
					}
					return .Ok;
				}
			}

			if (startChar == '=')
				cmdElement.mKind = .Equals;
			else if (startChar == '-')
				cmdElement.mKind = .NotContains;
			else
				cmdElement.mKind = .Contains;
			cmdElement.mString = new String(findStr);
			mCmds.Add(cmdElement);

			findStr.Clear();
			startChar = 0;

			return .Ok;
		}

		delegate bool OperatorDelegate(String lhs, char8 op, String rhs);
		public Result<void> Init(String searchStr, String outError, OperatorDelegate operatorHandler = null)
		{
			bool inQuote = false;

			char8 startChar = 0;

			String findStr = scope String();

			for (int i < searchStr.Length)
			{
				var c = searchStr[i];
				if (c == '\"')
				{
					inQuote = !inQuote;
					continue;
				}
				if (c == '\"')
				{
					c = searchStr[++i];
					findStr.Append(c);
					continue;
				}

				if ((c == ' ') && (!inQuote))
				{
					Try!(EndCmd(findStr, ref startChar, outError, operatorHandler));
					continue;
				}

				if ((startChar == 0) && (findStr.IsEmpty) && (!inQuote))
				{
					if ((c == '=') || (c == '-'))
					{
						startChar = c;
						continue;
					}
				}
				findStr.Append(c);
			}
			Try!(EndCmd(findStr, ref startChar, outError, operatorHandler));

			return .Ok;
		}

		public bool Matches(String checkStr)
		{
			for (var cmd in ref mCmds)
			{
				switch (cmd.mKind)
				{
				case .Contains:
					if (checkStr.IndexOf(cmd.mString, true) == -1)
						return false;
				case .NotContains:
					if (checkStr.IndexOf(cmd.mString, true) != -1)
						return false;
				case .Equals:
					if (!String.Equals(checkStr, cmd.mString, .OrdinalIgnoreCase))
						return false;
				}
			}

			return true;
		}
		
		public bool IsEmpty
		{
			get
			{
				return mCmds.Count == 0;
			}
		}
	}
}

