#pragma warning disable 168

namespace SlotRebuild
{
	// Mirrors Test1's Virtuals.bf: toggling the two comment blocks below adds four interfaces
	//  to ClassB and uses them, which grows the interface slot count between two compiles
	class Virtuals
	{
		class ClassA
		{
			public virtual int GetA(int a)
			{
				return a + 1000;
			}

			public virtual int Korf
			{
				get
				{
					return 123;
				}
			}
		}

		interface IFaceA
		{
			int GetA()
			{
				return 11;
			}
		}

		interface IFaceB
		{
			int GetB()
			{
				return 22;
			}
		}

		interface IFaceC
		{
			int GetC()
			{
				return 33;
			}
		}

		interface IFaceD
		{
			int GetD()
			{
				return 44;
			}
		}

		class ClassB : ClassA
/*ClassA_IFaces
		, IFaceA, IFaceB, IFaceC, IFaceD
*/
		{
			public override int GetA(int a)
			{
				return a + 2000;
			}

			public override int Korf
			{
				get
				{
					return 234;
				}
			}
		}

		public static int Test()
		{
			ClassB cb = scope .();
			ClassA ca = cb;
/*Test_IFaces
			IFaceA ia = cb;
			ia.GetA();
			IFaceB ib = cb;
			ib.GetB();
			IFaceC ic = cb;
			ic.GetC();
			IFaceD id = cb;
			id.GetD();
*/
			int c = ca.GetA(99);
			int d = ca.Korf;
			return c + d;
		}
	}
}
