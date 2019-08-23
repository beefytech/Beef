namespace System.Text
{
	class Decoder
	{
		public Result<int> GetChars(uint8[] data, int dataOfs, int dataLen, char8[] chars, int charOffset)
		{
			for (int32 i = 0; i < dataLen; i++)
				chars[i + charOffset] = (char8)data[i + dataOfs];
			return dataLen;
		}
	}
}
