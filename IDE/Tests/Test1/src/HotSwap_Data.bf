#pragma warning disable 168

using System;
using System.Diagnostics;

namespace IDETest
{
	class HotSwap_Data
	{
		struct StructA
		{
			public int mA = 100;
			/*StructA_mA2
			public int mA2 = 101;
			*/
			/*StructA_mA3
			public int mA3 = 102;
			*/
		}

		struct StructB
		{
			public int mB;
		}

		struct StructC : StructA
		{
			public StructB mSB;
			public int mC;
		}

		interface IFaceA
		{
			void IMethodA();
		}

		interface IFaceB
		{
			void IMethodB();
		}

		class ClassA : IFaceA
		{
			public void IMethodA()
			{
			}
		}

		class ClassB : IFaceB
		{
			public void IMethodB()
			{
			}
		}

		class ClassC :
			//*ClassC_IFaceA
			IFaceA
			/*@*/
			/*ClassC_IFaceB_WithoutComma
			IFaceB
			*/
			/*ClassC_IFaceB_WithComma
			, IFaceB
			*/
		{
			public void IMethodA()
			{
			}

			public void IMethodB()
			{
			}
		}

		static void Test01()
		{
			StructC sc = .();
			/*Test01_mA2
			int val = sc.mA2;
			*/
		}

		//*Func
		[AlwaysInclude]
		static void Func()
		{
			StructA sa = .();
		}
		/*@*/

		static void Test02()
		{
		}

		static void Test03()
		{
			/*Test03_delegate
			delegate void() funcPtr = new => Func;
			Console.WriteLine("Delegate: {0}", funcPtr);
			*/
		}

		static void Test04()
		{
			ClassA ca = scope .();
			IFaceA ia = ca;
			ia.IMethodA();
			ClassB cb = scope .();
			IFaceB ib = cb;
			ib.IMethodB();
			ClassC cc = scope .();
		}

		public static void Test()
		{
			/*Test_funcPtr
			function void() funcPtr = => Func;
			*/

			int a = 0;
			//Test_Test01
			Test01();
			Test01();

			//Test_Test02
			Test02();

			//Test_Test03
			Test03();
			Test03();

			//Test_Test04
			Test04();
		}
	}
}
