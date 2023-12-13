namespace System;

interface IParseable<T>
{
	public static Result<T> Parse(StringView val);
}

interface IParseable<T, TErr>
{
	public static Result<T,TErr> Parse(StringView val);
}