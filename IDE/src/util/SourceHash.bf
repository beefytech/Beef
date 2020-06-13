using System.Security.Cryptography;
using System;

namespace IDE.util
{
	public enum SourceHash
	{
		public enum Kind
		{
			None,
			MD5,
			SHA256
		}

		case None;
		case MD5(MD5Hash hash);
		case SHA256(SHA256Hash hash);

		public Kind GetKind()
		{
			switch (this)
			{
			case .MD5:
				return .MD5;
			case .SHA256:
				return .SHA256;
			default:
				return .None;
			}
		}

		public static SourceHash Create(StringView hashStr)
		{
			if (hashStr.Length == 32)
			{
				if (MD5Hash.Parse(hashStr) case .Ok(let parsedHash))
				{
			        return .MD5(parsedHash);
				}
			}
			else if (hashStr.Length == 64)
			{
				if (SHA256Hash.Parse(hashStr) case .Ok(let parsedHash))
				{
					return .SHA256(parsedHash);
				}
			}

			return .None;
		}

		public static SourceHash Create(Kind kind, StringView str, LineEndingKind lineEndingKind = .Unknown)
		{
			if ((lineEndingKind != .Unknown) && (lineEndingKind != .Lf))
			{
				if (lineEndingKind == .CrLf)
				{
					String newStr = scope .(8192)..Append(str);
					newStr.Replace("\n", "\r\n");
					return Create(kind, newStr);
				}
			}

			switch (kind)
			{
			case .MD5:
				return .MD5(Security.Cryptography.MD5.Hash(.((uint8*)str.Ptr, str.Length)));
			case .SHA256:
				return .SHA256(Security.Cryptography.SHA256.Hash(.((uint8*)str.Ptr, str.Length)));
			default:
				return .None;
			}
		}

		public static bool operator==(SourceHash lhs, SourceHash rhs)
		{
			switch (lhs)
			{
			case .None:
				return rhs case .None;
			case .MD5(let lhsMD5):
				if (rhs case .MD5(let rhsMD5))
					return lhsMD5 == rhsMD5;
			case .SHA256(let lhsSHA256):
				if (rhs case .SHA256(let rhsSHA256))
				return lhsSHA256 == rhsSHA256;
			default:
			}
			return false;
		}
	}
}
