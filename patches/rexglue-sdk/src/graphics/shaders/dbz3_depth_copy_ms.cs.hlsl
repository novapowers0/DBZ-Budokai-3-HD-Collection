// dbz3 (R15): dbz3_depth_copy for a multisampled host depth render target (native 2x MSAA):
// sample 0 of each pixel.
//
// Rebuild the header with:
//   fxc /nologo /T cs_5_1 /E main /O3 /Vn dbz3_depth_copy_ms_cs
//       /Fh bytecode/d3d12_5_1/dbz3_depth_copy_ms_cs.h dbz3_depth_copy_ms.cs.hlsl

cbuffer Dbz3DepthCopyConstants : register(b0) {
  uint2 dbz3_size;     // Destination size in pixels.
  uint2 dbz3_offset;   // Source texel of destination (0, 0).
  uint dbz3_unused;
};

Texture2DMS<float> dbz3_source : register(t0);
RWTexture2D<float> dbz3_dest : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= dbz3_size)) {
    return;
  }
  dbz3_dest[id.xy] = dbz3_source.Load(int2(id.xy + dbz3_offset), 0);
}
