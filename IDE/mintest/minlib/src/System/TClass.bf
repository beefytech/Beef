#pragma warning disable 168

using System;

class Foogie<T> where T : IHashable
{
	public void Do()
	{
		T val = default;
		val.GetHashCode();
	}
}

struct Zorbble
{
	public int mA;
	public void MainMethod()
	{

	}

	static void Zoff()
	{

	}
}