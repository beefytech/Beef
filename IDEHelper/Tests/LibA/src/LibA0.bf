using System;
using System.Collections;
using System.Diagnostics;

namespace LibA
{
	public interface ITaggable
	{
	    public String Tag
	    {
	        get;
	    }

	    public bool MatchesTag(String tag)
	    {
	        return Tag.Equals(tag);
	    }
	}

	struct LibAStruct
	{
		int mA;
	}

	interface IVal
	{
		int Val
		{
			get; set;
		}
	}

	class LibA0
	{
		public static int GetVal<T>(T val) where T : IVal
		{
			return val.Val;
		}

		public static void Dispose<T>(mut T val) where T : IDisposable
		{
			val.Dispose();
		}

		public static void Alloc<T>() where T : new, delete
		{
			let t = new T();
			delete t;
		}

		public static bool DictEquals(Dictionary<String, int> lhs, Dictionary<String, int> rhs)
		{
			return lhs == rhs;
		}

		public static int GetOverload0<T>() where T : var
		{
			T val = default;
			return Overload0(val);
		}

		public virtual int GetA()
		{
			return 1;
		}
	}

	extension LibA0
	{
		public override int GetA()
		{
			return 2;
		}
	}

	struct Handler
	{
		public static int Handle(Object obj)
		{
			return 0;
		}

		public static int HandleT<T>(T val) where T : var
		{
			return Handle(val);
		}
	}

	class LibA1
	{

	}

	class LibA2
	{
		public static void DoDispose<T>(mut T val) where T : IDisposable
		{
			val.Dispose();
		}

		public static bool DoDispose2<T>(mut T val) where T : var
		{
			return
				[IgnoreErrors(true)]
				{
					val.Dispose();
					true
				};
		}

		public static bool CheckEq<T>(T lhs, T rhs)
		{
			return lhs == rhs;
		}
	}

	class LibA3
	{
		public int mA = 3;
		public static LibA3 sLibA3 = new LibA3() ~ delete _;

		public this()
		{
			mA++;
		}
	}

	class LibA4
	{
		public int mA;
	}
}

class LibClassA
{
	public int32 mA = GetVal(7, 10, "LibA.LibClassA.mA");

	public static int32 sMagic = 1;

	public static this()
	{
		sMagic += 10;
	}

	public this()
	{
		//Debug.WriteLine("LibA.LibClassA()\n");
		mA += 100;
	}

	public this(int32 a)
	{
		mA += a;
	}

	public ~this()
	{
		sMagic += 20;
	}

	public static int32 GetVal(int32 val, int32 magic, String str)
	{
		//Debug.WriteLine("GetVal: {}", str);
		sMagic += magic;
		return val;
	}

	public int GetVal2()
	{
		return 9;
	}

	public static int GetVal3(Object obj)
	{
		return 30;
	}

	public extern int GetVal4();

	public static LibClassA Create()
	{
		LibClassA ca = new LibClassA();
		Test.Assert(ca.GetVal4() == 29);
		return ca;
	}
}

class LibClassB
{
	public static int DoGetVal3<T>(T val)
	{
		return LibClassA.GetVal3(val);
	}
}

static
{
	public static int Overload0(Object a)
	{
		return 0;
	}

	public static int Overload0(int8 a)
	{
		return 1;
	}
}
