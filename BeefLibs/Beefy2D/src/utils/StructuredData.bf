using System;
using System.Collections;
using System.Text;
using System.Diagnostics;
using System.IO;

namespace Beefy.utils
{
    public class StructuredData : ILeakIdentifiable
    {
		public enum Error
		{
			case FileError;
			case FormatError(int line);
			case ParseError;
			case ColonNotExpected;
			case KeyInArray;
			case KeyRequiredForVal;
			case ValueExpected;
			case PrecedingCommaExpected;
			case UnexpectedObjectEnd;
			case ExpectedArrayNameEnd;

			/*public override void ToString(String str)
			{
				switch (this)
				{
				case FormatError: str.Append("Format error");
				case ParseError: str.Append("Parse error");
				case ColonNotExpected: str.Append("Colon not expected");
				case KeyInArray: str.Append("Cannot add key/val to array");
				case KeyRequiredForVal:  str.Append("Key required for value");
				case ValueExpected: str.Append("Value expected");
				case PrecedingCommaExpected: str.Append("Preceding comma expected");
				case UnexpectedObjectEnd: str.Append("Unexpected object end");
				}
			}*/
		}

        public struct Enumerator : IEnumerator<StringView>
        {
			StructuredData mStructuredData;
			bool mIsFirst = true;

            public this(StructuredData data)
            {
                mStructuredData = data;
            }

            public StringView Current
            {
                get
                {
					if (mStructuredData.mCurrent.mLastKey != -1)
						return mStructuredData.mKeys[mStructuredData.mCurrent.mLastKey];
					return StringView();
                }
            }

            public bool MoveNext() mut
            {
				var current = ref mStructuredData.mCurrent;

				if (current.mLastValue == -1)
					return false;

                if (mIsFirst)
				{
					//mStructuredData.mOpenStack.Add(mStructuredData.mCurrent);
					//mStructuredData.mCurrent = CurrentEntry(mValues);

					mIsFirst = false;
					return true;
				}

				if (current.mLastKey != -1)
					current.mLastKey = mStructuredData.mNextKeys[current.mLastKey];
				current.mLastValue = mStructuredData.mNextValues[current.mLastValue];
				return current.mLastValue != -1;
            }

			public void Dispose()
			{
				mStructuredData.Close();
			}

			public Result<StringView> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
        }

		public class Values
		{
			public int32 mValueIdx = -1;

			public virtual bool IsArray
			{
				get
				{
                    return true;
				}
			}

			public virtual bool IsObject
			{
				get
				{
                    return false;
				}
			}

			public virtual bool ForceInline
			{
				get
				{
					return false;
				}
			}
		}

		public class InlineValues : Values
		{
			public override bool ForceInline
			{
				get
				{
					return true;
				}
			}
		}

		public class NamedValues : Values
		{
			public int32 mKeyIdx = -1;

			public override bool IsArray
			{
				get
				{
                    return false;
				}
			}

			public override bool IsObject
			{
				get
				{
                    return true;
				}
			}
		}

		public class InlineNamedValues : NamedValues
		{
			public override bool ForceInline
			{
				get
				{
					return true;
				}
			}
		}

		struct CurrentEntry
		{
			public Values mValues;
			public int32 mLastValue = -1;
			public int32 mLastKey = -1;

			public this(Values values)
			{
				mValues = values;
			}

			public bool HasValues
			{
				get
				{
					return mLastValue != -1;
				}
			}
		}

		static Object sEmptySentinel = new Object() ~ delete _;

		BumpAllocator mBumpAllocator = CreateAllocator() ~ delete _;
		String mSource;
		List<CurrentEntry> mOpenStack = new:mBumpAllocator List<CurrentEntry>() ~ delete:mBumpAllocator _;

		Values mHeadValues;

        CurrentEntry mCurrent = .(null);
		NamedValues mEmptyData;
        
        List<Object> mValues ~
	        {
				/*for (var item in _)
					if (item != sEmptySentinel)
						delete item;*/
				delete:mBumpAllocator _;
			};

        List<StringView> mKeys ~ delete:mBumpAllocator _;

		List<int32> mNextValues ~ delete:mBumpAllocator _;
		List<int32> mNextKeys ~ delete:mBumpAllocator _;

        Dictionary<String, Object> mMap ~ delete:mBumpAllocator _;

        DisposeProxy mStructuredDisposeProxy ~ delete _;
        
        bool mCanWrite = true;
        bool mCanRead = true;

        public this()
        {
            
        }

		public ~this()
		{
		}

		protected virtual BumpAllocator CreateAllocator()
		{
			var bumpAlloc = new BumpAllocator();
			bumpAlloc.DestructorHandling = .Ignore;
			return bumpAlloc;
		}

        /*public ListRef<String> Keys
        {
            get 
            {
				if (var namedValues = mCurrent as NamedValues)
				{
				 	return ListRef<String>(mKeys, namedValues.mKeyIdx, namedValues.mCount);
				}
                return ListRef<String>(null, 0, 0);
            }
        }

        public ListRef<Object> Values
        {
            get 
            {
                return ListRef<Object>(mValues, mCurrent.mValueIdx, mCurrent.mCount);
            }
        }
        
        public int Count
        {
            get { return mCurrent.mCount; }
        }*/

        public bool IsArray
        {
            get { return GetCurrent().GetType() == typeof(Values); }
        }

        public bool IsObject
        {
            get { return GetCurrent().GetType() == typeof(NamedValues); }
        }

		public bool IsEmpty
		{
			get
			{
				if (mCurrent.mLastValue != -1)
				{
					if (mValues[mCurrent.mLastValue] != sEmptySentinel)
						return false;
					else
					{
						// We need a (potentially) full value pass if we have sEmptySentinel
						int checkValue = mCurrent.mValues.mValueIdx;
						while (checkValue != -1)
						{
							if (mValues[checkValue] != sEmptySentinel)
							{
								return false;
							}
							checkValue = mNextValues[checkValue];
						}
					}
				}
				return true;
			}
		}

		public int GetValueCount()
		{
			int count = 0;
			int32 checkValue = mCurrent.mValues.mValueIdx;
			while (checkValue != -1)
			{
				if (mValues[checkValue] != sEmptySentinel)
					count++;
				checkValue = mNextValues[checkValue];
			}
			return count;
		}

		static int32 sIdx;
		int32 mIdx = sIdx++;
		public void ToLeakString(String str)
		{
			str.AppendF("Idx:{0}", mIdx);
		}

        public bool TryGet(StringView name, out Object value)
        {
			var current = GetCurrent();
			if ((current == null) || (current.GetType() != typeof(NamedValues)))
			{
				value = null;
				return false;
			}

			let namedValues = (NamedValues)current;

			int32 checkKey = namedValues.mKeyIdx;
			int32 checkValue = namedValues.mValueIdx;
			while (checkKey != -1)
			{
				if (mKeys[checkKey] == name)
				{
					value = mValues[checkValue];
					return true;
				}

				checkValue = mNextValues[checkValue];
				checkKey = mNextKeys[checkKey];
			}

			value = null;
			return false;
        }

		public bool TryGet(StringView name, out NamedValues namedValues, out int keyIdx, out int valueIdx)
		{
			var current = GetCurrent();
			if ((current == null) || (current.GetType() != typeof(NamedValues)))
			{
				//value = null;
				namedValues = null;
				valueIdx = -1;
				keyIdx = -1;
				return false;
			}

			namedValues = (NamedValues)current;

			int32 checkKey = namedValues.mKeyIdx;
			int32 checkValue = namedValues.mValueIdx;
			while (checkKey != -1)
			{
				if (mKeys[checkKey] == name)
				{
					valueIdx = checkValue;
					keyIdx = checkKey;
					return true;
				}

				checkValue = mNextValues[checkValue];
				checkKey = mNextKeys[checkKey];
			}

			namedValues = null;
			valueIdx = -1;
			keyIdx = -1;
			return false;
		}

        public bool Contains(StringView name)
        {
            Object value;
            return TryGet(name, out value);
        }

        public Object Get(StringView name)
        {
            Object value;
            TryGet(name, out value);
            return value;
        }

		public bool Get(StringView name, ref bool val)
		{
		    Object aVal = Get(name);
		    if ((aVal == null) || (!(aVal is bool)))
		        return false;
		    val = (bool)aVal;
			return true;
		}

		public void Get(StringView name, ref int32 val)
		{
			Object obj = Get(name);
			if (obj == null)
				return;
			switch (obj.GetType())
			{
			case typeof(Int64): val = (.)(int64)obj;
			default:
			}
		}

		public void Get(StringView name, ref float val)
		{
			Object obj = Get(name);
			if (obj == null)
				return;
			switch (obj.GetType())
			{
			case typeof(Int64): val = (int64)obj;
			case typeof(Float): val = (float)obj;
			default:
			}
		}

		public void Get<T>(StringView name, ref T val) where T : enum
		{
			Object obj = Get(name);
			if (obj == null)
				return;
			Result<T> result;
			if (let str = obj as String)
				result = Enum.Parse<T>(str);
			else if (obj is StringView)
				result = Enum.Parse<T>((StringView)obj);
			else
				return;

			if (result case .Ok(var parsedVal))
				val = parsedVal;
		}

		public void Get(StringView name, String outString)
		{
		    Object obj = Get(name);
			if (obj == null)
				return;
			outString.Clear();
		    if (obj is uint64)
		        obj.ToString(outString);
			if (obj != null)
			{
				var type = obj.GetType();
				if (type == typeof(StringView))
					outString.Append((StringView)obj);
				else if (type == typeof(String))
		            outString.Append((String)obj);
			}
		}

        public Object GetCurrent()
        {
			if (mCurrent.mLastValue == -1)
				return null;
			return mValues[mCurrent.mLastValue];
        }

        public void GetString(StringView name, String outString, String theDefault = null)
        {
            Object val = Get(name);

			outString.Clear();

            if (val is uint64)
                val.ToString(outString);

			if (val != null)
			{
				var type = val.GetType();
				if (type == typeof(StringView))
				{
					outString.Append((StringView)val);
					return;
				}
				if (type == typeof(String))
				{
                    outString.Append((String)val);
					return;
				}
			}
            
			if (theDefault != null)
            	outString.Append(theDefault);
			return;
        }

        public int32 GetInt(String name, int32 defaultVal = 0)
        {
            Object val = Get(name);
            if (val == null)
                return defaultVal;
			switch (val.GetType())
			{
			case typeof(Float): return (.)(float)val;
			case typeof(Int32): return (.)(int32)val;
			case typeof(Int64): return (.)(int64)val;
			case typeof(Int): return (.)(int)val;
			case typeof(String):
				if (int32.Parse((String)val) case .Ok(var fVal))
					return (.)fVal;
				return defaultVal;
			case typeof(StringView):
				if (int32.Parse((StringView)val) case .Ok(var fVal))
					return (.)fVal;
				return defaultVal;
			default: return defaultVal;
			}
        }

        public int64 GetLong(String name, int64 defaultVal = 0)
        {
            Object val = Get(name);
			if (val == null)
			    return defaultVal;
			switch (val.GetType())
			{
			case typeof(Float): return (.)(float)val;
			case typeof(Int32): return (.)(int32)val;
			case typeof(Int64): return (.)(int64)val;
			case typeof(Int): return (.)(int)val;
			case typeof(String):
				if (int64.Parse((String)val) case .Ok(var parsedVal))
					return (.)parsedVal;
				return defaultVal;
			case typeof(StringView):
				if (int64.Parse((StringView)val) case .Ok(var parsedVal))
					return (.)parsedVal;
				return defaultVal;
			default: return defaultVal;
			}
        }

        public uint64 GetULong(String name, uint64 defaultVal = 0)
        {
            Object val = Get(name);
			if (val == null)
			    return defaultVal;
			switch (val.GetType())
			{
			case typeof(Float): return (.)(float)val;
			case typeof(Int32): return (.)(int32)val;
			case typeof(Int64): return (.)(int64)val;
			case typeof(Int): return (.)(int)val;
			case typeof(String):
				if (int64.Parse((String)val) case .Ok(var parsedVal))
					return (.)parsedVal;
				return defaultVal;
			case typeof(StringView):
				if (int64.Parse((StringView)val) case .Ok(var parsedVal))
					return (.)parsedVal;
				return defaultVal;
			default: return defaultVal;
			}
        }

        public float GetFloat(String name, float defaultVal = 0)
        {
            Object val = Get(name);
			if (val == null)
				return defaultVal;
			switch (val.GetType())
			{
			case typeof(Float): return (.)(float)val;
			case typeof(Int32): return (.)(int32)val;
			case typeof(Int64): return (.)(int64)val;
			case typeof(Int): return (.)(int)val;
			case typeof(String):
				if (float.Parse((String)val) case .Ok(var parsedVal))
					return parsedVal;
				return defaultVal;
			case typeof(StringView):
				if (float.Parse((StringView)val) case .Ok(var parsedVal))
					return parsedVal;
				return defaultVal;
			default: return defaultVal;
			}
        }

        public bool GetBool(String name, bool defaultVal = false)
        {
            Object val = Get(name);
			if (val == null)
			    return defaultVal;
			switch (val.GetType())
			{
			case typeof(Boolean): return (bool)val;
			case typeof(String):
				if (bool.Parse((String)val) case .Ok(var parsedVal))
					return (.)parsedVal;
				return defaultVal;
			default: return defaultVal;
			}
        }

        public T GetEnum<T>(String name, T defaultVal = default(T)) where T : enum
        {
            Object obj = Get(name);
			if (obj == null)
				return defaultVal;

			Result<T> result;
			if (let str = obj as String)
				result = Enum.Parse<T>(str);
			else if (obj is StringView)
				result = Enum.Parse<T>((StringView)obj);
            else
				return defaultVal;

			if (result case .Ok(var val))
				return val;
			return defaultVal;
        }

		public bool GetEnum<T>(String name, ref T val) where T : enum
		{
			Object obj = Get(name);
			if (obj == null)
				return false;

			Result<T> result;
			if (let str = obj as String)
				result = Enum.Parse<T>(str);
			else if (obj is StringView)
				result = Enum.Parse<T>((StringView)obj);
			else
				return false;

			if (result case .Ok(out val))
				return true;
			return false;
		}

        ///
        public void GetCurString(String outString, String theDefault = null)
        {
            Object val = GetCurrent();

			outString.Clear();
			if (val is uint64)
			    val.ToString(outString);

			if (val != null)
			{
				var type = val.GetType();
				if (type == typeof(StringView))
				{
					outString.Append((StringView)val);
					return;
				}
				if (type == typeof(String))
				{
			        outString.Append((String)val);
					return;
				}
			}

			if (theDefault != null)
				outString.Append(theDefault);
			return;
        }

		public T GetCurEnum<T>(T theDefault = default) where T : enum
		{
			Object obj = GetCurrent();
			
			Result<T> result;
			if (let str = obj as String)
				result = Enum.Parse<T>(str);
			else if (obj is StringView)
				result = Enum.Parse<T>((StringView)obj);
			else
				return theDefault;

			if (result case .Ok(var val))
				return val;
			return theDefault;
		}

        public int32 GetCurInt(int32 theDefault = 0)
        {
            Object aVal = GetCurrent();
            if ((aVal == null) || (!(aVal is int64)))
                return theDefault;
            return (.)(int64)aVal;
        }

        public uint32 GetCurUInt(uint32 theDefault = 0)
        {
            Object aVal = GetCurrent();
            if ((aVal == null) || (!(aVal is uint32)))
                return theDefault;
            return (uint32)aVal;
        }

        public float GetCurFloat(float theDefault = 0)
        {
            Object aVal = GetCurrent();
            if ((aVal == null) || (!(aVal is float)))
                return theDefault;
            return (float)aVal;
        }

        public bool GetCurBool(bool theDefault = false)
        {
            Object aVal = GetCurrent();
            if ((aVal == null) || (!(aVal is bool)))
                return theDefault;
            return (bool)aVal;
        }

        ///
        
        public void DoAdd(ref CurrentEntry currentEntry, Object obj)
        {
            //Temporary type check?
            Debug.Assert(IsValidWriteObject(obj));
            Debug.Assert(mCanWrite);

            Debug.Assert(!currentEntry.mValues.IsObject); // Can't be an object

			Debug.Assert((currentEntry.mValues.mValueIdx == -1) == (currentEntry.mLastValue == -1));

			EnsureHasData();

			int valueIdx = mValues.Count;
			if (currentEntry.mLastValue != -1)
				mNextValues[currentEntry.mLastValue] = (int32)valueIdx;
			else
				currentEntry.mValues.mValueIdx = (int32)valueIdx;

			currentEntry.mLastValue = (int32)valueIdx;
			mValues.Add(obj);
			mNextValues.Add(-1);
        }

		public void Add<T>(T value) where T : struct
		{
		    DoAdd(ref mCurrent, new:mBumpAllocator box value);
		}

		public void Add(StringView value)
		{
			DoAdd(ref mCurrent, (Object)new:mBumpAllocator String(value));
		}

		public void Add<T>(StringView name, T value) where T : struct
		{
		    DoAdd(ref mCurrent, AllocStringView(name), new:mBumpAllocator box value);
		}

		public void ConditionalAdd<T>(StringView name, T value) where T : var
		{
			if (value != default(T))
				Add(name, value);
		}

		public void ConditionalAdd<T>(StringView name, T value, T defaultVal) where T : var
		{
			if ((value != null) && (value != defaultVal))
				Add(name, value);
		}

		void EnsureHasData()
		{
			if (mValues == null)
			{
				mValues = new:mBumpAllocator ListWithAlloc<Object>(mBumpAllocator);
				mKeys = new:mBumpAllocator ListWithAlloc<StringView>(mBumpAllocator);
				mNextKeys = new:mBumpAllocator ListWithAlloc<int32>(mBumpAllocator);
				mNextValues = new:mBumpAllocator ListWithAlloc<int32>(mBumpAllocator);
			}
		}

		void DoAdd(ref CurrentEntry currentEntry, StringView name, Object value)
		{
			EnsureHasData();

		    //Temporary type check?
		    Debug.Assert(IsValidWriteObject(value));
		    Debug.Assert(mCanWrite);

		    Debug.Assert(!currentEntry.mValues.IsArray); // Can't be an array

			Debug.Assert((currentEntry.mValues.mValueIdx == -1) == (currentEntry.mLastValue == -1));

			var namedValues = currentEntry.mValues as NamedValues;

			int valueIdx = mValues.Count;
			int keyIdx = mKeys.Count;

			if (currentEntry.mLastValue != -1)
				mNextValues[currentEntry.mLastValue] = (int32)valueIdx;
			else
				currentEntry.mValues.mValueIdx = (int32)valueIdx;

			if (currentEntry.mLastKey != -1)
				mNextKeys[currentEntry.mLastKey] = (int32)keyIdx;
			else
				namedValues.mKeyIdx = (int32)keyIdx;

			currentEntry.mLastValue = (int32)valueIdx;
			currentEntry.mLastKey = (int32)keyIdx;
			mKeys.Add(name);
			mNextKeys.Add(-1);
			mValues.Add(value);
			mNextValues.Add(-1);
		}

		StringView AllocStringView(StringView str)
		{
			int len = str.Length;
			char8* ptr = (char8*)mBumpAllocator.Alloc(len, 1);
			Internal.MemCpy(ptr, str.Ptr, len);
			return StringView(ptr, len);
		}

		public void Add(String name, StringView value)
		{
			DoAdd(ref mCurrent, AllocStringView(name), (Object)new:mBumpAllocator String(value));
		}

		public void ConditionalAdd(StringView name, StringView value, StringView defaultVal)
		{
			if (!(value == defaultVal))
				DoAdd(ref mCurrent, AllocStringView(name), (Object)new:mBumpAllocator String(value));
		}

		public void ConditionalAdd(StringView name, String value, String defaultVal)
		{
			if (!(value == defaultVal))
				DoAdd(ref mCurrent, AllocStringView(name), (Object)new:mBumpAllocator String(value));
		}

		public void ConditionalAdd(StringView name, StringView value)
		{
			if (!value.IsEmpty)
				DoAdd(ref mCurrent, AllocStringView(name), (Object)new:mBumpAllocator String(value));
		}

		public void ConditionalAdd(StringView name, String value)
		{
			if ((value != null) && (!value.IsEmpty))
				DoAdd(ref mCurrent, AllocStringView(name), (Object)new:mBumpAllocator String(value));
		}
        
        ///

        public void Close()
        {
            mCurrent = mOpenStack.PopBack();
        }

		public void RemoveIfEmpty()
		{
			bool hasValue = false;

			if (mCurrent.mLastValue != -1)
			{
				if (mValues[mCurrent.mLastValue] != sEmptySentinel)
					hasValue = true;
				else
				{
					// We need a (potentially) full value pass if we have sEmptySentinel
					int checkValue = mCurrent.mValues.mValueIdx;
					while (checkValue != -1)
					{
						if (mValues[checkValue] != sEmptySentinel)
						{
							hasValue = true;
							break;
						}
						checkValue = mNextValues[checkValue];
					}
				}
			}

			if (!hasValue)
			{
				var prev = ref mOpenStack.Back;
				var lastVal = mValues[prev.mLastValue];
				Debug.Assert(lastVal == mCurrent.mValues);

				if (lastVal == mCurrent.mValues)
				{
					mValues[prev.mLastValue] = sEmptySentinel;
				}
			}
		}

        public DisposeProxy Open(StringView name)
        {
            if (mStructuredDisposeProxy == null)
            {
                mStructuredDisposeProxy = new DisposeProxy();
                mStructuredDisposeProxy.mDisposeProxyDelegate = new => Close;
            }

			mOpenStack.Add(mCurrent);

            NamedValues namedValues;
			int keyIdx;
			int valueIdx;
            if (TryGet(name, out namedValues, out keyIdx, out valueIdx))
            {
                mCurrent = CurrentEntry(namedValues);
				mCurrent.mLastKey = (int32)keyIdx;
				mCurrent.mLastValue = (int32)valueIdx;
            }
            else
            {
				if (mEmptyData == null)
				{
					mEmptyData = new:mBumpAllocator NamedValues();
				}
                mCurrent = CurrentEntry(mEmptyData);
            }            

            return mStructuredDisposeProxy;
        }

		public Enumerator Enumerate(String name)
		{
			Enumerator enumerator;

		    mOpenStack.Add(mCurrent);

		    Object val;
		    if ((TryGet(name, out val)) && (let values = val as Values))
		    {
				mCurrent = CurrentEntry(values);
				if (let namedValues = values as NamedValues)
					mCurrent.mLastKey = namedValues.mKeyIdx;
				mCurrent.mLastValue = values.mValueIdx;
		        enumerator = Enumerator(this);
		    }
		    else
		    {
				mCurrent = CurrentEntry(null);
				enumerator = Enumerator(this);
		    }            

		    return enumerator;
		}

		public Enumerator Enumerate()
		{
			Enumerator enumerator;

		    mOpenStack.Add(mCurrent);

		    Object val = GetCurrent();
		    if (let values = val as Values)
		    {
				mCurrent = CurrentEntry(values);
				if (let namedValues = values as NamedValues)
					mCurrent.mLastKey = namedValues.mKeyIdx;
				mCurrent.mLastValue = values.mValueIdx;
		        enumerator = Enumerator(this);
		    }
		    else
		    {
				mCurrent = CurrentEntry(null);
				enumerator = Enumerator(this);
		    }            

		    return enumerator;
		}

        DisposeProxy GetDisposeProxy()
        {
            if (mStructuredDisposeProxy == null)
            {
                mStructuredDisposeProxy = new DisposeProxy();
                mStructuredDisposeProxy.mDisposeProxyDelegate = new => Close;
            }

            return mStructuredDisposeProxy;
        }

		public void CreateNew()
		{
			if (mCurrent.mValues == null)
			{
				NamedValues values = new:mBumpAllocator NamedValues();
				mCurrent = CurrentEntry(values);
			}
		}

        public DisposeProxy CreateArray()
        {
            Values values = new:mBumpAllocator Values();
            DoAdd(ref mCurrent, values);
			mOpenStack.Add(mCurrent);
            mCurrent = CurrentEntry(values);
            return GetDisposeProxy();
        }

        public DisposeProxy CreateObject(bool forceInline = false)
        {
			NamedValues values;
			if (forceInline)
				values = new:mBumpAllocator InlineNamedValues();
			else
				values = new:mBumpAllocator NamedValues();

			DoAdd(ref mCurrent, values);
			mOpenStack.Add(mCurrent);
			mCurrent = CurrentEntry(values);
			return GetDisposeProxy();
        }

        public DisposeProxy CreateArray(String name, bool forceInline = false)
        {
			Values values;
			if (forceInline)
				values = new:mBumpAllocator InlineValues();
			else
				values = new:mBumpAllocator Values();	

			DoAdd(ref mCurrent, AllocStringView(name), values);
			mOpenStack.Add(mCurrent);
			mCurrent = CurrentEntry(values);
			return GetDisposeProxy();
        }

        public DisposeProxy CreateObject(String name, bool forceInline = false)
        {
            NamedValues values;
			if (forceInline)
				values = new:mBumpAllocator InlineNamedValues();
			else
				values = new:mBumpAllocator NamedValues();	

			DoAdd(ref mCurrent, AllocStringView(name), values);
			mOpenStack.Add(mCurrent);
			mCurrent = CurrentEntry(values);
			return GetDisposeProxy();
        }

        static void StringEncode(String outString, StringView theString)
        {
			outString.Append('"');

            for (int32 i = 0; i < theString.Length; i++)
            {
                char8 c = theString.Ptr[i];
                char8 slashC = '\0';

                switch (c)
                {
                    case '\b':
                        slashC = 'b';
                        break;
                    case '\f':
                        slashC = 'f';
                        break;
                    case '\n':
                        slashC = 'n';
                        break;
                    case '\r':
                        slashC = 'r';
                        break;
                    case '\t':
                        slashC = 't';
                        break;
                    case '"':
                        slashC = '"';
                        break;
                    case '\\':
                        slashC = '\\';
                        break;
                }

                if (slashC != '\0')
                {
                    outString.Append('\\');
                    outString.Append(slashC);
                }
                else
                    outString.Append(c);
            }

            outString.Append('"');
        }

        static void StringDecode(String inString)
        {
            bool lastWasSlash = false;

			char8* headPtr = inString.Ptr;
			char8* strInPtr = headPtr;
			char8* strOutPtr = headPtr;
			char8* strInEndPtr = headPtr + inString.Length;

            while (strInPtr < strInEndPtr)
            {
                char8 c = *(strInPtr++);
				
                if (lastWasSlash)
                {
                    switch (c)
                    {
                        case 'b':
                            c = '\b';
                            break;
                        case 'f':
                            c = '\f';
                            break;
                        case 'n':
                            c = '\n';
                            break;
                        case 'r':
                            c = '\r';
                            break;
                        case 't':
                            c = '\t';
                            break;
                    }

                    lastWasSlash = false;
                }
                else if (c == '\\')
                {
                    lastWasSlash = true;
					continue;
				}

				*(strOutPtr++) = c;
            }

			inString.[Friend]mLength = (int32)(strOutPtr - headPtr);
        }

        bool IsValidWriteObject(Object theObject)
        {
			if (theObject == null)
				return true;

			var type = theObject.GetType();
			switch (type)
			{
			case typeof(Boolean): return true;
			case typeof(Int32): return true;
			case typeof(UInt32): return true;
			case typeof(Int64): return true;
			case typeof(UInt64): return true;
			case typeof(Float): return true;
			case typeof(Double): return true;	
			case typeof(Int): return true;
			case typeof(UInt): return true;
			case typeof(Values): return true;
			case typeof(InlineValues): return true;
			case typeof(NamedValues): return true;
			case typeof(InlineNamedValues): return true;
			case typeof(StringView): return true;
			default:
				if (type.IsEnum)
					return true;
				if ((theObject is String) ||
					(theObject is StructuredData))
					return true;
				return false;
			}
        }

        Result<void> ObjectToString(String str, Object theObject)
        {
			var type = theObject.GetType();
			
			Debug.Assert(type != null);

            if (theObject == null)
                str.Append("null");
            else if (type == typeof(String))
                StringEncode(str, StringView((String)theObject));
			else if (type == typeof(StringView))
				StringEncode(str, (StringView)theObject);
            else if (type == typeof(System.Int32))
                ((int32)theObject).ToString(str);
            else if (type == typeof(System.UInt32))
			{
                ((uint32)theObject).ToString(str);
                //str.Append("U");
			}
            else if (type == typeof(System.Int64))
			{
                ((int64)theObject).ToString(str);
                //str.Append("L");
			}
            else if (type == typeof(System.UInt64))
			{
                ((uint64)theObject).ToString(str);
                //str.Append("UL");
			}
			else if (type == typeof(System.Int))
			{
			    ((int32)(int)theObject).ToString(str);
			    //str.Append("L");
			}
			else if (type == typeof(System.UInt))
			{
			    ((uint32)(uint)theObject).ToString(str);
			    //str.Append("UL");
			}
            else if (type == typeof(System.Float))
			{
                str.AppendF("{0:0.0######}", (float)theObject);
			}
			else if (type == typeof(System.Double))
			{
			    str.AppendF("{0:0.0######}", (float)(double)theObject);
			}
            else if (type == typeof(System.Boolean))
			{
                str.Append(((bool)theObject) ? "true" : "false");
			}
            else if (type.IsEnum)
			{
				String enumStr = scope String();
				theObject.ToString(enumStr);
                StringEncode(str, StringView(enumStr));
			}
			else
			{
				theObject.ToString(str);
			}
            /*else
			{
                //return new Exception("Invalid type: " + theObject.GetType().FullName);
				String name = scope String();
				type.GetName(name);
				Debug.WriteLine("Invalid Type: {0}", name);
				return new Exception();
			}*/
			return .Ok;
        }        

        void ToJSONHelper(Values values, String outStr, bool humanReadable, String prefix)
        {
			String innerPrefix = scope String(prefix.Length + 1);
			innerPrefix.Append(prefix);
			if (humanReadable)
				innerPrefix.Append('\t');

            if (let namedValues = values as NamedValues)
			{
				int keyIdx = namedValues.mKeyIdx;
				int valueIdx = namedValues.mValueIdx;

			    outStr.Append('{');
			    if (humanReadable)
			        outStr.Append('\n');

				bool isFirst = true;
				while (valueIdx != -1)
				{
			        StringView key = mKeys[keyIdx];
			        Object value = mValues[valueIdx];
					if (value == sEmptySentinel)
					{
						valueIdx = mNextValues[valueIdx];
						keyIdx = mNextKeys[keyIdx];
						continue;
					}

					if (isFirst)
					{
						isFirst = false;
					}
					else
					{
						outStr.Append(',');
						if (humanReadable)
							outStr.Append('\n');
					}

			        if (humanReadable)
			            outStr.Append(innerPrefix);

					StringEncode(outStr, key);

			        if (humanReadable)
			            outStr.Append(": ");
			        else
			            outStr.Append(":");

			        if (let subValues = value as Values)
			        {
			            ToJSONHelper(subValues, outStr, humanReadable, innerPrefix);
			        }
			        else
			            ObjectToString(outStr, value);

					valueIdx = mNextValues[valueIdx];
					keyIdx = mNextKeys[keyIdx];
				}
				if (humanReadable)
					outStr.Append('\n');

			    if (humanReadable)
			        outStr.Append(prefix);
			    outStr.Append('}');                
			}
			else
            {
			    outStr.Append('[');
                if (humanReadable)
                    outStr.Append('\n');

				int valueIdx = values.mValueIdx;

				bool isFirst = true;
                while (valueIdx != -1)
                {
                    Object value = mValues[valueIdx];
					if (value == sEmptySentinel)
					{
						valueIdx = mNextValues[valueIdx];
						continue;
					}

					if (isFirst)
					{
						isFirst = false;
					}
					else
					{
						outStr.Append(',');
						if (humanReadable)
							outStr.Append('\n');
					}

                    if (humanReadable)
                        outStr.Append(innerPrefix);

                    if (let subValues = value as Values)
					{
					    ToJSONHelper(subValues, outStr, humanReadable, innerPrefix);
					}
					else
					    ObjectToString(outStr, value);

					valueIdx = mNextValues[valueIdx];
                }
				if (humanReadable)
					outStr.Append('\n');

                if (humanReadable)
                    outStr.Append(prefix);
                outStr.Append(']');                
            }
        }

        public void ToJSON(String str, bool humanReadable = false)
        {
            ToJSONHelper(mCurrent.mValues, str, humanReadable, "");
        }

		public void ToTOML(String outStr)
		{
			List<StringView> nameStack = scope List<StringView>();
			List<Values> valueStack = scope List<Values>();

			void EncodeName(StringView str)
			{
				bool NeedsEncoding(StringView str)
				{
					if (str.Length == 0)
						return true;

					if (str[0].IsNumber)
						return true;

					for (int i < str.Length)
					{
						char8 c = str[i];
						if ((c.IsLetterOrDigit) || (c == '_') || (c == '-'))
						{
							// Valid
						}
						else
							return true;
					}

					return false;
				}

				if (NeedsEncoding(str))
					StringEncode(outStr, str);
				else
					outStr.Append(str);
			}

			void EncodeObject(Object obj)
			{
				if (let values = obj as Values)
				{
					if (let namedValues = values as NamedValues)
					{
						int keyIdx = namedValues.mKeyIdx;
						int valueIdx = namedValues.mValueIdx;

						outStr.Append('{');

						bool wasFirst = true;
						while (valueIdx != -1)
						{
						    StringView key = mKeys[keyIdx];
						    Object value = mValues[valueIdx];
							if (value == sEmptySentinel)
							{
								valueIdx = mNextValues[valueIdx];
								keyIdx = mNextKeys[keyIdx];
								continue;
							}

							if (!wasFirst)
								outStr.Append(", ");

							EncodeName(key);
						    outStr.Append(" = ");
						    EncodeObject(value);

							valueIdx = mNextValues[valueIdx];
							keyIdx = mNextKeys[keyIdx];

							wasFirst = false;
						}

						outStr.Append('}');
						return;
					}

					bool wasFirst = true;
					outStr.Append("[");
					int valueIdx = values.mValueIdx;
					while (valueIdx != -1)
					{
					    Object value = mValues[valueIdx];
						if (value == sEmptySentinel)
						{
							valueIdx = mNextValues[valueIdx];
							continue;
						}

						if (!wasFirst)
							outStr.Append(", ");

						EncodeObject(value);
						
						valueIdx = mNextValues[valueIdx];

					    wasFirst = false;
					}
					outStr.Append("]");
					return;
				}

				ObjectToString(outStr, obj);
			}

			void EncodeHeader()
			{
				bool isArray = false;
				if (valueStack.Count > 1)
					isArray = valueStack[valueStack.Count - 2].IsArray;

				if (!outStr.IsEmpty)
					outStr.Append("\n");
				outStr.Append(isArray ? "[[" : "[");
				for (int nameIdx < nameStack.Count)
				{
					if (nameIdx > 0)
						outStr.Append(".");
					EncodeName(nameStack[nameIdx]);
				}
				outStr.Append(isArray ? "]]\n" : "]\n");
			}

			void EncodeValues(Values values)
			{
				valueStack.Add(values);
				defer valueStack.PopBack();

				if (let namedValues = values as NamedValues)
				{
					bool forceAllInline = namedValues is InlineNamedValues;

					/*if ((!outStr.IsEmpty) || (nameStack.Count > 0))
					{
						int valueIdx = namedValues.mValueIdx;

						bool needsHeader = false;
						while (valueIdx != -1)
						{
						    Object value = mValues[valueIdx];
							if (value == sEmptySentinel)
							{
								valueIdx = mNextValues[valueIdx];
								continue;
							}

							if ((!value is NamedValues) || (forceAllInline))
								needsHeader = true;

							valueIdx = mNextValues[valueIdx];
						}

						if (needsHeader)
							EncodeHeader();
					}*/

					bool needsHeader = ((!outStr.IsEmpty) || (nameStack.Count > 0));
						
					for (int pass = 0; pass < 2; pass++)
					{
						bool isInlinePass = pass == 0;

						int keyIdx = namedValues.mKeyIdx;
						int valueIdx = namedValues.mValueIdx;

						while (valueIdx != -1)
						{
						    StringView key = mKeys[keyIdx];

						    Object value = mValues[valueIdx];
							if (value == sEmptySentinel)
							{
								valueIdx = mNextValues[valueIdx];
								keyIdx = mNextKeys[keyIdx];
								continue;
							}

							bool doValuesInline = true;
							if ((!forceAllInline) && (var subValues = value as Values))
							{
								if (!subValues.ForceInline)
								{
									doValuesInline = false;
									if (subValues.IsArray)
									{
										int subValueIdx = subValues.mValueIdx;
										while (subValueIdx != -1)
										{
											Object subValue = mValues[subValueIdx];
											if (!subValue is NamedValues)
											{
												doValuesInline = true;
											}
											subValueIdx = mNextValues[subValueIdx];
										}
									}
								}

								if ((!doValuesInline) && (!isInlinePass))
								{
									nameStack.Add(key);
									EncodeValues(subValues);
									nameStack.PopBack();
								}
							}

							if (doValuesInline && isInlinePass)
							{
								if (needsHeader)
								{
									EncodeHeader();
									needsHeader = false;
								}

								EncodeName(key);
								outStr.Append(" = ");
								EncodeObject(value);
								outStr.Append("\n");
							}

							valueIdx = mNextValues[valueIdx];
							keyIdx = mNextKeys[keyIdx];
						}
					}
				}
				else
				{
					int valueIdx = values.mValueIdx;
					while (valueIdx != -1)
					{
						bool needsHeader = true;

					    Object value = mValues[valueIdx];
						if (value == sEmptySentinel)
						{
							valueIdx = mNextValues[valueIdx];
							continue;
						}

						if (var subValues = value as Values)
						{
							EncodeValues(subValues);

							needsHeader = true;
						}
						else
						{
							ThrowUnimplemented();

							/*if (needsHeader)
							{
								outStr.Append("[");
								for (int nameIdx < nameStack.Count)
								{
									if (nameIdx > 0)
										outStr.Append(".");
									EncodeName(nameStack[nameIdx]);
								}
								outStr.Append("]\n");
								needsHeader = false;
							}

							EncodeName(key);
							outStr.Append(" = ");
							ObjectToString(outStr, value);
							outStr.Append("\n");*/
						}

						valueIdx = mNextValues[valueIdx];
					}
				}
			}

		    EncodeValues(mCurrent.mValues);
		}

        public override void ToString(String str)
        {
            ToJSON(str, false);
        }

        Result<Object, Error> LoadJSONHelper(String string, ref int32 idx, ref int32 lineNum)
        {
			CurrentEntry currentEntry = default;
            Values values = null;
			NamedValues namedValues = null;
            Object valueData = null;
            
            int32 valueStartIdx = -1;
            int32 valueEndIdx = -1;
            
            int32 keyStartIdx = -1;
            int32 keyEndIdx = -1;

            bool valueHadWhitespace = false;
            bool inQuote = false;
            bool lastWasSlash = false;
            bool hadSlash = false;
			bool keyHadSlash = false;

			bool returningValues = false;
			
			int length = string.Length;
			char8* cPtr = string.Ptr;
            for (int32 char8Idx = idx; char8Idx < length; char8Idx++)
            {
                char8 c = cPtr[char8Idx];

                if (lastWasSlash)
                {
                    lastWasSlash = false;
                    valueEndIdx = char8Idx;
                    continue;
                }

                bool doProcess = false;
                bool atEnd = false;

                if (c == '\n')
                    lineNum++;
                else if (c == '\\')
                {
                    lastWasSlash = true;
                    hadSlash = true;
                }
                else if (c == '"')
                {
                    if (inQuote)
                    {
                        valueEndIdx = char8Idx;
                        inQuote = false;
                    }
                    else
                    {
                        if ((valueStartIdx != -1) && (valueHadWhitespace))
							return .Err(.FormatError(lineNum));

                        valueStartIdx = char8Idx;
                        inQuote = true;
                    }
                }
                else if (inQuote)
                {
                    valueEndIdx = char8Idx;
                }
                else if (c == '{')
                {
                    if (values != null)
                    {
						var result = LoadJSONHelper(string, ref char8Idx, ref lineNum);
						if (result case .Err(var err))
							return .Err(err);
                        valueData = result.Get();
                    }
                    else
                    {
                        values = namedValues = new:mBumpAllocator NamedValues();
						currentEntry = CurrentEntry(values);
                    }
                }
                else if (c == '}')
                {
                    if (namedValues == null)
                        return .Err(.UnexpectedObjectEnd);
                    atEnd = true;
                    if (((valueStartIdx != -1) || (valueData != null)) && (keyStartIdx != -1))
                        doProcess = true;
                }
                else if (c == '[')
                {
                    if (values != null)
                    {
						var result = LoadJSONHelper(string, ref char8Idx, ref lineNum);
						if (result case .Err(var err))
							return .Err(err);
						valueData = result.Get();
                    }
                    else
                    {
                        values = new:mBumpAllocator Values();
						currentEntry = CurrentEntry(values);
                    }
                }
                else if (c == ']')
                {
                    if ((values == null) || (namedValues != null))
						return .Err(Error.UnexpectedObjectEnd);
                    atEnd = true;
                    if ((valueStartIdx != -1) || (valueData != null))
                        doProcess = true;
                }                
                else if (c == ',')
                {
                    doProcess = true;
                }
                else if (c == ':')
                {
                    if ((keyStartIdx != -1) || (valueStartIdx == -1))
						return .Err(Error.ColonNotExpected);

                    keyStartIdx = valueStartIdx;
                    keyEndIdx = valueEndIdx;
					keyHadSlash = hadSlash;

                    valueStartIdx = -1;
                    valueEndIdx = -1;
                }
                else if (!c.IsWhiteSpace)
                {
                    if (valueStartIdx == -1)
                    {
                        valueHadWhitespace = false;
                        valueStartIdx = char8Idx;
                    }
                    else if (valueHadWhitespace)
						return .Err(Error.PrecedingCommaExpected);
                    valueEndIdx = char8Idx;
                }
                else
                {
                    valueHadWhitespace = true;
                }

                if (doProcess)
                {                    
                    Object aValue = null;
                    if (valueData != null)
                    {
                        if (valueStartIdx != -1)
							return .Err(Error.FormatError(lineNum));
                        aValue = valueData;
                        valueData = null;
                    }
                    else
                    {
                        if (valueStartIdx == -1)
							return .Err(Error.ValueExpected);

                        if (cPtr[valueStartIdx] == '"')
                        {
							if (hadSlash)
							{
                                String str = new:mBumpAllocator String(string, valueStartIdx + 1, valueEndIdx - valueStartIdx - 1);
                                StringDecode(str);
								aValue = str;
							}
							else
							{
								aValue = new:mBumpAllocator box StringView(string, valueStartIdx + 1, valueEndIdx - valueStartIdx - 1);
							}
                        }
                        else
                        {
							StringView strView = StringView(string, valueStartIdx, valueEndIdx - valueStartIdx + 1);

							char8 lastC = strView.Ptr[strView.Length - 1];
							switch (lastC)
							{
							case 'l':
								if (strView.Equals("null"))
									aValue = null;
							case 'e':
								if (strView.Equals("true"))
								    aValue = new:mBumpAllocator box true;
								else if (strView.Equals("false"))
								    aValue = new:mBumpAllocator box false;
							case 'U':
								ThrowUnimplemented();
							case 'L':
								ThrowUnimplemented();
							default:
								if (strView.Contains('.'))
								{
								    aValue = new:mBumpAllocator box (float)float.Parse(strView);
								}
								else
								{
									var parseVal = int64.Parse(strView);
									if (parseVal case .Ok(var intVal))
										aValue = new:mBumpAllocator box intVal;
									else
									{
										return .Err(Error.ParseError);
									}
								}
							}
                        }
                    }

                    if (namedValues == null)
                    {
                        if (keyStartIdx != -1)
							return .Err(Error.KeyInArray);
                        
						int valueIdx = mValues.Count;
						if (currentEntry.mLastValue != -1)
							mNextValues[currentEntry.mLastValue] = (int32)valueIdx;
						else
							currentEntry.mValues.mValueIdx = (int32)valueIdx;

						currentEntry.mLastValue = (int32)valueIdx;
						mValues.Add(aValue);
						mNextValues.Add(-1);
                    }
                    else
                    {
                        if (keyStartIdx == -1)
							return .Err(Error.KeyRequiredForVal);

						StringView key;
						if (keyHadSlash)
						{
							String keyStr = new:mBumpAllocator String(string, keyStartIdx + 1, keyEndIdx - keyStartIdx - 1);
							StringDecode(keyStr);
							key = StringView(keyStr);
						}
						else
						{
							key = StringView(string, keyStartIdx + 1, keyEndIdx - keyStartIdx - 1);
						}
                        
                        //DoAdd(ref currentEntry, key, aValue);
						int valueIdx = mValues.Count;
						int keyIdx = mKeys.Count;

						if (currentEntry.mLastValue != -1)
							mNextValues[currentEntry.mLastValue] = (int32)valueIdx;
						else
							currentEntry.mValues.mValueIdx = (int32)valueIdx;

						if (currentEntry.mLastKey != -1)
							mNextKeys[currentEntry.mLastKey] = (int32)keyIdx;
						else
							namedValues.mKeyIdx = (int32)keyIdx;

						currentEntry.mLastValue = (int32)valueIdx;
						currentEntry.mLastKey = (int32)keyIdx;
						mKeys.Add(key);
						mNextKeys.Add(-1);
						mValues.Add(aValue);
						mNextValues.Add(-1);
                        
                        keyStartIdx = -1;
                        keyEndIdx = -1;
                    }
                    
                    valueStartIdx = -1;
                    valueEndIdx = -1;
                    valueHadWhitespace = false;
                    hadSlash = false;
					keyHadSlash = false;
                }

                if (atEnd)
                {
                    idx = char8Idx;
					returningValues = true;
                    return values;
                }
            }

			returningValues = true;
            return values;
        }

		class LoadSection
		{
			public Dictionary<String, LoadSection> mSectionDict ~
				{
					if (_ != null)
					{
						for (var val in _.Values)
							delete val;
						delete _;
					}
				};
			public List<LoadSection> mSectionList ~
				{
					if (_ != null)
					{
						for (var val in _)
							delete val;
						delete _;
					}
				};
			public CurrentEntry mCurrentEntry;

			public bool IsArray
			{
				get
				{
					return mSectionList != null;
				}
			}
		}

		Result<void, Error> LoadTOMLHelper(String contentStr, NamedValues values, ref int32 idx, ref int32 lineNum)
		{
			LoadSection loadRoot = scope LoadSection();
			loadRoot.mSectionDict = new Dictionary<String, LoadSection>();
			loadRoot.mCurrentEntry = CurrentEntry(values);
			LoadSection loadSection = loadRoot;
			//CurrentEntry currentEntry = default;

			char8* cPtr = contentStr.CStr();

			char8 NextChar()
			{
				char8 c = cPtr[idx];
				if (c != 0)
					idx++;
				return c;
			}

			char8 PeekNextChar()
			{
				return cPtr[idx];
			}

			void EatWhiteSpace()
			{
				while (true)
				{
					char8 nextC = PeekNextChar();
					if ((nextC != ' ') && (nextC != '\t'))
						return;
					idx++;
				}
			}

			void EatInlineWhiteSpace()
			{
				while (true)
				{
					char8 nextC = PeekNextChar();
					switch (nextC)
					{
					case ' ': break;
					case '\t': break;
					case '\n': break;
					case '\r': break;
					default: return;
					}
					idx++;
				}
			}

			bool ReadToLineEnd()
			{
				bool foundContentChars = false;
				bool inComment = false;

				while (true)
				{
					char8 c = NextChar();
					if (c == 0)
						return !foundContentChars;
					if (c == '\r')
						continue;
					if (c == '\n')
					{
						lineNum++;
                        return !foundContentChars;
					}
					if (inComment)
						continue;
					if (c == '#')
					{
						if (!foundContentChars)
							inComment = true;
						continue;
					}
					if ((c != '\t') && (c != ' '))
						foundContentChars = true;
				}
			}

			bool ReadName(String outName)
			{
				int startIdx = idx;
				char8 c = NextChar();
				if (c == '"')
				{
					bool hadSlash = false;
					bool inSlash = false;
					while (true)
					{
						char8 nextC = NextChar();
						if (nextC == 0)
							return false;
						if (inSlash)
						{
							inSlash = false;
							continue;
						}
						if (nextC == '\\')
						{
							hadSlash = true;
                            inSlash = true;
							continue;
						}
						if (nextC == '"')
							break;
					}
					outName.Append(contentStr, startIdx + 1, idx - startIdx - 2);
					if (hadSlash)
						StringDecode(outName);
					return true;
				}
				else
				{
					while (true)
					{
						char8 nextC = NextChar();
						if ((nextC.IsLetterOrDigit) || (nextC == '_') || (nextC == '-'))
						{
							// Valid part of name, keep going
							continue;
						}
						idx--;
						outName.Append(contentStr, startIdx, idx - startIdx);
						return true;
					}
					
				}
			}

			Object ReadObject()
			{
				EatWhiteSpace();
				char8 c = NextChar();
				if ((c == '"') || (c == '\''))
				{
					String valueStr = scope String(256);
					idx--;
					if (!ReadName(valueStr))
						return null;

					return new:mBumpAllocator String(valueStr);
				}

				if (c == '[')
				{
					// Array

					Values values = new:mBumpAllocator Values();
					CurrentEntry arrayEntry = CurrentEntry(values);

					while (true)
					{
						EatInlineWhiteSpace();
						char8 nextC = NextChar();
						if (nextC == ']')
							break;

						if (!arrayEntry.HasValues)
							idx--;
						else if (nextC != ',')
							return null;

						EatInlineWhiteSpace();
						Object obj = ReadObject();
						if (obj == null)
							return null;
						DoAdd(ref arrayEntry, obj);
					}

					return values;
				}

				if (c == '{')
				{
					// Inline table

					NamedValues namedValues = new:mBumpAllocator NamedValues();
					CurrentEntry tableEntry = CurrentEntry(namedValues);

					while (true)
					{
						EatInlineWhiteSpace();

						char8 nextC = NextChar();
						if (nextC == '}')
							break;

						if (!tableEntry.HasValues)
							idx--;
						else if (nextC != ',')
							return null;

						EatInlineWhiteSpace();
						String key = scope String(256);
						if (!ReadName(key))
							return null;

						EatInlineWhiteSpace();
						char8 eqChar = NextChar();
						if (eqChar != '=')
							return null;

						EatInlineWhiteSpace();
						Object obj = ReadObject();
						if (obj == null)
							return null;

						DoAdd(ref tableEntry, new:mBumpAllocator String(key), obj);
					}

					return namedValues;
				}

				bool isNumber = false;
				if ((c == '-') || (c.IsNumber))
					isNumber = true;

				bool hasDate = false;
				bool hasTime = false;
				bool isFloat = false;
				bool isCompoundDate = false;

				int startIdx = idx - 1;
				ValueStrLoop: while (true)
				{
					char8 nextC = NextChar();
					if ((nextC == ' ') && (hasDate))
					{
						if (!isCompoundDate)
						{
							if (PeekNextChar().IsNumber)
							{
								isCompoundDate = true;
								continue;
							}
						}
					}

					switch (nextC)
					{
					case 0:
						break ValueStrLoop;
					case ' ', '\t', '\r', '\n', ',', ']', '}':
						idx--;
						break ValueStrLoop;
					}

					if (nextC == '-')
						hasDate = true;
					else if (nextC == ':')
						hasTime = true;
					else if (nextC == 'T')
						isCompoundDate = true;
					else if (nextC == '.')
						isFloat = true;
				}

				String value = scope String(256);
				value.Append(contentStr, startIdx, idx - startIdx);

				if ((hasDate) || (hasTime))
				{
					return new:mBumpAllocator String(value);
				}

				if (isNumber)
				{
					for (int i < value.Length)
						if (value[i] == '_')
							value.Remove(i--);

					if (isFloat)
					{
						switch (Float.Parse(value))
						{
						case .Err: return null;
						case .Ok(let num): return new:mBumpAllocator box num;
						}
					}

					switch (Int64.Parse(value))
					{
					case .Err: return null;
					case .Ok(let num): return new:mBumpAllocator box num;
					}
				}

				if (value == "true")
					return new:mBumpAllocator box true;

				if (value == "false")
					return new:mBumpAllocator box false;

				return null;
			}

			while (true)
			{
			    char8 c = NextChar();
				if (c == 0)
				{
					return .Ok;
				}	

				if (c == '#')
				{
					ReadToLineEnd();
					continue;
				}

			    if (c == '\n')
			    {
                    lineNum++;
					continue;
				}
			    if ((c == ' ') || (c == '\t') || (c == '\r'))
					continue;

				if (c == '[')
				{
					bool outerIsArray = PeekNextChar() == '[';
					if (outerIsArray)
						NextChar();

					loadSection = loadRoot;

					// TODO: Allow quoted names
					while (true)
					{
						String entryName = scope String(256);
						EatWhiteSpace();
						if (!ReadName(entryName))
							return .Err(.FormatError(lineNum));

						bool hasMoreParts = false;
						EatWhiteSpace();
						char8 nextC = NextChar();
						hasMoreParts = nextC == '.';

						bool isArray = outerIsArray && !hasMoreParts;

						String* keyPtr;
						LoadSection* valuePtr;
						if (loadSection.mSectionDict.TryAdd(entryName, out keyPtr, out valuePtr))
						{
							var key = new:mBumpAllocator String(entryName);

							*keyPtr = key;
							let newSection = new LoadSection();
							if (isArray)
							{
								newSection.mSectionList = new List<LoadSection>();
                                newSection.mCurrentEntry = CurrentEntry(new:mBumpAllocator Values());
							}
							else
							{
								newSection.mSectionDict = new Dictionary<String, LoadSection>();
                                newSection.mCurrentEntry = CurrentEntry(new:mBumpAllocator NamedValues());
							}

							DoAdd(ref loadSection.mCurrentEntry, key, newSection.mCurrentEntry.mValues);

							loadSection = newSection;
							*valuePtr = loadSection;
						}
						else
						{
                            loadSection = *valuePtr;

							if (hasMoreParts)
							{
								if (loadSection.mCurrentEntry.mValues.IsArray)
								{
									// Use most recent entry
									if (loadSection.mSectionList.Count == 0)
										return .Err(.FormatError(lineNum));
									loadSection = loadSection.mSectionList.Back;
								}
							}
							else
							{
								if (loadSection.mCurrentEntry.mValues.IsArray != isArray)
									return .Err(.FormatError(lineNum));
							}
						}

						if (isArray)
						{
							var newSection = new LoadSection();
							newSection.mSectionDict = new Dictionary<String, LoadSection>();
							newSection.mCurrentEntry = CurrentEntry(new:mBumpAllocator NamedValues());
							loadSection.mSectionList.Add(newSection);
							DoAdd(ref loadSection.mCurrentEntry, newSection.mCurrentEntry.mValues);
							loadSection = newSection;
						}
						

						if (hasMoreParts)
						{
							// Go deeper!
							continue;
						}

						if (nextC == ']')
						{
							if ((outerIsArray) && (NextChar() != ']'))
								return .Err(.ExpectedArrayNameEnd);
							break;
						}

						return .Err(.FormatError(lineNum));
					}
					continue;
				}

				// It's a value
				String key = scope String(256);
				idx--;
				if (!ReadName(key))
					return .Err(.FormatError(lineNum));

				EatWhiteSpace();
				char8 eqChar = NextChar();
				if (eqChar != '=')
					return .Err(.FormatError(lineNum));

				Object obj = ReadObject();
				if (obj == null)
					return .Err(.FormatError(lineNum));

				DoAdd(ref loadSection.mCurrentEntry, new:mBumpAllocator String(key), obj);

				if (!ReadToLineEnd())
					return .Err(.FormatError(lineNum));
			}
		}

		Result<void, Error> LoadXMLHelper(String contentStr, Values values, ref int32 idx, ref int32 lineNum)
		{
			LoadSection loadRoot = scope LoadSection();
			loadRoot.mSectionDict = new Dictionary<String, LoadSection>();
			loadRoot.mCurrentEntry = CurrentEntry(values);
			//LoadSection loadSection = loadRoot;
			//CurrentEntry currentEntry = default;

			char8* cPtr = contentStr.CStr();

			char8 NextChar()
			{
				char8 c = cPtr[idx];
				if (c != 0)
					idx++;
				return c;
			}

			char8* GetCharPtr()
			{
				return &cPtr[idx];
			}

			char8 PeekNextChar()
			{
				return cPtr[idx];
			}

			void EatWhiteSpace()
			{
				while (true)
				{
					char8 nextC = PeekNextChar();
					if ((nextC != ' ') && (nextC != '\t'))
						return;
					idx++;
				}
			}

			void ReadSection(ref CurrentEntry arrayEntry)
			{
				char8* dataStart = null;

				void FlushData()
				{
					if (dataStart != null)
					{
						StringView valueSV = StringView(dataStart, GetCharPtr() - dataStart - 1);
						valueSV.Trim();
						String value = new:mBumpAllocator String(valueSV);
						DoAdd(ref arrayEntry, value);
					}

					dataStart = null;
				}

				MainLoop: while (true)
				{
				    char8 c = NextChar();
					if (c == 0)
					{
						break;
					}

					if (c == '<')
					{
						FlushData();

						EatWhiteSpace();
						c = PeekNextChar();
						if (c == '/')
						{
							// Is closing
							while (true)
							{
								c = NextChar();
								if ((c == 0) || (c == '>'))
									return;
							}
						}

						NamedValues childNamedValues = null;
						CurrentEntry childTableEntry = default;

						void EnsureChildEntry()
						{
							if (childNamedValues != null)
								return;
							childNamedValues = new:mBumpAllocator NamedValues();
							childTableEntry = CurrentEntry(childNamedValues);
							DoAdd(ref arrayEntry, childNamedValues);
						}

						char8* namePtr = null;
						char8* nameEndPtr = null;
						char8* equalPtr = null;
						char8* valuePtr = null;

						while (true)
						{
							c = NextChar();
							if (c.IsWhiteSpace)
							{
								if ((namePtr != null) && (nameEndPtr == null))
									nameEndPtr = GetCharPtr() - 1;
								continue;
							}

							if (valuePtr != null)
							{
								if (c == '"')
								{
									EnsureChildEntry();
									StringView name = StringView(namePtr, nameEndPtr - namePtr + 1);
									name.Trim();
									StringView valueSV = StringView(valuePtr, GetCharPtr() - valuePtr - 1);
									String value = new:mBumpAllocator String(valueSV);
									DoAdd(ref childTableEntry, name, value);

									namePtr = null;
									nameEndPtr = null;
									equalPtr = null;
									valuePtr = null;
									continue;
								}
								continue;
							}

							if ((c == '?') || (c == '/'))
							{
								// Wait for close. Not nested.
								while (true)
								{
									c = NextChar();
									if ((c == 0) || (c == '>'))
										continue MainLoop;
								}
							}

							if (c == '>')
							{
								// Closing, but we're nested
								EnsureChildEntry();
								Values childArrayValues = new:mBumpAllocator Values();
								CurrentEntry childArrayEntry = CurrentEntry(childArrayValues);
								DoAdd(ref childTableEntry, ".", childArrayValues);

								ReadSection(ref childArrayEntry);
								continue MainLoop;
							}

							if (namePtr == null)
							{
								namePtr = GetCharPtr() - 1;
								continue;
							}

							if (equalPtr == null)
							{
								if (c == '=')
								{
									equalPtr = GetCharPtr() - 1;
									if (nameEndPtr == null)
										nameEndPtr = equalPtr - 1;
									continue;
								}
							}
							else
							{
								if (c == '"')
								{
									valuePtr = GetCharPtr();
									continue;
								}
							}

							if (nameEndPtr == null)
								continue;

							// Flush
							StringView name = StringView(namePtr, nameEndPtr - namePtr + 1);
							name.Trim();

							if (name.IsEmpty)
								continue;

							EnsureChildEntry();
							if (childTableEntry.mLastKey == -1)
							{
								Object value = new:mBumpAllocator String(name);
								DoAdd(ref childTableEntry, "", value);
							}
							else
							{
								Object value = new:mBumpAllocator box true;
								DoAdd(ref childTableEntry, name, value);
							}

							namePtr = null;
							nameEndPtr = null;
							idx--;
							continue;
						}
					}

					if ((!c.IsWhiteSpace) && (dataStart == null))
						dataStart = GetCharPtr() - 1;
				}

				FlushData();
			}

			ReadSection(ref loadRoot.mCurrentEntry);

			return .Ok;
		}

		protected Result<void, Error> Load()
		{
			EnsureHasData();
			int guessItems = mSource.Length / 32;
			mValues.Reserve(guessItems);
			mKeys.Reserve(guessItems);
			mNextValues.Reserve(guessItems);
			mNextKeys.Reserve(guessItems);

			bool isJson = false;
			bool isXml = false;
			bool mayBeJsonArray = false;
			for (char8 c in mSource.RawChars)
			{
				if (c.IsWhiteSpace)
					continue;

				if (mayBeJsonArray)
				{
					if (c == '[')
						continue; // Still ambiguous
					if ((c == '{') || (c == '"'))
						isJson = true;
					break;
				}
				else
				{
					if (c == '{')
						isJson = true;
					if (c == '<')
						isXml = true;
					if (c == '[')
					{
						mayBeJsonArray = true;
						continue;
					}
					break;
				}
			}

			int32 aLineNum = 1;
			int32 anIdx = 0;
			Object objResult;
			if (isJson)
			{
				let result = LoadJSONHelper(mSource, ref anIdx, ref aLineNum);
				if (result case .Err(var err))
					return .Err(err);
				objResult = result.Get();
			}
			else if (isXml)
			{
				var values = new:mBumpAllocator Values();
				let result = LoadXMLHelper(mSource, values, ref anIdx, ref aLineNum);
				if (result case .Err(var err))
					return .Err(err);
				objResult = values;
			}
			else
			{
				var values = new:mBumpAllocator NamedValues();
				let result = LoadTOMLHelper(mSource, values, ref anIdx, ref aLineNum);
				if (result case .Err(var err))
					return .Err(err);
				objResult = values;
			}

			if (var values = objResult as Values)
			{
				int valIdx = mValues.Count;

				mHeadValues = new:mBumpAllocator Values();
				mHeadValues.mValueIdx = (int32)valIdx;
				mCurrent.mValues = mHeadValues;
				mCurrent.mLastValue = (int32)valIdx;
				mValues.Add(values);
				mNextValues.Add(-1);

				return .Ok;
			}	
			else
			{
				return .Err(Error.FormatError(aLineNum));
			}
		}

		public Result<void, Error> Load(StringView fileName)
		{
			mSource = new:mBumpAllocator StringWithAlloc(mBumpAllocator);
			if (File.ReadAllText(scope String(fileName), mSource, true) case .Err)
			{
				return .Err(Error.FileError);
			}

			return Load();
		}

		public Result<void, Error> LoadFromString(StringView data)
		{
			mSource = new:mBumpAllocator StringWithAlloc(mBumpAllocator);
			mSource.Reserve(data.Length + 1);
			mSource.Append(data);
			return Load();
		}

    }
}

