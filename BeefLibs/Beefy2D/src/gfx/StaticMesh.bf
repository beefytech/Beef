using System;
using System.Diagnostics;
using Beefy.geom;

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

	[CallingConvention(.Stdcall), CLink]
	static extern void Gfx_StaticMesh_SetDepthStream(void* mesh, void* data, int32 vtxCount);

	// What a depth-only pass actually reads. CRepr because a uint64 beside a Vector3 is exactly the
	// mixed-alignment case Beef reorders (see ModelDef.VertexDef).
	[CRepr]
	public struct DepthVertex
	{
		public Vector3 mPosition;
		public uint32 mBoneIndices;
		public uint64 mBoneWeights;
	}

	public void* mNativeMesh;
	public int32 mVtxCount;
	public int32 mIdxCount;
	// Set once a compact stream has been uploaded for this mesh.
	public bool mHasDepthStream;

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

	// The same vertices in the same order over the same index buffer, position and bone slots only:
	// a depth-only pass then pulls 24 bytes a vertex instead of 72. Shaders whose vertex stage reads
	// more than that silently keep the full stream (see DXShader::Load).
	public void SetDepthStream(Span<DepthVertex> vertices)
	{
		if (vertices.Length != mVtxCount)
			return;
		Gfx_StaticMesh_SetDepthStream(mNativeMesh, vertices.Ptr, (int32)vertices.Length);
		mHasDepthStream = true;
	}

	public ~this()
	{
		if (mNativeMesh != null)
			Gfx_StaticMesh_Delete(mNativeMesh);
	}
}
