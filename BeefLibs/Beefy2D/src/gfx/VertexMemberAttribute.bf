using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;

namespace Beefy.gfx
{
	[AttributeUsage(.Field, .ReflectAttribute, ReflectUser=.All)]
    public struct VertexMemberAttribute : Attribute
    {        
        public VertexElementUsage mElementUsage;
        public int32 mUsageIndex;

        public this(VertexElementUsage elementUsage, int32 usageIndex = 0)
        {
            mElementUsage = elementUsage;
            mUsageIndex = usageIndex;
        }
    }
}
