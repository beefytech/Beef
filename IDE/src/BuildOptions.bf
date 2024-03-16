using System;
using Beefy.utils;

namespace IDE
{
	class BuildOptions
	{
		[Reflect(.All)]
		public enum LTOType
		{
			case None;
			case Thin;

			public static LTOType GetDefaultFor(Workspace.PlatformType platformType, bool isRelease)
			{
				if ((platformType == .Windows) && (isRelease))
					return .Thin;
				return .None;
			}
		}

		[Reflect(.All)]
		public enum EmitDebugInfo
		{
		    No,
		    Yes,
		    LinesOnly,            
		}

		[Reflect(.All)]
		public enum SIMDSetting
		{
		    None,
		    MMX,
		    SSE,
		    SSE2,
		    SSE3,
		    SSE4,
		    SSE41,
		    AVX,
		    AVX2,            
		}

		[Reflect]
		public enum BfOptimizationLevel
		{
		    case O0;
		    case O1;
		    case O2;
		    case O3;
			case Og;
			case OgPlus;

			public bool IsOptimized()
			{
				return (this != .Og) && (this != .OgPlus) && (this != .O0);
			}
		}

		[Reflect]
		public enum RelocType
		{
			NotSet,
			Static, 
			PIC, 
			DynamicNoPIC,
			ROPI,
			RWPI, 
			ROPI_RWPI
		}

		[Reflect]
		public enum PICLevel
		{
			NotSet,
			Not,
			Small, 
			Big
		}

		[Reflect]
		public enum AlwaysIncludeKind
		{
			NotSet,
			No,
			IncludeType,
			AssumeInstantiated,
			IncludeAll,
			IncludeFiltered
		}
	}

	public class DistinctBuildOptions
	{
		public enum CreateState
		{
			Normal,
			New,
			Deleted
		}
		public CreateState mCreateState;

		[Reflect]
		public String mFilter = new String() ~ delete _;
		[Reflect]
		public BuildOptions.SIMDSetting? mBfSIMDSetting;
		[Reflect]
		public BuildOptions.BfOptimizationLevel? mBfOptimizationLevel;
		[Reflect]
		public BuildOptions.EmitDebugInfo? mEmitDebugInfo;
		[Reflect]
		public bool? mRuntimeChecks;
		[Reflect]
		public bool? mInitLocalVariables;
		[Reflect]
		public bool? mEmitDynamicCastCheck;
		[Reflect]
		public bool? mEmitObjectAccessCheck; // Only valid with mObjectHasDebugFlags
		[Reflect]
		public bool? mArithmeticCheck;
		[Reflect]
		public int32? mAllocStackTraceDepth;
		[Reflect]
		public BuildOptions.AlwaysIncludeKind mReflectAlwaysInclude;
		[Reflect]
		public bool? mReflectBoxing;
		[Reflect]
		public bool? mReflectStaticFields;
		[Reflect]
		public bool? mReflectNonStaticFields;
		[Reflect]
		public bool? mReflectStaticMethods;
		[Reflect]
		public bool? mReflectNonStaticMethods;
		[Reflect]
		public bool? mReflectConstructors;
		[Reflect]
		public String mReflectMethodFilter = new String() ~ delete _;

		public ~this()
		{
		}

		public DistinctBuildOptions Duplicate()
		{
			var newVal = new DistinctBuildOptions();
			newVal.mFilter.Set(mFilter);
			newVal.mBfSIMDSetting = mBfSIMDSetting;
			newVal.mBfOptimizationLevel = mBfOptimizationLevel;
			newVal.mEmitDebugInfo = mEmitDebugInfo;
			newVal.mRuntimeChecks = mRuntimeChecks;
			newVal.mInitLocalVariables = mInitLocalVariables;
			newVal.mEmitDynamicCastCheck = mEmitDynamicCastCheck;
			newVal.mEmitObjectAccessCheck = mEmitObjectAccessCheck;
			newVal.mArithmeticCheck = mArithmeticCheck;
			newVal.mAllocStackTraceDepth = mAllocStackTraceDepth;
			newVal.mReflectAlwaysInclude = mReflectAlwaysInclude;
			newVal.mReflectBoxing = mReflectBoxing;
			newVal.mReflectStaticFields = mReflectStaticFields;
			newVal.mReflectNonStaticFields = mReflectNonStaticFields;
			newVal.mReflectStaticMethods = mReflectStaticMethods;
			newVal.mReflectNonStaticMethods = mReflectNonStaticMethods;
			newVal.mReflectConstructors = mReflectConstructors;
			newVal.mReflectMethodFilter.Set(mReflectMethodFilter);
			return newVal;
		}

		public void Deserialize(StructuredData data)
		{
			data.GetString("Filter", mFilter);
			if (data.Contains("BfSIMDSetting"))
				mBfSIMDSetting = data.GetEnum<BuildOptions.SIMDSetting>("BfSIMDSetting");
			if (data.Contains("BfOptimizationLevel"))
				mBfOptimizationLevel = data.GetEnum<BuildOptions.BfOptimizationLevel>("BfOptimizationLevel");
			if (data.Contains("EmitDebugInfo"))
				mEmitDebugInfo = data.GetEnum<BuildOptions.EmitDebugInfo>("EmitDebugInfo");
			if (data.Contains("RuntimeChecks"))
				mRuntimeChecks = data.GetBool("RuntimeChecks");
			if (data.Contains("InitLocalVariables"))
				mInitLocalVariables = data.GetBool("InitLocalVariables");
			if (data.Contains("EmitDynamicCastCheck"))
				mEmitDynamicCastCheck = data.GetBool("EmitDynamicCastCheck");
			if (data.Contains("EmitObjectAccessCheck"))
				mEmitObjectAccessCheck = data.GetBool("EmitObjectAccessCheck");
			if (data.Contains("ArithmeticCheck"))
				mArithmeticCheck = data.GetBool("ArithmeticCheck");
			if (data.Contains("AllocStackTraceDepth"))
				mAllocStackTraceDepth = data.GetInt("AllocStackTraceDepth");

			if (data.Contains("ReflectAlwaysInclude"))
				mReflectAlwaysInclude = data.GetEnum<BuildOptions.AlwaysIncludeKind>("ReflectAlwaysInclude");
			if (data.Contains("ReflectBoxing"))
				mReflectBoxing = data.GetBool("ReflectBoxing");
			if (data.Contains("ReflectStaticFields"))
				mReflectStaticFields = data.GetBool("ReflectStaticFields");
			if (data.Contains("ReflectNonStaticFields"))
				mReflectNonStaticFields = data.GetBool("ReflectNonStaticFields");
			if (data.Contains("ReflectStaticMethods"))
				mReflectStaticMethods = data.GetBool("ReflectStaticMethods");
			if (data.Contains("ReflectNonStaticMethods"))
				mReflectNonStaticMethods = data.GetBool("ReflectNonStaticMethods");
			if (data.Contains("ReflectConstructors"))
				mReflectConstructors = data.GetBool("ReflectConstructors");
			data.GetString("ReflectMethodFilter", mReflectMethodFilter);
		}
		
		public void Serialize(StructuredData data)
		{
			data.Add("Filter", mFilter);
			data.ConditionalAdd("BfSIMDSetting", mBfSIMDSetting);
			data.ConditionalAdd("BfOptimizationLevel", mBfOptimizationLevel);
			data.ConditionalAdd("EmitDebugInfo", mEmitDebugInfo);
			data.ConditionalAdd("RuntimeChecks", mRuntimeChecks);
			data.ConditionalAdd("InitLocalVariables", mInitLocalVariables);
			data.ConditionalAdd("EmitDynamicCastCheck", mEmitDynamicCastCheck);
			data.ConditionalAdd("EmitObjectAccessCheck", mEmitObjectAccessCheck);
			data.ConditionalAdd("ArithmeticCheck", mArithmeticCheck);
			data.ConditionalAdd("AllocStackTraceDepth", mAllocStackTraceDepth);
			data.ConditionalAdd("ReflectAlwaysInclude", mReflectAlwaysInclude);
			data.ConditionalAdd("ReflectBoxing", mReflectBoxing);
			data.ConditionalAdd("ReflectStaticFields", mReflectStaticFields);
			data.ConditionalAdd("ReflectNonStaticFields", mReflectNonStaticFields);
			data.ConditionalAdd("ReflectStaticMethods", mReflectStaticMethods);
			data.ConditionalAdd("ReflectNonStaticMethods", mReflectNonStaticMethods);
			data.ConditionalAdd("ReflectConstructors", mReflectConstructors);
			data.ConditionalAdd("ReflectMethodFilter", mReflectMethodFilter);
		}
	}
}
