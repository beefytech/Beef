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
        static extern void* Gfx_LoadShader(char8* fileName, void* vertexDefinition, char8* entrySuffix);

        [CallingConvention(.Stdcall), CLink]
        static extern void* Gfx_Shader_Delete(void* shader);

        [CallingConvention(.Stdcall), CLink]
        static extern void* Gfx_GetShaderParam(void* shader, String paramName);

        [CallingConvention(.Stdcall), CLink]
        static extern char8* Gfx_GetShaderError(void* shader);

        [CallingConvention(.Stdcall), CLink]
        static extern void Gfx_AddShaderIncludeDir(char8* dir);

        // Registers a search directory for #include resolution (after the including file's own
        // directory). Register before loading any shader that depends on it.
        public static void AddIncludeDir(StringView dir)
        {
            Gfx_AddShaderIncludeDir(dir.ToScopeCStr!());
        }

        // entrySuffix compiles alternate entry points ("VS"+suffix / "PS"+suffix) from the same
        // file -- how one surface-shader source yields its per-pass variants.
        //
        // A compile failure fills `outError` and returns null; with no `outError` it's fatal,
        // carrying the compiler's message -- so callers that can recover (hot reload, user
        // shaders) opt in, and everything else keeps fail-fast behavior.
        public static Shader CreateFromFile(StringView fileName, VertexDefinition vertexDefinition, StringView entrySuffix = "", String outError = null)
        {
			var useFileName = scope String(fileName);
			if (FilePackManager.TryMakeMemoryString(useFileName, scope $".fx_VS{entrySuffix}_vs_4_0"))
			{
				var useFileName2 = scope String(fileName);
				if (FilePackManager.TryMakeMemoryString(useFileName2, scope $".fx_PS{entrySuffix}_ps_4_0"))
				{
					useFileName.Append("\n");
					useFileName.Append(useFileName2);
				}
			}

			FilePackManager.TryMakeMemoryString(useFileName, ".fx");

            void* aNativeShader = Gfx_LoadShader(useFileName, vertexDefinition.mNativeVertexDefinition, entrySuffix.ToScopeCStr!());
            if (aNativeShader == null)
                return null;

            char8* error = Gfx_GetShaderError(aNativeShader);
            if (error != null)
            {
                if (outError != null)
                {
                    outError.Append(StringView(error));
                    Gfx_Shader_Delete(aNativeShader);
                    return null;
                }
                Runtime.FatalError(scope String(StringView(error)));
            }

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
