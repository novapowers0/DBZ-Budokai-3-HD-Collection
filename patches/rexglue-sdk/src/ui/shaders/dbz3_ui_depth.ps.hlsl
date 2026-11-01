// dbz3 (R15): pixel shader of the depth-tested immediate draws: the fragment is dropped where
// the game's scene depth (the R32 copy of the last whole-frame depth resolve, guest output
// size) is nearer. Without a scene depth (dbz3_enabled 0) it is the plain immediate.ps.
//
// Rebuild the header with:
//   fxc /nologo /T ps_5_1 /E main /O3 /Vn dbz3_ui_depth_ps
//       /Fh bytecode/d3d12_5_1/dbz3_ui_depth_ps.h dbz3_ui_depth.ps.hlsl

cbuffer Dbz3UiDepthConstants : register(b1) {
  float2 dbz3_origin;    // Guest output top-left in render target pixels.
  float2 dbz3_scale;     // Depth texels per render target pixel.
  float2 dbz3_size;      // Depth texture size.
  float dbz3_enabled;    // 0: no scene depth this paint.
  float dbz3_inverted;   // 1: greater = nearer (the guest's convention).
};

Texture2D<float4> xe_immediate_texture : register(t0);
SamplerState xe_immediate_texture_xe_sampler : register(s0);
Texture2D<float> dbz3_scene_depth : register(t1);

float4 main(float2 xe_texcoord : TEXCOORD0, float xe_depth : TEXCOORD1, float4 xe_color : COLOR,
            float4 xe_position : SV_Position) : SV_Target {
  // A negative vertex depth (a camera without the guest projection) is never tested.
  if (dbz3_enabled == 2.0) {
    // Probe (DBZ3_ZBUF_DEBUG): the scene depth behind the fragment, r + g / 255, and b = the
    // inversion flag; alpha 1.
    float2 q = clamp((xe_position.xy - dbz3_origin) * dbz3_scale, 0.0, dbz3_size - 1.0);
    float sd = dbz3_scene_depth.Load(int3(q, 0));
    float hi = floor(sd * 255.0) / 255.0;
    return float4(hi, saturate((sd - hi) * 255.0), dbz3_inverted, 1.0);
  }
  if (dbz3_enabled != 0.0 && xe_depth >= 0.0) {
    float2 p = (xe_position.xy - dbz3_origin) * dbz3_scale;
    if (all(p >= 0.0) && all(p < dbz3_size)) {
      float scene = dbz3_scene_depth.Load(int3(p, 0));
      // A small margin so the model's own contact with the ground is not cut.
      if (dbz3_inverted != 0.0 ? xe_depth < scene - 2.0e-6 : xe_depth > scene + 2.0e-6) {
        discard;
      }
    }
  }
  return xe_immediate_texture.SampleLevel(xe_immediate_texture_xe_sampler, xe_texcoord, 0.0) *
         xe_color;
}
