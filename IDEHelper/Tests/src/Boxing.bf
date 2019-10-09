using System;

namespace Tests
{
	class Boxing
	{
		interface IFace
		{
			int Get() mut;
		}

		struct StructA : IFace
		{
			public int mA = 100;

			public int Get() mut
			{
				mA += 1000;
				return mA;
			}
		}

		struct StructB : StructA, IHashable
		{
			public int mB = 200;

			public int GetHashCode()
			{
				return mB;
			}
		}

		struct StructC : StructB
		{
			public int mC = 300;
		}

		public static int GetFromIFace<T>(mut T val) where T : IFace
		{
			return val.Get();
		}

		[Test]
		public static void TestBasics()
		{
			StructA valA = .();
			Object obj0 = valA;
			IFace iface0 = (IFace)obj0;
			Test.Assert(GetFromIFace(mut valA) == 1100);
			Test.Assert(iface0.Get() == 1100);
			Test.Assert(GetFromIFace(iface0) == 2100);
			Test.Assert(valA.mA == 1100); // This should copy values

			StructB valB = .();
			IFace iface1 = valB;
			Test.Assert(GetFromIFace(mut valB) == 1100);
			Test.Assert(iface1.Get() == 1100);
			Test.Assert(GetFromIFace(iface1) == 2100);
			Test.Assert(valB.mA == 1100); // This should copy values
		}

		[Test]
		public static void TestPtr()
		{
			StructA* valA = scope .();
			Object obj0 = valA;
			IFace iface0 = (IFace)obj0;
			Test.Assert(GetFromIFace(mut valA) == 1100);
			Test.Assert(iface0.Get() == 2100);
			Test.Assert(GetFromIFace(iface0) == 3100);
			Test.Assert(valA.mA == 3100); // This should copy values

			StructB* valB = scope .();
			IFace iface1 = valB;
			Test.Assert(GetFromIFace(mut valB) == 1100);
			Test.Assert(iface1.Get() == 2100);
			Test.Assert(GetFromIFace(iface1) == 3100);
			Test.Assert(valB.mA == 3100); // This should copy values
		}

		public static int GetHash<T>(T val) where T : IHashable
		{
			return val.GetHashCode();
		}

		[Test]
		public static void TestPtrHash()
		{
			StructA* valA = scope .();
			StructB* valB = scope .();
			StructC* valC = scope .();

			IHashable ihA = valA;
			IHashable ihB = valB;
			IHashable ihC = valC;

			Test.Assert(ihA.GetHashCode() == (int)valA);
			Test.Assert(ihB.GetHashCode() == (int)valB);
			Test.Assert(ihC.GetHashCode() == (int)valC);

			Test.Assert(GetHash(ihA) == (int)valA);
			Test.Assert(GetHash(ihB) == (int)valB);
			Test.Assert(GetHash(ihC) == (int)valC);
		}
	}
}
