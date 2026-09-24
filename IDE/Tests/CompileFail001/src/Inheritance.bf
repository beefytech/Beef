#pragma warning disable 168

// Inheritance loops. The class loops used to recurse until the stack ran out and then hang the compiler walking a
//  circular base type chain
namespace IDETest.Inheritance
{
	class A : B { public virtual int GetA() => 1; }
	class B : A { } //FAIL causes a circular inheritance chain

	class C : D { }
	class D : E { }
	class E : C { } //FAIL causes a circular inheritance chain

	// Well formed itself, but derives from a member of the A/B loop
	class Outside : A { public override int GetA() => 2; }

	class G<T> : H<T> { }
	class H<T> : G<T> { } //FAIL causes a circular inheritance chain

	interface IA : IB { int GetIA(); }
	interface IB : IA { } //FAIL causes a data cycle
	class ImplIA : IA { public int GetIA() => 7; }

	class S : S { } //FAIL cannot be declare inner type

	class O : O.Inner //FAIL cannot be declare inner type
	{
		public class Inner : O { }
	}

	class Uses
	{
		public static void Use()
		{
			Outside outside = scope .();
			G<int> g = scope .();
			ImplIA impl = scope .();
		}
	}
}
