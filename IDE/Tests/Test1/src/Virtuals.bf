#pragma warning disable 168

namespace IDETest
{
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

		//#define A

		class ClassB : ClassA
/*ClassA_IFaces
		, IFaceA, IFaceB, IFaceC, IFaceD
*/
		{
			public override int GetA(int a)
			{
				PrintF("Skoof GetA\n");
				return a + 2000;
			}

			public override int Korf
			{
				get
				{
					PrintF("Skoof Korf\n");
					return 234;
				}
			}
		}

		public static void Test()
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

			//Test_Start
			int c = ca.GetA(99);
			int d = ca.Korf;
		}
	}
}