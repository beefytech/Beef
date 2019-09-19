namespace System.Diagnostics
{
	public enum ProcessPriorityClass
	{
		Normal = 0x20,
		Idle = 0x40,
		High = 0x80,
		RealTime = 0x100,

		BelowNormal = 0x4000,
		AboveNormal = 0x8000
	}
}
