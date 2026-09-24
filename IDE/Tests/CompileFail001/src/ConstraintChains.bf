#pragma warning disable 168

using System;

// Constraint violations involving self-referencing and mutually-dependent generic types. Constraint validation used
//  to re-enter itself here, so these check that guarding against that still reports every violation
namespace IDETest.ConstraintChains
{
	class Node<T> where T : Node<T> { }
	class Mid<T> : Node<T> where T : Mid<T> { }
	class End : Mid<End> { }
	class NotMid { }
	class Bad : Mid<NotMid> { } //FAIL must derive from 'IDETest.ConstraintChains.Mid<IDETest.ConstraintChains.NotMid>'

	class CrtpBad : Node<int> { } //FAIL must derive from 'IDETest.ConstraintChains.Node<int>'

	class Base<T> where T : IDisposable { }
	class Derived : Base<int> { } //FAIL must implement 'System.IDisposable'

	class PA<T> : PB<T> where T : IDisposable { }
	class PB<T> where T : IDisposable { }

	class Box<T> where T : struct { }

	class Uses
	{
		public static void Use()
		{
			End e = scope .();
			Bad b = scope .();
			CrtpBad c = scope .();
			Derived d = scope .();
			PA<int> p = scope .(); //FAIL must implement 'System.IDisposable'
			Box<String> boxed = scope .(); //FAIL must be a value type
		}
	}
}
