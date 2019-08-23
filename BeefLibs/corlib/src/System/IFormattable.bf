namespace System
{
	interface IFormattable
	{
		void ToString(String outString, String format, IFormatProvider formatProvider);
	}
}
