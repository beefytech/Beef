// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

// ==++==
//
//   Copyright (c) Microsoft Corporation.  All rights reserved.
//
// ==--==
namespace System.Globalization {
    using System.Text;
    using System;
    using System.Diagnostics.Contracts;
	using System.Diagnostics;
	using System.Threading;
    //
    // Property             Default Description
    // PositiveSign           '+'   Character used to indicate positive values.
    // NegativeSign           '-'   Character used to indicate negative values.
    // NumberDecimalSeparator '.'   The character used as the decimal separator.
    // NumberGroupSeparator   ','   The character used to separate groups of
    //                              digits to the left of the decimal point.
    // NumberDecimalDigits    2     The default number of decimal places.
    // NumberGroupSizes       3     The number of digits in each group to the
    //                              left of the decimal point.
    // NaNSymbol             "NaN"  The string used to represent NaN values.
    // PositiveInfinitySymbol"Infinity" The string used to represent positive
    //                              infinities.
    // NegativeInfinitySymbol"-Infinity" The string used to represent negative
    //                              infinities.
    //
    //
    //
    // Property                  Default  Description
    // CurrencyDecimalSeparator  '.'      The character used as the decimal
    //                                    separator.
    // CurrencyGroupSeparator    ','      The character used to separate groups
    //                                    of digits to the left of the decimal
    //                                    point.
    // CurrencyDecimalDigits     2        The default number of decimal places.
    // CurrencyGroupSizes        3        The number of digits in each group to
    //                                    the left of the decimal point.
    // CurrencyPositivePattern   0        The format of positive values.
    // CurrencyNegativePattern   0        The format of negative values.
    // CurrencySymbol            "$"      String used as local monetary symbol.
    //

	struct OwnedString
	{
		public String mString;
		public bool mOwned;

		public this(String unownedString)
		{
			mString = unownedString;
			mOwned = false;
		}

		public this(String str, bool owned)
		{
			mString = str;
			mOwned = owned;
		}

		public void Dispose() mut
		{
			if (mOwned)
				delete mString;
		}

		public void Set(StringView value) mut
		{
			if (!mOwned)
			{
				mString = new String(value);
				mOwned = true;
			}
			else
				mString.Set(value);
		}
	}

    public class NumberFormatInfo : /*ICloneable,*/ IFormatProvider
    {
        // invariantInfo is constant irrespective of your current culture.
        private static volatile NumberFormatInfo invariantInfo;

        // READTHIS READTHIS READTHIS
        // This class has an exact mapping onto a native structure defined in COMNumber.cpp
        // DO NOT UPDATE THIS WITHOUT UPDATING THAT STRUCTURE. IF YOU ADD BOOL, ADD THEM AT THE END.
        // ALSO MAKE SURE TO UPDATE mscorlib.h in the VM directory to check field offsets.
        // READTHIS READTHIS READTHIS
        protected int32[] numberGroupSizes = new int32[] (3) ~ delete _;
        protected int32[] currencyGroupSizes = new int32[] (3) ~ delete _;
        protected int32[] percentGroupSizes = new int32[] (3) ~ delete _;
        protected OwnedString positiveSign = .("+") ~ _.Dispose();
        protected OwnedString negativeSign = .("-") ~ _.Dispose();
        protected OwnedString numberDecimalSeparator = .(".") ~ _.Dispose();
        protected OwnedString numberGroupSeparator = .(",") ~ _.Dispose();
        protected OwnedString currencyGroupSeparator = .(",") ~ _.Dispose();;
        protected OwnedString currencyDecimalSeparator = .(".") ~ _.Dispose();
        protected OwnedString currencySymbol = .("\u{00a4}") ~ _.Dispose();  // U+00a4 is the symbol for International Monetary Fund.
        // The alternative currency symbol used in Win9x ANSI codepage, that can not roundtrip between ANSI and Unicode.
        // Currently, only ja-JP and ko-KR has non-null values (which is U+005c, backslash)
        // NOTE: The only legal values for this string are null and "\u005c"
        protected String ansiCurrencySymbol = null;
        protected OwnedString nanSymbol = .("NaN") ~ _.Dispose();
        protected OwnedString positiveInfinitySymbol = .("Infinity") ~ _.Dispose();
        protected OwnedString negativeInfinitySymbol = .("-Infinity") ~ _.Dispose();
        protected OwnedString percentDecimalSeparator = .(".") ~ _.Dispose();
        protected OwnedString percentGroupSeparator = .(",") ~ _.Dispose();
        protected OwnedString percentSymbol = .("%") ~ _.Dispose();
        protected OwnedString perMilleSymbol = .("\u{2030}") ~ _.Dispose();

        protected String[] nativeDigits = new .[] ("0", "1", "2", "3", "4", "5", "6", "7", "8", "9") ~ DeleteNativeDigits();

        protected int32 numberDecimalDigits = 2;
        protected int32 currencyDecimalDigits = 2;
        protected int32 currencyPositivePattern = 0;
        protected int32 currencyNegativePattern = 0;
        protected int32 numberNegativePattern = 1;
        protected int32 percentPositivePattern = 0;
        protected int32 percentNegativePattern = 0;
        protected int32 percentDecimalDigits = 2;

        protected int32 digitSubstitution = 1; // DigitShapes.None

        protected bool isReadOnly=false;
        
        // Is this NumberFormatInfo for invariant culture?
        protected bool m_isInvariant=false;

		void DeleteNativeDigits()
		{
			if ((!nativeDigits.IsEmpty) && ((Object)nativeDigits[0] != "0"))
			{
				for (var str in nativeDigits)
					delete str;
			}
			delete nativeDigits;
		}

        public this() : this(null)
		{
        }

        static private void VerifyDecimalSeparator(String decSep, String propertyName)
		{
            if (decSep==null) {
                /*throw new ArgumentNullException(propertyName,
                        Environment.GetResourceString("ArgumentNull_String"));*/
            }

            if (decSep.Length==0) {
                /*throw new ArgumentException(Environment.GetResourceString("Argument_EmptyDecString"));*/
            }
            Contract.EndContractBlock();

        }

        static private void VerifyGroupSeparator(String groupSep, String propertyName) {
            if (groupSep==null) {
                /*throw new ArgumentNullException(propertyName,
                        Environment.GetResourceString("ArgumentNull_String"));*/
            }
            Contract.EndContractBlock();
        }

        static private void VerifyNativeDigits(String [] nativeDig, String propertyName) {
            if (nativeDig==null) {
                /*throw new ArgumentNullException(propertyName,
                        Environment.GetResourceString("ArgumentNull_Array"));*/
            }

            if (nativeDig.Count != 10)
            {
                //throw new ArgumentException(Environment.GetResourceString("Argument_InvalidNativeDigitCount"), propertyName);
            }
            Contract.EndContractBlock();

            for(int i = 0; i < nativeDig.Count; i++)
            {
                if (nativeDig[i] == null)
                {
                    /*throw new ArgumentNullException(propertyName,
                            Environment.GetResourceString("ArgumentNull_ArrayValue"));*/
                }

            
                if (nativeDig[i].Length != 1) {
                    if(nativeDig[i].Length != 2) {
                        // Not 1 or 2 UTF-16 code points
                        /*throw new ArgumentException(Environment.GetResourceString("Argument_InvalidNativeDigitValue"), propertyName);*/
                    }
					/*else if (!char.IsSurrogatePair(nativeDig[i][0], nativeDig[i][1])) {
                        // 2 UTF-6 code points, but not a surrogate pair
                        /*throw new ArgumentException(Environment.GetResourceString("Argument_InvalidNativeDigitValue"), propertyName);*/
                    }*/
                }

                /*if (CharUnicodeInfo.GetDecimalDigitValue(nativeDig[i], 0) != i &&
                    CharUnicodeInfo.GetUnicodeCategory(nativeDig[i], 0) != UnicodeCategory.PrivateUse) {
                    // Not the appropriate digit according to the Unicode data properties
                    // (Digit 0 must be a 0, etc.).
                    throw new ArgumentException(Environment.GetResourceString("Argument_InvalidNativeDigitValue"), propertyName);
                }*/
            }
        }


        // We aren't persisting dataItem any more (since its useless & we weren't using it),
        // Ditto with m_useUserOverride.  Don't use them, we use a local copy of everything.
        public this(CultureData cultureData)
        {
            if (cultureData != null)
            {
                // We directly use fields here since these data is coming from data table or Win32, so we
                // don't need to verify their values (except for invalid parsing situations).
                cultureData.[Friend]GetNFIValues(this);

                if (cultureData.IsInvariantCulture)
                {
                    // For invariant culture
                    this.m_isInvariant = true;
                }
            }
        }

        private void VerifyWritable()
		{
            if (isReadOnly)
			{
				Runtime.FatalError("Read only");
                //throw new InvalidOperationException(Environment.GetResourceString("InvalidOperation_ReadOnly"));
            }
            Contract.EndContractBlock();
        }

        // Returns a default NumberFormatInfo that will be universally
        // supported and constant irrespective of the current culture.
        // Used by FromString methods.
        //

        /*public static NumberFormatInfo InvariantInfo {
            get {
                /*if (invariantInfo == null) {
                    // Lazy create the invariant info. This cannot be done in a .cctor because exceptions can
                    // be thrown out of a .cctor stack that will need this.
                    NumberFormatInfo nfi = new NumberFormatInfo();
                    nfi.m_isInvariant = true;
                    invariantInfo = ReadOnly(nfi);
                }
                return invariantInfo;*/

            }
        }*/


        public static NumberFormatInfo GetInstance(IFormatProvider formatProvider)
		{
            // Fast case for a regular CultureInfo
            NumberFormatInfo info;
            CultureInfo cultureProvider = formatProvider as CultureInfo;
            if (cultureProvider != null && !cultureProvider.[Friend]m_isInherited)
			{
                info = cultureProvider.mNumInfo;
                if (info != null) {
                    return info;
                }
                else {
                    return cultureProvider.NumberFormat;
                }
            }
            // Fast case for an NFI;
            info = formatProvider as NumberFormatInfo;
            if (info != null) {
                return info;
            }
            if (formatProvider != null) {
                info = formatProvider.GetFormat(typeof(NumberFormatInfo)) as NumberFormatInfo;
                if (info != null) {
                    return info;
                }
            }
            return CurrentInfo;
        }

        /*public Object Clone()
		{
            /*NumberFormatInfo n = (NumberFormatInfo)MemberwiseClone();
            n.isReadOnly = false;
            return n;*/
			return null;
        }*/


         public int32 CurrencyDecimalDigits
		{
            get { return currencyDecimalDigits; }
            set {
                if (value < 0 || value > 99)
				{
                    /*throw new ArgumentOutOfRangeException(
                                "CurrencyDecimalDigits",
                                String.Format(
                                    CultureInfo.CurrentCulture,
                                    Environment.GetResourceString("ArgumentOutOfRange_Range"),
                                    0,
                                    99));*/
                }
                Contract.EndContractBlock();
                VerifyWritable();
                currencyDecimalDigits = (.)value;
            }
        }


         public String CurrencyDecimalSeparator {
            get { return currencyDecimalSeparator.mString; }
            set {
                VerifyWritable();
                VerifyDecimalSeparator(value, "CurrencyDecimalSeparator");
                currencyDecimalSeparator.Set(value);
            }
        }


        public bool IsReadOnly {
            get {
                return isReadOnly;
            }
        }

        //
        // Check the values of the groupSize array.
        //
        // Every element in the groupSize array should be between 1 and 9
        // excpet the last element could be zero.
        //
        static protected void CheckGroupSize(String propName, int[] groupSize)
        {
            for (int i = 0; i < groupSize.Count; i++)
            {
                if (groupSize[i] < 1)
                {
                    if (i == groupSize.Count - 1 && groupSize[i] == 0)
                        return;
                    /*throw new ArgumentException(Environment.GetResourceString("Argument_InvalidGroupSize"), propName);*/
                }
                else if (groupSize[i] > 9)
                {
                    /*throw new ArgumentException(Environment.GetResourceString("Argument_InvalidGroupSize"), propName);*/
                }
            }
        }


        public Span<int32> CurrencyGroupSizes
		{
            get
			{
                //return ((int[])currencyGroupSizes.Clone());
				return currencyGroupSizes;
            }
            set
			{
                VerifyWritable();

				delete currencyGroupSizes;
				currencyGroupSizes = new int32[value.Length];
				for (int i < value.Length)
					currencyGroupSizes[i] = value[i];
                
                /*Int32[] inputSizes = (Int32[])value.Clone();
                CheckGroupSize("CurrencyGroupSizes", inputSizes);
                currencyGroupSizes = inputSizes;*/
            }

        }



        public Span<int32> NumberGroupSizes
		{
            get
			{
                return numberGroupSizes;
            }
            set
			{
                VerifyWritable();

				delete numberGroupSizes;
				numberGroupSizes = new int32[value.Length];
				for (int i < value.Length)
					numberGroupSizes[i] = value[i];
            }
        }


        public Span<int32> PercentGroupSizes
		{
            get
			{
                return percentGroupSizes;
            }
            set
			{
                VerifyWritable();

				delete percentGroupSizes;
				percentGroupSizes = new int32[value.Length];
				for (int i < value.Length)
					percentGroupSizes[i] = value[i];
            }

        }


         public StringView CurrencyGroupSeparator {
            get { return currencyGroupSeparator.mString; }
            set {
                VerifyWritable();
                //VerifyGroupSeparator(value, "CurrencyGroupSeparator");
				currencyGroupSeparator.Set(value);
            }
        }


         public StringView CurrencySymbol {
            get { return currencySymbol.mString; }
            set {
                VerifyWritable();
                currencySymbol.Set(value);
            }
        }

        // Returns the current culture's NumberFormatInfo.  Used by Parse methods.
        //

        public static NumberFormatInfo CurrentInfo {
            get {
                /*System.Globalization.CultureInfo culture = System.Threading.Thread.CurrentThread.CurrentCulture;
                if (!culture.m_isInherited) {
                    NumberFormatInfo info = culture.numInfo;
                    if (info != null) {
                        return info;
                    }
                }
                return ((NumberFormatInfo)culture.GetFormat(typeof(NumberFormatInfo)));*/
				return CultureInfo.CurrentCulture.NumberFormat;
            }
        }


        public StringView NaNSymbol
		{
            get
			 {
                return nanSymbol.mString;
            }
            set {
                /*if (value == null)
				{
                    throw new ArgumentNullException("NaNSymbol",
                        Environment.GetResourceString("ArgumentNull_String"));
                }
                Contract.EndContractBlock();
                VerifyWritable();*/
				//Contract.Assert(value != null);

				nanSymbol.Set(value);
            }
        }


		
         public int CurrencyNegativePattern {
            get { return currencyNegativePattern; }
            set {
				Debug.Assert((value >= 0) && (value <= 15));
                VerifyWritable();
                currencyNegativePattern = (.)value;
            }
        }


         public int NumberNegativePattern {
            get { return numberNegativePattern; }
            set {
				Debug.Assert((value >= 0) && (value <= 4));
                VerifyWritable();
                numberNegativePattern = (.)value;
            }
        }


         public int PercentPositivePattern {
            get { return percentPositivePattern; }
            set {
				Debug.Assert((value >= 0) && (value <= 3));
                VerifyWritable();
                percentPositivePattern = (.)value;
            }
        }


         public int PercentNegativePattern {
            get { return percentNegativePattern; }
            set {
                Debug.Assert((value >= 0) && (value <= 11));
                VerifyWritable();
                percentNegativePattern = (.)value;
            }
        }


         public StringView NegativeInfinitySymbol {
            get {
                return negativeInfinitySymbol.mString;
            }
            set {
                VerifyWritable();
                negativeInfinitySymbol.Set(value);
            }
        }


         public StringView NegativeSign {
            get { return negativeSign.mString; }
            set {
                VerifyWritable();
                negativeSign.Set(value);
            }
        }


         public int32 NumberDecimalDigits {
            get { return numberDecimalDigits; }
            set {
				Debug.Assert((value >= 0) && (value <= 99));
                VerifyWritable();
                numberDecimalDigits = (.)value;
            }
        }


         public StringView NumberDecimalSeparator {
            get { return numberDecimalSeparator.mString; }
            set {
                VerifyWritable();
                numberDecimalSeparator.Set(value);
            }
        }


         public StringView NumberGroupSeparator {
            get { return numberGroupSeparator.mString; }
            set {
                VerifyWritable();
                numberGroupSeparator.Set(value);
            }
        }


         public int CurrencyPositivePattern {
            get { return currencyPositivePattern; }
            set {
				Debug.Assert((value >= 0) && (value <= 3));
                VerifyWritable();
                currencyPositivePattern = (.)value;
            }
        }


         public StringView PositiveInfinitySymbol {
            get {
                return positiveInfinitySymbol.mString;
            }
            set {
                VerifyWritable();
                positiveInfinitySymbol.Set(value);
            }
        }


         public StringView PositiveSign {
            get { return positiveSign.mString; }
            set {
                VerifyWritable();
                positiveSign.Set(value);
            }
        }


         public int32 PercentDecimalDigits {
            get { return percentDecimalDigits; }
            set {
				Debug.Assert((value >= 0) && (value <= 99));
                VerifyWritable();
                percentDecimalDigits = (.)value;
            }
        }


         public StringView PercentDecimalSeparator
		{
            get { return percentDecimalSeparator.mString; }
            set {
                VerifyWritable();
                percentDecimalSeparator.Set(value);
            }
        }


         public StringView PercentGroupSeparator
		{
            get { return percentGroupSeparator.mString; }
            set {
                VerifyWritable();
                percentGroupSeparator.Set(value);
            }
        }


         public StringView PercentSymbol
		{
            get {
                return percentSymbol.mString;
            }
            set {
                VerifyWritable();
                percentSymbol.Set(value);
            }
        }


         public StringView PerMilleSymbol {
            get { return perMilleSymbol.mString; }
            set {
                VerifyWritable();
                perMilleSymbol.Set(value);
            }
        }

        public Span<String> NativeDigits
        {
            get { return nativeDigits; }
            set
            {
                VerifyWritable();

				DeleteNativeDigits();
				nativeDigits = new String[value.Length];
				for (int i < value.Length)
					nativeDigits[i] = new String(value[i]);
            }
        }

/*#if !FEATURE_CORECLR
        [System.Runtime.InteropServices.ComVisible(false)]
        public DigitShapes DigitSubstitution
        {
            get { return (DigitShapes)digitSubstitution; }
            set
            {
                VerifyWritable();
                VerifyDigitSubstitution(value, "DigitSubstitution");
                digitSubstitution = (int)value;
            }
        }
#endif // !FEATURE_CORECLR*/

        public Object GetFormat(Type formatType) {
            return formatType == typeof(NumberFormatInfo)? this: null;
        }

        public static NumberFormatInfo ReadOnly(NumberFormatInfo nfi)
		{
			Runtime.FatalError();

            /*if (nfi == null) {
                throw new ArgumentNullException("nfi");
            }
            Contract.EndContractBlock();
            if (nfi.IsReadOnly) {
                return (nfi);
            }
            NumberFormatInfo info = (NumberFormatInfo)(nfi.MemberwiseClone());
            info.isReadOnly = true;
            return info;*/
        }

        // private const NumberStyles InvalidNumberStyles = unchecked((NumberStyles) 0xFFFFFC00);
        private const NumberStyles InvalidNumberStyles = ~(.AllowLeadingWhite | .AllowTrailingWhite
                                                           | .AllowLeadingSign | .AllowTrailingSign
                                                           | .AllowParentheses | .AllowDecimalPoint
                                                           | .AllowThousands | .AllowExponent
                                                           | .AllowCurrencySymbol | .AllowHexSpecifier | .Hex);

        /*internal static void ValidateParseStyleInteger(NumberStyles style) {
            // Check for undefined flags
            if ((style & InvalidNumberStyles) != 0) {
                throw new ArgumentException(Environment.GetResourceString("Argument_InvalidNumberStyles"), "style");
            }
            Contract.EndContractBlock();
            if ((style & NumberStyles.AllowHexSpecifier) != 0) { // Check for hex number
                if ((style & ~NumberStyles.HexNumber) != 0) {
                    throw new ArgumentException(Environment.GetResourceString("Arg_InvalidHexStyle"));
                }
            }
        }

        internal static void ValidateParseStyleFloatingPoint(NumberStyles style) {
            // Check for undefined flags
            if ((style & InvalidNumberStyles) != 0) {
                throw new ArgumentException(Environment.GetResourceString("Argument_InvalidNumberStyles"), "style");
            }
            Contract.EndContractBlock();
            if ((style & NumberStyles.AllowHexSpecifier) != 0) { // Check for hex number
                throw new ArgumentException(Environment.GetResourceString("Arg_HexStyleNotSupported"));
            }
        }*/
    } // NumberFormatInfo
}









