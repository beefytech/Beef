namespace System;

[AttributeUsage(.Types | .Method)]
public struct StaticHookAttribute : Attribute, IOnTypeInit
{
	Type mSrcType;
	Type mDestType;

	public this()
	{
		mSrcType = null;
		mDestType = null;
	}

	public this(Type srcType)
	{
		mSrcType = srcType;
		mDestType = null;
	}

	public this(Type srcType, Type destType)
	{
		mSrcType = srcType;
		mDestType = destType;
	}

	[Comptime]
	public void OnTypeInit(Type type, Self* prev)
	{
		var emitStr = scope String();

		if (mDestType != null)
		{
			var hookAttr = mSrcType.GetCustomAttribute<Self>().Value;

			for (var methodInfo in mDestType.GetMethods(.Static))
			{
				if (!methodInfo.HasCustomAttribute<StaticHookAttribute>())
					continue;
				emitStr.AppendF($"public static function {methodInfo.ReturnType}");
				emitStr.Append("(");
				methodInfo.GetParamsDecl(emitStr);
				emitStr.AppendF($") s{methodInfo.Name};\n");
			}
			emitStr.Append("\n");

			emitStr.Append("static this\n{\n");
			for (var methodInfo in mDestType.GetMethods(.Static))
			{
				if (!methodInfo.HasCustomAttribute<StaticHookAttribute>())
					continue;

				emitStr.AppendF($"\ts{methodInfo.Name} = {mSrcType}.s{methodInfo.Name} ?? => {hookAttr.mSrcType}.{methodInfo.Name};\n");
				emitStr.AppendF($"\t{mSrcType}.s{methodInfo.Name} = => {mDestType}.{methodInfo.Name};\n");
			}
			emitStr.Append("}\n");
		}
		else
		{
			if (mSrcType == null)
				return;

			emitStr.Append("#pragma warning disable\n");
			for (var methodInfo in mSrcType.GetMethods(.Static))
			{
				if (!methodInfo.HasCustomAttribute<StaticHookAttribute>())
					continue;

				emitStr.AppendF($"public static function {methodInfo.ReturnType}");
				emitStr.Append("(");
				methodInfo.GetParamsDecl(emitStr);
				emitStr.AppendF($") s{methodInfo.Name};\n");

				emitStr.AppendF($"public static {methodInfo.ReturnType} {methodInfo.Name}");
				emitStr.Append("(");
				methodInfo.GetParamsDecl(emitStr);
				emitStr.Append(")\n");
				emitStr.Append("{\n");
				emitStr.AppendF($"\tif (s{methodInfo.Name} != null)\n");
				emitStr.AppendF($"\t\treturn s{methodInfo.Name}(");
				methodInfo.GetArgsList(emitStr);
				emitStr.AppendF($");\n\treturn {mSrcType}.{methodInfo.Name}(");
				methodInfo.GetArgsList(emitStr);
				emitStr.Append(");\n}\n\n");
			}
		}

		Compiler.EmitTypeBody(type, emitStr);
	}
}
