namespace System
{
	/// Specifies combinations of modifier and console keys that can interrupt the current process.
	public enum ConsoleSpecialKey
	{
		/// The Control modifier key plus the C console key.
		ControlC,
		/// The Control modifier key plus the BREAK console key.
		ControlBreak
	}

	public sealed class ConsoleCancelEventArgs : EventArgs
	{
		private ConsoleSpecialKey _type;
		private bool _cancel;

		private this() {}

		public this(ConsoleSpecialKey type)
		{
			_type = type;
			_cancel = false;
		}

		/// Gets or sets a value that indicates whether simultaneously pressing the Control modifier key and the C console key (Ctrl+C) or the Ctrl+Break keys terminates the current process.
		/// The default is false, which terminates the current process.
		public bool Cancel
		{
			get { return _cancel; }
			set { _cancel = value; }
		}

		public ConsoleSpecialKey SpecialKey
		{
			get { return _type; }
		}
	}

	public delegate void ConsoleCancelEventHandler(Object sender, ConsoleCancelEventArgs e);
}
