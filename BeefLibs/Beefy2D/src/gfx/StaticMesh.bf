using System;
using System.Diagnostics;

namespace Beefy.gfx;


// Immutable GPU vertex/index buffers uploaded once, drawn instanced with
// Graphics.DrawStaticMeshInstanced. Delete only once no queued draw can still reference it
// (ie after the layers that drew it have flushed).
public class StaticMesh
{
	[CallingConvention(.Stdcall), CLink]
	static extern void* Gfx_CreateStaticMesh(int32 vertexSize, void* vtxData, int32 vtxCount, void* idxData, int32 idxCount, int32 idx32);

	[CallingConvention(.Stdcall), CLink]
	static extern void Gfx_StaticMesh_Delete(void* mesh);

	public void* mNativeMesh;
	public int32 mVtxCount;
	public int32 mIdxCount;

	public static StaticMesh Create(VertexDefinition vertexDef, void* vertices, int vtxCount, uint16* indices, int idxCount)
	{
		return Create(vertexDef.mVertexSize, vertices, vtxCount, indices, idxCount, false);
	}

	public static StaticMesh Create(VertexDefinition vertexDef, void* vertices, int vtxCount, uint32* indices, int idxCount)
	{
		return Create(vertexDef.mVertexSize, vertices, vtxCount, indices, idxCount, true);
	}

	static StaticMesh Create(int32 vertexSize, void* vertices, int vtxCount, void* indices, int idxCount, bool idx32)
	{
		void* native = Gfx_CreateStaticMesh(vertexSize, vertices, (int32)vtxCount, indices, (int32)idxCount, idx32 ? 1 : 0);
		if (native == null)
			return null;
		StaticMesh mesh = new StaticMesh();
		mesh.mNativeMesh = native;
		mesh.mVtxCount = (int32)vtxCount;
		mesh.mIdxCount = (int32)idxCount;
		return mesh;
	}

	public ~this()
	{
		if (mNativeMesh != null)
			Gfx_StaticMesh_Delete(mNativeMesh);
	}
}
