using System.Diagnostics.Contracts;
using System.Threading;

namespace System.Globalization
{
	class CultureInfo : IFormatProvider
	{
		//public static readonly CultureInfo CurrentCulture = null;

		public CultureData mCultureData = new CultureData() ~ delete _;
		public NumberFormatInfo mNumInfo ~ delete _;

		static CultureInfo sInvariantCultureInfo ~ delete _; // Volatile?
		static CultureInfo sUserDefaultUICulture ~ delete _; // Volatile?
		static CultureInfo sUserDefaultCulture ~ delete _; // Volatile?
		public static CultureInfo mDefaultCultureInfo /*= new CultureInfo()*/ ~ delete _;

		[ThreadStatic]
		private static CultureInfo tlCurrentCulture;
		[ThreadStatic]
		private static CultureInfo tlCurrentUICulture;

		String m_name ~ delete _;
		bool m_isInherited;
		DateTimeFormatInfo dateTimeInfo ~ delete _;
		CultureData m_cultureData ~ delete _;
		Calendar calendar ~ delete _;
		bool m_isReadOnly;

		// LOCALE constants of interest to us internally and privately for LCID functions
		// (ie: avoid using these and use names if possible)
		private const int LOCALE_NEUTRAL              = 0x0000;
		private const int LOCALE_USER_DEFAULT         = 0x0400;
		private const int LOCALE_SYSTEM_DEFAULT       = 0x0800;
		private const int LOCALE_CUSTOM_DEFAULT       = 0x0c00;
		private const int LOCALE_CUSTOM_UNSPECIFIED   = 0x1000;
		private const int LOCALE_INVARIANT            = 0x007F;
		private const int LOCALE_TRADITIONAL_SPANISH  = 0x040a;

		public static CultureInfo DefaultThreadCurrentCulture
		{
			get
			{
				return mDefaultCultureInfo;
			}
		}

		public static CultureInfo UserDefaultCulture
		{
			get
			{
				return sUserDefaultCulture;
			}
		}

		public static CultureInfo InvariantCulture
		{
			get
			{
				return sInvariantCultureInfo;
			}
		}

		public StringView Name
		{
			get
			{
				return m_name;
			}
		}

		public NumberFormatInfo NumberFormat
		{
			get
			{
				if (mNumInfo == null)
				{
					var numInfo = new NumberFormatInfo(mCultureData);
					if (var prevValue = Interlocked.CompareExchange(ref mNumInfo, null, numInfo))
					{
						// This was already set - race condition
						delete numInfo;
						return prevValue;
					}
				}
				return mNumInfo;
			}
		}

		public static CultureInfo CurrentCulture
		{
		    get
			{
				if (Compiler.IsComptime)
					return InitUserDefaultCulture();
				if (tlCurrentCulture == null)
					tlCurrentCulture = CultureInfo.DefaultThreadCurrentCulture ?? CultureInfo.UserDefaultCulture;
		        return tlCurrentCulture;
		    }

		    set
			{
				Contract.Assert(value != null);
		        Contract.EndContractBlock();
		        tlCurrentCulture = value;
		    }
		}

		public static CultureInfo CurrentUICulture
		{
		    get
			{
				if (tlCurrentUICulture == null)
					tlCurrentUICulture = CultureInfo.DefaultThreadCurrentCulture ?? CultureInfo.UserDefaultCulture;
		        return tlCurrentUICulture;
		    }

		    set
			{
				Contract.Assert(value != null);
		        Contract.EndContractBlock();
		        tlCurrentUICulture = value;
		    }
		}

		public virtual Calendar Calendar {
		    get {
		        
		        if (calendar == null)
				{
		            //Contract.Assert(this.m_cultureData.CalendarIds.Length > 0, "this.m_cultureData.CalendarIds.Length > 0");
		            // Get the default calendar for this culture.  Note that the value can be
		            // from registry if this is a user default culture.
		            Calendar newObj = this.m_cultureData.[Friend]DefaultCalendar;

		            Interlocked.Fence();
		            newObj.[Friend]SetReadOnlyState(m_isReadOnly);
		            calendar = newObj;
		        }
		        return (calendar);
		    }
		}

		public virtual DateTimeFormatInfo DateTimeFormat
		{
		    get
			{
		        if (dateTimeInfo == null)
				{
		            // Change the calendar of DTFI to the specified calendar of this CultureInfo.
		            DateTimeFormatInfo temp = new DateTimeFormatInfo(
		                this.m_cultureData, this.Calendar);
		            temp.[Friend]m_isReadOnly = m_isReadOnly;
		            Interlocked.Fence();
		            dateTimeInfo = temp;
		        }
		        return (dateTimeInfo);
		    }

		    set {
		        /*if (value == null) {
		            throw new ArgumentNullException("value",
		                Environment.GetResourceString("ArgumentNull_Obj"));
		        }
		        Contract.EndContractBlock();*/
		        VerifyWritable();
		        dateTimeInfo = value;
		    }
		}

		//
		// The CultureData  instance that reads the data provided by our CultureData class.
		//
		//Using a field initializer rather than a static constructor so that the whole class can be lazy
		//init.
		private static readonly bool init = Init();
		private static bool Init()
		{

		    if (sInvariantCultureInfo == null) 
		    {
		        CultureInfo temp = new CultureInfo("", false);
		        temp.m_isReadOnly = true;
		        sInvariantCultureInfo = temp;
		    }
		    // First we set it to Invariant in case someone needs it before we're done finding it.
		    // For example, if we throw an exception in InitUserDefaultCulture, we will still need an valid
		    // s_userDefaultCulture to be used in Thread.CurrentCulture.
		    sUserDefaultCulture = sUserDefaultUICulture = sInvariantCultureInfo;

		    sUserDefaultCulture = InitUserDefaultCulture();
		    sUserDefaultUICulture = InitUserDefaultUICulture();
		    return true;
		}

		static CultureInfo InitUserDefaultCulture()
		{
		    String strDefault = scope String();
			GetDefaultLocaleName(LOCALE_USER_DEFAULT, strDefault);
		    if (strDefault.IsEmpty)
		    {
		        GetDefaultLocaleName(LOCALE_SYSTEM_DEFAULT, strDefault);

		        if (strDefault.IsEmpty)
		        {
		            // If system default doesn't work, keep using the invariant
		            return (CultureInfo.InvariantCulture);
		        }
		    }
		    CultureInfo temp = GetCultureByName(strDefault, true);
		    temp.m_isReadOnly = true;

		    return (temp);
		}

		static CultureInfo InitUserDefaultUICulture()
		{
		    String strDefault = scope .();
			GetUserDefaultUILanguage(strDefault);

		    // In most of cases, UserDefaultCulture == UserDefaultUICulture, so we should use the same instance if possible.
		    if (strDefault == UserDefaultCulture.Name)
		    {
		        return (UserDefaultCulture);
		    }

		    CultureInfo temp = GetCultureByName( strDefault, true);

		    if (temp == null)
		    {
		        return (CultureInfo.InvariantCulture);
		    }

		    temp.m_isReadOnly = true;

		    return (temp);
		}

		private this()
		{

		}

		public this(String name) : this(name, true)
		{
		}


		public this(String name, bool useUserOverride)
		{
		    // Get our data providing record
		    this.m_cultureData = CultureData.[Friend]GetCultureData(name, useUserOverride);

		    if (this.m_cultureData == null) {
		        //throw new CultureNotFoundException("name", name, Environment.GetResourceString("Argument_CultureNotSupported"));
				Runtime.FatalError();
		    }

		    this.m_name = new String(this.m_cultureData.[Friend]CultureName);
		    this.m_isInherited = (this.GetType() != typeof(System.Globalization.CultureInfo));
		}

		private void VerifyWritable()
		{
			Runtime.Assert(!m_isReadOnly);
		}

		public static this()
		{
			//sInvariantCultureInfo = new CultureInfo();
		}

		private static volatile bool s_isTaiwanSku;
		private static volatile bool s_haveIsTaiwanSku;
		static bool IsTaiwanSku
		{
		    get
		    {
		        if (!s_haveIsTaiwanSku)
		        {
					var language = scope String();
					GetSystemDefaultUILanguage(language);
		            s_isTaiwanSku = (language == "zh-TW");
		            s_haveIsTaiwanSku = true;
		        }
		        return (bool)s_isTaiwanSku;
		    }
		}

		static bool GetSystemDefaultUILanguage(String outStr)
		{
			outStr.Append("EN-us");
			return true;
		}

		public virtual Object GetFormat(Type formatType)
		{
		    if (formatType == typeof(NumberFormatInfo))
			{
		        return (NumberFormat);
		    }
		    if (formatType == typeof(DateTimeFormatInfo))
			{
		        return (DateTimeFormat);
		    }
		    return (null);
		}

		private static void GetDefaultLocaleName(int localeType, String outName)
		{
		    /*Contract.Assert(localeType == LOCALE_USER_DEFAULT || localeType == LOCALE_SYSTEM_DEFAULT, "[CultureInfo.GetDefaultLocaleName] localeType must be LOCALE_USER_DEFAULT or LOCALE_SYSTEM_DEFAULT");

		    string localeName = null;
		    if (InternalGetDefaultLocaleName(localeType, JitHelpers.GetStringHandleOnStack(ref localeName)))
		    {
		        return localeName;
		    }
		    return string.Empty;*/
			outName.Append("EN-us");
		}

		// Gets a cached copy of the specified culture from an internal hashtable (or creates it
		// if not found).  (Named version)
		public static CultureInfo GetCultureInfo(StringView name)
		{
		    // Make sure we have a valid, non-zero length string as name
		    CultureInfo retval = GetCultureInfoHelper(0, name, .());
		    return retval;
		}

		private static CultureInfo GetCultureByName(String name, bool userOverride)
		{           
		    // Try to get our culture
		    return userOverride ? new CultureInfo(name) : CultureInfo.GetCultureInfo(name);
		}

		// Helper function both both overloads of GetCachedReadOnlyCulture.  If lcid is 0, we use the name.
		// If lcid is -1, use the altName and create one of those special SQL cultures.
		static CultureInfo GetCultureInfoHelper(int lcid, StringView name, StringView altName)
		{
			return new CultureInfo();
		}

		private static void GetUserDefaultUILanguage(String langName)
		{
		    //NotImplemented
		}

		static Calendar GetCalendarInstance(int calType)
		{
		    if (calType==Calendar.[Friend]CAL_GREGORIAN) {
		        return new GregorianCalendar();
		    }
			Runtime.NotImplemented();
		    //return GetCalendarInstanceRare(calType);
		}
	}
}
