// dbz3: night sky of "Clima vivo" (dbz3_clima_noche). Runs where the relight
// does (dbz3_relight.hlsl): once per frame on the composed 3D scene, before the
// HUD, with the scene depth copied earlier, drawn with the relight full-screen
// vertex shader. Budokai 3 skies are flat material colours (not textures), so
// the far part of the scene (sky dome, clouds, distant scenery) is blended
// toward a navy gradient by its distance; the near ground and the fighters
// stay as they are.
//
// Rebuild the header (Windows SDK fxc) with:
//   fxc /nologo /T ps_5_1 /E night_ps /O3 /Vn dbz3_night_ps
//       /Fh bytecode/d3d12_5_1/dbz3_night_ps.h dbz3_night.hlsl

cbuffer Dbz3NightConstants : register(b0) {
  uint2 nt_size;       // Pixels processed.
  float nt_strength;   // Maximum blend toward the night colour (0..1).
  float nt_near;       // Distance (units of the near plane) where it starts...
  float nt_far;        // ...and where it is full.
  float3 nt_top;       // Night colour at the top of the screen...
  float3 nt_horizon;   // ...and at the bottom.
  float nt_unused;
};

Texture2D<float> nt_depth : register(t0);

float4 night_ps(float4 position : SV_Position) : SV_Target {
  int2 p = int2(position.xy);
  float d = nt_depth.Load(int3(clamp(p, int2(0, 0), int2(nt_size) - 1), 0));
  float z = 1.0 / max(1.0 - d, 1.0e-7);   // 0 near, 1 far (Direct3D 9 style)
  float w = smoothstep(nt_near, nt_far, z);
  float3 c = lerp(nt_top, nt_horizon, smoothstep(0.0, 0.9, position.y / float(nt_size.y)));
  return float4(c, nt_strength * w);
}
