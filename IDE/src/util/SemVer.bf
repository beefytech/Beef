using System;

namespace IDE.Util
{
	[Reflect]
	class SemVer
	{
		public struct Parts
		{
			public enum Kind
			{
				case Empty;
				case Num(int32 val);
				case Wild;

				public int32 NumOrDefault
				{
					get
					{
						if (this case .Num(let val))
							return val;
						return 0;
					}
				}
			}

			public Kind[3] mPart;
			public StringView mPreRelease;

			public Kind Major => mPart[0];
			public Kind Minor => mPart[1];
			public Kind Patch => mPart[2];
		}

		enum CompareKind
		{
			Caret, // Default
			Tilde,
			Equal,
			Gt,
			Gte,
			Lt,
			Lte
		}

		public String mVersion ~ delete _;

		public bool IsEmpty => String.IsNullOrEmpty(mVersion);

		public this()
		{

		}

		public this(SemVer semVer)
		{
			if (semVer.mVersion != null)
				mVersion = new String(semVer.mVersion);
		}

		public this(StringView version)
		{
			mVersion = new String(version);
		}

		public Result<void> Parse(StringView ver)
		{
			mVersion = new String(ver);
			return .Ok;
		}

		public static Result<Parts> GetParts(StringView version)
		{
			int startIdx = 0;
			int partIdx = 0;

			if (version.IsEmpty)
				return .Err;

			if (version.StartsWith("V", .OrdinalIgnoreCase))
				startIdx++;

			Parts parts = .();

			Result<void> SetPart(Parts.Kind kind)
			{
				if (partIdx >= 3)
					return .Err;
				parts.mPart[partIdx] = kind;
				partIdx++;
				return .Ok;
			}

			Result<void> FlushPart(int i)
			{
				StringView partStr = version.Substring(startIdx, i - startIdx);
				if (!partStr.IsEmpty)
				{
					int32 partNum = Try!(int32.Parse(partStr));
					Try!(SetPart(.Num(partNum)));
				}
				return .Ok;
			}

			for (int i in startIdx ..< version.Length)
			{
				char8 c = version[i];
				if (c.IsWhiteSpace)
					return .Err;

				if (c == '.')
				{
					Try!(FlushPart(i));
					startIdx = i + 1;
					continue;
				}
				else if (c.IsNumber)
				{
					continue;
				}
				else if (c == '-')
				{
					if (partIdx == 0)
						return .Err;
					parts.mPreRelease = version.Substring(i);
					return .Ok(parts);
				}
				else if (c == '*')
				{
					Try!(SetPart(.Wild));
					continue;
				}

				return .Err;
			}
			Try!(FlushPart(version.Length));

			return parts;
		}

		public Result<Parts> GetParts()
		{
			return GetParts(mVersion);
		}

		public static bool IsVersionMatch(StringView fullVersion, StringView wildcard)
		{
			int commaPos = wildcard.IndexOf(',');
			if (commaPos != -1)
				return IsVersionMatch(fullVersion, wildcard.Substring(0, commaPos)..Trim()) && IsVersionMatch(fullVersion, wildcard.Substring(commaPos + 1)..Trim());

			var wildcard;

			wildcard.Trim();
			CompareKind compareKind = .Caret;
			if (wildcard.StartsWith('^'))
			{
				compareKind = .Caret;
				wildcard.RemoveFromStart(1);
			}
			else if (wildcard.StartsWith('~'))
			{
				compareKind = .Tilde;
				wildcard.RemoveFromStart(1);
			}
			else if (wildcard.StartsWith('='))
			{
				compareKind = .Equal;
				wildcard.RemoveFromStart(1);
			}
			else if (wildcard.StartsWith('>'))
			{
				compareKind = .Gt;
				wildcard.RemoveFromStart(1);
				if (wildcard.StartsWith('='))
				{
					compareKind = .Gte;
					wildcard.RemoveFromStart(1);
				}
			}
			else if (wildcard.StartsWith('<'))
			{
				compareKind = .Lt;
				wildcard.RemoveFromStart(1);
				if (wildcard.StartsWith('='))
				{
					compareKind = .Lte;
					wildcard.RemoveFromStart(1);
				}
			}
			wildcard.Trim();

			// Does we include equality?
			if ((compareKind != .Gt) && (compareKind != .Lt))
			{
				if (fullVersion == wildcard)
					return true;
			}

			Parts full;
			if (!(GetParts(fullVersion) case .Ok(out full)))
				return false;
			Parts wild;
			if (!(GetParts(wildcard) case .Ok(out wild)))
				return false;

			// Don't allow a general wildcard to match a pre-prelease
			if ((!full.mPreRelease.IsEmpty) && (full.mPreRelease != wild.mPreRelease))
				return false;

			for (int partIdx < 3)
			{
				if (wild.mPart[partIdx] case .Wild)
					return true;
				int comp = full.mPart[partIdx].NumOrDefault <=> wild.mPart[partIdx].NumOrDefault;
				switch (compareKind)
				{
				case .Caret:
					if ((full.mPart[partIdx].NumOrDefault > 0) || (wild.mPart[partIdx].NumOrDefault > 0))
					{
						if (comp != 0)
							return false;
						// First number matches, now make sure we are at least a high enough version on the other numbers
						compareKind = .Gte;
					}
				case .Tilde:
					if (wild.mPart[partIdx] case .Empty)
						return true;
					if (partIdx == 2)
					{
						if (comp < 0)
							return false;
					}
					else if (comp != 0)
						return false;
				case .Equal:
					if (wild.mPart[partIdx] case .Empty)
						return true;
					if (comp != 0)
						return false;
				case .Gt:
					if (comp > 0)
						return true;
					if (partIdx == 2)
						return false;
					if (comp < 0)
						return false;
				case .Gte:
					if (comp < 0)
						return false;
				case .Lt:
					if (comp < 0)
						return true;
					if (partIdx == 2)
						return false;
					if (comp > 0)
						return false;
				case .Lte:
					if (comp > 0)
						return false;
				default:
				}
			}

			return true;
		}

		public static bool IsVersionMatch(SemVer fullVersion, SemVer wildcard) => IsVersionMatch(fullVersion.mVersion, wildcard.mVersion);

		public static Result<int> Compare(StringView lhs, StringView rhs)
		{
			Parts lhsParts;
			if (!(GetParts(lhs) case .Ok(out lhsParts)))
				return .Err;
			Parts rhsParts;
			if (!(GetParts(rhs) case .Ok(out rhsParts)))
				return .Err;

			int comp = 0;
			for (int partIdx < 3)
			{
				comp = lhsParts.mPart[partIdx].NumOrDefault <=> rhsParts.mPart[partIdx].NumOrDefault;
				if (comp != 0)
					return comp;
			}

			// Don't allow a general wildcard to match a pre-prelease
			if ((!lhsParts.mPreRelease.IsEmpty) || (!rhsParts.mPreRelease.IsEmpty))
			{
				if (lhsParts.mPreRelease.IsEmpty)
					return 1;
				if (rhsParts.mPreRelease.IsEmpty)
					return -1;
				return lhsParts.mPreRelease <=> rhsParts.mPreRelease;
			}

			return comp;
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.Append(mVersion);
		}

		public static int operator<=>(Self lhs, Self rhs) => (lhs?.mVersion ?? "") <=> (rhs?.mVersion ?? "");
	}
}
