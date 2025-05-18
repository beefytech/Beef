#pragma warning disable 168

namespace Tests;

namespace A.B
{
	class Zonk<T>
	{
		public static int sVal = 123;
	}

    static
    {
        public static void Main()
        {
            global::B.Init();
			global::A.B.Zonk<int>.sVal = 234;
			global::B.MethodT<float>();
			global::System.String str = null;
        }
    }
}

namespace B
{
    static
    {
        public static void Init()
        {
        }

		public static void MethodT<T>()
		{
		}
    }
}

class Lookups
{

}