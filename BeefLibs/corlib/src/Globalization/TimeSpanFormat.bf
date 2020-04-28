// ==++==
// 
//   Copyright (c) Microsoft Corporation.  All rights reserved.
// 
// ==--==
// <OWNER>[....]</OWNER>
// 
namespace System.Globalization {
    using System.Text;
    using System;
    using System.Diagnostics.Contracts;
    using System.Globalization;
	using System.Collections.Generic;

    static class TimeSpanFormat
	{
        private static void IntToString(int n, int digits, String outStr)
		{
			((int32)n).[Friend]ToString(outStr, digits);
        }

        protected static readonly FormatLiterals PositiveInvariantFormatLiterals  = TimeSpanFormat.FormatLiterals.[Friend]InitInvariant(false /*isNegative*/) ~ _.Dispose();
        protected static readonly FormatLiterals NegativeInvariantFormatLiterals  = TimeSpanFormat.FormatLiterals.[Friend]InitInvariant(true  /*isNegative*/) ~ _.Dispose();

        protected enum Pattern {
            None    = 0,
            Minimum = 1,
            Full    = 2,
        }  

        //
        //  Format
        //
        //  Actions: Main method called from TimeSpan.ToString
        // 
        protected static Result<void> Format(TimeSpan value, StringView format, IFormatProvider formatProvider, String outStr)
		{
			var format;

            if (format.IsNull || format.Length == 0)
                format = "c";

            // standard formats
            if (format.Length == 1) {               
                char8 f = format[0];

                if (f == 'c' || f == 't' || f == 'T')
                {
					FormatStandard(value, true, format, Pattern.Minimum, outStr);
					return .Ok;
				}
                if (f == 'g' || f == 'G') {
                    Pattern pattern;
                    DateTimeFormatInfo dtfi = DateTimeFormatInfo.GetInstance(formatProvider);

                    if ((int64)value < 0)
                        format = dtfi.[Friend]FullTimeSpanNegativePattern;
                    else
                        format = dtfi.[Friend]FullTimeSpanPositivePattern;
                    if (f == 'g')
                        pattern = Pattern.Minimum;
                    else
                        pattern = Pattern.Full;
                  
                    return FormatStandard(value, false, format, pattern, outStr);
                }
                //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));
				return .Err;
            }

            return FormatCustomized(value, format, DateTimeFormatInfo.GetInstance(formatProvider), outStr);
        }

        //
        //  FormatStandard
        //
        //  Actions: Format the TimeSpan instance using the specified format.
        // 
        private static Result<void> FormatStandard(TimeSpan value, bool isInvariant, StringView format, Pattern pattern, String outStr)
		{
            int32 day = (int32)((int64)value / TimeSpan.TicksPerDay);
            int64 time = (int64)value % TimeSpan.TicksPerDay;

            if ((int64)value < 0) {
                day = -day;
                time = -time;
            }
            int hours    = (int)(time / TimeSpan.TicksPerHour % 24);
            int minutes  = (int)(time / TimeSpan.TicksPerMinute % 60);
            int seconds  = (int)(time / TimeSpan.TicksPerSecond % 60);
            int fraction = (int)(time % TimeSpan.TicksPerSecond);

            FormatLiterals literal;
            if (isInvariant) {
                if ((int64)value < 0)
                    literal = NegativeInvariantFormatLiterals;
                else
                    literal = PositiveInvariantFormatLiterals;
            }
            else {
                literal = FormatLiterals();
                literal.[Friend]Init(format, pattern == Pattern.Full);
            }
            if (fraction != 0) { // truncate the partial second to the specified length
                fraction = (int)((int64)fraction / (int64)Math.Pow(10, DateTimeFormat.[Friend]MaxSecondsFractionDigits - literal.[Friend]ff));
            }

            // Pattern.Full: [-]dd.hh:mm:ss.fffffff
            // Pattern.Minimum: [-][d.]hh:mm:ss[.fffffff] 

            outStr.Append(literal.[Friend]Start);                           // [-]
            if (pattern == Pattern.Full || day != 0) {          //
				day.ToString(outStr);							// [dd]
                outStr.Append(literal.[Friend]DayHourSep);                  // [.]
            }                                                   //
            IntToString(hours, literal.[Friend]hh, outStr);          // hh
            outStr.Append(literal.[Friend]HourMinuteSep);                   // :
            IntToString(minutes, literal.[Friend]mm, outStr);        // mm
            outStr.Append(literal.[Friend]MinuteSecondSep);                 // :
            IntToString(seconds, literal.[Friend]ss, outStr);        // ss
            if (!isInvariant && pattern == Pattern.Minimum) {
                int effectiveDigits = literal.[Friend]ff;
                while (effectiveDigits > 0) {
                    if (fraction % 10 == 0) {
                        fraction = fraction / 10;
                        effectiveDigits--;
                    }
                    else {
                        break;
                    }
                }
                if (effectiveDigits > 0) {
                    outStr.Append(literal.[Friend]SecondFractionSep);           // [.FFFFFFF]
                    (fraction).ToString(outStr, DateTimeFormat.[Friend]fixedNumberFormats[effectiveDigits - 1], CultureInfo.InvariantCulture);
                }
            }
            else if (pattern == Pattern.Full || fraction != 0) {
                outStr.Append(literal.[Friend]SecondFractionSep);           // [.]
                IntToString(fraction, literal.[Friend]ff, outStr);   // [fffffff]
            }                                                   //
            outStr.Append(literal.[Friend]End);                             //
			return .Ok;
        }




        //
        //  FormatCustomized
        //
        //  Actions: Format the TimeSpan instance using the specified format.
        // 
        protected static Result<void> FormatCustomized(TimeSpan value, StringView format, DateTimeFormatInfo dtfi, String result)
		{                      

            Contract.Assert(dtfi != null, "dtfi == null");

            int day = (int)((int64)value / TimeSpan.TicksPerDay);
            int64 time = (int64)value % TimeSpan.TicksPerDay;

            if ((int64)value < 0) {
                day = -day;
                time = -time;
            }
            int hours    = (int)(time / TimeSpan.TicksPerHour % 24);
            int minutes  = (int)(time / TimeSpan.TicksPerMinute % 60);
            int seconds  = (int)(time / TimeSpan.TicksPerSecond % 60);
            int fraction = (int)(time % TimeSpan.TicksPerSecond);

            int64 tmp = 0;
            int i = 0;
            int tokenLen = 0;
            
            while (i < format.Length) {
                char8 ch = format[i];
                int nextChar;
                switch (ch) {
                case 'h':
                    tokenLen = DateTimeFormat.[Friend]ParseRepeatPattern(format, i, ch);
                    if (tokenLen > 2)
                        //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));
						return .Err;
                    DateTimeFormat.[Friend]FormatDigits(result, hours, tokenLen);
                    break;
                case 'm':
                    tokenLen = DateTimeFormat.[Friend]ParseRepeatPattern(format, i, ch);
                    if (tokenLen > 2)
						return .Err;
                        //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));
                    DateTimeFormat.[Friend]FormatDigits(result, minutes, tokenLen);
                    break;
                case 's':
                    tokenLen = DateTimeFormat.[Friend]ParseRepeatPattern(format, i, ch);
                    if (tokenLen > 2)
						return .Err;
                        //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));
                    DateTimeFormat.[Friend]FormatDigits(result, seconds, tokenLen);
                    break;
                case 'f':
                    //
                    // The fraction of a second in single-digit precision. The remaining digits are truncated. 
                    //
                    tokenLen = DateTimeFormat.[Friend]ParseRepeatPattern(format, i, ch);
                    if (tokenLen > DateTimeFormat.[Friend]MaxSecondsFractionDigits)
						return .Err;
                        //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));

                    tmp = (int64)fraction;
                    tmp /= (int64)Math.Pow(10, DateTimeFormat.[Friend]MaxSecondsFractionDigits - tokenLen);
                    (tmp).ToString(result, DateTimeFormat.[Friend]fixedNumberFormats[tokenLen - 1], CultureInfo.InvariantCulture);
                    break;
                case 'F':
                    //
                    // Displays the most significant digit of the seconds fraction. Nothing is displayed if the digit is zero.
                    //
                    tokenLen = DateTimeFormat.[Friend]ParseRepeatPattern(format, i, ch);
                    if (tokenLen > DateTimeFormat.[Friend]MaxSecondsFractionDigits)
						return .Err;
                        //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));

                    tmp = (int64)fraction;
                    tmp /= (int64)Math.Pow(10, DateTimeFormat.[Friend]MaxSecondsFractionDigits - tokenLen);
                    int effectiveDigits = tokenLen;
                    while (effectiveDigits > 0) {
                        if (tmp % 10 == 0) {
                            tmp = tmp / 10;
                            effectiveDigits--;
                        }
                        else {
                            break;
                        }
                    }
                    if (effectiveDigits > 0) {
                        (tmp).ToString(result, DateTimeFormat.[Friend]fixedNumberFormats[effectiveDigits - 1], CultureInfo.InvariantCulture);
                    }
                    break;
                case 'd':
                    //
                    // tokenLen == 1 : Day as digits with no leading zero.
                    // tokenLen == 2+: Day as digits with leading zero for single-digit days.
                    //
                    tokenLen = DateTimeFormat.[Friend]ParseRepeatPattern(format, i, ch);
                    if (tokenLen > 8)
						return .Err;
                        //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));
                    DateTimeFormat.[Friend]FormatDigits(result, day, tokenLen, true);
                    break;
                case '\'':
                case '\"':
                    //StringBuilder enquotedString = new StringBuilder();
                    tokenLen = DateTimeFormat.[Friend]ParseQuoteString(format, i, result); 
                    //result.Append(enquotedString);
                    break;
                case '%':
                    // Optional format character.
                    // For example, format string "%d" will print day 
                    // Most of the cases, "%" can be ignored.
                    nextChar = DateTimeFormat.[Friend]ParseNextChar(format, i);
                    // nextChar will be -1 if we already reach the end of the format string.
                    // Besides, we will not allow "%%" appear in the pattern.
                    if (nextChar >= 0 && nextChar != (int)'%')
					{
                        TimeSpanFormat.FormatCustomized(value, StringView((char8*)&nextChar, 1), dtfi, result);
                        tokenLen = 2;
                    }
                    else
                    {
                        //
                        // This means that '%' is at the end of the format string or
                        // "%%" appears in the format string.
                        //
                        //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));
						return .Err;
                    }
                    break;
                case '\\':
                    // Escaped character.  Can be used to insert character into the format string.
                    // For example, "\d" will insert the character 'd' into the string.
                    //
                    nextChar = DateTimeFormat.[Friend]ParseNextChar(format, i);
                    if (nextChar >= 0)
                    {
                        result.Append(((char8)nextChar));
                        tokenLen = 2;
                    } 
                    else
                    {
                        //
                        // This means that '\' is at the end of the formatting string.
                        //
                        //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));
						return .Err;
                    }
                    break;
                default:
                    //throw new FormatException(Environment.GetResourceString("Format_InvalidString"));
					return .Err;
                }
                i += tokenLen;
            }
            return .Ok;
        }




        protected struct FormatLiterals {
            String Start {
                get {
                    return literals[0];
                }
            }
            String DayHourSep {
                get {
                    return literals[1];
                }
            }
            String HourMinuteSep {
                get {
                    return literals[2];
                }
            }
            String MinuteSecondSep {
                get {
                    return literals[3];
                }
            }
            String SecondFractionSep {
                get {
                    return literals[4];
                }
            }
            String End {
                get {
                    return literals[5];
                }
            }
            String AppCompatLiteral;
            int dd;
            int hh;
            int mm;
            int ss;
            int ff;  

            private String[] literals;
			private List<String> ownedStrs;

			public void Dispose() mut
			{
				DeleteAndNullify!(literals);
				if (ownedStrs != null)
				{
					DeleteContainerAndItems!(ownedStrs);
					ownedStrs = null;
				}
			}

			String AddOwnedStr(String str) mut
			{
				if (ownedStrs == null)
					ownedStrs = new List<String>();
				ownedStrs.Add(str);
				return str;
			}

            /* factory method for static invariant FormatLiterals */
            static FormatLiterals InitInvariant(bool isNegative) {
                FormatLiterals x = FormatLiterals();
                x.literals = new String[6];
                x.literals[0] = isNegative ? "-" : String.Empty;
                x.literals[1] = ".";
                x.literals[2] = ":";
                x.literals[3] = ":";
                x.literals[4] = ".";
                x.literals[5] = String.Empty;  
                x.AppCompatLiteral = ":."; // MinuteSecondSep+SecondFractionSep;       
                x.dd = 2;
                x.hh = 2;
                x.mm = 2;
                x.ss = 2;
                x.ff = DateTimeFormat.[Friend]MaxSecondsFractionDigits;
                return x;
            }

            // For the "v1" TimeSpan localized patterns, the data is simply literal field separators with
            // the constants guaranteed to include DHMSF ordered greatest to least significant.
            // Once the data becomes more complex than this we will need to write a proper tokenizer for
            // parsing and formatting
            void Init(StringView format, bool useInvariantFieldLengths) mut
			{
                literals = new String[6];
                for (int i = 0; i < literals.Count; i++) 
                    literals[i] = "";
                dd = 0;
                hh = 0;
                mm = 0;
                ss = 0;
                ff = 0;

                String sb = scope String();
                bool inQuote = false;
                char8 quote = '\'';
                int  field = 0;

                for (int i = 0; i < format.Length; i++) {
                    switch (format[i]) {
                        case '\'':
                        case '\"':
                            if (inQuote && (quote == format[i])) {
                                /* we were in a quote and found a matching exit quote, so we are outside a quote now */
                                Contract.Assert(field >= 0 && field <= 5, "field >= 0 && field <= 5");
                                if (field >= 0 && field <= 5) {
                                    literals[field] = AddOwnedStr(new String(sb));
                                    sb.Length = 0;
                                    inQuote = false;
                                }
                                else {                                   
                                    return; // how did we get here?
                                }
                            }
                            else if (!inQuote) {
                                /* we are at the start of a new quote block */
                                quote = format[i];
                                inQuote = true;
                            }
                            else {
                                /* we were in a quote and saw the other type of quote character, so we are still in a quote */
                            }
                            break;
                        case '%':
                            Contract.Assert(false, "Unexpected special token '%', Bug in DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                            sb.Append(format[i]);
                        case '\\':
                            if (!inQuote) {
                                i++; /* skip next character that is escaped by this backslash or percent sign */
                                break;
                            }
                            sb.Append(format[i]);
                        case 'd':
                            if (!inQuote) {
                                Contract.Assert((field == 0 && sb.Length == 0) || field == 1,
                                                "field == 0 || field == 1, Bug in DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                                field = 1; // DayHourSep
                                dd++;
                            }
                            break;
                        case 'h':
                            if (!inQuote) {
                                Contract.Assert((field == 1 && sb.Length == 0) || field == 2,
                                                "field == 1 || field == 2, Bug in DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                                field = 2; // HourMinuteSep
                                hh++;
                            }
                            break;
                        case 'm':
                            if (!inQuote) {
                                Contract.Assert((field == 2 && sb.Length == 0) || field == 3,
                                                "field == 2 || field == 3, Bug in DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                                field = 3; // MinuteSecondSep
                                mm++;
                            }
                            break;
                        case 's':
                            if (!inQuote) {
                                Contract.Assert((field == 3 && sb.Length == 0) || field == 4,
                                                "field == 3 || field == 4, Bug in DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                                field = 4; // SecondFractionSep
                                ss++;
                            }
                            break;
                        case 'f':
                        case 'F':
                            if (!inQuote) {
                                Contract.Assert((field == 4 && sb.Length == 0) || field == 5,
                                                "field == 4 || field == 5, Bug in DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                                field = 5; // End
                                ff++;
                            }
                            break;
                        default:
                            sb.Append(format[i]);
                            break;
                    }
                }

                Contract.Assert(field == 5);
                AppCompatLiteral = AddOwnedStr(new String(MinuteSecondSep, SecondFractionSep));

                Contract.Assert(0 < dd && dd < 3, "0 < dd && dd < 3, Bug in System.Globalization.DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                Contract.Assert(0 < hh && hh < 3, "0 < hh && hh < 3, Bug in System.Globalization.DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                Contract.Assert(0 < mm && mm < 3, "0 < mm && mm < 3, Bug in System.Globalization.DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                Contract.Assert(0 < ss && ss < 3, "0 < ss && ss < 3, Bug in System.Globalization.DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");
                Contract.Assert(0 < ff && ff < 8, "0 < ff && ff < 8, Bug in System.Globalization.DateTimeFormatInfo.FullTimeSpan[Positive|Negative]Pattern");

                if (useInvariantFieldLengths) {
                    dd = 2;
                    hh = 2;
                    mm = 2;
                    ss = 2;
                    ff = DateTimeFormat.[Friend]MaxSecondsFractionDigits;
                }
                else {
                    if (dd < 1 || dd > 2) dd = 2;   // The DTFI property has a problem. let's try to make the best of the situation.
                    if (hh < 1 || hh > 2) hh = 2;
                    if (mm < 1 || mm > 2) mm = 2;
                    if (ss < 1 || ss > 2) ss = 2;
                    if (ff < 1 || ff > 7) ff = 7;
                }
            }
        } //end of struct FormatLiterals
    }
}
