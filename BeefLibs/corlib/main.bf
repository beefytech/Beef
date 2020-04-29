// Starting comment
// This is another comment
using System;
using System.Collections;
using System.Threading;
using System.Diagnostics;
using System.Collections;

namespace Hey.Dude.Bro
{
    struct StructA
    {
        //public int mVal;
        public int mVal;
    }
    
    abstract class TestClassBase
    {
        int mTCBaseVal = 9;

        //public int mMemberVal;

        //public abstract int VirtFunc();

        /*public virtual int Val
        {
            get
            {
                return 0;
			}
		} */
        
        public virtual int Val
        {
            get
            {
                return 0;
            }
        }
    }
    
    class TClass<T>
    {
        public const int sVal = 1;
        
        public class TClass2<T2, T3>
        {
            public int mVal;
            public const int sVal2 = 9;
        }
    }
    
    class TestClass : TestClassBase
    {
        this() : this(123)
        {
            PrintF("TestClass.this()\n");
            mMemberVal2 = 345;
        }

        this(int iVal)
        {
            PrintF("TestClass.this(%d)\n", iVal);
		}
        
        public override int Val
        {
            get
            {
                return 0;
            }
        }
        
        enum TestEnum2
        {
            EnumA,
            EnumB
        }

        double d = 123L;

        //var eVal = TestEnum2.EnumB;
        //var iVal = 123;
        //var tcVal = TestClass.ConstThing;
        int mMemberVal = 234;
        int mMemberVal2;
        //int Val;
        TestClass mNext;
        //int mNext;
        const int ConstThing = (int)0xe0434f4d;
        const float FloatThing = 123.45f;
        static int sStaticVal = 23;
        //static int sHeyCalls = Hey.Dude.Bro.TClass<float>.sVal;
        static int sHeyCalls = Hey.Dude.Bro.TestClass.ConstThing;
        
        public static int Hey(int inVal, int inVal2)
        {
            sHeyCalls++;
            PrintF("Hey %d %d\n", inVal, inVal2);
            if (inVal == 123)
            {
                int* ptr = null;
                *ptr = inVal;
			}
            return 123;
        }
        
        static void Method1(int val)
        {
        }
        
        public int Hey2(int heyInVal, int inVal2)
        {
            mMemberVal++;
            //PrintF("Hey2 %d %d\n", inVal, inVal2);
            return 234;
        }
        
        public static explicit operator int(TestClass pg)
        {
            return 12;
        }
        
        public TestClass GetSelf()
        {
            PrintF("Called GetSelf %08X\n", this);
            mMemberVal++;
            return this;
        }
        
        public int[] GetIntArray()
        {
            int size = 1000;
            int[] iArr = new int[size];
            for (int i = 0; i < size; i++)
                iArr[i] = i * 10 + 1;
            return iArr;
        }
        
        static int sCount = 0;

        public static void ThreadProc()
        {
            PrintF("Inside ThreadProc\n");
            int j = 0;
            int j2 = 0;
            while (true)
            {
                sCount++;
                j++;
                j2++;
			}
            /*while (true)
            {
                //Thread.Sleep(1000);
                CTest2();
                j++;
                j++;
                j++;
                j++;
                j++;
            }*/
        }
        
        public void StackOverflow(int i)
        {
            if ((i % 10000) == 0)
            {
                PrintF("StackOverflow: %d", i);
			}

            StackOverflow(i + 1);
		}

        /*public static StructA GetStructA()
        {
            return StructA();
		} */

        class DoTestClass
        {
            public int mX;
            public int mY;
		}

        public static void DoTest()
        {
            DoTestClass dtc = new DoTestClass();
            for (int i = 0; i < 200000000; i++)
            {
                dtc.mX += i;
                dtc.mY += dtc.mX;
			}
            delete dtc;
		}

        /*public static int Main()
        {
            return 123;
		} */

        public static int Main(string[] args)
        {
            //Stopwatch sw = new Stopwatch();

            //Console.WriteLine("Yo");
            //Console.WriteLine("What's up?");

            List<int> intList = new List<int>();
            intList.Add(2);
            intList.Add(3);
            intList.Add(4);

            intList.RemoveAt(1);

            intList.RemoveAt(5);
            

            char* strPtr = "WhoooooooooooooooooooooosieWhoooooooooooooooooooooosie";
            string str = scope string(strPtr);

            string[] splitStrs = scope { scope string(), scope string(), scope string() };
            //splitStrs = "Hey, man, wassap!".Split(splitStrs, ',');

            //string[] splitStrs = "Hey, man, wassap!".Split(splitStrs, ',');

            //string subtr = "What!".Substring(stack String(), 1, 2);

            return 0;
        }
    }
}
