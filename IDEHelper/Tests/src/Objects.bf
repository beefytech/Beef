namespace Tests
{
	class Objects
	{
		class ClassA
		{
			public virtual void MethodA()
			{

			}
		}

		class ClassB : ClassA
		{
			public override void MethodA()
 			{
				 base.MethodA();
			}
		}
	}
}
