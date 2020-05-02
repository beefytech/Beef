using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Reflection;
using System.Diagnostics;

namespace Beefy.gfx
{
    public class VertexDefinition
    {
        enum VertexElementFormat
        {
            Float,
            Vector2,
            Vector3,
            Vector4,
            Color,
            Byte4,
            Short2,
            Short4,
            NormalizedShort2,
            NormalizedShort4,
            HalfVector2,
            HalfVector4
        }

		[CRepr]
        struct VertexDefData
        {
            public VertexElementUsage mUsage;
            public int32 mUsageIndex;
            public VertexElementFormat mFormat;            
        }

#if !STUDIO_CLIENT
        [StdCall, CLink]
        static extern void* Gfx_CreateVertexDefinition(VertexDefData* elementData, int32 numElements);

        [StdCall, CLink]
        static extern void Gfx_VertexDefinition_Delete(void* nativeVertexDefinition);

        public void* mNativeVertexDefinition;
#else
        public IPCProxy<IStudioVertexDefinition> mStudioVertexDefinition;
#endif

        public int32 mVertexSize;
        public int32 mPosition2DOffset = -1;

        public static void FindPrimitives(Type type, List<Type> primitives)
        {
			if ((type.IsPrimitive) /*|| (field.FieldType.IsArray)*/)
			{
			    primitives.Add(type);
				return;
			}

			var typeInst = type as TypeInstance;
			if (typeInst == null)
				return;
			
            for (var field in typeInst.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance))
            {
                FindPrimitives(field.FieldType, primitives);
            }
        }

#if !STUDIO_CLIENT
        public static VertexDefinition CreateFromData(void* elementData, int32 numElements, int32 vertexSize)
        {
            var vertexDefinition = new VertexDefinition();
            vertexDefinition.mNativeVertexDefinition = Gfx_CreateVertexDefinition((VertexDefData*)elementData, numElements);
            vertexDefinition.mVertexSize = vertexSize;
            return vertexDefinition;
        }
#endif

        public this()
        {

        }

		public ~this()
		{
		    Gfx_VertexDefinition_Delete(mNativeVertexDefinition);
		}

        public this(Type type)
        {
			var typeInst = type as TypeInstance;
            var vertexDefDataArray = scope VertexDefData[typeInst.FieldCount];

            mVertexSize = typeInst.InstanceSize;
            
            List<Type> primitives = scope List<Type>(3);

            int32 fieldIdx = 0;
            for (var field in typeInst.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance))
            {
                var memberAttribute = field.GetCustomAttribute<VertexMemberAttribute>().Get();
				
                var vertexDefData = VertexDefData();
                
                vertexDefData.mUsage = memberAttribute.mElementUsage;
                vertexDefData.mUsageIndex = memberAttribute.mUsageIndex;
                
                primitives.Clear();
                FindPrimitives(field.FieldType, primitives);

                int32 floats = 0;
                int32 shorts = 0;
                int32 colors = 0;

                for (var prim in primitives)
                {
                    if (prim == typeof(float))
                        floats++;
                    else if (prim == typeof(uint16))
                        shorts++;
                    else if (prim == typeof(uint32))
                        colors++;
                }

                if (memberAttribute.mElementUsage == VertexElementUsage.Position2D)
                    mPosition2DOffset = field.MemberOffset;

                if (floats != 0)
                {
                    Debug.Assert(floats == primitives.Count);
                    Debug.Assert(floats <= 4);
                    vertexDefData.mFormat = VertexElementFormat.Float + floats - 1;
                }
                else if (shorts != 0)
                {
                    if (shorts == 2)
                        vertexDefData.mFormat = VertexElementFormat.Short2;
                    else if (shorts == 4)
                        vertexDefData.mFormat = VertexElementFormat.Short4;
                    else
                        Runtime.FatalError("Invalid short count");
                }
                else if (colors != 0)
                {
                    if (colors == 1)
                        vertexDefData.mFormat = VertexElementFormat.Color;
                    else
                        Runtime.FatalError("Invalid color count");
                }                

                vertexDefDataArray[fieldIdx++] = vertexDefData;
            }

#if !STUDIO_CLIENT
            mNativeVertexDefinition = Gfx_CreateVertexDefinition(vertexDefDataArray.CArray(), fieldIdx);
#else
            fixed (VertexDefData* vertexDefData = vertexDefDataArray)
            {
                IPCObjectId objId = BFApp.sApp.mStudioHost.Proxy.CreateVertexDefinition(Marshal.SizeOf(typeof(VertexDefData)) * vertexDefDataArray.Length, vertexDefData, vertexDefDataArray.Length, mVertexSize);
                mStudioVertexDefinition = IPCProxy<IStudioVertexDefinition>.Create(objId);
            }
#endif
        }
    }
}
