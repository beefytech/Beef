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

		[LinkName("#ProjectName")]
		public static extern String ProjectName;

		[LinkName("#ModuleName")]
		public static extern String ModuleName;

		[LinkName("#TimeLocal")]
		public static extern String TimeLocal;

		[LinkName("#IsConstEval")]
		public static extern bool IsConstEval;

		[LinkName("#IsBuilding")]
		public static extern bool IsBuilding;

		[LinkName("#IsReified")]
		public static extern bool IsReified;

		[LinkName("#CompileRev")]
		public static extern int32 CompileRev;

		[ConstEval]
		public static void Assert(bool cond)
		{
			if (!cond)
				Runtime.FatalError("Assert failed");
		}
	}
}
