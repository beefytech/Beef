using System.Collections;

namespace System.Text
{
	class Base64
	{
		static String lookup = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		static bool IsBase64(char8 c)
		{
			return (c.IsLetterOrDigit || (c == '+') || (c == '/'));
		}

		[Inline]
		static bool IsPad(char8 c)
		{
			return c == '=';
		}

		[Inline]
		static void a4_to_a3(uint8[] a3, uint8[] a4)
		{
			a3[0] = (a4[0] << 2) + ((a4[1] & 0x30) >> 4);
			a3[1] = ((a4[1] & 0xf) << 4) + ((a4[2] & 0x3c) >> 2);
			a3[2] = ((a4[2] & 0x3) << 6) + a4[3];
		}

		public static int getEncodedOutputLength(Span<uint8> data)
		{
			return (data.Length + 2 - ((data.Length + 2) % 3)) / 3 * 4;
		}

		public static int getDecodedOutputLength(String data)
		{
			int numEq = 0;
			for (int i = data.Length - 1; data[i] == '='; i--)
				numEq++;
			return ((6 * data.Length) / 8) - numEq;
		}

		public static Result<void> EncodeBytes(Span<uint8> data, String outStr, bool clearOutput = false)
		{
			if (data.Length == 0)
				return .Err;
			if (!clearOutput && !outStr.IsEmpty)
				return .Err;
			if(clearOutput)
				outStr.Clear();
			int32 val = 0;
			int32 valb = -6;
			outStr.Reserve(getEncodedOutputLength(data));
			for (let c in data)
			{
				val = (val << 8) + c;
				valb += 8;
				while (valb >= 0)
				{
					outStr.Append(lookup[(val >> valb) & 0x3F]);
					valb -= 6;
				}
			}
			if (valb > -6)
			{
				outStr.Append(lookup[((val << 8) >> (valb + 8)) & 0x3F]);
			}
			while (outStr.Length % 4 != 0)
			{
				outStr.Append('=');
			}
			return .Ok;
		}

		public static Result<void> DecodeBytes(String data, Span<uint8> outStr)
		{
			int in_len = data.Length;
			if (in_len < 4)
				return .Err;
			if (outStr.Length != getDecodedOutputLength(data))
				return .Err;
			int32 i = 0, j = 0;
			int32 decLen = 0;
			int counter = 0;
			let a3 = scope uint8[3];
			let a4 = scope uint8[4];
			while (in_len-- > 0 && !IsPad(data[decLen]) && IsBase64(data[decLen]))
			{
				a4[i++] = (uint8)data[decLen++];
				if (i == 4)
				{
					for (i = 0; i < 4; i++)
						a4[i] = (uint8)lookup.IndexOf((char8)a4[i]);
					a4_to_a3(a3, a4);
					for (i = 0; i < 3; i++)
						outStr.Ptr[counter++] = (a3[i]);
					i = 0;
				}
			}
			if (i != 0)
			{
				for (j = i; j < 4; j++)
					a4[j] = 0;
				for (j = 0; j < 4; j++)
					a4[j] = (uint8)lookup.IndexOf((char8)a4[j]);
				a4_to_a3(a3, a4);
				for (j = 0; j < i - 1; j++)
					outStr.Ptr[counter++] = a3[j];
			}
			return .Ok;
		}

		public static Result<void> EncodeString(String data, String outStr, bool clearOutput = false)
		{
			return [Inline]EncodeBytes(.((uint8*)data.Ptr, data.Length), outStr, clearOutput);
		}

		public static Result<void> DecodeString(String data, String outStr, bool clearOutput = false)
		{
			int in_len = data.Length;
			if (in_len < 4)
				return .Err;
			if(!clearOutput && !outStr.IsEmpty)
				return .Err;
			if(clearOutput)
			   outStr.Clear();
			int32 i = 0, j = 0;
			int32 decLen = 0;
			let a3 = scope uint8[3];
			let a4 = scope uint8[4];
			outStr.Reserve(getDecodedOutputLength(data));
			while (in_len-- > 0 && !IsPad(data[decLen]) && IsBase64(data[decLen]))
			{
				a4[i++] = (uint8)data[decLen++];
				if (i == 4)
				{
					for (i = 0; i < 4; i++)
						a4[i] = (uint8)lookup.IndexOf((char8)a4[i]);
					a4_to_a3(a3, a4);
					for (i = 0; i < 3; i++)
						outStr.Append((char8)a3[i]);
					i = 0;
				}
			}
			if (i != 0)
			{
				for (j = i; j < 4; j++)
					a4[j] = 0;
				for (j = 0; j < 4; j++)
					a4[j] = (uint8)lookup.IndexOf((char8)a4[j]);
				a4_to_a3(a3, a4);
				for (j = 0; j < i - 1; j++)
					outStr.Append((char8)a3[j]);
			}
			return .Ok;
		}
	}
}
