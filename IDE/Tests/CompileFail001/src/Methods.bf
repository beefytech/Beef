using System;

namespace IDETest
{
	class Methods
	{
		public class ClassA
		{

		}

		public class ClassB
		{
			public static implicit operator ClassA(ClassB zongo)
			{
				return default;
			}
		}

		public static void MethodA(ClassA zong, int arg)
		{

		}

		public static void MethodA(ClassB zong, params Object[] args)
		{

		}

		public static void MethodB(ClassB zong, params Object[] args)
		{

		}

		public static void MethodB(ClassA zong, int arg)
		{

		}

		public static void Test()
		{
			ClassB cb = scope .();
			MethodA(cb, 123); //FAIL
			MethodB(cb, 234); //FAIL
		}
	}
}
