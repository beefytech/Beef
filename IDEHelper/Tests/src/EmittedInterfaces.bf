using System;

namespace Tests
{
	class EmittedInterfaces
	{
		interface IWidget
		{
		}

		[AttributeUsage(.Struct)]
		struct WidgetAttribute : Attribute, IComptimeTypeApply
		{
			[Comptime]
			public void ApplyToType(Type type)
			{
				Compiler.EmitAddInterface(type, typeof(IWidget));
			}
		}

		// Completes IWidget before the [Widget] structs have it emitted, the order the constraint check used to miss
		struct DirectWidget : IWidget
		{
			public int32 mValue;
		}

		[Widget]
		struct CounterWidget
		{
			public int32 mValue;
		}

		[Widget]
		struct VectorWidget
		{
			public int32 mValue;
		}

		abstract class Element
		{
			public abstract int32 Value { get; }
			public abstract int32 Mount();
		}

		class Holder<T> : Element where T : IWidget, struct
		{
			public T mWidget;

			public override int32 Value => 7;
			public override int32 Mount() => 11;
		}

		class BoxElement<T> : Element where T : IWidget, struct
		{
			public T mWidget;

			public override int32 Value => 13;
			public override int32 Mount() => 17;
		}

		class VectorElement<TExtra> : BoxElement<VectorWidget>
		{
		}

		static Element MakeHolder() => new Holder<CounterWidget>();

		[Test]
		public static void TestConstraintOnEmittedInterface()
		{
			Element element = MakeHolder();
			let holder = (Holder<CounterWidget>)element;
			Test.Assert(holder.Mount() == 11);
			Test.Assert(element.Value == 7);
			delete element;

			Element vector = new VectorElement<int>();
			Test.Assert(vector.Mount() == 17);
			Test.Assert(vector.Value == 13);
			delete vector;
		}
	}
}
