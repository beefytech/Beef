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

	    All = Assembly | Module | Class | Struct | Enum | Constructor |
	        Method | Property | Field | StaticField | Interface | Parameter |
	    	Delegate | Function | ReturnValue | GenericParameter | Invocation | MemberAccess,
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
	    internal AttributeTargets mAttributeTarget = AttributeTargets.All;
		internal AttributeFlags mAttributeFlags = .None;
		internal ReflectKind mReflectUser = .None;

	    public this(AttributeTargets validOn)
	    {
	        mAttributeTarget = validOn;
	    }

		internal this(AttributeTargets validOn, AttributeFlags flags)
		{
		    mAttributeTarget = validOn;
			mAttributeFlags = flags;
		}

	    internal this(AttributeTargets validOn, bool allowMultiple, bool inherited)
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

	[AttributeUsage(AttributeTargets.All)]
	public struct ReflectAttribute : Attribute
	{
	    public this(ReflectKind reflectKind = .All)
		{
		}
	}

    [AttributeUsage(AttributeTargets.Method /*1*/ | .Invocation | .Property)]
    public struct InlineAttribute : Attribute
    {
        
    }

	[AttributeUsage(AttributeTargets.Invocation)]
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

	[AttributeUsage(AttributeTargets.MemberAccess)]
	public struct FriendAttribute : Attribute
	{
	    
	}

	[AttributeUsage(AttributeTargets.MemberAccess)]
	public struct SkipAccessCheckAttribute : Attribute
	{
	    
	}

	[AttributeUsage(.Method | .Class | .Struct | .Enum)]
	public struct OptimizeAttribute : Attribute
	{
	    
	}

    [AttributeUsage(AttributeTargets.Method /*2*/ | AttributeTargets.StaticField)]
    public struct CLinkAttribute : Attribute
    {

	}

	[AttributeUsage(AttributeTargets.Method /*2*/ | AttributeTargets.StaticField)]
	public struct LinkNameAttribute : Attribute
	{
		public this(String linkName)
		{
			
		}
	}

	[AttributeUsage(AttributeTargets.Method | .Delegate | .Function)]
	public struct StdCallAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Method /*2*/)]
	public struct CVarArgsAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Method /*2*/)]
	public struct NoReturnAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Method /*2*/)]
	public struct SkipCallAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Method /*2*/)]
	public struct IntrinsicAttribute : Attribute
	{
		public this(String intrinName)
		{

		}
	}

	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct /*2*/)]
	public struct StaticInitPriorityAttribute : Attribute
	{
	    public this(int priority)
	    {

	    }
	}

	[AttributeUsage(AttributeTargets.Class /*2*/ | AttributeTargets.Struct /*2*/)]
    public struct StaticInitAfterAttribute : Attribute
    {
		public this()
		{

		}

		public this(Type afterType)
		{

		}
	}

	[AttributeUsage(AttributeTargets.Struct)]
	public struct ForceAddrAttribute : Attribute
	{

	}

	/// This attribute is required on constructors that include 'append' allocations.
	[AttributeUsage(AttributeTargets.Constructor)]
	public struct AllowAppendAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
	public struct PackedAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
	public struct UnionAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
	public struct CReprAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Class | AttributeTargets.Struct)]
	public struct OrderedAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Field | .Method /*2*/)]
	public struct NoShowAttribute : Attribute
	{

	}

	[AttributeUsage(AttributeTargets.Field | .Method /*2*/)]
	public struct HideAttribute : Attribute
	{

	}


	[AttributeUsage(.Parameter)]
	public struct HideNameAttribute : Attribute
	{
	}

	[AttributeUsage(AttributeTargets.Method/*, AlwaysIncludeTarget=true*/)]
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

	[AttributeUsage(AttributeTargets.StaticField | AttributeTargets.Field, .NotInherited)]
	public struct ThreadStaticAttribute : Attribute
	{
		public this()
		{
		}
	}

	/// The [Checked] attribute is used to mark a method or a method invocation as being "checked", meaning
	/// that the method applies extra runtime checks such as bounds checking or other parameter or state validation.
	[AttributeUsage(.Invocation | .Method | .Property)]
	public struct CheckedAttribute : Attribute
	{

	}

	/// The [Unchecked] attribute is used to mark a method or a method invocation as being "unchecked", meaning
	/// that the method omits runtime checks such as bounds checking or other parameter or state validation.
	[AttributeUsage(.Invocation | .Method | .Property)]
	public struct UncheckedAttribute : Attribute
	{
	}

	/// Generally used as a per-method optimization, [DisableChecks] will cause calls within this method to
	/// call the unchecked version of methods. By default, only debug builds perform checked calls.
	[AttributeUsage(.Method | .Constructor)]
	public struct DisableChecksAttribute : Attribute
	{
	}

	/// Generally used as a per-method optimization, [DisableObjectAccessChecks] will avoid the runtime per-object-access
	/// checks which by default are only applied in debug builds anyway.
	[AttributeUsage(AttributeTargets.Method/*, AlwaysIncludeTarget=true*/)]
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

	/// If [NoDiscard] is used on a method, the the compiler will show a warning if the result is discarded.
	/// If used on a type, the compiler will show an warning if any method returns that type and the caller discards the result.
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
