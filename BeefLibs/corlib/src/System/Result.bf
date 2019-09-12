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

		[SkipCall]
		public void Dispose()
		{

		}

		[SkipCall]
		static void NoDispose<TVal>()
		{

		}

		static void NoDispose<TVal>() where TVal : IDisposable
		{
			Internal.FatalError("Result must be disposed", 1);
		}

		public void ReturnValueDiscarded()
		{
		    if (this case .Err(let err))
				Internal.FatalError("Unhandled error in result", 1);
			NoDispose<T>();
		}
	}

	extension Result<T> where T : IDisposable
	{
		public void Dispose()
		{
			if (this case .Ok(var val))
				val.Dispose();
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

		[SkipCall]
		public void Dispose()
		{

		}

		[SkipCall]
		static void NoDispose<TVal>()
		{

		}

		static void NoDispose<TVal>() where TVal : IDisposable
		{
			Internal.FatalError("Result must be disposed", 1);
		}

		public void ReturnValueDiscarded()
		{
		    if (this case .Err(var err))
			{
				Internal.FatalError(scope String()..AppendF("Unhandled error in result:\n ", err), 1);
			}
			NoDispose<T>();
			NoDispose<TErr>();
		}
	}

	extension Result<T, TErr> where T : IDisposable
	{
		public void Dispose()
		{
			if (this case .Ok(var val))
				val.Dispose();
		}
	}

	extension Result<T, TErr> where TErr : IDisposable
	{
		public void Dispose()
		{
			if (this case .Err(var err))
				err.Dispose();
		}
	}

	extension Result<T, TErr> where T : IDisposable where TErr : IDisposable
	{
		public void Dispose()
		{
			if (this case .Ok(var val))
				val.Dispose();
			else if (this case .Err(var err))
				err.Dispose();
		}
	}
}
