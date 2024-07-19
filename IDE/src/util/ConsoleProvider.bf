namespace IDE.util;

class ConsoleProvider
{
	public struct Cell
	{
		public char32 mChar;
		public uint32 mFgColor;
		public uint32 mBgColor;
	}

	public virtual void Get(int col, int row)
	{

	}
}

class WinNativeConsoleProvider
{
}