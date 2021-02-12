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
	}
}
