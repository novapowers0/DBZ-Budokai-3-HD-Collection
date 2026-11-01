// dbz3 (R15): vertex shader of the depth-tested immediate draws (textures created with
// ImmediateTextureFilter::kDbz3LinearDepth). Same positions as immediate.vs; TEXCOORD carries
// the texture coordinates as two halves in x and the guest depth (0..1) of the vertex in y.
//
// Rebuild the header with:
//   fxc /nologo /T vs_5_1 /E main /O3 /Vn dbz3_ui_depth_vs
//       /Fh bytecode/d3d12_5_1/dbz3_ui_depth_vs.h dbz3_ui_depth.vs.hlsl

cbuffer push_consts_xe : register(b0) {
  float2 xe_coordinate_space_size_inv;
};

void main(float2 xe_in_position : POSITION, uint2 xe_in_packed : TEXCOORD,
          float4 xe_in_color : COLOR, out float2 xe_out_texcoord : TEXCOORD0,
          out float xe_out_depth : TEXCOORD1, out float4 xe_out_color : COLOR,
          out float4 xe_out_position : SV_Position) {
  xe_out_texcoord = float2(f16tof32(xe_in_packed.x), f16tof32(xe_in_packed.x >> 16u));
  xe_out_depth = asfloat(xe_in_packed.y);
  xe_out_color = xe_in_color;
  xe_out_position = float4(xe_in_position * xe_coordinate_space_size_inv * float2(2.0, -2.0) +
                               float2(-1.0, 1.0),
                           0.0, 1.0);
}
