using System;
using System.Reflection;

namespace Tests
{
	/// MethodInfo.Invoke, and the reflection paths that go through it.
	///
	/// Invoke is the only dynamic call Beef has: it builds an argument list at run time and
	/// dispatches through FFILIB. That made it libffi-only, and the wasm runtime is built with
	/// BF_DISABLE_FFI, so every Invoke there answered FFIResult_NoFFI and failed. The visible
	/// symptom was not Invoke at all - Type.GetCustomAttribute has to CONSTRUCT the attribute
	/// to hand it back, and construction is an Invoke, so an attribute that was plainly present
	/// read back as absent on the web while HasCustomAttribute (a type id compare, no call)
	/// still said yes.
	///
	/// The cases below are the ABI shapes a wasm dynamic call has to get right: the scalars,
	/// a 64 bit value, a struct passed by value (one pointer under the wasm ABI) and a struct
	/// RETURNED by value (a hidden pointer prepended to the arguments).
	class ReflectionInvoke
	{
		[AttributeUsage(.Struct, .ReflectAttribute, ReflectUser = .Type | .NonStaticFields)]
		struct MarkAttribute : Attribute
		{
			public String mName;
			public int32 mVersion;

			public this(String name, int32 version)
			{
				mName = name;
				mVersion = version;
			}
		}

		[Mark("marked", 7)]
		struct Marked
		{
			public int32 mA;
			public float mB;
		}

		[Reflect]
		struct Small
		{
			public int32 mX;
			public int32 mY;
		}

		[Reflect]
		struct Big
		{
			public double mA;
			public double mB;
			public double mC;
		}

		/// Reflect covers the type; the METHODS need AlwaysInclude or they are stripped and
		/// GetMethods finds nothing to invoke.
		[Reflect, AlwaysInclude(IncludeAllMethods=true)]
		class Callee
		{
			public static int32 AddInt(int32 a, int32 b) => a + b;
			public static float AddFloat(float a, float b) => a + b;
			public static double AddDouble(double a, double b) => a + b;
			public static int64 AddLong(int64 a, int64 b) => a + b;

			/// A struct BY VALUE, which the wasm ABI passes as one pointer.
			public static int32 SumSmall(Small s) => s.mX + s.mY;
			public static double SumBig(Big b) => b.mA + b.mB + b.mC;

			/// A struct RETURNED by value, which prepends a hidden destination pointer.
			public static Small MakeSmall(int32 x, int32 y) => .() { mX = x, mY = y };

			public static void NoReturn(int32 a) { }
		}

		/// The one static method of that name. Invoke takes its arguments directly and a
		/// null target for a static, which is the shape the other reflection tests use.
		static MethodInfo Method(StringView name)
		{
			let type = (TypeInstance)typeof(Callee);
			for (let method in type.GetMethods())
			{
				if (method.Name == name)
					return method;
			}
			Runtime.FatalError(scope $"Callee.{name} is not reflected");
		}

		[Test]
		public static void TestInvokeScalars()
		{
			var result = Method("AddInt").Invoke(null, (int32)20, (int32)22).Get();
			Test.Assert(result.Get<int32>() == 42);
			result.Dispose();

			result = Method("AddFloat").Invoke(null, 1.5f, 2.25f).Get();
			Test.Assert(result.Get<float>() == 3.75f);
			result.Dispose();

			result = Method("AddDouble").Invoke(null, 1.5, 2.25).Get();
			Test.Assert(result.Get<double>() == 3.75);
			result.Dispose();
		}

		[Test]
		public static void TestInvokeInt64()
		{
			// Past 32 bits on purpose: wasm hands an i64 across the JS boundary as a BigInt,
			// and a truncating conversion would still pass with small values.
			var result = Method("AddLong").Invoke(null, (int64)5000000000, (int64)1).Get();
			Test.Assert(result.Get<int64>() == 5000000001);
			result.Dispose();
		}

		[Test]
		public static void TestInvokeStructByValue()
		{
			Small small = .() { mX = 3, mY = 4 };
			var result = Method("SumSmall").Invoke(null, small).Get();
			Test.Assert(result.Get<int32>() == 7);
			result.Dispose();

			// Bigger than a register pair, to catch anything that special cases small structs.
			Big big = .() { mA = 1.0, mB = 2.0, mC = 4.0 };
			result = Method("SumBig").Invoke(null, big).Get();
			Test.Assert(result.Get<double>() == 7.0);
			result.Dispose();
		}

		[Test]
		public static void TestInvokeStructReturn()
		{
			var result = Method("MakeSmall").Invoke(null, (int32)11, (int32)13).Get();
			let small = result.Get<Small>();
			Test.Assert((small.mX == 11) && (small.mY == 13));
			result.Dispose();
		}

		[Test]
		public static void TestInvokeVoid()
		{
			var result = Method("NoReturn").Invoke(null, (int32)1).Get();
			result.Dispose();
		}

		/// The case that surfaced this: an attribute is reachable either way, but only
		/// GetCustomAttribute has to build one, and building it is an Invoke.
		[Test]
		public static void TestGetCustomAttributeConstructs()
		{
			let type = typeof(Marked);
			Test.Assert(type.HasCustomAttribute<MarkAttribute>());

			Test.Assert(type.GetCustomAttribute<MarkAttribute>() case .Ok(let attr));
			Test.Assert(attr.mName == "marked");
			Test.Assert(attr.mVersion == 7);
		}
	}
}
