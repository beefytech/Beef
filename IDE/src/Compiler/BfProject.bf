using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;

namespace IDE.Compiler
{
    public class BfProject
    {
		enum Flags : int32
		{
			None           	= 0,
			MergeFunctions 	= 1,
			CombineLoads   	= 2,
			VectorizeLoops 	= 4,
			VectorizeSLP   	= 8,
			SingleModule   	= 0x10,
			AsmOutput		= 0x20,
			AsmOutput_ATT	= 0x40,
		}

        [StdCall, CLink]
        extern static void BfProject_Delete(void* nativeBfProject);

        [StdCall, CLink]
        extern static void BfProject_ClearDependencies(void* nativeBfProject);

        [StdCall, CLink]
        extern static void BfProject_AddDependency(void* nativeBfProject, void* nativeDepProject);

        [StdCall, CLink]
        extern static void BfProject_SetDisabled(void* nativeBfProject, bool disabled);

        [StdCall, CLink]
        extern static void BfProject_SetOptions(void* nativeBfProject, int32 targetType, char8* startupObject, char8* preprocessorMacros,
            int32 optLevel, int32 ltoType, int32 relocType, int32 picLevel, Flags flags);

        public void* mNativeBfProject;
        public bool mDisabled;

        public void Dispose()
        {            
            BfProject_Delete(mNativeBfProject);
        }

        public void ClearDependencies()
        {
            BfProject_ClearDependencies(mNativeBfProject);
        }

        public void AddDependency(BfProject depProject)
        {
            BfProject_AddDependency(mNativeBfProject, depProject.mNativeBfProject);
        }

        public void SetDisabled(bool disabled)
        {
            mDisabled = disabled;
            BfProject_SetDisabled(mNativeBfProject, disabled);
        }

        public void SetOptions(Project.TargetType targetType, String startupObject, List<String> preprocessorMacros,
            BuildOptions.BfOptimizationLevel optLevel, BuildOptions.LTOType ltoType, BuildOptions.RelocType relocType, BuildOptions.PICLevel picLevel,
			bool mergeFunctions, bool combineLoads, bool vectorizeLoops, bool vectorizeSLP)
        {
			Flags flags = default;
			void SetFlags(bool val, Flags flag)
			{
				if (val)
					flags |= flag;
			}
			SetFlags(mergeFunctions, .MergeFunctions);
			SetFlags(combineLoads, .CombineLoads);
			SetFlags(vectorizeLoops, .VectorizeLoops);
			SetFlags(vectorizeSLP, .VectorizeSLP);

            String macrosStr = scope String();
            macrosStr.Join("\n", preprocessorMacros.GetEnumerator());
            BfProject_SetOptions(mNativeBfProject, (int32)targetType, startupObject, macrosStr, 
                (int32)optLevel, (int32)ltoType, (int32)relocType, (int32)picLevel, flags);
        }

    }
}
