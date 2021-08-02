using System;
using System.Collections;

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

		class ClassT<T>
		{
			public void Test<T2>(T t, T2 t2)
			{
				
			}

			public void Test<T2>(T t, T2 t2) //FAIL
			{
				
			}
		}

		public static void Boing<TA, TB, TC>()
		{
			ClassT<TC>.Test<TB>(default, default); //FAIL 'IDETest.Methods.ClassT<TC>.Test<TB>(TC t, TB t2)' is a candidate
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

		public static mixin MixinA()
		{
			MixinA!(); //FAIL
		}

		public static mixin MixinB<T>(T val) where T : var
		{
		}

		public static mixin MixinB<T, T2>(T val) where T : List<T2>
		{
			MixinB!(val);
		}

		public static mixin MixinC<T>(T val) where T : var
		{
		}

		public static mixin MixinC<T, T2>(T val) where T : List<T2>
		{
			if (!val.IsEmpty)
				MixinC!(val.Front);
		}

		public static void Test()
		{
			ClassB cb = scope .();
			MethodA(cb, 123); //FAIL
			MethodB(cb, 234); //FAIL

			List<List<int>> list = scope .();
			MixinB!(list); //FAIL
			MixinC!(list);
		}
	}
}
