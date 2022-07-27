using System.Collections;
using System.Diagnostics;

namespace System
{
	// Event owns the delegates it contains
	// Event supports removals and additions while enumerating without invalidating the enumerator.
	// Event can also safely be Disposed and even have its memory reclaimed during enumeration
	// Event is not thread-safe: access must be protected during modification, and during enumeration
	struct Event<T> where T : Delegate
	{
		// mData can be null with no listeners, T for one listener, or List<T> for multiple
		//  If we are enumerating then mData points to the enumerator.
		int mData;

#if BF_64_BIT
		const int sIsEnumerating = (.)0x8000'0000'0000'0000;
		const int sHadEnumRemoves = 0x4000'0000'0000'0000;
		const int sFlagsMask = (.)0xC000'0000'0000'0000;
		const int sDataMask = ~sFlagsMask;
#else
		const int sIsEnumerating = 1;
		const int sHadEnumRemoves = 2;
		const int sFlagsMask = 3;
		const int sDataMask = ~sFlagsMask;
#endif

		public bool HasListeners
		{
			get
			{
				return Target != null;
			}
		}

		public bool IsEmpty
		{
			get
			{
				Object data = Internal.UnsafeCastToObject((void*)(mData & sDataMask));

				if (data == null)
					return true;

				if (var list = data as List<T>)
				{
					return list.Count == 0;
				}
				return false;
			}
		}

		public int Count
		{
			get
			{
				Object data = Internal.UnsafeCastToObject((void*)(mData & sDataMask));

				if (data == null)
					return 0;

				if (var list = data as List<T>)
				{
					return list.Count;
				}
				return 1;
			}
		}

		public Object Target
		{
			get
			{
				if (mData & sIsEnumerating != 0)
				{
					Enumerator* enumerator = (Enumerator*)(void*)(mData & sDataMask);
					return enumerator.[Friend]mTarget;
				}
				return Internal.UnsafeCastToObject((void*)mData);
			}

			set mut
			{
				if (mData & sIsEnumerating != 0)
				{
					Enumerator* enumerator = (Enumerator*)(void*)(mData & sDataMask);
					enumerator.[Friend]mTarget = value;
				}
				else
				{
					mData = (int)Internal.UnsafeCastToPtr(value);
				}
			}
		}

		protected override void GCMarkMembers()
		{
			if (mData & sDataMask != 0)
                GC.Mark(Internal.UnsafeCastToObject((void*)mData));
		}

		public void Add(T ownDelegate) mut
		{
			Object data = Target;
			if (data == null)
			{
				Target = ownDelegate;
				return;
			}

			if (var list = data as List<T>)
			{
				list.Add(ownDelegate);
			}
			else
			{
				var list = new List<T>();
				list.Add((T)data);
				list.Add(ownDelegate);
				Target = list;
			}
		}

		public void AddFront(T ownDelegate) mut
		{
			Object data = Target;
			if (data == null)
			{
				Target = ownDelegate;
				return;
			}

			if (var list = data as List<T>)
			{
				list.Insert(0, ownDelegate);
			}
			else
			{
				var list = new List<T>();
				list.Add(ownDelegate);
				list.Add((T)data);
				Target = list;
			}
		}

		public Result<void> Remove(T compareDelegate, bool deleteDelegate = false) mut
		{
			Object data = Target;

			if (var list = data as List<T>)
			{
				int32 idx = -1;
				for (int32 i = 0; i < list.Count; i++)
					if (Delegate.Equals(list[i], compareDelegate))
					{
						idx = i;
						break;
					}
				if (idx == -1)
					return .Err;

				if (deleteDelegate)
					delete list[idx];

				if (mData & sIsEnumerating != 0)
				{
					// In order to avoid invalidating the enumerator's index during enumeration, we
					//  just null this entry out and then we physically remove the entries upon
					//  Dispose of the enumerator
					list[idx] = null;
					mData |= sHadEnumRemoves;
				}
				else
					list.RemoveAt(idx);

				if (list.Count == 0)
				{
					delete list;
					Target = null;
				}
			}
			else
			{
				T dlgMember = (T)data;				
				if (!Delegate.Equals(compareDelegate, dlgMember))
					return .Err;
				if (deleteDelegate)
					delete dlgMember;
				Target = null;
			}

			return .Ok;
		}

		public rettype(T) Invoke(params T p) mut
		{
			var result = default(rettype(T));
			for (var dlg in this)
			{
				result = dlg(params p);
			}
			return result;
		}

		public void Dispose() mut
		{
			if (mData == 0)
				return;
			var data = Target;
			if (var list = data as List<T>)
			{
				for (var dlg in list)
					delete dlg;
			}
			delete data;
			Target = null;
		}
		
		public Enumerator GetEnumerator() mut
		{
			return Enumerator(ref this);
		}

		public struct Enumerator : IEnumerator<T>
		{
			Event<T>* mEvent;
			Object mTarget;
			int32 mIdx;
			Enumerator* mRootEnumerator;
			T mCurrent;

			public this(ref Event<T> event)
			{
				mEvent = &event;
				mIdx = -2;
				mRootEnumerator = null;
				mCurrent = null;
				mTarget = null;
			}

			public void Dispose() mut
			{
				if (mRootEnumerator == &this)
				{
					if ((mEvent.mData & Event<T>.sHadEnumRemoves != 0) && (mTarget != null))
					{
						var list = (List<T>)mTarget;

						// Remove nulls
						for (int32 i = 0; i < list.Count; i++)
						{
							if (list[i] == null)
							{
								list.RemoveAt(i);
								i--;
							}
						}

						if (list.Count == 0)
						{
							delete list;
							mEvent.mData = 0;
							return;
						}
					}

					mEvent.mData = (int)Internal.UnsafeCastToPtr(mTarget);
				}
			}

			public void Reset() mut
			{
				mIdx = -1;
			}

			public bool MoveNext() mut
			{
				if (mIdx == -2)
				{
					// Attach
					if (mEvent.mData & sIsEnumerating == 0)
					{
						mTarget = mEvent.Target;
						mEvent.mData = (int)(void*)(&this) | sIsEnumerating;
						mRootEnumerator = &this;
					}
					else
					{
						mRootEnumerator = (Enumerator*)(void*)(mEvent.mData & Event<T>.sDataMask);
					}
					mIdx = -1;
				}

				var data = mRootEnumerator.mTarget;
				if (data == null)
					return false;

				if (var list = data as List<T>)
				{
					repeat
					{
						mIdx++;
						if (mIdx >= list.Count)
							return false;
						mCurrent = list[mIdx];
					}
					while (mCurrent == null);
				}
				else
				{
					mIdx++;
					if (mIdx > 0)
						return false;
					mCurrent = (T)data;
				}

				return true;
			}

			public T Current
			{
				get
				{
					return mCurrent;
				}
			}

			public Result<T> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return mCurrent;
			}
		}
	}
}
