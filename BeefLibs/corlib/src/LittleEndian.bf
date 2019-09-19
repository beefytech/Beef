namespace System
{
	class LittleEndian
	{
		public static void Write(void* ptr, uint32 val)
		{
			*((uint32*)ptr) = val;
		}
	}
}
