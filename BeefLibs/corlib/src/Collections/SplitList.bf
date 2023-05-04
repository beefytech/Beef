using System;
using System.Reflection;
using System.Diagnostics;

namespace System.Collections
{
	class SplitList<T> : IEnumerable<Entry>, IList, ICollection<T> where T : struct
	{
		private const int_cosize cDefaultCapacity = 4;

		private void* mItems;
		private int_cosize mSize;
		private int_cosize mAllocSize;
#if VERSION_LIST
		private int32 mVersion;
		const String cVersionError = "List changed during enumeration";
#endif

		[Inline] public int AllocSize => mAllocSize;

		static void GetFields(List<FieldInfo> fieldList)
		{
			if (typeof(T).IsUnion)
				return;
			for (var field in typeof(T).GetFields())
			{
				if (field.IsStatic)
					continue;
				fieldList.Add(field);
			}
		}

		[Comptime, OnCompile(.TypeInit)]
		static void Init()
		{
			var fields = GetFields(.. scope .());
			String code = scope $"""
				int_cosize[{Math.Max(0, fields.Count - 1)}] mOffsets;
				""";
			Compiler.EmitTypeBody(typeof(Self), code);

			if (typeof(T).IsUnion)
				Runtime.FatalError("Cannot use SplitList on a union");
		}

		public struct Data
		{
			SelfOuter mList;

			public this(SelfOuter list)
			{
				mList = list;
			}

			[Comptime, OnCompile(.TypeInit)]
			static void Init()
			{
				var fields = GetFields(.. scope .());
				String code = scope .();
				for (var field in fields)
				{
					code.AppendF($"[Inline] public Span<{field.FieldType}> {field.Name} => .((.)((uint8*)mList.mItems");
					if (@field.Index > 0)
						code.AppendF($" + mList.mOffsets[{@field.Index - 1}]");
					code.AppendF($"), mList.mSize);\n");
				}
				Compiler.EmitTypeBody(typeof(Self), code);
			}
		}

		[Comptime]
		static void Emit_Get(String prefix, String idx, String item)
		{
			var fields = GetFields(.. scope .());
			String code = scope .();
			for (var field in fields)
			{
				code.AppendF($"{item}.[Friend]{field.Name} = (({field.FieldType}*)((uint8*){prefix}mItems");
				if (@field.Index > 0)
					code.AppendF($" + {prefix}mOffsets[{@field.Index - 1}]");
				code.AppendF($"))[{idx}];\n");
			}

			Compiler.MixinRoot(code);
		}

		[Comptime]
		static void Emit_Set(String prefix, String idx, String item)
		{
			String code = scope .();
			var fields = GetFields(.. scope .());
			for (var field in fields)
			{
				if (field.IsStatic)
					continue;
				code.AppendF($"(({field.FieldType}*)((uint8*){prefix}mItems");
				if (@field.Index > 0)
					code.AppendF($" + {prefix}mOffsets[{@field.Index - 1}]");
				code.AppendF($"))[{idx}] = {item}.[Friend]{field.Name};\n");
			}

			Compiler.MixinRoot(code);
		}

		[Comptime]
		static void Emit_Copy(String destOfs, String srcOfs, String length)
		{
			var fields = GetFields(.. scope .());
			String code = scope .();
			for (var field in fields)
			{
				if (@field.Index > 0)
					code.AppendF($"Internal.MemMove((uint8*)mItems + mOffsets[{@field.Index - 1}] + {destOfs} * {field.FieldType.Stride}, (uint8*)mItems + mOffsets[{@field.Index - 1}] + {srcOfs} * {field.FieldType.Stride}, mSize * {field.FieldType.Stride});\n");
				else
					code.AppendF($"Internal.MemMove((uint8*)mItems + {destOfs} * {field.FieldType.Stride}, (uint8*)mItems + {srcOfs} * {field.FieldType.Stride}, {length} * {field.FieldType.Stride});\n");
			}
			Compiler.MixinRoot(code);
		}

		public struct Entry
		{
			SelfOuter mList;
			int_cosize mIdx;

			[Inline]
			public this(SelfOuter list, int idx)
			{
				mList = list;
				mIdx = (.)idx;
			}

			[Comptime, OnCompile(.TypeInit)]
			static void Init()
			{
				var fields = GetFields(.. scope .());
				String code = scope .();
				for (var field in fields)
				{
					code.AppendF($"[Inline] public ref {field.FieldType} {field.Name} => ref (({field.FieldType}*)((uint8*)mList.mItems");
					if (@field.Index > 0)
						code.AppendF($" + mList.mOffsets[{@field.Index - 1}]");
					code.AppendF($"))[mIdx];\n");
				}

				Compiler.EmitTypeBody(typeof(Self), code);
			}

			public T Value
			{
				get
				{
					T value = ?;
					Emit_Get("mList.", "mIdx", "value");
					return value;
				}

				set
				{
					Emit_Set("mList.", "mIdx", "value");
				}
			}

			public static T operator implicit(Self self) => self.[Inline]Value;
		}

		public ~this()
		{
			Free(mItems);
		}

		public Entry this[int index]
		{
			[Checked]
			get
			{
				Runtime.Assert((uint)index < (uint)mSize);
				return .(this, index);
			}

			[Unchecked, Inline]
			get
			{
				return .(this, index);
			}
		}

		Variant IList.this[int index]
		{
			get
			{
				return [Unbound]Variant.Create(this[index]);
			}

			set
			{
				ThrowUnimplemented();
			}
		}

		public int Count => mSize;

		[Inline] public Data Data => .(this);

		protected virtual void* Alloc(int byteSize)
		{
			return new uint8[byteSize]*;
		}

		protected virtual void Free(void* val)
		{
			delete val;
		}

		void* Realloc(int newSize, bool autoFree)
		{
			[Comptime]
			void Emit_Start()
			{
				var fields = GetFields(.. scope .());
				String code = scope $"int_cosize[{Math.Max(0, fields.Count - 1)}] newOffsets;\n";
				FieldInfo prevFieldInfo = default; 
				for (var field in fields)
				{
					if (@field.Index > 0)
					{
						code.AppendF($"newOffsets[{@field.Index - 1}] = (.)Math.Align(");
						if (@field.Index > 1)
							code.AppendF($"newOffsets[{@field.Index - 2}] + ");
						code.AppendF($"newSize * {prevFieldInfo.FieldType.Stride}, {field.FieldType.Align});\n");
					}
					prevFieldInfo = field;
				}

				if (fields.Count == 0)
					code.AppendF("int newSizeBytes = 0;\n");
				else if (fields.Count == 1)
					code.AppendF($"int newSizeBytes = newSize * {typeof(T).Stride};\n");
				else
					code.AppendF($"int newSizeBytes = newOffsets[{fields.Count - 2}] + newSize * {fields[fields.Count - 1].FieldType.Stride};\n");

				Compiler.MixinRoot(code);
			}

			[Comptime]
			void Emit_Copy()
			{
				var fields = GetFields(.. scope .());
				String code = scope .();
				for (var field in fields)
				{
					if (@field.Index > 0)
						code.AppendF($"Internal.MemCpy((uint8*)newItems + newOffsets[{@field.Index - 1}], (uint8*)mItems + mOffsets[{@field.Index - 1}], mSize * {field.FieldType.Stride});\n");
					else
						code.AppendF($"Internal.MemCpy(newItems, mItems, mSize * {field.FieldType.Stride});\n");
				}
				Compiler.MixinRoot(code);
			}

			void* oldAlloc = null;
			if (newSize > 0)
			{
				Emit_Start();
				void* newItems = Alloc(newSizeBytes);
				if (mSize > 0)
				{
					Emit_Copy();
				}
				oldAlloc = mItems;
				mItems = newItems;
				mOffsets = newOffsets;
				mAllocSize = (.)newSize;
			}
			else
			{
				oldAlloc = mItems;
				mItems = null;
				mAllocSize = 0;
			}

			if ((autoFree) && (oldAlloc != null))
			{
				Free(oldAlloc);
				return null;
			}

			return oldAlloc;
		}

		public void* EnsureCapacity(int min, bool autoFree)
		{
			int allocSize = AllocSize;
			if (allocSize >= min)
				return null;
			
			int_cosize newCapacity = (int_cosize)(allocSize == 0 ? cDefaultCapacity : allocSize * 2);
			// Allow the list to grow to maximum possible capacity (~2G elements) before encountering overflow.
			// Note that this check works even when mItems.Length overflowed thanks to the (uint) cast
			//if ((uint)newCapacity > Array.MaxArrayLength) newCapacity = Array.MaxArrayLength;
			if (newCapacity < min) newCapacity = (int_cosize)min;
			return Realloc(newCapacity, autoFree);
		}

		public void Reserve(int size)
		{
			EnsureCapacity(size, true);
		}

		/// Adds an item to the back of the list.
		public void Add(T item)
		{
			if (mSize == AllocSize)
			{
				// We free after the insert to allow for inserting a ref to another list element
				let oldPtr = EnsureCapacity(mSize + 1, false);
				Emit_Set("", "mSize", "item");
				mSize++;
				Free(oldPtr);
				return;
			}

			Emit_Set("", "mSize", "item");
			mSize++;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public void IList.Add(Variant item)
		{
			Add(item.Get<T>());
		}

		public void IList.Insert(int index, Variant item)
		{
			Runtime.NotImplemented();
		}

		protected override void GCMarkMembers()
		{
			[Comptime]
			void Emit()
			{
				String code = scope .();
				var fields = GetFields(.. scope .());
				for (var field in fields)
				{
					if (!field.FieldType.WantsMark)
						continue;
					code.AppendF($"for (int i < mSize) {{ GC.Mark!((({field.FieldType}*)((uint8*)mItems");
					if (@field.Index > 0)
						code.AppendF($" + mOffsets[{@field.Index - 1}]");
					code.Append("))[i]); }\n");
				}
				Compiler.MixinRoot(code);
			}

			if (mItems == null)
				return;
			let type = typeof(T);
			if ((type.[Friend]mTypeFlags & .WantsMark) == 0)
				return;
			Emit();
		}

		public void Clear()
		{
			if (mSize > 0)
			{
				mSize = 0;
#if VERSION_LIST
				mVersion++;
#endif
			}
		}

		public bool Contains(T item)
		{
			for (int i < mSize)
				if (this[i].Value == item)
					return true;
			return false;
		}

		public bool IList.Contains(Variant item)
		{
			return Contains(item.Get<T>());
		}

		public void CopyTo(Span<T> span)
		{
			for (int i < span.Length)
				span[i] = this[i].Value;
		}

		public bool Remove(T item)
		{
			int index = IndexOf(item);
			if (index >= 0)
			{
				RemoveAt(index);
				return true;
			}

			return false;
		}

		public bool IList.Remove(Variant item)
		{
			return Remove(item.Get<T>());
		}

		public void RemoveAt(int index)
		{
			Debug.Assert((uint)index < (uint)mSize);
			if (index < mSize - 1)
			{
				int copySize = mSize - index - 1;
				(void)copySize;
				Emit_Copy("index", "(index + 1)", "copySize");
			}
			mSize--;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public void RemoveRange(int index, int count)
		{
			Debug.Assert((uint)index + (uint)count <= (uint)mSize);
			if (index + count <= mSize - 1)
			{
				for (int i = index; i < mSize - count; i++)
					mItems[i] = mItems[i + count];
			}
			mSize -= (.)count;
#if VERSION_LIST
			mVersion++;
#endif
		}

		/// Will change the order of items in the list
		public void RemoveAtFast(int index)
		{
			Debug.Assert((uint32)index < (uint32)mSize);
			if (mSize > 1)
		        this[index].Value = this[mSize - 1].Value;
			mSize--;
#if VERSION_LIST
			mVersion++;
#endif
		}

		public Enumerator GetEnumerator()
		{
			return .(this);
		}

		public int FindIndex(Predicate<T> match)
		{
			for (int i = 0; i < mSize; i++)
				if (match(this[i].Value))
					return i;
			return -1;
		}

		public int IndexOf(T item)
		{
			for (int i = 0; i < mSize; i++)
				if (this[i].Value == item)
					return i;
			return -1;
		}

		public int IList.IndexOf(Variant item)
		{
			return IndexOf(item.Get<T>());
		}

		public int IndexOf(T item, int index)
		{
			for (int i = index; i < mSize; i++)
				if (this[i].Value == item)
					return i;
			return -1;
		}

		public int IndexOf(T item, int index, int count)
		{
			for (int i = index; i < index + count; i++)
				if (this[i].Value == item)
					return i;
			return -1;
		}

		public int IndexOfStrict(T item)
		{
			for (int i = 0; i < mSize; i++)
				if (this[i].Value === item)
					return i;
			return -1;
		}

		public int IndexOfStrict(T item, int index)
		{
			for (int i = index; i < mSize; i++)
				if (this[i].Value === item)
					return i;
			return -1;
		}

		public int IndexOfStrict(T item, int index, int count)
		{
			for (int i = index; i < index + count; i++)
				if (this[i].Value === item)
					return i;
			return -1;
		}

		public int IndexOfAlt<TAlt>(TAlt item) where TAlt : IHashable where bool : operator T == TAlt
		{
			for (int i = 0; i < mSize; i++)
				if (this[i].Value == item)
					return i;
			return -1;
		}

		public int LastIndexOf(T item)
		{
			for (int i = mSize - 1; i >= 0; i--)
				if (this[i].Value == item)
					return i;
			return -1;
		}

		public int LastIndexOfStrict(T item)
		{
			for (int i = mSize - 1; i >= 0; i--)
				if (this[i].Value === item)
					return i;
			return -1;
		}

		public struct Enumerator : IEnumerator<Entry>, IResettable
		{
		    private SplitList<T> mList;
		    private int mIndex;
#if VERSION_LIST
		    private int32 mVersion;
#endif
		    public this(SplitList<T> list)
		    {
		        mList = list;
		        mIndex = 0;
#if VERSION_LIST
		        mVersion = list.mVersion;
#endif
		    }

#if VERSION_LIST
			void CheckVersion()
			{
				if (mVersion != mList.mVersion)
					Runtime.FatalError(cVersionError);
			}
#endif

		    public void Dispose()
		    {
		    }

		    public bool MoveNext() mut
		    {
		        var localList = mList;
		        if ((uint(mIndex) < uint(localList.mSize)))
		        {
		            mIndex++;
		            return true;
		        }			   
		        return MoveNextRare();
		    }

		    private bool MoveNextRare() mut
		    {
#if VERSION_LIST
				CheckVersion();
#endif
		    	mIndex = mList.mSize + 1;
		        return false;
		    }

		    public Entry Current
		    {
		        get
		        {
		            return mList[mIndex - 1];
		        }
		    }

			public int Index
			{
				get
				{
					return mIndex - 1;
				}				
			}

			public int Count
			{
				get
				{
					return mList.Count;
				}				
			}

			public void Remove() mut
			{
				int curIdx = mIndex - 1;
				mList.RemoveAt(curIdx);
#if VERSION_LIST
				mVersion = mList.mVersion;
#endif
				mIndex = curIdx;
			}

			public void RemoveFast() mut
			{
				int curIdx = mIndex - 1;
				int lastIdx = mList.Count - 1;
				if (curIdx < lastIdx)
		            mList[curIdx].Value = mList[lastIdx].Value;
				mList.RemoveAt(lastIdx);
#if VERSION_LIST
				mVersion = mList.mVersion;
#endif
				mIndex = curIdx;
			}
		    
		    public void Reset() mut
		    {
		        mIndex = 0;
		    }

			public Result<Entry> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}
	}
}
