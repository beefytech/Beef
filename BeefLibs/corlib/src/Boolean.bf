namespace System
{
	struct Boolean : bool, IHashable
	{
		//
		// Public Constants
		//
		           
		// The public string representation of true.
		public const String TrueString  = "True";

		// The public string representation of false.
		public const String FalseString = "False";

		public override void ToString(String strBuffer)
		{
		    strBuffer.Append(((bool)this) ? TrueString : FalseString);
		}

		public int GetHashCode()
		{
			return ((bool)this) ? 1 : 0;
		}

		public static Result<bool> Parse(StringView val)
		{
			if (val.IsEmpty)
				return .Err;

			if (val.Equals(TrueString, true))
				return true;

			if (val.Equals(FalseString, true))
				return false;

			return .Err;
		}
	}
}
