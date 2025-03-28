#pragma warning disable 168

using System;
using System.Reflection;

namespace Tests;

class Params
{
	[AttributeUsage(.Method)]
	struct StringFormatAttribute : Attribute, IOnMethodInit
	{
		[Comptime]
		public void OnMethodInit(MethodInfo methodInfo, Self* prev)
		{
			String code = scope .();

			String format = null;

			if (var constType = methodInfo.GetGenericArgType(0) as ConstExprType)
			{
				if (constType.ValueType == typeof(String))
				{
					format = String.GetById((.)constType.ValueData);
				}
			}

			if (String.IsNullOrEmpty(format))
				return;

			int pos = 0;
			int len = format.Length;
			char8 ch = '\x00';

			String s = null;
			String fmt = "";
			int autoArgIdx = 0;
			bool hasTempStr = false;

			void FormatError()
			{
				Runtime.FatalError("Format Error");
			}

			String bufOut = scope .();

			void FlushOut()
			{
				if (bufOut.IsEmpty)
					return;
				code.Append("outStr.Append(");
				bufOut.Quote(code);
				code.Append(");\n");
				bufOut.Clear();
			}

			while (true)
			{
				int p = pos;
				int i = pos;
				while (pos < len)
				{
					ch = format[pos];

					pos++;
					if (ch == '}')
					{
						if (pos < len && format[pos] == '}') // Treat as escape character for }}
							pos++;
						else
							FormatError();
					}

					if (ch == '{')
					{
						if (pos < len && format[pos] == '{') // Treat as escape character for {{
							pos++;
						else
						{
							pos--;
							break;
						}
					}

					bufOut.Append(ch);
				}

				if (pos == len) break;
				pos++;
				int index = 0;
				if (pos == len || (ch = format[pos]) < '0' || ch > '9')
				{
					if ((pos < len) &&
						((ch == '}') || (ch == ':') || (ch == ',')))
						index = autoArgIdx++;
					else
						FormatError();
				}
				else
				{
					repeat
					{
						index = index * 10 + ch - '0';
						pos++;
						if (pos == len) FormatError();
						ch = format[pos];
					}
			        while (ch >= '0' && ch <= '9' && index < 1000000);
				}

				var paramType = methodInfo.GetParamType(index + 3);
				var paramName = methodInfo.GetParamName(index + 3);

				while (pos < len && (ch = format[pos]) == ' ') pos++;
				bool leftJustify = false;
				int width = 0;
				if (ch == ',')
				{
					pos++;
					while (pos < len && format[pos] == ' ') pos++;

					if (pos == len) FormatError();
					ch = format[pos];
					if (ch == '-')
					{
						leftJustify = true;
						pos++;
						if (pos == len) FormatError();
						ch = format[pos];
					}
					if (ch < '0' || ch > '9') FormatError();
					repeat
					{
						width = width * 10 + ch - '0';
						pos++;
						if (pos == len) FormatError();
						ch = format[pos];
					}
			        while (ch >= '0' && ch <= '9' && width < 1000000);
				}

				while (pos < len && (ch = format[pos]) == ' ') pos++;

				//Object arg = args[index];
				if (ch == ':')
				{
					if (fmt == "")
						fmt = scope:: String(64);
					else
						fmt.Clear();

					bool isFormatEx = false;
					pos++;
					p = pos;
					i = pos;
					while (true)
					{
						if (pos == len) FormatError();
						ch = format[pos];
						pos++;
						if (ch == '{')
						{
							isFormatEx = true;
							if (pos < len && format[pos] == '{')  // Treat as escape character for {{
								pos++;
							else
								FormatError();
						}
						else if (ch == '}')
						{
							// We only treat '}}' as an escape character if the format had an opening '{'. Otherwise we just close on the first '}'
							if ((isFormatEx) && (pos < len && format[pos] == '}'))  // Treat as escape character for }}
								pos++;
							else
							{
								pos--;
								break;
							}
						}

						if (fmt == null)
						{
							fmt = scope:: String(0x100);
						}
						fmt.Append(ch);
					}
				}
				if (ch != '}') Runtime.FatalError("Format error");
				pos++;
				FlushOut();

				bool checkNull = paramType.IsObject || paramType.IsInterface;
				bool hasWidth = width != 0;
				bool hasFmt = fmt != "";

				if (checkNull)
				{
					code.AppendF($"if ({paramName} == null)\n");
					code.Append("    outStr.Append(\"null\");\nelse\n{\n");
				}

				if ((!leftJustify) && (width > 0))
				{
					// We need a temporary string
					if (!hasTempStr)
					{
						code.Insert(0, "var s = scope String(128);\n");
						hasTempStr = false;
					}
					else
					{
						code.Append("s.Clear();\n");
					}

					if (paramType.ImplementsInterface(typeof(IFormattable)))
					{
						if (!hasFmt)
						{
							code.AppendF("if (provider != null)\n    ");
						}

						code.AppendF($"((IFormattable){paramName}).ToString(s, ");
						fmt.Quote(code);
						code.AppendF($", provider);\n");

						if (!hasFmt)
						{
							code.AppendF("else\n");
							code.AppendF($"    {paramName}.ToString(s);\n");
						}
					}
					else
					{
						code.AppendF($"{paramName}.ToString(s);\n");
					}

					code.AppendF($"outStr.Append(' ', {width} - s.[Friend]mLength);\n");
					code.Append("outStr.Append(s);\n");
				}
				else
				{
					if (hasWidth)
					{
						code.AppendF($"int start_{pos} = outStr.[Friend]mLength;\n");
					}

					if (paramType.ImplementsInterface(typeof(IFormattable)))
					{
						if (!hasFmt)
						{
							code.AppendF("if (provider != null)\n    ");
						}

						code.AppendF($"((IFormattable){paramName}).ToString(outStr, ");
						fmt.Quote(code);
						code.AppendF($", provider);\n");

						if (!hasFmt)
						{
							code.AppendF("else\n");
							code.AppendF($"    {paramName}.ToString(outStr);\n");
						}
					}
					else
					{
						code.AppendF($"{paramName}.ToString(outStr);\n");
					}

					if (hasWidth)
					{
						code.AppendF($"outStr.Append(' ', {width} - (outStr.[Friend]mLength - start_{pos}));\n");
					}
				}

				if (checkNull)
				{
					code.Append("}\n");
				}
			}
			FlushOut();
			Compiler.EmitMethodEntry(methodInfo, code);
		}
	}

	[StringFormat]
	static void StrFormat<TStr, TArgs>(String outStr, IFormatProvider provider, TStr formatStr, params TArgs args) where TStr : const String where TArgs : Tuple
	{

	}

	class ClassA<T> where T : Tuple
	{
		public static int Test(delegate int(char8 a, params T) dlg, params T par)
		{
			return dlg('A', params par);
		}
	}

	class ClassB : ClassA<(int a, float b)>
	{

	}

	static void Test1<T>(params T args) where T : Tuple
	{
		Test3<T>(args); 
	}

	static void Test2<T>(params T args) where T : Delegate
	{
		var vals = args;
		Test3(args);
	}

	static void Test3<T>(T args) where T : Tuple
	{

	}

	static void Test4<T>(params (int, float) args)
	{
		var a = args;
		int arg0 = a.0;
		float arg1 = a.1;
	}

	struct StructA
	{
		public int mA;

		public override void ToString(String strBuffer)
		{
			strBuffer.AppendF($"StructA({mA})");
		}
	}

	[Test]
	public static void TestBasics()
	{
		int val = ClassB.Test(scope (a, __a, b) =>
			{
				return (.)a + (.)__a + (.)b;
			}, 10, 2.3f);
		Test.Assert(val == 65+10+2);

		Test1(1, 2.3f);
		delegate void(int a, float b) dlg = default;
		Test2<delegate void(int a, float b)>(1, 2.3f);

		String tStr = null;

		StructA sa = .() { mA = 123 };
		String str = StrFormat(.. scope .(), null, $"This is a test string {123,-6} with some numbers in it {234,6} {0x1234:X} {sa} {tStr}.");
		Test.Assert(str == "This is a test string 123    with some numbers in it    234 1234 StructA(123) null.");
	}
}