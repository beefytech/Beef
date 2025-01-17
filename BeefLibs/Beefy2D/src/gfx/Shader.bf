using System;
using System.Collections;
using System.Text;
using res;

namespace Beefy.gfx
{
    public class ShaderParam
    {        
        public void* mNativeShaderParam;

        public this(void* shaderParam)
        {
        }
    }

#if !STUDIO_CLIENT
    public class Shader
    {
        public void* mNativeShader;
        public Dictionary<String, ShaderParam> mShaderParamMap;

        [CallingConvention(.Stdcall), CLink]
        static extern void* Gfx_LoadShader(char8* fileName, void* vertexDefinition);

        [CallingConvention(.Stdcall), CLink]
        static extern void* Gfx_Shader_Delete(void* shader);

        [CallingConvention(.Stdcall), CLink]
        static extern void* Gfx_GetShaderParam(void* shader, String paramName);

        public static Shader CreateFromFile(StringView fileName, VertexDefinition vertexDefinition)
        {
			var useFileName = scope String(fileName);
			if (FilePackManager.TryMakeMemoryString(useFileName, ".fx_VS_vs_4_0"))
			{
				var useFileName2 = scope String(fileName);
				if (FilePackManager.TryMakeMemoryString(useFileName2, ".fx_PS_ps_4_0"))
				{
					useFileName.Append("\n");
					useFileName.Append(useFileName2);
				}
			}

			FilePackManager.TryMakeMemoryString(useFileName, ".fx");

            void* aNativeShader = Gfx_LoadShader(useFileName, vertexDefinition.mNativeVertexDefinition);
            if (aNativeShader == null)
                return null;

            Shader aShader = new Shader(aNativeShader);
            return aShader;
        }        

        public this(void* nativeShader)
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
