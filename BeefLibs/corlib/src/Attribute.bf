using System.Reflection;
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
		Alias        = 0x100000,
		Block        = 0x200000,
		DelegateTypeRef = 0x400000,
		FunctionTypeRef = 0x800000,

		Types		 = .Struct | .Enum | .Function | .Class | .Interface,
		ValueTypes	 = .Struct | .Enum | .Function,

	    All = Assembly | Module | Class | Struct | Enum | Constructor |
	        Method | Property | Field | StaticField | Interface | Parameter |
	    	Delegate | Function | ReturnValue | GenericParameter | Invocation | MemberAccess |
			Alloc | Delete | Alias | Block,
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
		DynamicBoxing = 0x80,
		//User = 0x100, // Internal Use
		AllMembers = 0x7F,
		All = 0xFF, // Doesn't include dynamic boxing

		ApplyToInnerTypes = 0x200
	}

	public enum AttributeFlags
	{
		None,
		DisallowAllowMultiple = 1,
		NotInherited = 2,
		ReflectAttribute = 4,
		AlwaysIncludeTarget = 8
	}

	public enum AlwaysIncludeFlags
	{
		None,
		Type = 1,
		IncludeAllMethods = 2,
		AssumeInstantiated = 4,
		All = Type | IncludeAllMethods | AssumeInstantiated
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

		public AlwaysIncludeFlags AlwaysIncludeUser
		{
			set {}
		}
	}

	[AttributeUsage(.All)]
	public struct ReflectAttribute : Attribute
	{
	    public this(ReflectKind reflectKind = .AllMembers)
		{
		}

		public ReflectKind ReflectImplementer
		{
			set mut { }
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
	    public bool AssumeInstantiated
		{
			set { }
		}

		public bool IncludeAllMethods
		{
			set { }
		}
	}

	[AttributeUsage(.MemberAccess | .Alloc)]
	public struct FriendAttribute : Attribute
	{
	    
	}

	[AttributeUsage(.MemberAccess)]
	public struct NoExtensionAttribute : Attribute
	{
	    
	}

	
	[AttributeUsage(.Block)]
	public struct ConstSkipAttribute : Attribute
	{
	}

	[AttributeUsage(.Block)]
	public struct IgnoreErrorsAttribute : Attribute
	{
		public this(bool stopOnErrors = false)
		{
		}
	}

	[AttributeUsage(.Method | .Class | .Struct | .Enum)]
	public struct OptimizeAttribute : Attribute
	{
	    
	}

	[AttributeUsage(.Method | .Class | .Struct | .Enum)]
	public struct NoDebugAttribute : Attribute
	{
	    
	}

	[AttributeUsage(.Method | .Class | .Struct | .Enum)]
	public struct UseLLVMAttribute : Attribute
	{
	    
	}

    [AttributeUsage(.Method /*2*/ | .Constructor | .StaticField)]
    public struct CLinkAttribute : Attribute
    {

	}

	[AttributeUsage(.Method /*2*/ | .Constructor | .StaticField)]
	public struct LinkNameAttribute : Attribute
	{
		public enum MangleKind
		{
			Beef,
			C,
			CPP
		}

		public this(String linkName)
		{
		}

		public this(MangleKind mangleKind)
		{
		}
	}

	[AttributeUsage(.Parameter | .ReturnValue)]
	public struct MangleConstAttribute : Attribute
	{

	}

	[AttributeUsage(.Method | .Constructor | .Delegate | .Function | .DelegateTypeRef | .FunctionTypeRef)]
	public struct CallingConventionAttribute : Attribute
	{
		public enum Kind
		{
			Unspecified,
			Cdecl,
			Stdcall,
			Fastcall,
		}

		public this(Kind callingConvention)
		{

		}
	}

	[Obsolete("Use [CallingConvention(.Stdcall)]", false)]
	[AttributeUsage(.Method | .Constructor | .Delegate | .Function | .Property)]
	public struct StdCallAttribute : Attribute
	{

	}

	[AttributeUsage(.Method /*2*/)]
	public struct NoReturnAttribute : Attribute
	{

	}

	[AttributeUsage(.Method /*2*/)]
	public struct NoSplatAttribute : Attribute
	{

	}

	[AttributeUsage(.Method /*2*/)]
	public struct SkipCallAttribute : Attribute
	{

	}

	[AttributeUsage(.Method)]
	struct OnCompileAttribute : Attribute
	{
		public enum Kind
		{
			None,
			TypeInit,
			TypeDone
		}

		public this(Kind kind)
		{
		}
	}

	[AttributeUsage(.Method)]
	public struct ComptimeAttribute : Attribute
	{
		public this()
		{

		}

		public bool OnlyFromComptime
		{
			set
			{
			}
		}

		public bool ConstEval
		{
			set
			{
			}
		}
	}

	[AttributeUsage(.Invocation)]
	public struct ConstEvalAttribute : Attribute
	{
		public this()
		{

		}
	}

	[AttributeUsage(.Method /*2*/)]
	public struct IntrinsicAttribute : Attribute
	{
		public this(String intrinName)
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

	/// This attribute is required on constructors that include 'append' allocations.
	[AttributeUsage(.Constructor)]
	public struct AllowAppendAttribute : Attribute
	{

	}

	[AttributeUsage(.Class | .Struct)]
	public struct PackedAttribute : Attribute
	{
		public this()
		{

		}

		public this(int align)
		{

		}
	}

	[AttributeUsage(.Class | .Struct | .Alloc | .Field)]
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

	[AttributeUsage(.Class | .Struct | .Enum)]
	public struct CReprAttribute : Attribute
	{

	}

	[AttributeUsage(.Class | .Struct)]
	public struct OrderedAttribute : Attribute
	{

	}

	[AttributeUsage(.Struct)]
	public struct UnderlyingArrayAttribute : Attribute
	{
		public this(Type t, int size, bool isVector)
		{

		}
	}

	[AttributeUsage(.Field | .StaticField | .Method | .Property | .Types)]
	public struct NoShowAttribute : Attribute
	{
		public this()
		{

		}

		public this(bool allowDirect)
		{

		}
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
		public String Tag;
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
	[AttributeUsage(.Method | .MemberAccess)]
	public struct DisableObjectAccessChecksAttribute : Attribute
	{
	}

	[AttributeUsage(.Method | .Constructor | .Class | .Struct | .Alias | .Interface | .Property)]
	public struct ObsoleteAttribute : Attribute
	{
		public this()
		{

		}

		public this(bool isError)
		{

		}

		public this(String error)
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

	[AttributeUsage(.Method | .Constructor | .Class | .Struct | .Alias | .Property | .Field)]
	public struct ErrorAttribute : Attribute
	{
		public this(String error)
		{

		}
	}

	[AttributeUsage(.Method | .Constructor | .Class | .Struct | .Alias | .Property | .Field)]
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

	interface IOnTypeInit
	{
		void OnTypeInit(Type type, Self* prev) mut;
	}

	interface IOnTypeDone
	{
		void OnTypeDone(Type type, Self* prev) mut;
	}

	interface IOnFieldInit
	{
		void OnFieldInit(FieldInfo fieldInfo, Self* prev) mut;
	}

	interface IOnMethodInit
	{
		void OnMethodInit(MethodInfo methodInfo, Self* prev) mut;
	}

	interface IComptimeTypeApply
	{
		void ApplyToType(Type type);
	}

	interface IComptimeMethodApply
	{
		void ApplyToMethod(MethodInfo methodInfo);
	}
}
