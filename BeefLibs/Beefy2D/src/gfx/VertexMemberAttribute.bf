using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;

namespace Beefy.gfx
{
	[AttributeUsage(.Field, .ReflectAttribute, ReflectUser=.All)]
    public struct VertexMemberAttribute : Attribute
    {        
        public VertexElementUsage mElementUsage;
        public int32 mUsageIndex;
		// Instanced draws (Graphics.DrawStaticMeshInstanced) feed this element from a per-instance
		// stream instead of the vertex; at most one per vertex type.
		public bool mPerInstance;

        public this(VertexElementUsage elementUsage, int32 usageIndex = 0, bool perInstance = false)
        {
            mElementUsage = elementUsage;
            mUsageIndex = usageIndex;
			mPerInstance = perInstance;
        }
    }
}
