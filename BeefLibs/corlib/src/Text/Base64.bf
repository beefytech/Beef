using System;
using System.Collections;

namespace System.Text
{
	public static class Base64
	{
		private static char8* charMap = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		public enum DecodeError
		{
			case BadCharacter(char8 character);
			case BadPadding;
		}

		public static Result<void, DecodeError> Decode(String input, String output)
		{
			var data = scope List<uint8>();
			switch (Decode(input, data))
			{
				case .Ok:
					var str = scope String((char8*)data.Ptr, data.Count);
					output.Append(str);
					return .Ok;
				case .Err(let err):
					return .Err(err);
			}
		}

		public static Result<void, DecodeError> Decode(String input, List<uint8> data)
		{
			if (input.Length % 4 != 0)
			{
				return .Err(.BadPadding);
			}

			int cur = 0;
			while (cur < input.Length - 3)
			{
				uint8 n1, n2, n3, n4;
				switch (MapCharToId(input[cur]))
				{
				    case .Ok(let v): n1 = v;
				    case .Err(let err): return .Err(err);
				}
				switch (MapCharToId(input[cur+1]))
				{
				    case .Ok(let v): n2 = v;
				    case .Err(let err): return .Err(err);
				}
				switch (MapCharToId(input[cur+2]))
				{
				    case .Ok(let v): n3 = v;
				    case .Err(let err): return .Err(err);
				}
				switch (MapCharToId(input[cur+3]))
				{
				    case .Ok(let v): n4 = v;
				    case .Err(let err): return .Err(err);
				}

				uint8 b1 = (n1 << 2) + (n2 >> 4);
				uint8 b2 = (n2 << 4) + (n3 >> 2);
				uint8 b3 = (n3 << 6) + n4;

				data.Add(b1); data.Add(b2); data.Add(b3);

				cur += 4;
			}

			return .Ok;
		}

		public static void Encode(String input, String output)
		{
			var bytes = scope Span<char8>(input.CStr(), input.Length);
			Encode(*bytes, output);
		}

		public static void Encode(Span<char8> input, String output)
		{
			int cur = 0;
			while (cur < input.Length)
			{
				uint8 c1 = (uint8)input[cur];
				uint8 c2 = cur < input.Length - 1 ? (uint8)input[cur+1] : 0;
				uint8 c3 = cur < input.Length - 2 ? (uint8)input[cur+2] : 0;

				uint8 n1 = c1 >> 2;
				uint8 n2 = ((c1 << 6) + (c2 >> 2)) >> 2;
				uint8 n3 = ((c2 << 4) + (c3 >> 4)) >> 2;
				uint8 n4 = (c3 << 2) >> 2;

				output.Append(charMap[n1]);
				output.Append(charMap[n2]);
				if (cur < input.Length - 1) output.Append(charMap[n3]);
				if (cur < input.Length - 2) output.Append(charMap[n4]);

				cur += 3;
			}

			while (output.Length % 4 != 0)
				output.Append("=");
		}

		private static Result<uint8, DecodeError> MapCharToId(char8 char)
		{
			if (char >= 'A' && char <= 'Z') return .Ok((uint8)(char - 65));
			if (char >= 'a' && char <= 'z') return .Ok((uint8)(char - 71));
			if (char >= '0' && char <= '9') return .Ok((uint8)(char + 4));
			if (char == '+') return .Ok(62);
			if (char == '/') return .Ok(63);
			if (char == '=') return .Ok(0);

			return .Err(.BadCharacter(char));
		}
	}
}