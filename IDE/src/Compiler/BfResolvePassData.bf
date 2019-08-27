using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace IDE.Compiler
{
    public class BfResolvePassData
    {
        [StdCall, CLink]
        static extern void BfResolvePassData_Delete(void* bfResolvePassData);

        [StdCall, CLink]
        static extern void BfResolvePassData_SetLocalId(void* bfResolvePassData, int32 localId);

		[StdCall, CLink]
		static extern void BfResolvePassData_SetTypeGenericParamIdx(void* resolvePassData, int typeGenericParamIdx);

		[StdCall, CLink]
		static extern void BfResolvePassData_SetMethodGenericParamIdx(void* resolvePassData, int typeGenericParamIdx);

        [StdCall, CLink]
        static extern void BfResolvePassData_SetSymbolReferenceTypeDef(void* bfResolvePassData, char8* replaceStr);

        [StdCall, CLink]
        static extern void BfResolvePassData_SetSymbolReferenceFieldIdx(void* bfResolvePassData, int32 fieldIdx);

        [StdCall, CLink]
        static extern void BfResolvePassData_SetSymbolReferenceMethodIdx(void* bfResolvePassData, int32 methodIdx);

        [StdCall, CLink]
        static extern void BfResolvePassData_SetSymbolReferencePropertyIdx(void* bfResolvePassData, int32 propertyIdx);        

		[StdCall, CLink]
		static extern void BfResolvePassData_SetDocumentationRequest(void* bfResolvePassData, char8* entryName);

        //

        //[StdCall, CLink]
        //static extern void* BfParser_CreateResolvePassData(void* bfSystem, int32 resolveType);

        public void* mNativeResolvePassData;

        public ~this()
        {
            BfResolvePassData_Delete(mNativeResolvePassData);
        }

        public void SetLocalId(int32 localId)
        {
            BfResolvePassData_SetLocalId(mNativeResolvePassData, localId);
        }

		public void SetTypeGenericParamIdx(int32 genericParamIdx)
		{
			BfResolvePassData_SetTypeGenericParamIdx(mNativeResolvePassData, genericParamIdx);
		}

		public void SetMethodGenericParamIdx(int32 genericParamIdx)
		{
			BfResolvePassData_SetMethodGenericParamIdx(mNativeResolvePassData, genericParamIdx);
		}
        
        public void SetSymbolReferenceTypeDef(String typeDefName)
        {
            BfResolvePassData_SetSymbolReferenceTypeDef(mNativeResolvePassData, typeDefName);
        }

        public void SetSymbolReferenceFieldIdx(int32 fieldIdx)
        {
            BfResolvePassData_SetSymbolReferenceFieldIdx(mNativeResolvePassData, fieldIdx);
        }

        public void SetSymbolReferenceMethodIdx(int32 methodIdx)
        {
            BfResolvePassData_SetSymbolReferenceMethodIdx(mNativeResolvePassData, methodIdx);
        }

        public void SetSymbolReferencePropertyIdx(int32 propertyIdx)
        {
            BfResolvePassData_SetSymbolReferencePropertyIdx(mNativeResolvePassData, propertyIdx);
        }        

		public void SetDocumentationRequest(String entryName)
		{
			BfResolvePassData_SetDocumentationRequest(mNativeResolvePassData, entryName);
		}

        public static BfResolvePassData Create(ResolveType resolveType = ResolveType.Autocomplete)
        {
            var resolvePassData = new BfResolvePassData();
            resolvePassData.mNativeResolvePassData = BfParser.[Friend]BfParser_CreateResolvePassData(null, (int32)resolveType);
            return resolvePassData;
        }
    }
}
