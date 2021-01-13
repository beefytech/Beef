using System.Reflection;
namespace System
{
	static class Compiler
	{
		public struct MethodBuilder
		{
			void* mNative;

			public void Emit(String str)
			{
				Comptime_MethodBuilder_EmitStr(mNative, str);
			}

			public void Emit(Type type)
			{

			}
		}

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

		[LinkName("#IsComptime")]
		public static extern bool IsComptime;

		[LinkName("#IsBuilding")]
		public static extern bool IsBuilding;

		[LinkName("#IsReified")]
		public static extern bool IsReified;

		[LinkName("#CompileRev")]
		public static extern int32 CompileRev;

		[Comptime]
		public static void Assert(bool cond)
		{
			if (!cond)
				Runtime.FatalError("Assert failed");
		}

		static extern void* Comptime_MethodBuilder_EmitStr(void* native, StringView str);
		static extern void* Comptime_CreateMethod(int32 typeId, StringView methodName, Type returnType, MethodFlags methodFlags);
		static extern void Comptime_EmitTypeBody(int32 typeId, StringView text);
		static extern void Comptime_EmitMethodEntry(int64 methodHandle, StringView text);
		static extern void Comptime_EmitMethodExit(int64 methodHandle, StringView text);
		static extern void Comptime_EmitMixin(StringView text);

		[Comptime(OnlyFromComptime=true)]
		public static MethodBuilder CreateMethod(Type owner, StringView methodName, Type returnType, MethodFlags methodFlags)
		{
			MethodBuilder builder = .();
			builder.[Friend]mNative = Comptime_CreateMethod((.)owner.TypeId, methodName, returnType, methodFlags);
			return builder;
		}

		[Comptime(OnlyFromComptime=true)]
		public static void EmitTypeBody(Type owner, StringView text)
		{
			Comptime_EmitTypeBody((.)owner.TypeId, text);
		}

		[Comptime(OnlyFromComptime=true)]
		public static void EmitMethodEntry(ComptimeMethodInfo methodHandle, StringView text)
		{
			Comptime_EmitMethodEntry(methodHandle.mNativeMethodInstance, text);
		}

		[Comptime(OnlyFromComptime=true)]
		public static void EmitMethodExit(ComptimeMethodInfo methodHandle, StringView text)
		{
			Comptime_EmitMethodExit(methodHandle.mNativeMethodInstance, text);
		}

		[Comptime]
		public static void Mixin(StringView text)
		{
			if (Compiler.IsComptime)
				Comptime_EmitMixin(text);
		}
	}
}
