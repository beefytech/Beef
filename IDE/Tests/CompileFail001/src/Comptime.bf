using System;
using System.Reflection;

namespace IDETest
{
	class Comptime
	{
		interface IMarker
		{
		}

		[AttributeUsage(.Method)]
		struct AddInterfaceAttribute : Attribute, IComptimeMethodApply
		{
			[Comptime]
			public void ApplyToMethod(MethodInfo method)
			{
				Compiler.EmitAddInterface(method.DeclaringType, typeof(IMarker));
			}
		}

		struct Owner
		{
			[AddInterface] //FAIL
			public void Update()
			{
			}
		}

		public static void Test()
		{
			Owner().Update();
		}
	}
}
