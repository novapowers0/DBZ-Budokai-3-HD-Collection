// DBZ3 HD Collection - upscale de texturas guest en la cache de texturas host
// (capa exterior: NO se modifica ningun fichero del juego).
//
// Recibe la textura ya desempaquetada/descomprimida por los load shaders (un
// buffer RGBA8 a resolucion 1x, fila a fila con su pitch) y escribe el NIVEL
// `xe_level` del recurso host (a Nx, N = xe_factor) con un kernel Catmull-Rom
// (bicubico). El nivel 0 se lee directamente; los niveles siguientes se generan
// promediando el bloque 2^level x 2^level del nivel 0 (box filter), de modo que
// la cadena de mips sea coherente sin depender del empaquetado de mips del
// guest. Se ejecuta UNA sola vez por nivel y por carga de textura (no por frame).

ByteAddressBuffer xe_upscale_source : register(t0);
RWTexture2D<float4> xe_upscale_dest : register(u0);

cbuffer xe_upscale_constants : register(b0) {
  uint xe_src_width;   // ancho del NIVEL 0 del guest
  uint xe_src_height;  // alto del NIVEL 0 del guest
  uint xe_dst_width;   // ancho del nivel destino (ya multiplicado por factor)
  uint xe_dst_height;  // alto del nivel destino
  uint xe_src_pitch;   // pitch de la fila del buffer 1x (nivel 0)
  uint xe_src_offset;  // offset del nivel 0 dentro del buffer
  uint xe_factor;      // factor de upscale (N)
  uint xe_level;       // nivel de mip que se escribe
};

float4 XeLoadSourceTexel(int2 xe_coord) {
  xe_coord = clamp(xe_coord, int2(0, 0),
                   int2(int(xe_src_width) - 1, int(xe_src_height) - 1));
  uint xe_offset = xe_src_offset + uint(xe_coord.y) * xe_src_pitch +
                   uint(xe_coord.x) * 4u;
  uint xe_value = xe_upscale_source.Load(xe_offset);
  return float4(float((xe_value >> 0u) & 0xFFu), float((xe_value >> 8u) & 0xFFu),
                float((xe_value >> 16u) & 0xFFu),
                float((xe_value >> 24u) & 0xFFu)) * (1.0 / 255.0);
}

// Texel del nivel `xe_level`, promediando el bloque 2^level x 2^level del nivel
// 0 (nivel 0 -> un solo texel, sin coste adicional).
//
// RENDIMIENTO: promediar el bloque COMPLETO es O(4^level) por tap y, peor aun,
// en los mips altos quedan muy pocos hilos de shader: p.ej. el nivel 9 de una
// textura 1024x512 tiene ~18 hilos, cada uno ejecutando 16 taps x 512x512 =
// 4,2M de lecturas EN SERIE con acumulador dependiente -> cientos de ms de
// frame (los tirones que se ven al cargar texturas nuevas). Se muestrea una
// rejilla de como maximo kXeMaxBlockSamples por eje (paso uniforme sobre el
// bloque): para niveles bajos (bloque <= 8) es EXACTO, y para los altos es una
// aproximacion que quita el blow-up de coste sin afectar visiblemente (los mips
// altos son minificacion borrosa). Reduce el trabajo por hilo de millones de
// iteraciones a <= 64.
static const uint kXeMaxBlockSamples = 8u;
float4 XeLoadLevelTexel(uint2 xe_level_size, int2 xe_coord) {
  xe_coord = clamp(xe_coord, int2(0, 0),
                   int2(int(xe_level_size.x) - 1, int(xe_level_size.y) - 1));
  uint xe_block = 1u << xe_level;
  uint xe_step = max(xe_block / kXeMaxBlockSamples, 1u);
  int2 xe_base = xe_coord * int2(int(xe_block), int(xe_block));
  float4 xe_sum = 0.0;
  float xe_count = 0.0;
  for (uint xe_j = 0u; xe_j < xe_block; xe_j += xe_step) {
    for (uint xe_i = 0u; xe_i < xe_block; xe_i += xe_step) {
      xe_sum += XeLoadSourceTexel(xe_base + int2(int(xe_i), int(xe_j)));
      xe_count += 1.0;
    }
  }
  return xe_sum / xe_count;
}

[numthreads(8, 8, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  if (xe_thread_id.x >= xe_dst_width || xe_thread_id.y >= xe_dst_height) {
    return;
  }
  uint2 xe_level_size = uint2(max(xe_src_width >> xe_level, 1u),
                              max(xe_src_height >> xe_level, 1u));
  // Centro del texel de destino -> posicion en la imagen fuente del nivel.
  float2 xe_src_pos = (float2(xe_thread_id.xy) + 0.5) / float(xe_factor) - 0.5;
  int2 xe_base = int2(floor(xe_src_pos));
  float2 xe_frac = xe_src_pos - float2(xe_base);
  // Pesos Catmull-Rom (a = -0.5).
  float2 xe_w0 = xe_frac * (-0.5 + xe_frac * (1.0 - 0.5 * xe_frac));
  float2 xe_w1 = 1.0 + xe_frac * xe_frac * (-2.5 + 1.5 * xe_frac);
  float2 xe_w2 = xe_frac * (0.5 + xe_frac * (2.0 - 1.5 * xe_frac));
  float2 xe_w3 = xe_frac * xe_frac * (-0.5 + 0.5 * xe_frac);
  float2 xe_w[4] = {xe_w0, xe_w1, xe_w2, xe_w3};
  float4 xe_accum = 0.0;
  [unroll] for (int xe_j = 0; xe_j < 4; ++xe_j) {
    [unroll] for (int xe_i = 0; xe_i < 4; ++xe_i) {
      float4 xe_texel = XeLoadLevelTexel(xe_level_size, xe_base + int2(xe_i - 1, xe_j - 1));
      xe_accum += xe_texel * (xe_w[xe_i].x * xe_w[xe_j].y);
    }
  }
  // Clamp anti-ringing: el Catmull-Rom puro sobre/bajo-dispara en bordes de
  // contraste fuerte (contornos de glifos del HUD, letras), y ese halo se ve
  // como EMBORRONADO sucio. Se acota el resultado al rango [min, max] de las 16
  // muestras: mantiene la nitidez del kernel sin el sobre-disparo.
  float4 xe_min = 1.0;
  float4 xe_max = 0.0;
  [unroll] for (int xe_j2 = 0; xe_j2 < 4; ++xe_j2) {
    [unroll] for (int xe_i2 = 0; xe_i2 < 4; ++xe_i2) {
      float4 xe_t = XeLoadLevelTexel(xe_level_size, xe_base + int2(xe_i2 - 1, xe_j2 - 1));
      xe_min = min(xe_min, xe_t);
      xe_max = max(xe_max, xe_t);
    }
  }
  xe_upscale_dest[xe_thread_id.xy] = saturate(clamp(xe_accum, xe_min, xe_max));
}
