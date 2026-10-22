using System;

namespace IDETest.EnumBaseCycle
{
	// Partially typed underlying type, so the attribute below parses as a sized array size
	public enum ENodeType : u //FAIL
	[AttributeUsage(.Method | .Property)] //FAIL
	public struct NodeAttribute : Attribute
	{
		public String Category;
	}
}
