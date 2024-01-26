#pragma warning disable 4203

using System.Reflection;
namespace System;

static class HashCode
{
	class HashHelper<T>
	{
		[OnCompile(.TypeInit), Comptime]
		public static void Generate()
		{
			var t = typeof(T);

			String code = scope .();

			code.AppendF($"public static int Get(T value)\n");
			code.Append("{\n");

			if (t.IsTypedPrimitive)
			{
				code.AppendF($"\treturn SelfOuter.Get(({t.UnderlyingType})value);");
			}
			else if (t.IsEnum)
			{
				code.Append("\tint hash = 0;\n");

				if ((t.IsEnum) && (t.UnderlyingType == null))
				{
					code.AppendF($"\tswitch (value)\n");
					code.Append("\t{\n");

					int enumCaseCount = 0;
					for (var field in t.GetFields())
					{
						if (!field.IsEnumCase)
							continue;
						code.AppendF($"\tcase .{field.Name}");

						int tupleMemberCount = 0;
						var fieldType = field.FieldType;
						if (fieldType.IsTuple)
						{
							code.Append("(");
							for (var tupleField in fieldType.GetFields())
							{
								if (tupleMemberCount > 0)
									code.Append(", ");
								code.AppendF($"var val{tupleMemberCount}");
								tupleMemberCount++;
							}
							code.Append(")");
						}

						code.Append(":\n");

						if (enumCaseCount > 0)
							code.AppendF($"\t\thash = {enumCaseCount};\n");

						int tupleMemberIdx = 0;
						int hashedTupleMemberCount = 0;
						for (var tupleField in fieldType.GetFields())
						{
							if (IsHashable(tupleField.FieldType))
							{
								if ((enumCaseCount == 0) && (hashedTupleMemberCount == 0))
									code.AppendF($"\t\thash = SelfOuter.Get(val{tupleMemberIdx});\n");
								else
									code.AppendF($"\t\thash = Mix(hash, val{tupleMemberIdx});\n");
								hashedTupleMemberCount++;
							}
							tupleMemberIdx++;
						}

						enumCaseCount++;
					}
					code.Append("\t}\n");
				}

				code.Append("\treturn hash;\n");
			}
			else if (t.IsUnion)
			{
				code.AppendF($"\treturn SelfOuter.Get(&value, {t.Size});\n");
			}
			else
			{
				if (!t.IsValueType)
				{
					code.AppendF("\tif (value == null)\n");
					code.AppendF("\t\treturn 0;\n");
				}

				code.Append("\tint hash = 0;\n");

				if (var sizedArray = t as SizedArrayType)
				{
					if (IsHashable(sizedArray.UnderlyingType))
					{
						code.AppendF($"\tfor (int i < {sizedArray.ElementCount})\n");
						code.AppendF($"\t\thash = Mix(hash, value[i]);\n");
					}
				}
				else
				{
					int fieldCount = 0;
					for (var field in t.GetFields())
					{
						if (field.IsStatic)
							continue;
						if (!IsHashable(field.FieldType))
							continue;
						if (fieldCount == 0)
							code.AppendF($"\thash = SelfOuter.Get(value.");
						else
							code.AppendF($"\thash = Mix(hash, value.");
						if (!field.IsPublic)
							code.Append("[Friend]");
						code.AppendF($"{field.Name});\n");
						++fieldCount;
					}
				}

				code.Append("\treturn hash;\n");
			}
			code.Append("}");

		    Compiler.EmitTypeBody(typeof(Self), code);
		}
	}

	static int Get<T>(T value) where T : IHashable
	{
		if (value == null)
			return 0;
		return value.GetHashCode();
	}

	public static int Generate<T>(T value)
	{
		if (Compiler.IsBuilding)
		{
			return HashHelper<T>.Get(value);
		}
		else
		{
			[IgnoreErrors(true)]
			{
				return HashHelper<T>.Get(value);
			}
	#unwarn
			return 0;
		}
	}

	static int Get(void* ptr, int size)
	{
		int bytesLeft = size;
		int hash = 0;
		uint8* curPtr = (.)ptr;
		let intSize = sizeof(int);
		while (bytesLeft >= intSize)
		{
			hash = (hash ^ *((int*)curPtr)) &+ (hash &* 16777619);
			bytesLeft -= intSize;
			curPtr += intSize;
		}

		while (bytesLeft >= 1)
		{
			hash = ((hash ^ (int)*curPtr) << 5) &- hash;
			bytesLeft--;
			curPtr++;
		}

		return hash;
	}

	public static int Mix(int hash, int hash2)
	{
		return ((hash ^ hash2) << 5) &- hash;
	}

	static int Mix<T>(int hash, T value) where T : IHashable
	{
		return ((hash ^ Get(value)) << 5) &- hash;
	}

	static bool IsHashable(Type t)
	{
		if (t == null)
			return false;
		if (t.IsPrimitive)
			return true;
		for (var iface in t.Interfaces)
			if (iface == typeof(IHashable))
				return true;
		return IsHashable(t.BaseType);
	}
}