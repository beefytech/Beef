using System;

namespace Tests;

class ConstraintReification
{
	struct Yes;

	struct Trait<T>
	{
		public typealias Result = comptype(Check(typeof(T)));

		[Comptime]
		static Type Check(Type type) => typeof(Yes);
	}

	static struct Dispatch<T>
	{
		public static int Get() => 0;
	}

	extension Dispatch<T> where Trait<T>.Result : Yes
	{
		public new static int Get() => 1;
	}

	static int Get<T>() where Trait<T>.Result : Yes => 2;

	// Resolve probe types inside comptime rather than passing them as generic arguments
	// from const expressions, which would also exercise constant-resolution reification.
	[Comptime]
	static int GetExtensionTraitId() => typeof(Trait<int32>).GetTypeId();

	[Comptime]
	static int GetMethodTraitId() => typeof(Trait<float>).GetTypeId();

	static Type GetRuntimeType(int typeId)
	{
		if (typeId >= (int)Type.TypeIdEnd)
			return null;
		return Type.[Friend]GetType((.)typeId);
	}

	[Test]
	public static void TestReification()
	{
		Test.Assert(Dispatch<int32>.Get() == 1);
		Test.Assert(Get<float>() == 2);

		const int extensionTraitId = GetExtensionTraitId();
		const int methodTraitId = GetMethodTraitId();
		Test.Assert(GetRuntimeType(extensionTraitId) == null);
		Test.Assert(GetRuntimeType(methodTraitId) == null);

		// Runtime use can still promote a type previously resolved only as a constraint.
		Test.Assert(Dispatch<uint8>.Get() == 1);
		let runtimeTrait = typeof(Trait<uint8>);
		Test.Assert(GetRuntimeType((.)runtimeTrait.TypeId) == runtimeTrait);
	}
}
