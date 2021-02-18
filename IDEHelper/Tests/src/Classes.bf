using System;
namespace Tests
{
	class Classes
	{
		public abstract class Plugin<T> where T : Plugin<T>, new, class
		{
			public abstract void OnLoad();
			public abstract void OnUnload();
		}

		public abstract class ProgramPlugin<T> : Plugin<T> where T : ProgramPlugin<T>
		{
		}

		public class ExamplePlugin : ProgramPlugin<ExamplePlugin>
		{
			public override void OnLoad()
			{
			}

			public override void OnUnload()
			{
			}
		}

		class ClassA<T0, T1>
		{
			//public ClassA<T0, ClassA<T0, T1>> mVal;
			
			public ClassA<T0, ClassA<T0, T1>> GetRecursive()
			{
				return null;
			}
		}

		[Test]
		static void TestBasics()
		{
			ClassA<int, float> ca = scope .();
			Test.Assert(typeof(decltype(ca.GetRecursive())) == typeof(ClassA<int, ClassA<int, float>>));
		}
	}
}
