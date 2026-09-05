using System;
using System.Collections;

namespace Beefy.mcp
{
	// Helpers for building JSON text directly. Tool results are assembled as raw JSON strings (the
	// same approach as BeefPerf's BPJson) so a tool can emit exactly the shape it wants without
	// going through an intermediate tree; StructuredData.ToJSON is the alternative when a tree is
	// more convenient.
	public static class MCPJson
	{
		const String cBase64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		static char8 HexDigit(int32 val)
		{
			return (val < 10) ? (char8)('0' + val) : (char8)('A' + val - 10);
		}

		// Appends str as a quoted JSON string. Bytes >= 0x80 pass through untouched: the payload is
		// already UTF-8 and JSON is defined over UTF-8, so re-encoding them as \u escapes would only
		// mangle multi-byte text.
		public static void Escape(StringView str, String outStr)
		{
			outStr.Append('"');
			for (int i < str.Length)
			{
				char8 c = str[i];
				switch (c)
				{
				case '"': outStr.Append("\\\"");
				case '\\': outStr.Append("\\\\");
				case '\n': outStr.Append("\\n");
				case '\r': outStr.Append("\\r");
				case '\t': outStr.Append("\\t");
				default:
					if ((uint8)c < 0x20)
					{
						outStr.Append("\\u00");
						outStr.Append(HexDigit(((uint8)c >> 4) & 0xF));
						outStr.Append(HexDigit((uint8)c & 0xF));
					}
					else
						outStr.Append(c);
				}
			}
			outStr.Append('"');
		}

		public static void AddStr(String outStr, StringView name, StringView value, bool comma = true)
		{
			if (comma)
				outStr.Append(',');
			Escape(name, outStr);
			outStr.Append(':');
			Escape(value, outStr);
		}

		public static void AddNum(String outStr, StringView name, int64 value, bool comma = true)
		{
			if (comma)
				outStr.Append(',');
			Escape(name, outStr);
			outStr.Append(':');
			value.ToString(outStr);
		}

		public static void AddFloat(String outStr, StringView name, double value, bool comma = true)
		{
			if (comma)
				outStr.Append(',');
			Escape(name, outStr);
			outStr.Append(':');
			if ((value.IsNaN) || (value.IsInfinity))
				outStr.Append("null"); // JSON has no spelling for these
			else
				value.ToString(outStr);
		}

		public static void AddBool(String outStr, StringView name, bool value, bool comma = true)
		{
			if (comma)
				outStr.Append(',');
			Escape(name, outStr);
			outStr.Append(':');
			outStr.Append(value ? "true" : "false");
		}

		// Adds an already-serialized JSON value under name
		public static void AddRaw(String outStr, StringView name, StringView rawJson, bool comma = true)
		{
			if (comma)
				outStr.Append(',');
			Escape(name, outStr);
			outStr.Append(':');
			outStr.Append(rawJson);
		}

		// Schemas written inline in code use ' where JSON wants ", so they stay readable instead of
		// becoming a wall of backslashes. Nothing passed through here may contain a literal apostrophe.
		public static void Quote(StringView str, String outStr)
		{
			for (int i < str.Length)
				outStr.Append((str[i] == '\'') ? '"' : str[i]);
		}

		// Standard base64 with padding, as MCP image content requires
		public static void Base64Encode(Span<uint8> data, String outStr)
		{
			outStr.Reserve(outStr.Length + ((data.Length + 2) / 3) * 4);

			int i = 0;
			while (i + 2 < data.Length)
			{
				uint32 val = ((uint32)data[i] << 16) | ((uint32)data[i + 1] << 8) | (uint32)data[i + 2];
				outStr.Append(cBase64Chars[(int)((val >> 18) & 0x3F)]);
				outStr.Append(cBase64Chars[(int)((val >> 12) & 0x3F)]);
				outStr.Append(cBase64Chars[(int)((val >> 6) & 0x3F)]);
				outStr.Append(cBase64Chars[(int)(val & 0x3F)]);
				i += 3;
			}

			int remaining = data.Length - i;
			if (remaining == 1)
			{
				uint32 val = (uint32)data[i] << 16;
				outStr.Append(cBase64Chars[(int)((val >> 18) & 0x3F)]);
				outStr.Append(cBase64Chars[(int)((val >> 12) & 0x3F)]);
				outStr.Append("==");
			}
			else if (remaining == 2)
			{
				uint32 val = ((uint32)data[i] << 16) | ((uint32)data[i + 1] << 8);
				outStr.Append(cBase64Chars[(int)((val >> 18) & 0x3F)]);
				outStr.Append(cBase64Chars[(int)((val >> 12) & 0x3F)]);
				outStr.Append(cBase64Chars[(int)((val >> 6) & 0x3F)]);
				outStr.Append('=');
			}
		}
	}
}
