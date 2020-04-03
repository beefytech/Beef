namespace System
{
    public struct Attribute
    {
    }

    public enum AttributeTargets
    {
	    Assembly     = 0x0001,
	    Module       = 0x0002,
	    Class        = 0x0004,
	    Struct       = 0x0008,
	    Enum         = 0x0010,
	    Constructor  = 0x0020,
	    Method       = 0x0040,
	    Property     = 0x0080,
	    Field        = 0x0100,
	    StaticField  = 0x0200,
	    Interface    = 0x0400,
	    Parameter    = 0x0800,
	    Delegate     = 0x1000,
		Function     = 0x2000,
	    ReturnValue  = 0x4000,
	    //@todo GENERICS: document GenericParameter
	    GenericParameter = 0x8000,
		Invocation   = 0x10000,
		MemberAccess = 0x20000,
		Alloc        = 0x40000,
		Delete       = 0x80000,

	    All = Assembly | Module | Class | Struct | Enum | Constructor |
	        Method | Property | Field | StaticField | Interface | Parameter |
	    	Delegate | Function | ReturnValue | GenericParameter | Invocation | MemberAccess |
			Alloc | Delete,
	}

	public enum ReflectKind
	{
		None = 0,
		Type = 1,
		NonStaticFields = 2,
		StaticFields = 4,
		DefaultConstructor = 8,
		Constructors = 0x10,
		StaticMethods = 0x20,
		Methods = 0x40,
		All = 0x7F,

		ApplyToInnerTypes = 0x80,
	}

	public enum AttributeFlags
	{
		None,
		DisallowAllowMultiple = 1,
		NotInherited = 2,
		ReflectAttribute = 4,
		AlwaysIncludeTarget = 8
	}

    public sealed struct AttributeUsageAttribute : Attribute
	{
	    AttributeTargets mAttributeTarget = .All;
		AttributeFlags mAttributeFlags = .None;
		ReflectKind mReflectUser = .None;

	    public this(AttributeTargets validOn)
	    {
	        mAttributeTarget = validOn;
	    }

		public this(AttributeTargets validOn, AttributeFlags flags)
		{
		    mAttributeTarget = validOn;
			mAttributeFlags = flags;
		}

	    public this(AttributeTargets validOn, bool allowMultiple, bool inherited)
	    {
	        mAttributeTarget = validOn;
			if (!allowMultiple)
				mAttributeFlags |= .DisallowAllowMultiple;
			if (!inherited)
				mAttributeFlags |= .NotInherited;
	    }

	    public AttributeTargets ValidOn
	    {
	        get { return mAttributeTarget; }
	    }

		public ReflectKind ReflectUser
		{
			get { return mReflectUser; }
			set mut { mReflectUser = value; }
		}
	}

	[AttributeUsage(.All)]
	public struct ReflectAttribute : Attribute
	{
	    public this(ReflectKind reflectKind = .All)
		{
		}
	}

    [AttributeUsage(.Method | .Constructor |  .Invocation)]
    public struct InlineAttribute : Attribute
    {
        
    }

	[AttributeUsage(.Invocation)]
	public struct UnboundAttribute : Attribute
	{
	    
	}

	[AttributeUsage(.Class | .Struct | .Interface | .Method | .Constructor)]
	public struct AlwaysIncludeAttribute : Attribute
	{
		bool mAssumeInstantiated;

	    public bool AssumeInstantiated
		{
			get { return mAssumeInstantiated; }
			set mut { mAssumeInstantiated = value; }
		}
	}

	[AttributeUsage(.MemberAccess | .Alloc | .Delete)]
	public struct FriendAttribute : Attribute
	{
	    
	}

	[AttributeUsage(.Method | .Class | .Struct | .Enum)]
	public struct UseLLVMAttribute : Attribute
	{
	    
	}

	[AttributeUsage(.Method | .Class | .Struct | .Enum)]
	public struct OptimizeAttribute : Attribute
	{
	    
	}

    [AttributeUsage(.Method /*2*/ | .StaticField)]
    public struct CLinkAttribute : Attribute
    {

	}

	[AttributeUsage(.Method /*2*/ | .StaticField)]
	public struct LinkNameAttribute : Attribute
	{
		public this(String linkName)
		{
			
		}
	}

	[AttributeUsage(.Method | .Delegate | .Function)]
	public struct StdCallAttribute : Attribute
	{

	}

	[AttributeUsage(.Method /*2*/)]
	public struct CVarArgsAttribute : Attribute
	{

	}

	[AttributeUsage(.Method /*2*/)]
	public struct NoReturnAttribute : Attribute
	{

	}

	[AttributeUsage(.Method /*2*/)]
	public struct SkipCallAttribute : Attribute
	{

	}

	[AttributeUsage(.Method /*2*/)]
	public struct IntrinsicAttribute : Attribute
	{
		public this(String intrinName)
		{

		}

		public this(String intrinName, Type t0)
		{

		}

		public this(String intrinName, Type t0, Type t1)
		{

		}

		public this(String intrinName, Type t0, Type t1, Type t2)
		{

		}
	}

	[AttributeUsage(.Class | .Struct /*2*/)]
	public struct StaticInitPriorityAttribute : Attribute
	{
	    public this(int priority)
	    {

	    }
	}

	[AttributeUsage(.Class /*2*/ | .Struct /*2*/)]
    public struct StaticInitAfterAttribute : Attribute
    {
		public this()
		{

		}

		public this(Type afterType)
		{

		}
	}

	[AttributeUsage(.Struct)]
	public struct ForceAddrAttribute : Attribute
	{

	}

	[AttributeUsage(.Constructor)]
	public struct AllowAppendAttribute : Attribute
	{

	}

	[AttributeUsage(.Class | .Struct)]
	public struct PackedAttribute : Attribute
	{

	}

	[AttributeUsage(.Class | .Struct | .Alloc)]
	public struct AlignAttribute : Attribute
	{
		public this(int align)
		{

		}
	}

	[AttributeUsage(.Enum)]
	public struct AllowDuplicatesAttribute : Attribute
	{
	    
	}

	[AttributeUsage(.Class | .Struct)]
	public struct UnionAttribute : Attribute
	{

	}

	[AttributeUsage(.Class | .Struct)]
	public struct CReprAttribute : Attribute
	{

	}

	[AttributeUsage(.Class | .Struct)]
	public struct OrderedAttribute : Attribute
	{

	}

	[AttributeUsage(.Field | .Method /*2*/)]
	public struct NoShowAttribute : Attribute
	{

	}

	[AttributeUsage(.Field | .Method /*2*/)]
	public struct HideAttribute : Attribute
	{

	}


	[AttributeUsage(.Parameter)]
	public struct HideNameAttribute : Attribute
	{
	}

	[AttributeUsage(.Method/*, AlwaysIncludeTarget=true*/)]
	public struct TestAttribute : Attribute
	{
		public bool ShouldFail;
		public bool Ignore;
		public bool Profile;
	}

	public struct ImportAttribute : Attribute
	{
	    public this(String libName)
		{
		}
	}

	public struct ExportAttribute : Attribute
	{

	}

	[AttributeUsage(.StaticField | .Field, .NotInherited)]
	public struct ThreadStaticAttribute : Attribute
	{
		public this()
		{
		}
	}

	[AttributeUsage(.Invocation | .Method | .Property)]
	public struct CheckedAttribute : Attribute
	{

	}

	[AttributeUsage(.Invocation | .Method | .Property)]
	public struct UncheckedAttribute : Attribute
	{
	}

	[AttributeUsage(.Method | .Constructor)]
	public struct DisableChecksAttribute : Attribute
	{
	}

	[AttributeUsage(.Method | .MemberAccess)]
	public struct DisableObjectAccessChecksAttribute : Attribute
	{
	}

	[AttributeUsage(.Method | .Constructor)]
	public struct ObsoleteAttribute : Attribute
	{
		public this(bool isError)
		{

		}

		public this(String error, bool isError)
		{

		}
	}

	[AttributeUsage(.Method)]
	public struct CommutableAttribute : Attribute
	{

	}

	[AttributeUsage(.Method | .Constructor)]
	public struct ErrorAttribute : Attribute
	{
		public this(String error)
		{

		}
	}

	[AttributeUsage(.Method | .Constructor)]
	public struct WarnAttribute : Attribute
	{
		public this(String error)
		{

		}
	}

	[AttributeUsage(.Method | .Class | .Struct)]
	public struct NoDiscardAttribute : Attribute
	{
		public this()
		{

		}

		public this(String message)
		{

		}
	}
}