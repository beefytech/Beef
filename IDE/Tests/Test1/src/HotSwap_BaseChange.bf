#pragma warning disable 168

namespace IDETest
{
	class HotSwap_BaseChange
	{
		class ClassA
		{
			public virtual int MethodA0()
			{
				return 100;
			}

			public virtual int MethodA1()
			{
				return 101;
			}
		}

		class ClassB
		{
			public virtual int MethodB0()
			{
				return 200;
			}

			/*ClassB_MethodB1
			public virtual int MethodB1()
			{
				return 201;
			}
			*/
		}	

		/*ClassC_0
		class ClassC : ClassA
		{
			public virtual int MethodC0()
			{
				return 300;
			}
		}
		*/

		/*ClassC_1
		class ClassC : ClassB
		{
			public virtual int MethodC0()
			{
				return 300;
			}
		}
		*/

		/*ClassC_2
		class ClassC : ClassB
		{
			public virtual int MethodC0()
			{
				return 1300;
			}

			public virtual int MethodC1()
			{
				return 1301;
			}
		}
		*/

		/*ClassC_3
		class ClassC : ClassA
		{
			// FAILS
			public this() : base(123)
			{
			}

			public virtual int MethodC0()
			{
				return 300;
			}
		}
		*/


		static void DoTest0()
		{
			//PrintF("Hey!\n");
			/*DoTest0_Body
			ClassC cc = scope .();
			int a0 = cc.MethodA0();
			int a1 = cc.MethodA1();
			int c0 = cc.MethodC0();
			*/
		}

		static void DoTest1()
		{
			/*DoTest1_Body
			ClassC cc = scope .();
			int b0 = cc.MethodB0();
			int c0 = cc.MethodC0();
			b0 = cc.MethodB0();
			c0 = cc.MethodC0();
			DoTest2(cc);
			*/
		}

		/*DoTest2_Decl
		static void DoTest2(ClassC cc)
		{
			/*DoTest2_Body
			int b0 = cc.MethodB0();
			int b1 = cc.MethodB1();
			int c0 = cc.MethodC0();
			int c1 = cc.MethodC1();
			*/
		}
		*/

		static void DoTest3()
		{
			/*DoTest3_Body
			ClassC cc = scope .();
			*/
		}

		public static void Test()
		{
			// Reify these methods and types
			ClassA ca = scope .();
			ca.MethodA0();
			ca.MethodA1();
			ClassB cb = scope .();
			cb.MethodB0();

			int a = 0;
			//Test_Start
			DoTest0();
			DoTest1();
			DoTest3();
		}
	}
}
