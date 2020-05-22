#pragma warning disable 168

using System;

interface IItem
{
	public int Id { get; set; }
}

class Mintesto
{
	public static T Alloc<T>() where T : new
	{
		return new T();
	}

	public static void Dispose<T>(mut T val) where T : IDisposable
	{
		val.Dispose();
	}

	public static int Get<T>(mut T val) where T : IItem
	{
		return val.Id;
	}
}