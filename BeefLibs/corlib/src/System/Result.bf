namespace System
{
	enum Result<T>
	{
		case Ok(T val);
		case Err(void err);

		[Inline]
		T Unwrap()
		{
			switch (this)
			{
			case .Ok(var val): return val;
			case .Err:
				{
					Internal.FatalError("Unhandled error in result", 2);
				}
			}
		}

		public T Value
		{
			get
			{
				return Unwrap();
			}
		}

		public static implicit operator Result<T>(T value)
		{
		    return .Ok(value);
		}

		public static implicit operator T(Result<T> result)
		{
			return result.Unwrap();
		}

		public void IgnoreError()
		{
		}

		public T Get()
		{
			return Unwrap();
		}

		public T Get(T defaultVal)
		{
			if (this case .Ok(var val))
				return val;
			return defaultVal;
		}

		public T GetValueOrDefault()
		{
			if (this case .Ok(var val))
				return val;
			return default(T);
		}

		public void ReturnValueDiscarded()
		{
		    if (this case .Err(let err))
				Internal.FatalError("Unhandled error in result", 1);
		}
	}

	enum Result<T, TErr>
	{
		case Ok(T val);
		case Err(TErr err);

		T Unwrap()
		{
			switch (this)
			{
			case .Ok(var val): return val;
			case .Err(var err):
				{
					Internal.FatalError(scope String()..AppendF("Unhandled error in result:\n ", err), 2);
				}
			}
		}

		public static implicit operator Result<T, TErr>(T value)
		{
		    return .Ok(value);
		}

		public static implicit operator T(Result<T, TErr> result)
		{
			return result.Unwrap();
		}

		public void IgnoreError()
		{
		}

		public T Get()
		{
			return Unwrap();
		}

		public T Get(T defaultVal)
		{
			if (this case .Ok(var val))
				return val;
			return defaultVal;
		}

		public T GetValueOrDefault()
		{
			if (this case .Ok(var val))
				return val;
			return default(T);
		}

		public void ReturnValueDiscarded()
		{
		    if (this case .Err(var err))
			{
				Internal.FatalError(scope String()..AppendF("Unhandled error in result:\n ", err), 1);
			}
		}
	}
}
