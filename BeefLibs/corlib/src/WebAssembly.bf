#if BF_PLATFORM_WASM

namespace System;

using System.Interop;

class WebAssembly
{
	[CLink]
	private static extern int32 emscripten_asm_const_int(char8* code, char8* arg_sigs, ...);
	[CLink]
	private static extern void emscripten_asm_const_ptr(char8* code, char8* arg_sigs, ...);
	[CLink]
	private static extern double emscripten_asm_const_double(char8* code, char8* arg_sigs, ...);

	[Intrinsic(":add_string_to_section")]
	private static extern char8* add_string_to_section(uint8* string, uint8* section);

	private static Span<uint8> GetStringData(String value)
	{
	    return StringView(value).ToRawData();
	}

	/**
	* Returns the string added to the specified data section. 
	*/
	private static char8* AddStringToSection<T0, T1>(T0 value, T1 section) where T0 : const String where T1 : const String
	{
		static uint8[?] data = GetStringData(value);
		static uint8[?] sectionStr = GetStringData(section);

	    #unwarn
		return add_string_to_section(&data, &sectionStr);
	}

	private static void GetArgSigInternal(Type t, String s)
	{
		switch(t)
		{
		case typeof(float):
			s.Append('f');
		case typeof(double):
			s.Append('d');
		case typeof(c_ulong): fallthrough;
		case typeof(c_ulonglong): fallthrough;
		case typeof(c_longlong): fallthrough;
		case typeof(c_long):
			s.Append('j');
		default:
#if BF_32_BIT
			s.Append('i');
#else
			s.Append('p');
#endif
		}
	}

	[Comptime]
	static void JSGetArgSig(Type t, String s)
	{
		if(t.IsTuple)
		{
			int count = t.FieldCount;
			for(int i = 0; i < count; i++)
			{
				var type = t.GetField(i).Get().FieldType;
				GetArgSigInternal(type, s);
			}
		}else
			GetArgSigInternal(t, s);
	}

	private static String JSGetResultName(Type t)
	{
		if(t.IsPointer)
			return "ptr";
	    else if (t.IsFloatingPoint)
	        return "double";
	    else
	        return "int";
	}

	[Comptime]
	private static void GetArgString<T>(String s)
	{
		Type type = typeof(T);
		int fieldCount = 0;
		if (type.IsTuple)
		{
			fieldCount = type.FieldCount;
			for(int i = 0; i< fieldCount; i++)
			{
				if(i == fieldCount-1)
					s.AppendF("p0.{}", i);
				else
					s.AppendF("p0.{}, ", i);
			}
		}
		else
			s.Append("p0");
	}

	private static String JSGetCallString<TResult, TCall, T0>() where TCall : const String
	{
	    if (TCall == null)
	        return "";
	    var argSigs = JSGetArgSig(typeof(T0), .. scope .());
		var argString = GetArgString<T0>(.. scope .());
	    return new $"result = emscripten_asm_const_{JSGetResultName(typeof(TResult))}(AddStringToSection({TCall.Quote(.. scope .())}, \"em_asm\"), \"{argSigs}\", {argString});";
	}

	public static TResult JSCall<TResult, TCall, T0>(TCall callString, T0 p0) where TCall : const String
	{
	    TResult result = default;
	    Compiler.Mixin(JSGetCallString<TResult, const TCall, T0>());
	    return result;
	}
}


#endif
