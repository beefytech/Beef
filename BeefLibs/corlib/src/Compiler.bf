using System.Reflection;
namespace System
{
	[AttributeUsage(.Method)]
	struct OnCompileAttribute : Attribute
	{
		public enum Kind
		{
			None,
			TypeInit,
			TypeDone
		}

		public this(Kind kind)
		{
		}
	}

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
		static extern void Comptime_EmitDefinition(int32 typeId, StringView text);

		[Comptime(OnlyFromComptime=true)]
		public static MethodBuilder CreateMethod(Type owner, StringView methodName, Type returnType, MethodFlags methodFlags)
		{
			MethodBuilder builder = .();
			builder.[Friend]mNative = Comptime_CreateMethod((.)owner.TypeId, methodName, returnType, methodFlags);
			return builder;
		}

		[Comptime(OnlyFromComptime=true)]
		public static void EmitDefinition(Type owner, StringView text)
		{
			Comptime_EmitDefinition((.)owner.TypeId, text);
		}

		interface IComptimeTypeApply
		{
			void ApplyToType(Type type);
		}

		interface IComptimeMethodApply
		{
			void ApplyToMethod(Type type);
		}
	}
}
