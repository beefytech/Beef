using System;

namespace Tests
{
	class Reflection
	{
		[AttributeUsage(.Field, .ReflectAttribute)]
		struct AttrAAttribute : Attribute
		{
			public int32 mA;
			public float mB;
			public String mC;
			public String mD;

			public this(int32 a, float b, String c, String d)
			{
				PrintF("this: %p A: %d B: %f", this, a, (double)b);

				mA = a;
				mB = b;
				mC = c;
				mD = d;
			}
		}

		[AttributeUsage(.Field)]
		struct AttrBAttribute : Attribute
		{
			int32 mA;
			float mB;

			public this(int32 a, float b)
			{
				mA = a;
				mB = b;
			}
		}


		[Reflect]
		class ClassA
		{
			[AlwaysInclude]
			static float StaticMethodA(int32 a, int32 b, float c)
			{
				return a + b + c;
			}

			[AlwaysInclude]
			float MemberMethodA(int32 a, int32 b, float c)
			{
				return a + b + c;
			}

			public virtual int GetA(int32 a)
			{
				return a + 1000;
			}

			public virtual int GetB(int32 a)
			{
				return a + 3000;
			}
		}

		class ClassA2 : ClassA
		{
			public override int GetA(int32 a)
 			{
				 return a + 2000;
			}

			public override int GetB(int32 a)
			{
				 return a + 4000;
			}
		}

		[Reflect(.All)]
		class ClassB
		{
			[AttrA(11, 22, "StrA", "StrB")]
			int mA;
			[AttrB(44, 55)]
			int mB;
			int mC;
		}

		[Reflect(.Type)]
		class ClassC
		{
			[AttrA(11, 22, "StrA", "StrC")]
			int mA;
			[AttrB(44, 55)]
			int mB;
			int mC;
		}

		[Test]
		static void TestA()
		{
			ClassA ca = scope ClassA();
			ClassA2 ca2 = scope ClassA2();
			Test.Assert(ca2.GetA(9) == 2009);

			int methodIdx = 0;
			var typeInfo = typeof(ClassA);
			for (let methodInfo in typeInfo.GetMethods())
			{
				switch (methodIdx)
				{
				case 0:
					Test.Assert(methodInfo.Name == "StaticMethodA");
					var result = methodInfo.Invoke(null, 100, (int32)20, 3.0f).Get();
					Test.Assert(result.Get<float>() == 123);
					result.Dispose();
				case 1:
					Test.Assert(methodInfo.Name == "MemberMethodA");
					var result = methodInfo.Invoke(ca, 100, (int32)20, 3.0f).Get();
					Test.Assert(result.Get<float>() == 123);
					result.Dispose();
				case 2:
					Test.Assert(methodInfo.Name == "GetA");
					var result = methodInfo.Invoke(ca, 123).Get();
					Test.Assert(result.Get<int>() == 1123);
					result.Dispose();
					result = methodInfo.Invoke(ca2, 123).Get();
					Test.Assert(result.Get<int>() == 2123);
					result.Dispose();
				case 3:
					Test.Assert(methodInfo.Name == "__BfCtor");
					Test.Assert(methodInfo.IsConstructor);
				case 4:
					Test.FatalError(); // Shouldn't have any more
				}

				methodIdx++;
			}
		}

		[Test]
		static void TestB()
		{
			ClassB cb = scope ClassB();
			int fieldIdx = 0;
			for (let fieldInfo in cb.GetType().GetFields())
			{
				switch (fieldIdx)
				{
				case 0:
					Test.Assert(fieldInfo.Name == "mA");
					var attrA = fieldInfo.GetCustomAttribute<AttrAAttribute>().Get();
					Test.Assert(attrA.mA == 11);
					Test.Assert(attrA.mB == 22);
					Test.Assert(attrA.mC == "StrA");
					Test.Assert(attrA.mD == "StrB");
				}

				fieldIdx++;
			}
		}
	}
}
