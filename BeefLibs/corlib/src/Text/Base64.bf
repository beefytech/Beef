namespace System.Text
{
	class Base64
	{
		static String lookup = new String("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/") ~ delete _;

		public static void Encode(String inStr, String outStr)
		{
			outStr.Reserve(inStr.Length);
			int val = 0;
			int valb = -6;
			for (let c in inStr.RawChars)
			{
				val = (val << 8) + (uint8)c;
				valb += 8;
				while (valb >= 0)
				{
					outStr.Append(lookup[(val >> valb) & 0x3F]);
					valb -= 6;
				}
			}
			if (valb > -6) { outStr.Append(lookup[((val << 8) >> (valb + 8)) & 0x3F]); }
			while (outStr.Length % 0x4 != 0)
			{
				outStr.Append('=');
			}
		}

		static bool IsPad(char8 c)
		{
			return c == '=';
		}

		static bool IsBase64(char8 c)
		{
			return (c.IsLetterOrDigit || (c == '+') || (c == '/'));
		}

		public static void Decode(String inStr, String outStr)
		{
			int in_len = inStr.Length;
			int i = 0;
			int j = 0;
			int in_ = 0;
			uint8[4] char_array_4 = ?;
			uint8[3] char_array_3 = ?;
			while (in_len-- > 0 && !IsPad(inStr[in_]) && IsBase64(inStr[in_]))
			{
				char_array_4[i++] = (uint8)inStr[in_]; in_++;
				if (i == 4)
				{
					for (i = 0; i < 4; i++)
						char_array_4[i] = (uint8)lookup.IndexOf((char8)char_array_4[i]);
					char_array_3[0] = (char_array_4[0] << 0x2) + ((char_array_4[1] & 0x30) >> 0x4);
					char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 0x2);
					char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
					for (i = 0; (i < 3); i++)
						outStr.Append((char8)char_array_3[i]);
					i = 0;
				}
			}
			if (i != 0)
			{
				for (j = i; j < 4; j++)
					char_array_4[j] = 0;
				for (j = 0; j < 4; j++)
					char_array_4[j] = (uint8)lookup.IndexOf((char8)char_array_4[j]);
				char_array_3[0] = (char_array_4[0] << 0x2) + ((char_array_4[1] & 0x30) >> 0x4);
				char_array_3[1] = ((char_array_4[1] & 0xf) << 0x4) + ((char_array_4[2] & 0x3c) >> 0x2);
				char_array_3[2] = ((char_array_4[2] & 0x3) << 0x6) + char_array_4[3];
				for (j = 0; (j < i - 1); j++) outStr.Append((char8)char_array_3[j]);
			}
		}
	}
}
