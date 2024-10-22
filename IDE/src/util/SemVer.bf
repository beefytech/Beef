using System;
using System.Collections;

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

			public static int operator<=>(Self lhs, Self rhs)
			{
				for (int i < 3)
				{
					int val = lhs.mPart[i].NumOrDefault <=> rhs.mPart[i].NumOrDefault;
					if (val != 0)
						return val;
				}
				return 0;
			}
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

		enum VersionSpec
		{
			case String(StringView version);
			case Parts(SemVer.Parts parts);
		}

		static bool IsVersionMatch(VersionSpec fullVersion, StringView wildcard)
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
				if (fullVersion case .String(let fullStr))
					if (fullStr == wildcard)
						return true;
			}

			Parts full;
			switch (fullVersion)
			{
			case .String(let fullStr):
				if (!(GetParts(fullStr) case .Ok(out full)))
					return false;
			case .Parts(let fullParts):
				full = fullParts;
			}
			
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

		public static bool IsVersionMatch(StringView fullVersion, StringView wildcard)
		{
			return IsVersionMatch(.String(fullVersion), wildcard);
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

		static void FindEmbeddedVersion(StringView version, List<int32[3]> parts, ref int32[3] highestParts)
		{
			int startIdx = 0;
			int partIdx = 0;
			int sectionCount = 0;

			if (version.IsEmpty)
				return;

			Result<void> SetPart(Parts.Kind kind)
			{
				if (partIdx >= 3)
					return .Err;
				int32 val = 0;
				if (kind case .Num(out val)) {}
				if (partIdx == 0)
					parts.Add(default);
				parts.Back[partIdx] = val;
				highestParts[partIdx] = Math.Max(highestParts[partIdx], val);
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
				if (c == '.')
				{
					FlushPart(i).IgnoreError();
					startIdx = i + 1;
					continue;
				}
				else if (c.IsNumber)
				{
					continue;
				}
				else
				{
					startIdx = i + 1;
					partIdx = 0;
					sectionCount++;
				}

			}
			FlushPart(version.Length).IgnoreError();
		}

		public static bool GetHighestConstraint(StringView ver1, StringView ver2, String outVer)
		{
			if (ver1.IsEmpty)
			{
				if (ver2.IsEmpty)
					return false;
				outVer.Set(ver2);
				return true;
			}

			if (ver2.IsEmpty)
			{
				outVer.Set(ver1);
				return true;
			}

			List<int32[3]> embeddedParts = scope .();
			int32[3] highestParts = default;
			FindEmbeddedVersion(ver1, embeddedParts, ref highestParts);
			FindEmbeddedVersion(ver2, embeddedParts, ref highestParts);

			StringView bestVer = default;
			SemVer.Parts bestParts = default;

			bool CheckMatch(SemVer.Parts parts)
			{
				bool match1 = IsVersionMatch(.Parts(parts), ver1);
				bool match2 = IsVersionMatch(.Parts(parts), ver2);

				StringView verMatch = default;

				if ((match1) && (!match2))
					verMatch = ver1;
				else if ((match2) && (!match1))
					verMatch = ver2;
				else
					return false;

				if (parts <=> bestParts > 0)
				{
					bestParts = parts;
					bestVer = verMatch;
				}
				return true;
			}

			bool CheckParts(int32[3] parts)
			{
				SemVer.Parts checkParts = default;
				for (int i < 3)
				{
					int32 val = parts[i];
					if (val < 0)
						return false;
					checkParts.mPart[i] = .Num(val);
				}
				return CheckMatch(checkParts);
			}

			// Try variations of explicitly-stated versions in constraints and try to find versions that match one constraint but not another,
			//  then select the constraint that allowed the highest version in that the other constraint didn't
			for (var parts in embeddedParts)
			{
				CheckParts(.(parts[0], parts[1], parts[2]));
				CheckParts(.(parts[0], parts[1], parts[2] - 1));
				CheckParts(.(parts[0], parts[1], parts[2] + 1));
				CheckParts(.(parts[0], parts[1] + 1, 0));
				CheckParts(.(parts[0], parts[1] - 1, 999999));
				CheckParts(.(parts[0] + 1, 0, 0));
				CheckParts(.(parts[0] - 1, 999999, 999999));
			}

			if (!bestVer.IsEmpty)
			{
				outVer.Set(bestVer);
				return true;
			}

			return false;
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.Append(mVersion);
		}

		public static int operator<=>(Self lhs, Self rhs) => (lhs?.mVersion ?? "") <=> (rhs?.mVersion ?? "");
	}
}
