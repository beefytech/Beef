namespace System
{
	enum Result<T>
	{
		case Ok(T _val);
		case Err(void _err);

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
		    if (this case .Err)
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
					String errStr = scope String();
					err.ToString(errStr);
					String showErr = scope String();
					showErr.ConcatInto("Unhandled error in result:\n ", errStr);
					Internal.FatalError(showErr, 2);
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
				String errStr = scope String();
				err.ToString(errStr);
				String showErr = scope String();
				showErr.ConcatInto("Unhandled error in result:\n ", errStr);
				Internal.FatalError(showErr, 1);
			}
		}
	}
}
