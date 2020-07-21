#pragma warning disable 168

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

			public this(int32 a, float b, String c, String d = "D")
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
			public int32 mA;
			public float mB;

			public this(int32 a, float b)
			{
				mA = a;
				mB = b;
			}
		}

		[AttributeUsage(.Class | .Method, .ReflectAttribute)]
		struct AttrCAttribute : Attribute
		{
			public int32 mA;
			public float mB;

			public this(int32 a, float b)
			{
				mA = a;
				mB = b;
			}
		}

		[AttributeUsage(.Class | .Method, ReflectUser=.All)]
		struct AttrDAttribute : Attribute
		{

		}

		[Reflect]
		class ClassA
		{
			public int32 mA = 123;
			public String mStr = "A";

			public static int32 sA = 234;
			public static String sStr = "AA";

			[AlwaysInclude, AttrC(71, 72)]
			static float StaticMethodA(int32 a, int32 b, float c, ref int32 d, ref StructA sa)
			{
				d += a + b;
				sa.mA += a;
				sa.mB += b;
				return a + b + c;
			}

			[AlwaysInclude]
			static StructA StaticMethodB(ref int32 a, ref String b)
			{
				a += 1000;
				b = "B";

				StructA sa;
				sa.mA = 12;
				sa.mB = 34;
				return sa;
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

		[Reflect, AlwaysInclude(IncludeAllMethods=true)]
		struct StructA
		{
			public int mA;
			public int mB;

			int GetA(int a)
			{
				return a + mA * 100;
			}

			int GetB(int a) mut
			{
				mB += a;
				return a + mA * 100;
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

		[Reflect(.All), AttrC(1, 2)]
		class ClassB
		{
			[AttrA(11, 22, "StrA")]
			public int mA = 1;
			[AttrB(44, 55)]
			public int mB = 2;
			public int mC = 3;
			public String mStr = "ABC";
		}

		[AttrD]
		class ClassC
		{
			public int mA = 1;
			public int mB = 2;
		}

		class ClassD
		{
			public int mA = 1;
			public int mB = 2;

			[AttrD]
			public void MethodA()
			{

			}
		}

		class ClassE
		{
			public int mA = 1;
			public int mB = 2;

			[AttrD]
			public void MethodA()
			{

			}
		}

		[Test]
		static void TestTypes()
		{
			Type t = typeof(int32);
			Test.Assert(t.InstanceSize == 4);
			t = typeof(int64);
			Test.Assert(t.InstanceSize == 8);
		}

		[Test]
		static void TestA()
		{
			ClassA ca = (ClassA)typeof(ClassA).CreateObject().Value;
			defer delete ca;
			Test.Assert(ca.mA == 123);
			Test.Assert(ca.mStr === "A");

			ClassA2 ca2 = scope ClassA2();

			Test.Assert(ca2.GetA(9) == 2009);

			int methodIdx = 0;
			var typeInfo = typeof(ClassA);
			for (let methodInfo in typeInfo.GetMethods())
			{
				switch (methodIdx)
				{
				case 0:
					StructA sa = .() { mA = 1, mB = 2 };

					Test.Assert(methodInfo.Name == "StaticMethodA");
					int32 a = 0;
					var result = methodInfo.Invoke(null, 100, (int32)20, 3.0f, &a, &sa).Get();
					Test.Assert(a == 120);
					Test.Assert(sa.mA == 101);
					Test.Assert(sa.mB == 22);
					Test.Assert(result.Get<float>() == 123);
					result.Dispose();

					Object aObj = a;
					Object saObj = sa;
					Test.Assert(methodInfo.Name == "StaticMethodA");
					result = methodInfo.Invoke(null, 100, (int32)20, 3.0f, aObj, saObj).Get();
					Test.Assert(a == 120);
					Test.Assert(sa.mA == 101);
					Test.Assert(sa.mB == 22);
					int32 a2 = (int32)aObj;
					StructA sa2 = (StructA)saObj;
					Test.Assert(a2 == 240);
					Test.Assert(sa2.mA == 201);
					Test.Assert(sa2.mB == 42);
					Test.Assert(result.Get<float>() == 123);
					result.Dispose();

					result = methodInfo.Invoke(.(), .Create(100), .Create((int32)20), .Create(3.0f), .Create(&a), .Create(&sa)).Get();
					Test.Assert(a == 240);
					Test.Assert(sa.mA == 201);
					Test.Assert(sa.mB == 42);
					Test.Assert(result.Get<float>() == 123);
					result.Dispose();

					Variant aV = .CreateOwned(a);
					Variant saV = .CreateOwned(sa);
					result = methodInfo.Invoke(.(), .Create(100), .Create((int32)20), .Create(3.0f), aV, saV).Get();
					Test.Assert(a == 240);
					Test.Assert(sa.mA == 201);
					Test.Assert(sa.mB == 42);
					a2 = aV.Get<int32>();
					sa2 = saV.Get<StructA>();
					Test.Assert(a2 == 360);
					Test.Assert(sa2.mA == 301);
					Test.Assert(sa2.mB == 62);
					Test.Assert(result.Get<float>() == 123);
					aV.Dispose();
					saV.Dispose();
					result.Dispose();

					let attrC = methodInfo.GetCustomAttribute<AttrCAttribute>().Get();
					Test.Assert(attrC.mA == 71);
					Test.Assert(attrC.mB == 72);
				case 1:
					Test.Assert(methodInfo.Name == "StaticMethodB");

					var fieldA = typeInfo.GetField("mA").Value;
					var fieldStr = typeInfo.GetField("mStr").Value;

					var fieldAV = fieldA.GetValueReference(ca).Value;
					var fieldStrV = fieldStr.GetValueReference(ca).Value;

					Test.Assert(fieldAV.Get<int32>() == 123);
					Test.Assert(fieldStrV.Get<String>() == "A");

					var res = methodInfo.Invoke(.(), fieldAV, fieldStrV).Value;
					Test.Assert(ca.mA == 1123);
					Test.Assert(ca.mStr == "B");
					var sa = res.Get<StructA>();
					Test.Assert(sa.mA == 12);
					Test.Assert(sa.mB == 34);
					res.Dispose();

					Test.Assert(fieldAV.Get<int32>() == 1123);
					Test.Assert(fieldStrV.Get<String>() == "B");
					fieldAV.Dispose();
					fieldStrV.Dispose();

					fieldAV = fieldA.GetValue(ca).Value;
					fieldStrV = fieldStr.GetValue(ca).Value;
					Test.Assert(fieldAV.Get<int32>() == 1123);
					Test.Assert(fieldStrV.Get<String>() == "B");
					fieldAV.Dispose();
					fieldStrV.Dispose();

					var fieldSA = typeInfo.GetField("sA").Value;
					var fieldSStr = typeInfo.GetField("sStr").Value;
					var fieldSAV = fieldSA.GetValueReference(null).Value;
					var fieldSStrV = fieldSStr.GetValueReference(null).Value;
					Test.Assert(fieldSAV.Get<int32>() == 234);
					Test.Assert(fieldSStrV.Get<String>() == "AA");
					res = methodInfo.Invoke(.(), fieldSAV, fieldSStrV).Value;
					Test.Assert(fieldSAV.Get<int32>() == 1234);
					Test.Assert(fieldSStrV.Get<String>() == "B");
					Test.Assert(ClassA.sA == 1234);
					Test.Assert(ClassA.sStr == "B");
					res.Dispose();
					fieldSAV.Dispose();
					fieldSStrV.Dispose();

				case 2:
					Test.Assert(methodInfo.Name == "MemberMethodA");
					var result = methodInfo.Invoke(ca, 100, (int32)20, 3.0f).Get();
					Test.Assert(result.Get<float>() == 123);
					result.Dispose();

					result = methodInfo.Invoke(.Create(ca), .Create(100), .Create((int32)20), .Create(3.0f)).Get();
					Test.Assert(result.Get<float>() == 123);
					result.Dispose();
				case 3:
					Test.Assert(methodInfo.Name == "GetA");
					var result = methodInfo.Invoke(ca, 123).Get();
					Test.Assert(result.Get<int>() == 1123);
					result.Dispose();
					result = methodInfo.Invoke(ca2, 123).Get();
					Test.Assert(result.Get<int>() == 2123);
					result.Dispose();
					result = methodInfo.Invoke(.Create(ca2), .Create(123)).Get();
					Test.Assert(result.Get<int>() == 2123);
					result.Dispose();
				case 4:
					Test.Assert(methodInfo.Name == "__BfStaticCtor");
					Test.Assert(methodInfo.IsConstructor);
				case 5:
					Test.Assert(methodInfo.Name == "__BfCtor");
					Test.Assert(methodInfo.IsConstructor);
				case 6:
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
					Test.Assert(attrA.mD == "D");
				}

				fieldIdx++;
			}

			Test.Assert(fieldIdx == 4);
			var fieldInfo = cb.GetType().GetField("mC").Value;
			int cVal = 0;
			fieldInfo.GetValue(cb, out cVal);
			fieldInfo.SetValue(cb, cVal + 1000);
			Test.Assert(cb.mC == 1003);

			Variant variantVal = Variant.Create(123);
			fieldInfo.SetValue(cb, variantVal);
			Test.Assert(cb.mC == 123);

			fieldInfo = cb.GetType().GetField("mStr").Value;
			String str = null;
			fieldInfo.GetValue(cb, out str);
			Test.Assert(str == "ABC");
			fieldInfo.SetValue(cb, "DEF");
			Test.Assert(cb.mStr == "DEF");
		}

		[Test]
		static void TestC()
		{
			let attrC = typeof(ClassB).GetCustomAttribute<AttrCAttribute>().Get();
			Test.Assert(attrC.mA == 1);
			Test.Assert(attrC.mB == 2);

			ClassC c = scope .();
			Test.Assert(typeof(ClassC).GetField("mA").Value.Name == "mA");
			Test.Assert(typeof(ClassC).GetField("mB").Value.Name == "mB");

			ClassD cd = scope .();
			Test.Assert(typeof(ClassD).GetField("mA") case .Err);

			// Should this reified call cause fields to be reflected?
			/*ClassE ce = scope .();
			ce.MethodA();
			Test.Assert(typeof(ClassE).GetField("mA").Value.Name == "mA");*/
		}

		[Test]
		static void TestStructA()
		{
			StructA sa = .() { mA = 12, mB = 23 };
			var typeInfo = typeof(StructA);

			int methodIdx = 0;
			for (let methodInfo in typeInfo.GetMethods())
			{
				switch (methodIdx)
				{
				case 0:
					Test.Assert(methodInfo.Name == "GetA");

					var result = methodInfo.Invoke(sa, 34).Get();
					Test.Assert(result.Get<int32>() == 1234);
					result.Dispose();

					result = methodInfo.Invoke(&sa, 34).Get();
					Test.Assert(result.Get<int32>() == 1234);
					result.Dispose();

					Variant saV = .Create(sa);
					defer saV.Dispose();
					result = methodInfo.Invoke(saV, .Create(34));
					Test.Assert(result.Get<int32>() == 1234);
					result.Dispose();

					result = methodInfo.Invoke(.Create(&sa), .Create(34));
					Test.Assert(result.Get<int32>() == 1234);
					result.Dispose();
				case 1:
					Test.Assert(methodInfo.Name == "GetB");

					var result = methodInfo.Invoke(sa, 34).Get();
					Test.Assert(result.Get<int32>() == 1234);
					Test.Assert(sa.mB == 23);
					result.Dispose();

					result = methodInfo.Invoke(&sa, 34).Get();
					Test.Assert(result.Get<int32>() == 1234);
					Test.Assert(sa.mB == 57);
					result.Dispose();

					Variant saV = .Create(sa);
					defer saV.Dispose();
					result = methodInfo.Invoke(saV, .Create(34));
					Test.Assert(result.Get<int32>() == 1234);
					Test.Assert(sa.mB == 57);
					result.Dispose();

					result = methodInfo.Invoke(.Create(&sa), .Create(34));
					Test.Assert(result.Get<int32>() == 1234);
					Test.Assert(sa.mB == 91);
					result.Dispose();

				case 2:
					Test.Assert(methodInfo.Name == "__BfCtor");
				case 3:
					Test.Assert(methodInfo.Name == "__Equals");
				case 4:
					Test.Assert(methodInfo.Name == "__StrictEquals");
				default:
					Test.FatalError(); // Shouldn't have any more
				}

				methodIdx++;
			}
		}
	}
}
