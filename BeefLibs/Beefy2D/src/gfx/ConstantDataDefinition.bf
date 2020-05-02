using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Reflection;
using System.Diagnostics;

namespace Beefy.gfx
{
    public class ConstantDataDefinition
    {
        public enum DataType
        {
            Float,
            Vector2,
            Vector3,
            Vector4,
            Matrix,

			VertexShaderUsage = 0x100,
			PixelShaderUsage = 0x200
        }

        public int32 mDataSize;
        public DataType[] mDataTypes ~ delete _;

		public this(int32 dataSize, DataType[] dataTypes)
		{
			mDataSize = dataSize;
			mDataTypes = dataTypes;
		}

        public this(Type type)
        {
			ThrowUnimplemented();

            /*var fields = type.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);            
            int memberCount = fields.Length;
            mDataTypes = new int[memberCount];

            mDataSize = Marshal.SizeOf(type);
            
            List<Type> primitives = new List<Type>();

            int fieldIdx = 0;
            foreach (var field in fields)                
            {
                var memberAttribute = field.GetCustomAttribute<VertexMemberAttribute>();
                
                primitives.Clear();
                VertexDefinition.FindPrimitives(field.FieldType, primitives);

                int floats = 0;
                int shorts = 0;
                int colors = 0;

                foreach (var prim in primitives)
                {
                    if (prim == typeof(float))
                        floats++;
                    else if (prim == typeof(ushort))
                        shorts++;
                    else if (prim == typeof(uint))
                        colors++;
                }

                DataType dataType = DataType.Single;
                int usageType = 1;

                if (floats != 0)
                {
                    Debug.Assert(floats == primitives.Count);
                    if (floats == 16)
                        dataType = DataType.Matrix;
                    else
                    {
                        Debug.Assert(floats <= 4);
                        dataType = DataType.Single + floats - 1;
                    }
                }
                else if (shorts != 0)
                {
                    /*if (shorts == 2)
                        dataType = DataType.Short2;
                    else if (shorts == 4)
                        dataType = DataType.Short4;
                    else*/
                        Debug.Fail("Invalid short count");
                }
                else if (colors != 0)
                {
                    /*if (colors == 1)
                        vertexDefData.mFormat = VertexElementFormat.Color;
                    else*/
                        Debug.Fail("Invalid color count");
                }

                mDataTypes[fieldIdx++] = (int)dataType | (usageType << 8);
            } */
        }
    }
}
