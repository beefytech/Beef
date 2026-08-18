using System;

namespace Beefy.gfx;

// One compute entry point of a .fx file (default "CS", profile cs_5_0). Bind resources with
// Graphics.SetComputeTexture / SetComputeUAV / SetComputeConstantData, then Graphics.Dispatch.
public class ComputeShader
{
	[CallingConvention(.Stdcall), CLink]
	static extern void* Gfx_LoadComputeShader(char8* fileName, char8* entry);

	[CallingConvention(.Stdcall), CLink]
	static extern void Gfx_ComputeShader_Delete(void* shader);

	public void* mNativeShader;

	public static ComputeShader CreateFromFile(StringView fileName, StringView entry = "CS")
	{
		void* native = Gfx_LoadComputeShader(scope String(fileName), scope String(entry));
		if (native == null)
			return null;
		ComputeShader shader = new ComputeShader();
		shader.mNativeShader = native;
		return shader;
	}

	public ~this()
	{
		if (mNativeShader != null)
			Gfx_ComputeShader_Delete(mNativeShader);
	}
}
