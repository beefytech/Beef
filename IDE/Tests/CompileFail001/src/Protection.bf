#pragma warning disable 168

using System;

namespace Tests
{
	class Protection
	{        
		class ClassA
		{
			private int mAPriv;
			protected int mAProt;

            private this(int a)
            {
            }

            protected this()
            {
            }

			private void MethodA(int a)
			{
			}

			protected void MethodA(float b)
			{
			}

			class InnerA1
			{                
				public void Method1(ClassA ca)
				{
                    var ca2 = new ClassA();
					ca.mAPriv = 1;
					ca.mAProt = 1;
				}
			}

            private void PrivA()
            {
            }

            protected void ProtA()
            {
            }

			public mixin GetPriv()
			{
				mAPriv
			}
		}

		class ClassB : ClassA
		{
			private int mBPriv;
			protected int mBProt;

            public this() : base()
            {
				MethodA(1);
            }

            public this(int a) : base(123) //FAIL
            {

            }

            private void PrivB()
			{
			    var ca = new ClassA(); //FAIL
			    base.PrivA(); //FAIL
				ca.GetPriv!();
			}

			protected void ProtB()
			{
			    base.ProtA();
			}

			public class InnerBa
			{
				public void MethodIBa1(ClassA ca)
				{
                    var ca2 = new ClassA(); //FAIL
                    ca.mAPriv = 1; //FAIL
					ca.mAProt = 1; //FAIL
					ca.[Friend]mAPriv = 1;
				}

				public void MethodIBa2(ClassB cb)
				{
					cb.mAPriv = 1; //FAIL
					cb.mAProt = 1;
					cb.mBPriv = 1;
					cb.mBProt = 1;
				}
			}

			public void Method1(ClassA ca)
			{
				ca.mAPriv = 1; //FAIL
				ca.mAProt = 1; //FAIL
                mAPriv = 1; //FAIL
				mAProt = 1;
			}
		}

		class ClassQ : ClassB.InnerBa
		{
			public void MethodQ3(ClassA ca)
			{
				ca.mAProt = 123; //FAIL
                ca.mAPriv = 1; //FAIL                
            }

            public void MethodQ4(ClassB cb)
            {
                cb.mAProt = 123; //FAIL
                cb.mAPriv = 1; //FAIL                
            }
        }

        class ClassI<T>
        {
            private int mPrivI;
            protected int mProtI;

            private void PrivI()
            {
            }

            protected void ProtI()
            {
            }

            public void Method1(ClassI<T> val)
            {
                val.mPrivI = 1;
                val.mProtI = 1;
                val.PrivI();
                val.ProtI();                
                mPrivI = 1;
                mProtI = 2;
                PrivI();
                ProtI();
            }

            public void Method2<T2>(ClassI<T2> val)
            {
                val.mPrivI = 1;
                val.mProtI = 1;
                val.PrivI();
                val.ProtI();
                mPrivI = 1;
                mProtI = 2;
                PrivI();
                ProtI();
            }

            class InnerI
            {
                public void Method1(ClassI<T> val)
                {
                    val.mPrivI = 1;
                    val.PrivI();                    
                }

                public void Method2<T2>(ClassI<T2> val)
                {
                    val.mPrivI = 1;
                    val.PrivI();                    
                }
            }
        }

        class ClassJ<T> : ClassI<T>
        {
            public void Method3(ClassI<T> val)
            {
                val.mPrivI = 1; //FAIL
                val.mProtI = 1; //FAIL
                val.PrivI(); //FAIL
                val.ProtI(); //FAIL
                mPrivI = 1; //FAIL
                mProtI = 2;
                PrivI(); //FAIL
                ProtI();
            }

            public void Method4<T2>(ClassI<T2> val)
            {
                val.mPrivI = 1; //FAIL
                val.mProtI = 1; //FAIL
                val.PrivI(); //FAIL
                val.ProtI(); //FAIL
                mPrivI = 1; //FAIL
                mProtI = 2;
                PrivI(); //FAIL
                ProtI();
            }
        }

		class ClassR
		{
			public void Test(ClassA ca)
			{
				ca.GetPriv!();
			}
		}

		class ClassS
		{
			private static int sS0;
			public static int sS1;

			class InnerS
			{

			}

			public class InnerS2
			{

			}
		}

		class ClassT
		{
			private static int sT0;
			public static int sT1;

			class InnerT : ClassS
			{
				class MoreInnerT
				{
					class Boop : InnerS //FAIL
					{
						public void Use()
						{
							sS0 = 123; //FAIL
							sS1 = 234;

							sT0 = 345;
							sT1 = 456;
						}
					}

					class Zoop : InnerS2
					{

					}
				}
			}
		}
	}
}