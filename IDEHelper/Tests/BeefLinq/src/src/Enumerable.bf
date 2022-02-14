using System.Collections;
using System;
using internal System.Linq;

namespace System.Linq
{
	public static
	{
		public static class Enumerable
		{
			public struct EmptyEnumerable<TSource> : IEnumerable<TSource>
			{
				public Enumerator GetEnumerator() => .();

				public struct Enumerator : IEnumerator<TSource>
				{
					public Result<TSource> GetNext()
					{
						return .Err;
					}
				}
			}

			public static EmptyEnumerable<TSource> Empty<TSource>() => .();

			public struct RangeEnumerable<TSource> : IEnumerable<TSource> where TSource : operator TSource + int
			{
				TSource mStart;
				TSource mEnd;

				public this(TSource start, TSource end)
				{
					mStart = start;
					mEnd = end;
				}

				public Enumerator GetEnumerator() => .(mStart, mEnd);

				public struct Enumerator : IEnumerator<TSource>
				{
					TSource mCurrent;
					TSource mEnd;

					public this(TSource start, TSource end)
					{
						mCurrent = start;
						mEnd = end;
					}

					public Result<TSource> GetNext() mut
					{
						if (mCurrent == mEnd)
							return .Err;

						let next = mCurrent;
						mCurrent = mCurrent + 1;
						return .Ok(next);
					}
				}
			}

			public static RangeEnumerable<TSource>
				Range<TSource>(TSource count)
				where TSource : operator TSource + int
			{
				return .(default, count);
			}

			public static RangeEnumerable<TSource>
				Range<TSource>(TSource start, TSource end)
				where TSource : operator TSource + int
				where TSource : operator TSource + TSource
			{
				return .(start, end);
			}

			public struct RepeatEnumerable<TSource> : IEnumerable<TSource>
			{
				TSource mValue;
				int mCount;

				public this(TSource value, int count)
				{
					mValue = value;
					mCount = count;
				}

				public Enumerator GetEnumerator() => .(mValue, mCount);

				public struct Enumerator : IEnumerator<TSource>
				{
					TSource mValue;
					int mCount;

					public this(TSource value, int count)
					{
						mValue = value;
						mCount = count;
					}

					public Result<TSource> GetNext() mut
					{
						if (--mCount >= 0)
							return .Ok(mValue);

						return .Err;
					}
				}
			}

			public static RepeatEnumerable<TSource>
				Repeat<TSource>(TSource value, int count)
			{
				return .(value, count);
			}
		}


		#region Matching
		public static bool All<TEnum, TSource, TPredicate>(this TEnum items, TPredicate predicate)
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
		{
			return InternalAll(items, predicate);
		}

		public static bool All<TCollection, TSource, TPredicate>(this TCollection items, TPredicate predicate)
			where TCollection : concrete, IEnumerable<TSource>
			where TPredicate : delegate bool(TSource)
		{
			return InternalAll<decltype(default(TCollection).GetEnumerator()), TSource, TPredicate>(items.GetEnumerator(), predicate);
		}

		static bool InternalAll<TEnum, TSource, TPredicate>(TEnum items, TPredicate predicate)
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
		{
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;
				switch (enumerator.GetNext())
				{
				case .Ok(let val): if (!predicate(val)) return false;
				case .Err: return false;
				}

				while (enumerator.GetNext() case .Ok(let val))
					if (!predicate(val))
						return false;

				return true;
			}
		}

		public static bool Any<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			for (var it in items)
				return true;

			return false;
		}

		public static bool Any<TCollection, TSource, TPredicate>(this TCollection items, TPredicate predicate)
			where TCollection : concrete, IEnumerable<TSource>
			where TPredicate : delegate bool(TSource)
		{
			for (var it in items)
				if (predicate(it))
					return true;

			return false;
		}

		public static bool Any<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			for (var it in items)
				return true;

			return false;
		}

		public static bool Any<TEnum, TSource, TPredicate>(this TEnum items, TPredicate predicate)
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
		{
			for (var it in items)
				if (predicate(it))
					return true;

			return false;
		}

		public static bool Contains<TCollection, TSource>(this TCollection items, TSource source)
			where TCollection : concrete, IEnumerable<TSource>
			where bool : operator TSource == TSource
		{
			for (var it in items)
				if (it == source)
					return true;

			return false;
		}

		public static bool Contains<TEnum, TSource>(this TEnum items, TSource source)
			where TEnum : concrete, IEnumerator<TSource>
			where bool : operator TSource == TSource
		{
			for (var it in items)
				if (it == source)
					return true;

			return false;
		}


		public static bool SequenceEquals<TLeft, TRight, TSource>(this TLeft left, TRight right)
			where TLeft : concrete, IEnumerable<TSource>
			where TRight : concrete, IEnumerable<TSource>
			where bool : operator TSource == TSource
		{
			return InternalSequenceEquals<
				decltype(default(TLeft).GetEnumerator()),
				decltype(default(TRight).GetEnumerator()),
				TSource>(left.GetEnumerator(), right.GetEnumerator());
		}

		public static bool SequenceEquals<TLeft, TRight, TSource>(this TLeft left, TRight right)
			where TLeft : concrete, IEnumerable<TSource>
			where TRight : concrete, IEnumerator<TSource>
			where bool : operator TSource == TSource
		{
			return InternalSequenceEquals<
				decltype(default(TLeft).GetEnumerator()),
				TRight,
				TSource>(left.GetEnumerator(), right);
		}

		public static bool SequenceEquals<TLeft, TRight, TSource>(this TLeft left, TRight right)
			where TLeft : concrete, IEnumerator<TSource>
			where TRight : concrete, IEnumerable<TSource>
			where bool : operator TSource == TSource
		{
			return InternalSequenceEquals<
				TLeft,
				decltype(default(TRight).GetEnumerator()),
				TSource>(left, right.GetEnumerator());
		}

		public static bool SequenceEquals<TLeft, TRight, TSource>(this TLeft left, TRight right)
			where TLeft : concrete, IEnumerator<TSource>
			where TRight : concrete, IEnumerator<TSource>
			where bool : operator TSource == TSource
		{
			return InternalSequenceEquals<TLeft, TRight, TSource>(left, right);
		}

		static bool InternalSequenceEquals<TLeft, TRight, TSource>(TLeft left, TRight right)
			where TLeft : concrete, IEnumerator<TSource>
			where TRight : concrete, IEnumerator<TSource>
			where bool : operator TSource == TSource
		{
			using (var iterator0 = Iterator.Wrap<TLeft, TSource>(left))
			{
				var e0 = iterator0.mEnum;
				using (var iterator1 = Iterator.Wrap<TRight, TSource>(right))
				{
					var e1 = iterator1.mEnum;
					while (true)
					{
						switch (e0.GetNext())
						{
						case .Ok(let i0):
							switch (e1.GetNext())
							{
							case .Ok(let i1):
								if (i0 != i1)
									return false;
							case .Err:
								return false;
							}
						case .Err:
							return e1.GetNext() case .Err;
						}
					}
				}
			}
		}

		#endregion

		#region Aggregates

		public static TSource Average<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
			where TSource : operator TSource / int
			where TSource : operator TSource + TSource
		{
			return InternalAverage<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator());
		}

		public static TSource Average<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
			where TSource : operator TSource / int
			where TSource : operator TSource + TSource
		{
			return InternalAverage<TEnum, TSource>(items);
		}

		static TSource InternalAverage<TEnum, TSource>(TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
			where TSource : operator TSource / int
			where TSource : operator TSource + TSource
		{
			var count = 0;
			TSource sum = ?;
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;

				switch (enumerator.GetNext())
				{
				case .Ok(let val):
					sum = val;
					count++;
				case .Err: return default;
				}

				while (enumerator.GetNext() case .Ok(let val))
				{
					sum += val;
					count++;
				}

				return sum / count;
			}
		}

		public static TSource Max<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
			where bool : operator TSource < TSource
		{
			return InternalMax<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator());
		}

		public static TSource Max<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
			where bool : operator TSource < TSource
		{
			return InternalMax<TEnum, TSource>(items);
		}

		static TSource InternalMax<TEnum, TSource>(TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
			where bool : operator TSource < TSource
		{
			TSource max = ?;
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;
				switch (enumerator.GetNext())
				{
				case .Ok(let val): max = val;
				case .Err: return default;
				}

				while (enumerator.GetNext() case .Ok(let val))
				{
					let next = val;
					if (max < next)
						max = next;
				}
			}
			return max;
		}

		public static TSource Min<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
			where bool : operator TSource < TSource
		{
			return InternalMin<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator());
		}

		public static TSource Min<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
			where bool : operator TSource < TSource
		{
			return InternalMin<TEnum, TSource>(items);
		}

		static TSource InternalMin<TEnum, TSource>(TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
			where bool : operator TSource < TSource
		{
			TSource min = ?;
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;
				switch (enumerator.GetNext())
				{
				case .Ok(let val): min = val;
				case .Err: return default;
				}

				while (enumerator.GetNext() case .Ok(let val))
				{
					let next = val;
					if (next < min)
						min = next;
				}
			}
			return min;
		}

		public static TSource Sum<TCollection, TSource, TPredicate>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
			where TPredicate : delegate bool(TSource)
			where TSource : operator TSource + TSource
		{
			return InternalSum<decltype(default(TCollection).GetEnumerator()), TSource, TPredicate>(items.GetEnumerator());
		}

		public static TSource Sum<TEnum, TSource, TPredicate>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
			where TSource : operator TSource + TSource
		{
			return InternalSum<TEnum, TSource, TPredicate>(items);
		}

		static TSource InternalSum<TEnum, TSource, TPredicate>(TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
			where TSource : operator TSource + TSource
		{
			TSource sum = ?;
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;
				switch (enumerator.GetNext())
				{
				case .Ok(let val): sum = val;
				case .Err: return default;
				}

				while (enumerator.GetNext() case .Ok(let val))
					sum += val;
			}
			return sum;
		}

		public static int Count<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			if (typeof(TCollection) == typeof(List<TSource>))
				return (items as List<TSource>).Count;
			if (typeof(TCollection) == typeof(TSource[]))
				return (items as TSource[]).Count;

			return InternalCount<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator());
		}


		public static int Count<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			return InternalCount<TEnum, TSource>(items);
		}

		public static int InternalCount<TEnum, TSource>(TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			var count = 0;
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;
				while (enumerator.GetNext() case .Ok)
					count++;
			}
			return count;
		}

		#endregion

		#region Find in enumerable

		internal static bool InternalElementAt<TEnum, TSource>(TEnum items, int index, out TSource val)
			where TEnum : concrete, IEnumerator<TSource>
		{
			var index;
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;
				while (--index > 0)
				{
					if (enumerator.GetNext() case .Err)
						break;
				}

				if (index == 0 && enumerator.GetNext() case .Ok(out val))
					return true;
			}
			val = default;
			return false;
		}

		public static TSource ElementAt<TCollection, TSource>(this TCollection items, int index)
			where TCollection : concrete, IEnumerable<TSource>
		{
			if (InternalElementAt<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator(), index, let val))
				return val;

			Runtime.FatalError("Not enough elements in the sequence.");
		}

		public static TSource ElementAt<TEnum, TSource>(this TEnum items, int index)
			where TEnum : concrete, IEnumerator<TSource>
		{
			if (InternalElementAt<TEnum, TSource>(items, index, let val))
				return val;

			Runtime.FatalError("Not enough elements in the sequence.");
		}

		public static TSource ElementAtOrDefault<TCollection, TSource>(this TCollection items, int index)
			where TCollection : concrete, IEnumerable<TSource>
		{
			if (InternalElementAt<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator(), index, let val))
				return val;

			return default;
		}

		public static TSource ElementAtOrDefault<TEnum, TSource>(this TEnum items, int index)
			where TEnum : concrete, IEnumerator<TSource>
		{
			if (InternalElementAt<TEnum, TSource>(items, index, let val))
				return val;

			return default;
		}

		public static bool InternalFirst<TEnum, TSource>(TEnum items, out TSource val)
			where TEnum : concrete, IEnumerator<TSource>
		{
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;
				if (enumerator.GetNext() case .Ok(out val))
					return true;
			}

			val = default;
			return false;
		}

		public static TSource First<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			if (InternalFirst<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator(), let val))
				return val;
			Runtime.FatalError("Sequence contained no elements.");
		}

		public static TSource First<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			if (InternalFirst<TEnum, TSource>(items, let val))
				return val;
			Runtime.FatalError("Sequence contained no elements.");
		}

		public static TSource FirstOrDefault<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			if (InternalFirst<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator(), let val))
				return val;

			return default;
		}

		public static TSource FirstOrDefault<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			if (InternalFirst<TEnum, TSource>(items, let val))
				return val;

			return default;
		}

		internal static bool InternalLast<TEnum, TSource>(TEnum items, out TSource val)
			where TEnum : concrete, IEnumerator<TSource>
		{
			val = ?;
			var found = false;
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;
				if (enumerator.GetNext() case .Ok(out val))
					found = true;

				while (enumerator.GetNext() case .Ok(let temp))
					val = temp;
			}

			if (!found)
				val = default;
			return found;
		}

		public static TSource Last<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			if (InternalLast<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator(), let val))
				return val;

			Runtime.FatalError("Sequence contained no elements.");
		}

		public static TSource Last<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			if (InternalLast<TEnum, TSource>(items, let val))
				return val;

			Runtime.FatalError("Sequence contained no elements.");
		}

		public static TSource LastOrDefault<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			if (InternalLast<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator(), let val))
				return val;

			return default;
		}

		public static TSource LastOrDefault<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			if (InternalLast<TEnum, TSource>(items, let val))
				return val;

			return default;
		}

		internal static bool InternalSingle<TEnum, TSource>(TEnum items, out TSource val)
			where TEnum : concrete, IEnumerator<TSource>
		{
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;

				if (enumerator.GetNext() case .Ok(out val))
				{
					if (enumerator.GetNext() case .Err)
						return true;

					Runtime.FatalError("Sequence matched more than one element.");
				}
			}

			val = default;
			return false;
		}

		public static TSource Single<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			if (InternalSingle<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator(), let val))
				return val;

			Runtime.FatalError("Sequence contained no elements.");
		}

		public static TSource Single<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			if (InternalSingle<TEnum, TSource>(items, let val))
				return val;

			Runtime.FatalError("Sequence contained no elements.");
		}

		public static TSource SingleOrDefault<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			if (InternalSingle<decltype(default(TCollection).GetEnumerator()), TSource>(items.GetEnumerator(), let val))
				return val;

			return default;
		}

		public static TSource SingleOrDefault<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			if (InternalSingle<TEnum, TSource>(items, let val))
				return val;

			return default;
		}

		#endregion

#region Enumerable Chains
		struct Iterator
		{
			public static Iterator<TEnum, TSource> Wrap<TEnum, TSource>(TEnum items)
				where TEnum : concrete, IEnumerator<TSource>
			{
				return .(items);
			}

			public static Iterator<decltype(default(TCollection).GetEnumerator()), TSource> Wrap<TCollection, TSource>(TCollection items)
				where TCollection : concrete, IEnumerable<TSource>
			{
				return .(items.GetEnumerator());
			}
		}

		struct Iterator<TEnum, TSource> : IDisposable
			where TEnum : concrete, IEnumerator<TSource>
		{
			internal TEnum mEnum;

			public this(TEnum items)
			{
				mEnum = items;
			}

			[SkipCall]
			public void Dispose() mut { }

			public static implicit operator Iterator<TEnum, TSource>(TEnum enumerator) => .(enumerator);
		}

		extension Iterator<TEnum, TSource> : IDisposable where TEnum : IDisposable
		{
			public void Dispose() mut => mEnum.Dispose();
		}

		struct SelectEnumerable<TSource, TEnum, TSelect, TResult> : Iterator<TEnum, TSource>, IEnumerable<TResult>
			where TSelect : delegate TResult(TSource)
			where TEnum : concrete, IEnumerator<TSource>
		{
			TSelect mDlg;

			public this(TEnum e, TSelect dlg) : base(e)
			{
				mDlg = dlg;
			}

			Result<TResult> GetNext() mut
			{
				if (mEnum.GetNext() case .Ok(let val))
					return mDlg(val);

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TResult>
			{
				SelfOuter mEnum;
				public this(SelfOuter enumerator)
				{
					mEnum = enumerator;
				}
				public Result<TResult> GetNext() mut => mEnum.GetNext();
			}
		}

		public static SelectEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TSelect, TResult>
			Select<TCollection, TSource, TSelect, TResult>(this TCollection items, TSelect select)
			where TCollection : concrete, IEnumerable<TSource>
			where TSelect : delegate TResult(TSource)
		{
			return .(items.GetEnumerator(), select);
		}

		public static SelectEnumerable<TSource, TEnum, TSelect, TResult>
			Select<TEnum, TSource, TSelect, TResult>(this TEnum items, TSelect select)
			where TEnum : concrete, IEnumerator<TSource>
			where TSelect : delegate TResult(TSource)
		{
			return .(items, select);
		}

		struct WhereEnumerable<TSource, TEnum, TPredicate> : IEnumerable<TSource>, IDisposable
			where TPredicate : delegate bool(TSource)
			where TEnum : concrete, IEnumerator<TSource>
		{
			TPredicate mPredicate;
			Iterator<TEnum, TSource> mIterator;

			public this(TEnum enumerator, TPredicate predicate) 
			{
				mIterator = enumerator;
				mPredicate = predicate;
			}

			Result<TSource> GetNext() mut
			{
				while (mIterator.mEnum.GetNext() case .Ok(let val))
					if (mPredicate(val))
						return .Ok(val);

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>, IDisposable
			{
				SelfOuter mSelf;
				public this(SelfOuter self)
				{
					mSelf = self;
				}
				public Result<TSource> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut
				{
					mSelf.Dispose();
				}
			}

			public void Dispose() mut
			{
				mIterator.Dispose();
			}
		}

		public static WhereEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TPredicate>
			Where<TCollection, TSource, TPredicate>(this TCollection items, TPredicate predicate)
			where TCollection : concrete, IEnumerable<TSource>
			where TPredicate : delegate bool(TSource)
		{
			return .(items.GetEnumerator(), predicate);
		}

		public static WhereEnumerable<TSource, TEnum, TPredicate>
			Where<TEnum, TSource, TPredicate>(this TEnum items, TPredicate predicate)
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
		{
			return .(items, predicate);
		}

		struct TakeEnumerable<TSource, TEnum> : Iterator<TEnum, TSource>, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
		{
			int mCount;

			public this(TEnum enumerator, int count) : base(enumerator)
			{
				mCount = count;
			}

			public Result<TSource> GetNext() mut
			{
				while (mCount-- > 0 && mEnum.GetNext() case .Ok(let val))
					return val;

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>
			{
				SelfOuter mEnum;
				public this(SelfOuter enumerator)
				{
					mEnum = enumerator;
				}
				public Result<TSource> GetNext() mut => mEnum.GetNext();
			}
		}

		public static TakeEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			Take<TCollection, TSource>(this TCollection items, int count)
			where TCollection : concrete, IEnumerable<TSource>
		{
			return .(items.GetEnumerator(), count);
		}

		public static TakeEnumerable<TSource, TEnum>
			Take<TEnum, TSource>(this TEnum items, int count)
			where TEnum : concrete, IEnumerator<TSource>
		{
			return .(items, count);
		}

		struct TakeWhileEnumerable<TSource, TEnum, TPredicate> : Iterator<TEnum, TSource>, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
		{
			TPredicate mPredicate;

			public this(TEnum enumerator, TPredicate predicate) : base(enumerator)
			{
				mPredicate = predicate;
			}

			Result<TSource> GetNext() mut
			{
				if (mEnum.GetNext() case .Ok(let val))
					if (mPredicate(val))
						return .Ok(val);

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>
			{
				SelfOuter mEnum;
				public this(SelfOuter enumerator)
				{
					mEnum = enumerator;
				}
				public Result<TSource> GetNext() mut => mEnum.GetNext();
			}
		}

		public static TakeWhileEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TPredicate>
			TakeWhile<TCollection, TSource, TPredicate>(this TCollection items, TPredicate predicate)
			where TCollection : concrete, IEnumerable<TSource>
			where TPredicate : delegate bool(TSource)
		{
			return .(items.GetEnumerator(), predicate);
		}

		public static TakeWhileEnumerable<TSource, TEnum, TPredicate>
			TakeWhile<TEnum, TSource, TPredicate>(this TEnum items, TPredicate predicate)
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
		{
			return .(items, predicate);
		}

		struct SkipEnumerable<TSource, TEnum> : Iterator<TEnum, TSource>, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
		{
			int mCount;

			public this(TEnum enumerator, int count) : base(enumerator)
			{
				mCount = count;
			}

			public Result<TSource> GetNext() mut
			{
				while (mCount-- > 0 && mEnum.GetNext() case .Ok(?)) { }

				while (mEnum.GetNext() case .Ok(let val))
					return val;

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>
			{
				SelfOuter mEnum;
				public this(SelfOuter enumerator)
				{
					mEnum = enumerator;
				}
				public Result<TSource> GetNext() mut => mEnum.GetNext();
			}
		}

		public static SkipEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			Skip<TCollection, TSource>(this TCollection items, int count)
			where TCollection : concrete, IEnumerable<TSource>
		{
			return .(items.GetEnumerator(), count);
		}

		public static SkipEnumerable<TSource, TEnum>
			Skip<TEnum, TSource>(this TEnum items, int count)
			where TEnum : concrete, IEnumerator<TSource>
		{
			return .(items, count);
		}

		struct SkipWhileEnumerable<TSource, TEnum, TPredicate> : Iterator<TEnum, TSource>, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
		{
			TPredicate mPredicate;
			int mState = 0;

			public this(TEnum enumerator, TPredicate predicate) : base(enumerator)
			{
				mPredicate = predicate;
			}

			public Result<TSource> GetNext() mut
			{
				switch (mState) {
				case 0:
					while (mEnum.GetNext() case .Ok(let val))
					{
						if (!mPredicate(val))
						{
							mState = 1;
							return .Ok(val);
						}
					}
				case 1:
					return mEnum.GetNext();
				}

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>
			{
				SelfOuter mEnum;
				public this(SelfOuter enumerator)
				{
					mEnum = enumerator;
				}
				public Result<TSource> GetNext() mut => mEnum.GetNext();
			}
		}

		public static SkipWhileEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TPredicate>
			SkipWhile<TCollection, TSource, TPredicate>(this TCollection items, TPredicate predicate)
			where TCollection : concrete, IEnumerable<TSource>
			where TPredicate : delegate bool(TSource)
		{
			return .(items.GetEnumerator(), predicate);
		}

		public static SkipWhileEnumerable<TSource, TEnum, TPredicate>
			SkipWhile<TEnum, TSource, TPredicate>(this TEnum items, TPredicate predicate)
			where TEnum : concrete, IEnumerator<TSource>
			where TPredicate : delegate bool(TSource)
		{
			return .(items, predicate);
		}

		struct DefaultIfEmptyEnumerable<TSource, TEnum> : Iterator<TEnum, TSource>, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
		{
			TSource mDefaultValue;
			int mState = 0;

			public this(TEnum enumerator, TSource defaultValue) : base(enumerator)
			{
				mDefaultValue = defaultValue;
			}

			public Result<TSource> GetNext() mut
			{
				switch (mState) {
				case 0:
					if (mEnum.GetNext() case .Ok(let val))
					{
						mState = 1;
						return .Ok(val);
					}

					mState = 2;
					return .Ok(mDefaultValue);
				case 1:
					return mEnum.GetNext();
				}

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>
			{
				SelfOuter mEnum;
				public this(SelfOuter enumerator)
				{
					mEnum = enumerator;
				}
				public Result<TSource> GetNext() mut => mEnum.GetNext();
			}
		}

		public static DefaultIfEmptyEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			DefaultIfEmpty<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			return .(items.GetEnumerator(), default);
		}

		public static DefaultIfEmptyEnumerable<TSource, TEnum>
			DefaultIfEmpty<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
		{
			return .(items, default);
		}

		public static DefaultIfEmptyEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			DefaultIfEmpty<TCollection, TSource>(this TCollection items, TSource defaultValue = default)
			where TCollection : concrete, IEnumerable<TSource>
		{
			return .(items.GetEnumerator(), defaultValue);
		}

		public static DefaultIfEmptyEnumerable<TSource, TEnum>
			DefaultIfEmpty<TEnum, TSource>(this TEnum items, TSource defaultValue = default)
			where TEnum : concrete, IEnumerator<TSource>
		{
			return .(items, defaultValue);
		}

		struct DistinctEnumerable<TSource, TEnum> : IEnumerable<TSource>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			HashSet<TSource> mDistinctValues;
			HashSet<TSource>.Enumerator mEnum;
			Iterator<TEnum, TSource> mIterator;
			int mState = 0;

			public this(TEnum enumerator)
			{
				mIterator = .(enumerator);
				mDistinctValues = new .();
				mEnum = default;
			}

			public Result<TSource> GetNext() mut
			{
				switch (mState) {
				case 0:
					var enumerator = mIterator.mEnum;
					while (enumerator.GetNext() case .Ok(let val))
						mDistinctValues.Add(val);

					mIterator.Dispose();
					mIterator = default;
					mEnum = mDistinctValues.GetEnumerator();
					mState = 1;
					fallthrough;
				case 1:
					return mEnum.GetNext();
				}

				return .Err;
			}

			public void Dispose() mut
			{
				mEnum.Dispose();
				DeleteAndNullify!(mDistinctValues);
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>, IDisposable
			{
				SelfOuter mEnum;
				public this(SelfOuter enumerator)
				{
					mEnum = enumerator;
				}
				public Result<TSource> GetNext() mut => mEnum.GetNext();

				public void Dispose() mut => mEnum.Dispose();
			}
		}

		public static DistinctEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			Distinct<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
			where TSource : IHashable
		{
			return .(items.GetEnumerator());
		}

		public static DistinctEnumerable<TSource, TEnum>
			Distinct<TEnum, TSource>(this TEnum items)
			where TEnum : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			return .(items);
		}

		struct ReverseEnumerable<TSource, TEnum> : IEnumerable<TSource>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
		{
			List<TSource> mCopyValues;
			List<TSource>.Enumerator mEnum;
			Iterator<TEnum, TSource> mIterator;
			int mIndex = -1;

			public this(TEnum enumerator)
			{
				mIterator = .(enumerator);
				mCopyValues = new .();
				mEnum = default;
			}

			public Result<TSource> GetNext() mut
			{
				switch (mIndex) {
				case -1:
					var enumerator = mIterator.mEnum;
					while (enumerator.GetNext() case .Ok(let val))
						mCopyValues.Add(val);

					mIterator.Dispose();
					mIterator = default;
					mEnum = mCopyValues.GetEnumerator();
					mIndex = mCopyValues.Count;
					fallthrough;
				default:
					if (--mIndex >= 0)
						return .Ok(mCopyValues[mIndex]);

					return .Err;
				}
			}

			public void Dispose() mut
			{
				mEnum.Dispose();
				DeleteAndNullify!(mCopyValues);
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>, IDisposable
			{
				SelfOuter mSelf;
				public this(SelfOuter self)
				{
					mSelf = self;
				}
				public Result<TSource> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut
				{
					mSelf.Dispose();
				}
			}
		}

		public static ReverseEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			Reverse<TCollection, TSource>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
		{
			return .(items.GetEnumerator());
		}

		struct MapEnumerable<TSource, TEnum, TResult> : Iterator<TEnum, TSource>, IEnumerator<TResult>, IEnumerable<TResult>
			where bool : operator TSource < TSource
			where TSource : operator TSource - TSource
			where TResult : operator TResult + TResult
			where TResult : operator TResult - TResult
			where float : operator float / TSource
			where float : operator TSource * float
			where float : operator float / TResult
			where TResult : operator explicit float
			where TEnum : concrete, IEnumerator<TSource>
		{
			int mState = 0;
			float mScale = 0f, mMapScale;
			TSource mMin = default;
			TResult mMapMin;

			public this(TEnum enumerator, TResult mapMin, TResult mapMax) : base(enumerator)
			{
				mMapMin = mapMin;
				mMapScale = 1f / (mapMax - mapMin);
			}

			public Result<TResult> GetNext() mut
			{
				switch (mState) {
				case 0:
					var copyEnum = mEnum;
					switch (copyEnum.GetNext()) {
					case .Ok(let first):
						var min = first;
						var max = first;

						while (copyEnum.GetNext() case .Ok(let next))
						{
							if (next < min) min = next;
							if (max < next) max = next;
						}

						mMin = min;
						mScale = 1f / (max - min);
						if (mScale == default)
						{
							mState = 2;
							return .Ok(default);
						}

						mState = 1;
					case .Err: return .Err;
					}
					fallthrough;
				case 1:
					if (mEnum.GetNext() case .Ok(let val))
						return (TResult)(((val - mMin) * mScale) / mMapScale) + mMapMin;
				case 2:
					if (mEnum.GetNext() case .Ok(let val))
						return .Ok(default);
				}

				return .Err;
			}

			/*typealias SelfOuter = MapEnumerable<TSource, TEnum, TResult>;
			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator: IEnumerator<TResult>
			{
				SelfOuter mEnum;
				public this(SelfOuter enumerator)
				{
					mEnum = enumerator;
				}
				public Result<TResult> GetNext() mut => mEnum.GetNext();
			}*/

			public Self GetEnumerator() => this;
		}

		public static MapEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TResult>
			Map<TCollection, TSource, TResult>(this TCollection items, TResult min, TResult max)
			where TCollection : concrete, IEnumerable<TSource>
			where bool : operator TSource < TSource
			where TSource : operator TSource - TSource
			where TResult : operator TResult + TResult
			where TResult : operator TResult - TResult
			where float : operator float / TSource
			where float : operator TSource * float
			where float : operator float / TResult
			where TResult : operator explicit float
		{
			return .(items.GetEnumerator(), min, max);
		}

		public static MapEnumerable<TSource, TEnum, TResult>
			Map<TEnum, TSource, TResult>(this TEnum items, TResult min, TResult max)
			where TEnum : concrete, IEnumerator<TSource>
			where bool : operator TSource < TSource
			where TSource : operator TSource - TSource
			where TResult : operator TResult + TResult
			where TResult : operator TResult - TResult
			where float : operator float / TSource
			where float : operator TSource * float
			where float : operator float / TResult
			where TResult : operator explicit float
		{
			return .(items, min, max);
		}
#endregion

#region ToXYZ methods
		public static void ToDictionary<TCollection, TSource, TKeyDlg, TKey, TValueDlg, TValue>(this TCollection items, TKeyDlg keyDlg, TValueDlg valueDlg, Dictionary<TKey, TValue> output)
			where TCollection : concrete, IEnumerable<TSource>
			where TKey : IHashable
			where TKeyDlg : delegate TKey(TSource)
			where TValueDlg : delegate TValue(TSource)
		{
			for (var it in items)
				output.Add(keyDlg(it), valueDlg(it));
		}

		public static void ToDictionary<TEnum, TSource, TKeyDlg, TKey, TValueDlg, TValue>(this TEnum items, TKeyDlg keyDlg, TValueDlg valueDlg, Dictionary<TKey, TValue> output)
			where TEnum : concrete, IEnumerator<TSource>
			where TKey : IHashable
			where TKeyDlg : delegate TKey(TSource)
			where TValueDlg : delegate TValue(TSource)
		{
			for (var it in items)
				output.Add(keyDlg(it), valueDlg(it));
		}

		public static void ToDictionary<TCollection, TSource, TKeyDlg, TKey>(this TCollection items, TKeyDlg keyDlg, Dictionary<TKey, TSource> output)
			where TCollection : concrete, IEnumerable<TSource>
			where TKey : IHashable
			where TKeyDlg : delegate TKey(TSource)
		{
			for (var it in items)
				output.Add(keyDlg(it), it);
		}

		public static void ToDictionary<TEnum, TSource, TKeyDlg, TKey>(this TEnum items, TKeyDlg keyDlg, Dictionary<TKey, TSource> output)
			where TEnum : concrete, IEnumerator<TSource>
			where TKey : IHashable
			where TKeyDlg : delegate TKey(TSource)
		{
			for (var it in items)
				output.Add(keyDlg(it), it);
		}

		public static void ToHashSet<TCollection, TSource>(this TCollection items, HashSet<TSource> output)
			where TCollection : concrete, IEnumerable<TSource>
			where TSource : IHashable
		{
			for (var it in items)
				output.Add(it);
		}

		public static void ToHashSet<TEnum, TSource>(this TEnum items, HashSet<TSource> output)
			where TEnum : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			for (var it in items)
				output.Add(it);
		}

		public static void ToList<TCollection, TSource>(this TCollection items, List<TSource> output)
			where TCollection : concrete, IEnumerable<TSource>
		{
			for (var it in items)
				output.Add(it);
		}

		public static void ToList<TEnum, TSource>(this TEnum items, List<TSource> output)
			where TEnum : concrete, IEnumerator<TSource>
		{
			for (var it in items)
				output.Add(it);
		}
#endregion

#region Aggregates
		public static TSource
			Aggregate<TCollection, TSource, TAccumulate>(this TCollection items, TAccumulate accumulate)
			where TCollection : concrete, IEnumerable<TSource>
			where TAccumulate : delegate TSource(TSource, TSource)
		{
			if (InternalAggregate<decltype(default(TCollection).GetEnumerator()), TSource, TAccumulate>(items.GetEnumerator(), accumulate, let result))
				return result;

			Runtime.FatalError("No elements in the sequence.");
		}

		public static TSource
			Aggregate<TEnum, TSource, TAccumulate>(this TEnum items, TAccumulate accumulate)
			where TEnum : concrete, IEnumerator<TSource>
			where TAccumulate : delegate TSource(TSource, TSource)
		{
			if (InternalAggregate<TEnum, TSource, TAccumulate>(items, accumulate, let result))
				return result;

			Runtime.FatalError("No elements in the sequence.");
		}

		public static TAccumulate
			Aggregate<TCollection, TSource, TAccumulate, TAccDlg>(this TCollection items, TAccumulate seed, TAccDlg accumulate)
			where TCollection : concrete, IEnumerable<TSource>
			where TAccDlg : delegate TAccumulate(TAccumulate, TSource)
		{
			if (InternalAggregate<decltype(default(TCollection).GetEnumerator()), TSource, TAccumulate, TAccDlg>(items.GetEnumerator(), seed, accumulate, let result))
				return result;

			return seed;
		}

		public static TAccumulate
			Aggregate<TEnum, TSource, TAccumulate, TAccDlg>(this TEnum items, TAccumulate seed, TAccDlg accumulate)
			where TEnum : concrete, IEnumerator<TSource>
			where TAccDlg : delegate TAccumulate(TAccumulate, TSource)
		{
			if (InternalAggregate<TEnum, TSource, TAccumulate, TAccDlg>(items, seed, accumulate, let result))
				return result;

			return seed;
		}

		public static TResult
			Aggregate<TCollection, TSource, TAccumulate, TAccDlg, TResult, TResDlg>(this TCollection items, TAccDlg accumulate, TResDlg resultSelector)
			where TCollection : concrete, IEnumerable<TSource>
			where TAccDlg : delegate TSource(TSource, TSource)
			where TResDlg : delegate TResult(TSource)
		{
			if (InternalAggregate<decltype(default(TCollection).GetEnumerator()), TSource, TAccDlg>(items.GetEnumerator(), accumulate, let result))
				return resultSelector(result);

			return resultSelector(default);
		}

		public static TResult
			Aggregate<TEnum, TSource, TAccumulate, TAccDlg, TResult, TResDlg>(this TEnum items, TAccDlg accumulate, TResDlg resultSelector)
			where TEnum : concrete, IEnumerator<TSource>
			where TAccDlg : delegate TSource(TSource, TSource)
			where TResDlg : delegate TResult(TSource)
		{
			if (InternalAggregate<TEnum, TSource, TAccDlg>(items, accumulate, let result))
				return resultSelector(result);

			return resultSelector(default);
		}

		public static TResult
			Aggregate<TCollection, TSource, TAccumulate, TAccDlg, TResult, TResDlg>(this TCollection items, TAccumulate seed, TAccDlg accumulate, TResDlg resultSelector)
			where TCollection : concrete, IEnumerable<TSource>
			where TAccDlg : delegate TAccumulate(TAccumulate, TSource)
			where TResDlg : delegate TResult(TAccumulate)
		{
			if (InternalAggregate<decltype(default(TCollection).GetEnumerator()), TSource, TAccumulate, TAccDlg>(items.GetEnumerator(), seed, accumulate, let result))
				return resultSelector(result);

			return resultSelector(seed);
		}

		public static TResult
			Aggregate<TEnum, TSource, TAccumulate, TAccDlg, TResult, TResDlg>(this TEnum items, TAccumulate seed, TAccDlg accumulate, TResDlg resultSelector)
			where TEnum : concrete, IEnumerator<TSource>
			where TAccDlg : delegate TAccumulate(TAccumulate, TSource)
			where TResDlg : delegate TResult(TAccumulate)
		{
			if (InternalAggregate<TEnum, TSource, TAccumulate, TAccDlg>(items, seed, accumulate, let result))
				return resultSelector(result);

			return resultSelector(seed);
		}

		static bool InternalAggregate<TEnum, TSource, TAccDlg>(TEnum items, TAccDlg accumulate, out TSource aggregate)
			where TEnum : concrete, IEnumerator<TSource>
			where TAccDlg : delegate TSource(TSource, TSource)
		{
			aggregate = default;
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;

				//I guess we need at least 2 elements to do an accumulation without a seed?
				if (!(enumerator.GetNext() case .Ok(out aggregate)))
					return false;

				while (enumerator.GetNext() case .Ok(let val))
					aggregate = accumulate(aggregate, val);
			}
			return true;
		}


		static bool InternalAggregate<TEnum, TSource, TAccumulate, TAccDlg>(TEnum items, TAccumulate seed, TAccDlg func, out TAccumulate result)
			where TEnum : concrete, IEnumerator<TSource>
			where TAccDlg : delegate TAccumulate(TAccumulate, TSource)
		{
			TAccumulate sum = seed;
			var accumulated = false;
			using (var iterator = Iterator.Wrap(items))
			{
				var enumerator = iterator.mEnum;

				if (enumerator.GetNext() case .Ok(let val))
				{
					sum = func(sum, val);
					accumulated = true;
				}

				if (accumulated)
					while (enumerator.GetNext() case .Ok(let val))
						sum = func(sum, val);
			}

			result = sum;
			return accumulated;
		}
		#endregion

#region GroupBy

		struct DynamicArray<TValue> : IDisposable
		{
			TValue[] mPtr = default;
			Span<TValue> mSpan = default;
			int mLength = 0;
			int mSize = 4;
			int mIndex = 0;

			public int Length => mLength;

			public this()
			{
				this.mPtr = new TValue[mSize];
				this.mLength = 0;
			}

			public ref TValue this[int index] => ref mPtr[index];

			public void Dispose() mut
			{
				DeleteAndNullify!(mPtr);
				mPtr = null;
			}
			public void Add(TValue value) mut
			{
				if (mLength + 1 > mSize)
				{
					var newSize = mSize * 3 / 2;
					var dst = new TValue[newSize];
					Array.Copy(mPtr, dst, mLength);
					Swap!(mPtr, dst);
					delete dst;
					mSize = newSize;
				}
				mPtr[mLength++] = value;
			}

			public Span<TValue>.Enumerator GetEnumerator() mut
			{
				mSpan = .(mPtr, 0, mLength);
				return mSpan.GetEnumerator();
			}

			public static implicit operator Span<TValue>(Self it) => .(it.mPtr, 0, it.mLength);
		}

		public extension DynamicArray<TValue> : IDisposable
			where TValue : IDisposable
		{
			public void Dispose() mut
			{
				for (var it in mPtr)
					it.Dispose();

				base.Dispose();
			}
		}

		public struct Grouping<TKey, TValue> : IEnumerable<TValue>, IDisposable, IResettable
		{
			List<TValue> mValues;
			int mIndex = 0;
			public readonly TKey Key;

			public this(TKey key)
			{
				Key = key;
				mValues = new .();
			}

			public void Reset() mut
			{
				mIndex = 0;
			}

			public List<TValue>.Enumerator GetEnumerator()
			{
				return mValues.GetEnumerator();
			}

			public void Add(TValue value) mut
			{
				mValues.Add(value);
			}

			public void Dispose() mut
			{
				DeleteAndNullify!(mValues);
			}
		}

		public class GroupByResult<TKey, TValue> : IEnumerable<Grouping<TKey, TValue>>
			where bool : operator TKey == TKey//where TKey : IHashable
		{
			DynamicArray<Grouping<TKey, TValue>> mResults = .() ~ mResults.Dispose();

			public int Count => mResults.Length;

			public void Add(Grouping<TKey, TValue> group)
			{
				mResults.Add(group);
			}

			public ref Grouping<TKey, TValue> this[int index] => ref mResults[index];

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IRefEnumerator<Grouping<TKey, TValue>*>, IEnumerator<Grouping<TKey, TValue>>, IResettable
			{
				SelfOuter mSelf;
				Span<Grouping<TKey, TValue>> mSpan;
				int mIndex = 0;

				public this(SelfOuter self)
				{
					mSelf = self;
					mSpan = self.mResults;
				}

				public Result<Grouping<TKey, TValue>> GetNext() mut
				{
					if (mIndex < mSpan.Length)
						return .Ok(mSpan[mIndex++]);

					return .Err;
				}

				public Result<Grouping<TKey, TValue>*> GetNextRef() mut
				{
					if (mIndex < mSpan.Length)
						return .Ok(&mSpan[mIndex++]);

					return .Err;
				}

				public void Reset() mut
				{
					mIndex = 0;
				}
			}
		}

		public struct GroupByEnumerable<TSource, TEnum, TKey, TKeyDlg, TValue, TValueDlg> : IEnumerable<Grouping<TKey, TValue>>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
			where bool : operator TKey == TKey//where TKey : IHashable
			where TKeyDlg : delegate TKey(TSource)
			where TValueDlg : delegate TValue(TSource)
		{
			GroupByResult<TKey, TValue> mResults;
			TKeyDlg mKeyDlg;
			TValueDlg mValueDlg;
			Iterator<TEnum, TSource> mIterator;
			int mIndex = -1;
			bool mDeleteResult;

			public this(GroupByResult<TKey, TValue> results, TEnum enumerator, TKeyDlg keyDlg, TValueDlg valueDlg, bool deleteResult)
			{
				mResults = results;
				mIterator = .(enumerator);
				mKeyDlg = keyDlg;
				mValueDlg = valueDlg;
				mDeleteResult = deleteResult;
			}

			Result<Grouping<TKey, TValue>> GetNext() mut
			{
				if (mIndex == -1)
				{
					while (mIterator.mEnum.GetNext() case .Ok(let val))
					{
						let k = mKeyDlg(val);
						let v = mValueDlg(val);
						var added = false;
						for (var it in ref mResults)
						{
							if (it.Key == k)
							{
								it.Add(v);
								added = true;
							}
						}

						if (!added)
						{
							var group = mResults.Add(.. .(k));
							group.Add(v);
						}
					}
					mIndex = 0;
				}

				if (mIndex < mResults.Count)
					return mResults[mIndex++];

				return .Err;
			}

			public void Dispose() mut
			{
				mIterator.Dispose();
				if (mDeleteResult)
					DeleteAndNullify!(mResults);
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<Grouping<TKey, TValue>>, IDisposable
			{
				SelfOuter mSelf;

				public this(SelfOuter self)
				{
					mSelf = self;
				}

				public Result<Grouping<TKey, TValue>> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut => mSelf.Dispose();
			}
		}

		extension GroupByEnumerable<TSource, TEnum, TKey, TKeyDlg, TValue, TValueDlg>
			where TValueDlg : Object
		{
			public void Dispose() mut
			{
				base.Dispose();
				DeleteAndNullify!(mValueDlg);
			}
		}

		public static GroupByEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TKey, TKeyDlg, TSource, delegate TSource(TSource)>
			GroupBy<TCollection, TSource, TKey, TKeyDlg>(this TCollection items, TKeyDlg key)
			where TCollection : concrete, IEnumerable<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where TKey : IHashable
		{
			//guess we could optimize out this scope with some code duplication
			return .(new .(), items.GetEnumerator(), key, new (val) => val, true);
		}

		public static GroupByEnumerable<TSource, TEnum, TKey, TKeyDlg, TSource, delegate TSource(TSource)>
			GroupBy<TEnum, TSource, TKey, TKeyDlg>(this TEnum items, TKeyDlg key)
			where TEnum : concrete, IEnumerator<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where TKey : IHashable
		{
			//guess we could optimize out this scope with some code duplication
			return .(new .(), items, key, new (val) => val, true);
		}

		public static GroupByEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TKey, TKeyDlg, TSource, delegate TSource(TSource)>
			GroupBy<TCollection, TSource, TKey, TKeyDlg>(this TCollection items, TKeyDlg key, GroupByResult<TKey, TSource> results)
			where TCollection : concrete, IEnumerable<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where TKey : IHashable
		{
			//guess we could optimize out this scope with some code duplication
			return .(results, items.GetEnumerator(), key, new (val) => val, false);
		}

		public static GroupByEnumerable<TSource, TEnum, TKey, TKeyDlg, TSource, delegate TSource(TSource)>
			GroupBy<TEnum, TSource, TKey, TKeyDlg>(this TEnum items, TKeyDlg key, GroupByResult<TKey, TSource> results)
			where TEnum : concrete, IEnumerator<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where TKey : IHashable
		{
			//guess we could optimize out this scope with some code duplication
			return .(results, items, key, new (val) => val, false);
		}

		/*public static GroupByEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TKey, TKeyDlg,
		TSource, delegate TSource(TSource)> GroupBy<TCollection, TSource, TKey, TKeyDlg>(this TCollection items, TKeyDlg
		key, GroupByResult<TKey, TSource> results) where TCollection : concrete, IEnumerable<TSource> where TKeyDlg :
		delegate TKey(TSource) where TKey : IHashable
		{
			//guess we could optimize out this scope with some code duplication
			return .(results, items.GetEnumerator(), key, scope (val) => val);
		}*/
#endregion
		struct UnionEnumerable<TSource, TEnum, TEnum2> : IEnumerable<TSource>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			HashSet<TSource> mDistinctValues;
			Iterator<TEnum, TSource> mSource;
			Iterator<TEnum2, TSource> mOther;
			int mState = 0;

			public this(TEnum sourceEnumerator, TEnum2 otherEnumerator)
			{
				mSource = sourceEnumerator;
				mOther = otherEnumerator;

				mDistinctValues = new .();
			}

			Result<TSource> GetNext() mut
			{
				switch (mState) {
				case 0:
					var e = mSource.mEnum;
					while (e.GetNext() case .Ok(let val))
						if (mDistinctValues.Add(val))
							return .Ok(val);

					mState++;
					fallthrough;
				case 1:
					var e = mOther.mEnum;
					while (e.GetNext() case .Ok(let val))
						if (mDistinctValues.Add(val))
							return .Ok(val);

					mState++;
				}

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>, IDisposable
			{
				SelfOuter mSelf;

				public this(SelfOuter self)
				{
					mSelf = self;
				}

				public Result<TSource> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut => mSelf.Dispose();
			}

			public void Dispose() mut
			{
				mSource.Dispose();
				mOther.Dispose();
				DeleteAndNullify!(mDistinctValues);
			}
		}

		public static UnionEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), decltype(default(TCollection2).GetEnumerator())>
			Union<TCollection, TCollection2, TSource>(this TCollection items, TCollection2 other)
			where TCollection : concrete, IEnumerable<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
			where TSource : IHashable
		{
			return .(items.GetEnumerator(), other.GetEnumerator());
		}

		public static UnionEnumerable<TSource, TEnum, decltype(default(TCollection2).GetEnumerator())>
			Union<TEnum, TCollection2, TSource>(this TEnum items, TCollection2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
			where TSource : IHashable
		{
			return .(items, other.GetEnumerator());
		}

		public static UnionEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TEnum>
			Union<TCollection, TEnum, TSource>(this TCollection items, TEnum other)
			where TCollection : concrete, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			return .(items.GetEnumerator(), other);
		}

		public static UnionEnumerable<TSource, TEnum, TEnum2>
			Union<TEnum, TEnum2, TSource>(this TEnum items, TEnum2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			return .(items, other);
		}

		struct ExceptEnumerable<TSource, TEnum, TEnum2> : IEnumerable<TSource>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			HashSet<TSource> mDistinctValues;
			Iterator<TEnum, TSource> mSource;
			Iterator<TEnum2, TSource> mOther;
			int mState = 0;

			public this(TEnum sourceEnumerator, TEnum2 otherEnumerator)
			{
				mSource = sourceEnumerator;
				mOther = otherEnumerator;

				mDistinctValues = new .();
			}

			Result<TSource> GetNext() mut
			{
				switch (mState) {
				case 0:
					var e = mOther.mEnum;
					while (e.GetNext() case .Ok(let val))
						mDistinctValues.Add(val);

					mState++;
					fallthrough;
				case 1:
					var e = mSource.mEnum;
					while (e.GetNext() case .Ok(let val))
						if (mDistinctValues.Add(val))
							return .Ok(val);

					mState++;
				}

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>, IDisposable
			{
				SelfOuter mSelf;

				public this(SelfOuter self)
				{
					mSelf = self;
				}

				public Result<TSource> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut => mSelf.Dispose();
			}

			public void Dispose() mut
			{
				mSource.Dispose();
				mOther.Dispose();
				DeleteAndNullify!(mDistinctValues);
			}
		}

		public static ExceptEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), decltype(default(TCollection2).GetEnumerator())>
			Except<TCollection, TCollection2, TSource>(this TCollection items, TCollection2 other)
			where TCollection : concrete, IEnumerable<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
			where TSource : IHashable
		{
			return .(items.GetEnumerator(), other.GetEnumerator());
		}

		public static ExceptEnumerable<TSource, TEnum, decltype(default(TCollection2).GetEnumerator())>
			Except<TEnum, TCollection2, TSource>(this TEnum items, TCollection2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
			where TSource : IHashable
		{
			return .(items, other.GetEnumerator());
		}

		public static ExceptEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TEnum>
			Except<TCollection, TEnum, TSource>(this TCollection items, TEnum other)
			where TCollection : concrete, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			return .(items.GetEnumerator(), other);
		}

		public static ExceptEnumerable<TSource, TEnum, TEnum2>
			Except<TEnum, TEnum2, TSource>(this TEnum items, TEnum2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			return .(items, other);
		}

		struct IntersectEnumerable<TSource, TEnum, TEnum2> : IEnumerable<TSource>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			HashSet<TSource> mDistinctValues;
			Iterator<TEnum, TSource> mSource;
			Iterator<TEnum2, TSource> mIntersect;
			int mState = 0;

			public this(TEnum sourceEnumerator, TEnum2 intersectEnumerator)
			{
				mSource = .(sourceEnumerator);
				mIntersect = .(intersectEnumerator);
				mDistinctValues = new .();
			}

			Result<TSource> GetNext() mut
			{
				switch (mState)
				{
				case 0:
					var e = mSource.mEnum;
					while (e.GetNext() case .Ok(let val))
						mDistinctValues.Add(val);

					mState++;
					fallthrough;
				case 1:
					var e = mIntersect.mEnum;
					while (e.GetNext() case .Ok(let val))
						if (mDistinctValues.Remove(val))
							return .Ok(val);

					mState++;
				}

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>, IDisposable
			{
				SelfOuter mSelf;

				public this(SelfOuter self)
				{
					mSelf = self;
				}

				public Result<TSource> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut => mSelf.Dispose();
			}

			public void Dispose() mut
			{
				mSource.Dispose();
				mIntersect.Dispose();
				DeleteAndNullify!(mDistinctValues);
			}
		}

		public static IntersectEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), decltype(default(TCollection2).GetEnumerator())>
			Intersect<TCollection, TCollection2, TSource>(this TCollection items, TCollection2 other)
			where TCollection : concrete, IEnumerable<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
			where TSource : IHashable
		{
			return .(items.GetEnumerator(), other.GetEnumerator());
		}

		public static IntersectEnumerable<TSource, TEnum, decltype(default(TCollection2).GetEnumerator())>
			Intersect<TEnum, TCollection2, TSource>(this TEnum items, TCollection2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
			where TSource : IHashable
		{
			return .(items, other.GetEnumerator());
		}

		public static IntersectEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TEnum>
			Intersect<TCollection, TEnum, TSource>(this TCollection items, TEnum other)
			where TCollection : concrete, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			return .(items.GetEnumerator(), other);
		}

		public static IntersectEnumerable<TSource, TEnum, TEnum2>
			Intersect<TEnum, TEnum2, TSource>(this TEnum items, TEnum2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
			where TSource : IHashable
		{
			return .(items, other);
		}

		struct ZipEnumerable<TSource, TEnum, TEnum2, TResult, TSelect> : IEnumerable<TResult>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
			where TSelect : delegate TResult(TSource first, TSource second)
		{
			Iterator<TEnum, TSource> mSource;
			Iterator<TEnum2, TSource> mOther;
			TSelect mSelect;

			public this(TEnum sourceEnumerator, TEnum2 otherEnumerator, TSelect select)
			{
				mSource = sourceEnumerator;
				mOther = otherEnumerator;
				mSelect = select;
			}

			Result<TResult> GetNext() mut
			{
				if (mSource.mEnum.GetNext() case .Ok(let first))
					if (mOther.mEnum.GetNext() case .Ok(let second))
						return mSelect(first, second);

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TResult>, IDisposable
			{
				SelfOuter mSelf;

				public this(SelfOuter self)
				{
					mSelf = self;
				}

				public Result<TResult> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut => mSelf.Dispose();
			}


			public void Dispose() mut
			{
				mSource.Dispose();
				mOther.Dispose();
			}
		}

		public static ZipEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), decltype(default(TCollection2).GetEnumerator()), TResult, TSelect>
			Zip<TCollection, TCollection2, TSource, TResult, TSelect>(this TCollection items, TCollection2 other, TSelect select)
			where TCollection : concrete, IEnumerable<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
			where TSelect : delegate TResult(TSource first, TSource second)
		{
			return .(items.GetEnumerator(), other.GetEnumerator(), select);
		}

		public static ZipEnumerable<TSource, TEnum, decltype(default(TCollection2).GetEnumerator()), TResult, TSelect>
			Zip<TEnum, TCollection2, TSource, TResult, TSelect>(this TEnum items, TCollection2 other, TSelect select)
			where TEnum : concrete, IEnumerator<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
			where TSelect : delegate TResult(TSource first, TSource second)
		{
			return .(items, other.GetEnumerator(), select);
		}

		public static ZipEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TEnum, TResult, TSelect>
			Zip<TCollection, TEnum, TSource, TResult, TSelect>(this TCollection items, TEnum other, TSelect select)
			where TCollection : concrete, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
			where TSelect : delegate TResult(TSource first, TSource second)
		{
			return .(items.GetEnumerator(), other, select);
		}

		public static ZipEnumerable<TSource, TEnum, TEnum2, TResult, TSelect>
			Zip<TEnum, TEnum2, TSource, TResult, TSelect>(this TEnum items, TEnum2 other, TSelect select)
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
			where TSelect : delegate TResult(TSource first, TSource second)
		{
			return .(items, other, select);
		}

		struct ConcatEnumerable<TSource, TEnum, TEnum2> : IEnumerable<TSource>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
		{
			Iterator<TEnum, TSource> mFirst;
			Iterator<TEnum2, TSource> mSecond;
			int mState = 0;

			public this(TEnum firstEnumerator, TEnum2 secondEnumerator)
			{
				mFirst = firstEnumerator;
				mSecond = secondEnumerator;
			}

			Result<TSource> GetNext() mut
			{
				switch (mState) {
				case 0:
					if (mFirst.mEnum.GetNext() case .Ok(let val))
						return .Ok(val);

					mState++;
					fallthrough;
				case 1:
					if (mSecond.mEnum.GetNext() case .Ok(let val))
						return .Ok(val);

					mState++;
				}

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>, IDisposable
			{
				SelfOuter mSelf;

				public this(SelfOuter self)
				{
					mSelf = self;
				}

				public Result<TSource> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut => mSelf.Dispose();
			}

			public void Dispose() mut
			{
				mFirst.Dispose();
				mSecond.Dispose();
			}
		}



		public static ConcatEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), decltype(default(TCollection2).GetEnumerator())>
			Concat<TCollection, TCollection2, TSource>(this TCollection items, TCollection2 other)
			where TCollection : concrete, IEnumerable<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
		{
			return .(items.GetEnumerator(), other.GetEnumerator());
		}

		public static ConcatEnumerable<TSource, TEnum, decltype(default(TCollection2).GetEnumerator())>
			Concat<TEnum, TCollection2, TSource>(this TEnum items, TCollection2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
		{
			return .(items, other.GetEnumerator());
		}

		public static ConcatEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TEnum>
			Concat<TCollection, TEnum, TSource>(this TCollection items, TEnum other)
			where TCollection : concrete, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
		{
			return .(items.GetEnumerator(), other);
		}

		public static ConcatEnumerable<TSource, TEnum, TEnum2>
			Concat<TEnum, TEnum2, TSource>(this TEnum items, TEnum2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
		{
			return .(items, other);
		}

		public static ConcatEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), decltype(default(TCollection2).GetEnumerator())>
			Append<TCollection, TCollection2, TSource>(this TCollection items, TCollection2 other)
			where TCollection : concrete, IEnumerable<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
		{
			return .(items.GetEnumerator(), other.GetEnumerator());
		}

		public static ConcatEnumerable<TSource, TEnum, decltype(default(TCollection2).GetEnumerator())>
			Append<TEnum, TCollection2, TSource>(this TEnum items, TCollection2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
		{
			return .(items, other.GetEnumerator());
		}

		public static ConcatEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TEnum>
			Append<TCollection, TEnum, TSource>(this TCollection items, TEnum other)
			where TCollection : concrete, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
		{
			return .(items.GetEnumerator(), other);
		}

		public static ConcatEnumerable<TSource, TEnum, TEnum2>
			Append<TEnum, TEnum2, TSource>(this TEnum items, TEnum2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
		{
			return .(items, other);
		}

		public static ConcatEnumerable<TSource, decltype(default(TCollection2).GetEnumerator()), decltype(default(TCollection).GetEnumerator())>
			Prepend<TCollection, TCollection2, TSource>(this TCollection items, TCollection2 other)
			where TCollection : concrete, IEnumerable<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
		{
			return .(other.GetEnumerator(), items.GetEnumerator());
		}

		public static ConcatEnumerable<TSource, decltype(default(TCollection2).GetEnumerator()), TEnum>
			Prepend<TEnum, TCollection2, TSource>(this TEnum items, TCollection2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TCollection2 : concrete, IEnumerable<TSource>
		{
			return .(other.GetEnumerator(), items);
		}

		public static ConcatEnumerable<TSource, TEnum, decltype(default(TCollection).GetEnumerator())>
			Prepend<TCollection, TEnum, TSource>(this TCollection items, TEnum other)
			where TCollection : concrete, IEnumerable<TSource>
			where TEnum : concrete, IEnumerator<TSource>
		{
			return .(other, items.GetEnumerator());
		}

		public static ConcatEnumerable<TSource, TEnum2, TEnum>
			Prepend<TEnum, TEnum2, TSource>(this TEnum items, TEnum2 other)
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TSource>
		{
			return .(other, items);
		}

		

		static class OrderByComparison<T>
			where int : operator T <=> T
		{
			typealias TCompare = delegate int(T lhs, T rhs);
			public readonly static TCompare Comparison = (new (lhs, rhs) => lhs <=> rhs) ~ delete _;
		}

		struct SortedEnumerable<TSource, TEnum, TKey, TKeyDlg, TCompare> : IEnumerator<(TKey key, TSource value)>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where TCompare : delegate int(TKey lhs, TKey rhs)
		{
			List<(TKey key, TSource value)> mOrderedList;
			Iterator<TEnum, TSource> mIterator;
			TKeyDlg mKey;
			TCompare mCompare;
			int mIndex;
			int mCount = 0;
			bool mDescending;

			public this(TEnum firstEnumerator, TKeyDlg key, TCompare compare, bool descending)
			{
				mOrderedList = new .();
				mKey = key;
				mIterator = firstEnumerator;
				mCompare = compare;
				mIndex = -1;
				mDescending = descending;
			}

			public Result<(TKey key, TSource value)> GetNext() mut
			{
				if (mIndex == -1)
				{
					while (mIterator.mEnum.GetNext() case .Ok(let val))
						mOrderedList.Add((mKey(val), val));

					mOrderedList.Sort(scope (l, r) => mCompare(l.key, r.key));
					mCount = mOrderedList.Count;//keeping vars local
					mIndex = mDescending ? mCount : 0;
				}

				if (mDescending)
				{
					if (mIndex > 0)
						return .Ok(mOrderedList[--mIndex]);
				}
				else if (mIndex < mCount)
					return .Ok(mOrderedList[mIndex++]);

				return .Err;
			}


			public Enumerator GetEnumerator() => .(this);
			public struct Enumerator : IEnumerator<(TKey key, TSource value)>, IDisposable
			{
				SelfOuter mSelf;

				public this(SelfOuter self)
				{
					mSelf = self;
				}

				public Result<(TKey key, TSource value)> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut => mSelf.Dispose();
			}


			public void Dispose() mut
			{
				mIterator.Dispose();
				DeleteAndNullify!(mOrderedList);
			}
		}

		//if the data is a large struct, this could get pretty slow with all the copies
		//we may need to investigate the ability to use ptrs instead if the type is a struct?
		struct OrderByEnumerable<TSource, TEnum> : IEnumerable<TSource>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
		{
			typealias TCompare = delegate int(TSource lhs, TSource rhs);

			struct Comparer
			{
				public TCompare comparer;
				public bool descending;
			}

			DynamicArray<TSource> mValues;
			int[] mMap;
			//List<TSource> mOrderedList;
			List<Comparer> mCompares = default;
			Iterator<TEnum, TSource> mIterator;
			int mIndex;

			public this(TEnum enumerator, TCompare compare, bool descending)
			{
				mCompares = new .();
				var add = mCompares.GrowUnitialized(1);
				add.comparer = compare;
				add.descending = descending;

				mValues = ?;
				mMap = null;
				mIterator = enumerator;
				mIndex = -1;
			}

			void QuickSort(int left, int right) {
				var left;
				var right;
#unwarn
			    repeat {
			        int i = left;
			        int j = right;
			        int x = mMap[i + ((j - i) >> 1)];
			        repeat {
			            while (i < mMap.Count && CompareKeys(x, mMap[i]) > 0) i++;
			            while (j >= 0 && CompareKeys(x, mMap[j]) < 0) j--;
			            if (i > j) break;
			            if (i < j) {
			                int temp = mMap[i];
			                mMap[i] = mMap[j];
			                mMap[j] = temp;
			            }
			            i++;
			            j--;
			        } while (i <= j);
			        if (j - left <= right - i) {
			            if (left < j) QuickSort(left, j);
			            left = i;
			        }
			        else {
			            if (i < right) QuickSort(i, right);
			            right = j;
			        }
			    } while (left < right);
			}

			[Inline]
			int CompareKeys(int index1, int index2)
			{
				for(var cmp in ref mCompares)
				{
					int c = cmp.comparer(mValues[index1], mValues[index2]);
					if (c != 0)
						return cmp.descending ? -c : c;
				}

				return index1 - index2;
			}

			Result<TSource> GetNext() mut
			{
				if (mIndex == -1)
				{
					mIndex = 0;
					mValues = .();
					while (mIterator.mEnum.GetNext() case .Ok(let val))
						mValues.Add(val);

					let count = mValues.Length;
					mMap = new int[count];
					for (int i = 0; i < count; i++) mMap[i] = i;
					QuickSort( 0, count - 1);
				}

				if (mIndex < mValues.Length)
					return .Ok(mValues[mMap[mIndex++]]);

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TSource>, IDisposable
			{
				SelfOuter mSelf;

				public this(SelfOuter self)
				{
					mSelf = self;
				}

				public Result<TSource> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut => mSelf.Dispose();
			}

			public void Dispose() mut
			{
				mIterator.Dispose();

				for(var it in mCompares)
					delete it.comparer;
				DeleteAndNullify!(mCompares);
				DeleteAndNullify!(mMap);
				mValues.Dispose();
			}

			public Self ThenBy<TKey2, TKeyDlg2, TCompare2>(TKeyDlg2 keySelect, TCompare2 comparison) mut
				where TKeyDlg2 : delegate TKey2(TSource)
				where TCompare2 : delegate int(TKey2 lhs, TKey2 rhs)
			{
				var add = mCompares.GrowUnitialized(1);
				add.comparer = new (l, r) => comparison(keySelect(l), keySelect(r));
				add.descending = false;
				return this;
			}

			public Self ThenBy<TKey2, TKeyDlg2>(TKeyDlg2 keySelect) 
				where TKeyDlg2 : delegate TKey2(TSource)
				where int : operator TKey2 <=> TKey2
			{
				let comparison = OrderByComparison<TKey2>.Comparison;
				var add = mCompares.GrowUnitialized(1);
				add.comparer = new (l, r) => comparison(keySelect(l), keySelect(r));
				add.descending = false;
				return this;
			}

			
			public Self ThenByDescending<TKey2, TKeyDlg2, TCompare2>(TKeyDlg2 keySelect, TCompare2 comparison) mut
				where TKeyDlg2 : delegate TKey2(TSource)
				where TCompare2 : delegate int(TKey2 lhs, TKey2 rhs)
			{
				var add = mCompares.GrowUnitialized(1);
				add.comparer = new (l, r) => comparison(keySelect(l), keySelect(r));
				add.descending = true;
				return this;
			}

			public Self ThenByDescending<TKey2, TKeyDlg2>(TKeyDlg2 keySelect) 
				where TKeyDlg2 : delegate TKey2(TSource)
				where int : operator TKey2 <=> TKey2
			{
				let comparison = OrderByComparison<TKey2>.Comparison;
				var add = mCompares.GrowUnitialized(1);
				add.comparer = new (l, r) => comparison(keySelect(l), keySelect(r));
				add.descending = true;
				return this;
			}
		}

		public static OrderByEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			OrderBy<TCollection, TSource, TKey, TKeyDlg>(this TCollection items, TKeyDlg keySelect)
			where TCollection : concrete, IEnumerable<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where int : operator TKey <=> TKey
		{
			let comparison = OrderByComparison<TKey>.Comparison;
			return .(items.GetEnumerator(), new (l, r) => comparison(keySelect(l), keySelect(r)), false);
		}

		public static OrderByEnumerable<TSource, TEnum>
			OrderBy<TEnum, TSource, TKey, TKeyDlg>(this TEnum items, TKeyDlg keySelect)
			where TEnum : concrete, IEnumerator<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where int : operator TKey <=> TKey
		{
			let comparison = OrderByComparison<TKey>.Comparison;
			return .(items, new (l, r) => comparison(keySelect(l), keySelect(r)), false);
		}

		public static OrderByEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			OrderBy<TCollection, TSource, TKey, TKeyDlg, TCompare>(this TCollection items, TKeyDlg keySelect, TCompare comparison)
			where TCollection : concrete, IEnumerable<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where TCompare : delegate int(TKey lhs, TKey rhs)
		{
			return .(items.GetEnumerator(), new (l, r) => comparison(keySelect(l), keySelect(r)), false);
		}

		public static OrderByEnumerable<TSource, TEnum>
			OrderBy<TEnum, TSource, TKey, TKeyDlg, TCompare>(this TEnum items, TKeyDlg keySelect, TCompare comparison)
			where TEnum : concrete, IEnumerator<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where TCompare : delegate int(TKey lhs, TKey rhs)
		{
			return .(items, new (l, r) => comparison(keySelect(l), keySelect(r)), false);
		}

		public static OrderByEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			OrderByDescending<TCollection, TSource, TKey, TKeyDlg>(this TCollection items, TKeyDlg keySelect)
			where TCollection : concrete, IEnumerable<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where int : operator TKey <=> TKey
		{
			let comparison = OrderByComparison<TKey>.Comparison;
			return .(items.GetEnumerator(),  new (l, r) => comparison(keySelect(l), keySelect(r)), true);
		}

		public static OrderByEnumerable<TSource, TEnum>
			OrderByDescending<TEnum, TSource, TKey, TKeyDlg>(this TEnum items, TKeyDlg keySelect)
			where TEnum : concrete, IEnumerator<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where int : operator TKey <=> TKey
		{
			let comparison = OrderByComparison<TKey>.Comparison;
			return .(items,  new (l, r) => comparison(keySelect(l), keySelect(r)), true);
		}

		public static OrderByEnumerable<TSource, decltype(default(TCollection).GetEnumerator())>
			OrderByDescending<TCollection, TSource, TKey, TKeyDlg, TCompare>(this TCollection items, TKeyDlg keySelect, TCompare comparison)
			where TCollection : concrete, IEnumerable<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where TCompare : delegate int(TKey lhs, TKey rhs)
		{
			return .(items.GetEnumerator(),new (l, r) => comparison(keySelect(l), keySelect(r)), true);
		}

		public static OrderByEnumerable<TSource, TEnum>
			OrderByDescending<TEnum, TSource, TKey, TKeyDlg, TCompare>(this TEnum items, TKeyDlg keySelect, TCompare comparison)
			where TEnum : concrete, IEnumerator<TSource>
			where TKeyDlg : delegate TKey(TSource)
			where TCompare : delegate int(TKey lhs, TKey rhs)
		{
			return .(items, new (l, r) => comparison(keySelect(l), keySelect(r)), true);
		}

		struct SelectManyEnumerable<TSource, TEnum, TSelect, TResult, TEnum2> : IEnumerable<TResult>, IDisposable
			where TEnum : concrete, IEnumerator<TSource>
			where TEnum2 : concrete, IEnumerator<TResult>
			where TSelect : delegate TEnum2(TSource)
		{
			public readonly static String SelectManyEnum = new String() ~ delete _;

			Iterator<TEnum, TSource> mItems;
			Iterator<TEnum2, TResult> mCurrent = default;
			TSelect mSelect;
			int mState = -1;

			public this(TEnum firstEnumerator, TSelect select)
			{
				mItems = firstEnumerator;
				mSelect = select;
			}

			Result<TResult> GetNext(out bool moveNext) mut
			{
				if (mState < 1)
				{
					if (mState == 0)
						mCurrent.Dispose();

					if (mItems.mEnum.GetNext() case .Ok(var val))
					{
						mCurrent = mSelect(val);
						mState = 1;
					}
					else
					{
						//no more elements
						moveNext = false;
						return .Err;
					}
				}

				if (mCurrent.mEnum.GetNext() case .Ok(let val))
				{
					moveNext = false;
					return .Ok(val);
				}

				//done with current enumerator
				mState = 0;
				moveNext = true;
				return .Err;
			}

			Result<TResult> GetNext() mut
			{
				var moveNext = true;
				while (moveNext)
				{
					let result = GetNext(out moveNext);
					if (!moveNext)
						return result;
				}

				return .Err;
			}

			public Enumerator GetEnumerator() => .(this);

			public struct Enumerator : IEnumerator<TResult>, IDisposable
			{
				SelfOuter mSelf;

				public this(SelfOuter self)
				{
					mSelf = self;
				}

				public Result<TResult> GetNext() mut => mSelf.GetNext();

				public void Dispose() mut => mSelf.Dispose();
			}

			public void Dispose() mut
			{
				mItems.Dispose();
			}
		}
		extension SelectManyEnumerable<TSource, TEnum, TSelect, TResult, TEnum2>
			where TSelect : Object
		{
			public void Dispose() mut
			{
				base.Dispose();
				DeleteAndNullify!(mSelect);
			}
		}

		public static SelectManyEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), delegate decltype(default(TCollection2).GetEnumerator())(TSource), TResult, decltype(default(TCollection2).GetEnumerator())>
			SelectMany<TCollection, TSource, TCollection2, TSelect, TResult>(this TCollection items, TSelect select)
			where TCollection : concrete, IEnumerable<TSource>
			where TCollection2 : concrete, IEnumerable<TResult>
			where TSelect : delegate TCollection2(TSource)
		{
			return .(items.GetEnumerator(), new (x) => select(x).GetEnumerator());
		}

		public static SelectManyEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TSelect, TResult, TEnum2>
			SelectMany<TCollection, TSource, TEnum2, TSelect, TResult>(this TCollection items, TSelect select)
			where TCollection : concrete, IEnumerable<TSource>
			where TEnum2 : concrete, IEnumerator<TResult>
			where TSelect : delegate TEnum2(TSource)
		{
			return .(items.GetEnumerator(), select);
		}

		/*struct OfTypeEnumerable<TSource, TEnum, TOf> : Iterator<TEnum, TSource>, IEnumerator<TOf>, IEnumerable<TOf>
			where TEnum : concrete, IEnumerator<TSource>
			where TSource : class
		{
			public this(TEnum enumerator) : base(enumerator)
			{
			}

			public Result<TOf> GetNext() mut
			{
				while (mEnum.GetNext() case .Ok(let val))
				{
					if (val is TOf)
						return .Ok(*(TOf*)Internal.UnsafeCastToPtr(val));
				}
				return .Err;
			}

			public Self GetEnumerator()
			{
				return this;
			}
		}

		public static OfTypeEnumerable<TSource, decltype(default(TCollection).GetEnumerator()), TOf>
			OfType<TCollection, TSource, TOf>(this TCollection items)
			where TCollection : concrete, IEnumerable<TSource>
			where TSource : class
		{
			return .(items.GetEnumerator());
		}*/


	}
}
