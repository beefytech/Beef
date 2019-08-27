using System;
using System.Collections.Generic;
using System.Text;

namespace Beefy.gfx
{
    public class ShaderParam
    {        
        public void* mNativeShaderParam;

        internal this(void* shaderParam)
        {
        }
    }

#if !STUDIO_CLIENT
    public class Shader
    {
        public void* mNativeShader;
        public Dictionary<String, ShaderParam> mShaderParamMap;

        [StdCall, CLink]
        static extern void* Gfx_LoadShader(char8* fileName, void* vertexDefinition);

        [StdCall, CLink]
        static extern void* Gfx_Shader_Delete(void* shader);

        [StdCall, CLink]
        static extern void* Gfx_GetShaderParam(void* shader, String paramName);

        public static Shader CreateFromFile(String fileName, VertexDefinition vertexDefinition)
        {
            void* aNativeShader = Gfx_LoadShader(fileName, vertexDefinition.mNativeVertexDefinition);
            if (aNativeShader == null)
                return null;

            Shader aShader = new Shader(aNativeShader);
            return aShader;
        }        

        internal this(void* nativeShader)
        {
            mNativeShader = nativeShader;        
        }

        public ~this()
        {
            Gfx_Shader_Delete(mNativeShader);
        }

        ShaderParam GetParam(String paramName)
        {
            ShaderParam aShaderParam = null;
            if (!mShaderParamMap.TryGetValue(paramName, out aShaderParam))
            {
                void* nativeShaderParam = Gfx_GetShaderParam(mNativeShader, paramName);
                if (nativeShaderParam != null)
                    aShaderParam = new ShaderParam(nativeShaderParam);
                mShaderParamMap[paramName] = aShaderParam;
            }
            return aShaderParam;
        }
    }
#else
    public class Shader : IStudioShader
    {
        public IPCProxy<IStudioShader> mStudioShader;

        public static Shader CreateFromFile(string fileName, VertexDefinition vertexDefinition)
        {
            Shader shader = new Shader();
            IPCObjectId objId = BFApp.StudioHostProxy.CreateShaderFromFile(fileName, vertexDefinition.mStudioVertexDefinition);
            shader.mStudioShader = IPCProxy<IStudioShader>.Create(objId);
            return shader;
        }

        internal Shader()
        {            
        }
    }
#endif
}
