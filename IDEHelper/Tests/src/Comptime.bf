using System;

namespace Tests
{
	class Comptime
	{
		[AttributeUsage(.All)]
		struct IFaceAAttribute : Attribute, IComptimeTypeApply
		{
			String mMemberName;
			int32 mInitVal;

			public int32 InitVal
			{
				set mut
				{
					mInitVal = value;
				}
			}

			public this(String memberName)
			{
				mMemberName = memberName;
				mInitVal = 0;
			}

			[Comptime]
			public void ApplyToType(Type type)
			{
				Compiler.EmitDefinition(type, scope $"""
					public int32 m{mMemberName} = {mInitVal};
					public int32 GetVal{mMemberName}() => mC;
					""");

			}
		}

		[IFaceA("C", InitVal=345)]
		class ClassA
		{
			public int mA = 123;

			[OnCompile(.TypeInit), Comptime]
			public static void Generate()
			{
				Compiler.EmitDefinition(typeof(Self), """
					public int32 mB = 234;
					public int32 GetValB() => mB;
					""");
			}
		}

		[Test]
		public static void TestBasics()
		{
			ClassA ca = scope .();
			Test.Assert(ca.mA == 123);
			Test.Assert(ca.mB == 234);
			Test.Assert(ca.GetValB() == 234);
			Test.Assert(ca.mC == 345);
			Test.Assert(ca.GetValC() == 345);
		}
	}
}
