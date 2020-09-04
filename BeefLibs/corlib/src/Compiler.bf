namespace System
{
	static class Compiler
	{
		[LinkName("#CallerLineNum")]
		public static extern int CallerLineNum;

		[LinkName("#CallerFilePath")]
		public static extern String CallerFilePath;

		[LinkName("#CallerFileName")]
		public static extern String CallerFileName;

		[LinkName("#CallerFileDir")]
		public static extern String CallerFileDir;

		[LinkName("#CallerMemberName")]
		public static extern String CallerMemberName;

		[LinkName("#CallerProject")]
		public static extern String CallerProject;

		[LinkName("#CallerExpression")]
		public static extern String[Int32.MaxValue] CallerExpression;

		[LinkName("#TimeLocal")]
		public static extern String TimeLocal;
	}
}
