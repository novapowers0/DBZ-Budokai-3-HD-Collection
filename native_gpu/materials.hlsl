// Host shaders written from the documented Budokai 3 material semantics.
// No Xbox microcode, Xenos registers, PM4 packets, or translated shaders.
cbuffer Frame : register(b0) {
    float4 centerScale; // center xyz, orthographic scale
    float4 view;        // cos(yaw), sin(yaw), height/width, rim strength
    float4 lightPass;   // light xyz, 0=material / 1=outline
    float4 diffuse;    // diffuse rgb, 1=base texture / 0=flat material
    float4 eyeMode;    // camera position xyz, 0=viewer / 1=live world-space frame
    row_major float4x4 viewProjection;
    float4 liveControl;  // UV offset xy, apply fog z, 0=viewer / 1=textured color / 2=shadow
    float4 liveFog;      // reserved for the validated game fog parameters
    float4 liveTint;     // per-draw modulation of CPU vertex colors
    float4 liveMix;      // RGB blend color and its interpolation factor
    float4 liveFogColor;
    float4 alphaTest; // enabled, native comparison code, reference, toon camera w
};
Texture2D<float4> baseTexture : register(t0);
Texture2D<float4> rampTexture : register(t1);
// Used only by the separate stencil-copy entry point; the color shader uses t1
// as a float texture. Each compiled entry point has one typed binding at t1.
Texture2D<uint> stencilTexture : register(t1);
StructuredBuffer<float4> characterPalette : register(t2);
SamplerState textureSampler : register(s0);
SamplerState rampSampler : register(s1);
SamplerState shadowSampler : register(s2); // checked mask fetch: point, clamp edge
SamplerState videoChromaSampler : register(s3); // checked video chroma: point, wrap
SamplerState imageMirrorSampler : register(s4);
SamplerState imageMirrorPointSampler : register(s5);
SamplerState imageWrapClampLinear : register(s6);
SamplerState imageWrapClampPoint : register(s7);
SamplerState imageWrapMirrorLinear : register(s8);
SamplerState imageWrapMirrorPoint : register(s9);
SamplerState imageClampWrapLinear : register(s10);
SamplerState imageClampWrapPoint : register(s11);
SamplerState imageClampMirrorLinear : register(s12);
SamplerState imageClampMirrorPoint : register(s13);

struct Vertex {
    float3 p : POSITION; float3 n : NORMAL; float2 uv : TEXCOORD0;
    float4 color : COLOR0; float fog : TEXCOORD1;
};
struct Fragment {
    float4 p : SV_POSITION;
    float3 n : NORMAL;
    float2 uv : TEXCOORD0;
    float3 cameraDirection : TEXCOORD1;
    float4 color : COLOR0;
    float fog : TEXCOORD2;
};
float3 rotate(float3 p) {
    return float3(view.x * p.x + view.y * p.z, p.y,
                  -view.y * p.x + view.x * p.z);
}
float4 restoreQuaternion(float3 xyz) {
    // The compact palette omits positive w. Keep float32 cancellation here:
    // reconstructing nearly 180-degree rotations in double changes the pose.
    precise float squared = dot(xyz, xyz);
    return float4(xyz, sqrt(abs(1 - squared)));
}
float3 quaternionRotate(float4 q, float3 v) {
    float3 t = 2 * cross(q.xyz, v) / dot(q, q);
    return v + q.w * t + cross(q.xyz, t);
}
Fragment vertexMain(Vertex v) {
    Fragment o = (Fragment)0;
    float3 n = rotate(v.n);
    float3 p = rotate(v.p - centerScale.xyz);
    // Inverted-hull outline; native draw, separate PSO and depth handling.
    if (lightPass.w > 0.5) p += n * (0.006 / centerScale.w);
    o.p = float4(p.x * centerScale.w * view.z, p.y * centerScale.w,
                 0.5 - p.z * centerScale.w * 0.08, 1);
    o.n = n;
    o.uv = v.uv;
    o.cameraDirection = float3(0, 0, 1);
    o.color = v.color;
    o.fog = v.fog;
    if (eyeMode.w > 0.5) {
        float3 world = v.p;
        if (lightPass.w > 0.5) world += v.n * (0.006 / centerScale.w);
        o.p = mul(viewProjection, float4(world, 1));
        o.n = v.n;
        o.cameraDirection = eyeMode.xyz - world;
    }
    if (liveControl.w > 0.5) {
        o.p = mul(viewProjection, float4(v.p, 1));
        o.uv = v.uv + liveControl.xy;
        o.color = v.color * liveTint;
    }
    if (liveControl.w > 3.5 && liveControl.w < 12.5) {
        float3 p = v.p, n = v.n;
        uint mode = (uint)liveControl.w;
        bool coloredSkin = mode == 10;
        if (mode == 4 || mode == 7 || mode == 11 || coloredSkin) {
            uint joint = 4 + 3 * (uint)(coloredSkin ? v.n.x : v.color.x);
            float weight = coloredSkin ? v.n.y : v.color.y;
            if (coloredSkin) n = 0;
            float4 a = characterPalette[joint], b = characterPalette[joint + 1], c = characterPalette[joint + 2];
            float4 rotation = restoreQuaternion(float3(a.w, b.w, c.w));
            float4 first = restoreQuaternion(b.xyz), second = restoreQuaternion(c.xyz);
            if (dot(first, second) < 0) second = -second;
            float4 blend = first * (1 - weight) + second * weight;
            p = quaternionRotate(rotation, quaternionRotate(blend, p)) + a.xyz;
            n = quaternionRotate(rotation, quaternionRotate(blend, n));
        }
        if (mode != 5) {
            p = p.x * characterPalette[0].xyz + p.y * characterPalette[1].xyz + p.z * characterPalette[2].xyz + characterPalette[3].xyz;
            n = n.x * characterPalette[0].xyz + n.y * characterPalette[1].xyz + n.z * characterPalette[2].xyz;
        }
        o.p = mul(viewProjection, float4(p, 1));
        o.n = n; o.uv = v.uv + liveControl.xy;
        o.cameraDirection = p - liveMix.xyz;
        o.color = float4(1, 1, 1, liveTint.w * (any(v.n != 0) ? 1 : 0));
        o.fog = clamp(saturate((liveFog.x - o.p.z) * liveFog.y), 1 - liveFog.w, 1 - liveFog.z);
        if (mode == 7 || mode == 8) {
            float len = length(n);
            p += (len > 0 ? n / len : float3(0, 0, 0)) * liveFog.x;
            o.p = mul(viewProjection, float4(p, 1));
            o.color = liveTint;
            o.color.a *= any(v.n != 0) ? 1 : 0;
        }
        if (coloredSkin) o.color = v.color * liveTint;
    }
    if (eyeMode.w > 1.5) {
        // Explicit clip input, including w, preserves perspective interpolation
        // in independent geometry oracles and pretransformed host draws.
        o.p = float4(v.p, v.fog);
        o.fog = v.n.x;
    }
    // This UV pair is interpolated independently by the checked postprocess VS.
    if (liveControl.w == 14) o.n.xy = v.uv - liveFog.xy;
    return o;
}
float3 fxaaSample(float2 uv, float2 bias) {
    precise float2 position = uv + bias;
    return liveFog.z > 0.5 ? baseTexture.Sample(rampSampler, position).rgb : baseTexture.Sample(textureSampler, position).rgb;
}
float fxaaLuma(float3 color) {
    precise float blue = color.b * 0.11400000005960464;
    precise float red = color.r * 0.29899999499320984;
    precise float green = color.g * 0.5870000123977661;
    precise float sum = blue + red;
    return sum + green;
}
float2 fxaaOffset(float2 uv, float2 direction, float offset, float2 stepSize) {
    precise float2 delta = direction * offset;
    delta = delta * stepSize;
    return delta + uv;
}
float4 imageSample(float2 uv, float2 bias) {
    precise float2 position = uv + bias;
    float4 sampled = 0;
    float vAddress = liveFog.z > 0.5 ? liveFog.z - 1 : liveFog.y;
    bool pointFilter = liveFog.x > 0.5;
    if (liveFog.y < 0.5 && vAddress > 0.5) {
        if (vAddress > 1.5) {
            if (pointFilter) return baseTexture.Sample(imageWrapMirrorPoint, position);
            return baseTexture.Sample(imageWrapMirrorLinear, position);
        }
        if (pointFilter) return baseTexture.Sample(imageWrapClampPoint, position);
        return baseTexture.Sample(imageWrapClampLinear, position);
    }
    if (liveFog.y > 0.5 && liveFog.y < 1.5 && (vAddress < 0.5 || vAddress > 1.5)) {
        if (vAddress > 1.5) {
            if (pointFilter) return baseTexture.Sample(imageClampMirrorPoint, position);
            return baseTexture.Sample(imageClampMirrorLinear, position);
        }
        if (pointFilter) return baseTexture.Sample(imageClampWrapPoint, position);
        return baseTexture.Sample(imageClampWrapLinear, position);
    }
    if (liveFog.y > 1.5 && vAddress < 1.5) {
        // Reflect U before a clamp sampler. This preserves edge texels for
        // point/bilinear base-level copies and stays below SM5's 16 samplers.
        position.x = 1 - abs(frac(position.x * 0.5) * 2 - 1);
        if (vAddress > 0.5) {
            if (pointFilter) return baseTexture.Sample(shadowSampler, position);
            return baseTexture.Sample(rampSampler, position);
        }
        if (pointFilter) return baseTexture.Sample(imageClampWrapPoint, position);
        return baseTexture.Sample(imageClampWrapLinear, position);
    }
    if (liveFog.y > 1.5) {
        if (liveFog.x > 0.5) return baseTexture.Sample(imageMirrorPointSampler, position);
        return baseTexture.Sample(imageMirrorSampler, position);
    }
    if (liveFog.x > 0.5) {
        if (liveFog.y > 0.5) sampled = baseTexture.Sample(shadowSampler, position);
        else sampled = baseTexture.Sample(videoChromaSampler, position);
    } else {
        if (liveFog.y > 0.5) sampled = baseTexture.Sample(rampSampler, position);
        else sampled = baseTexture.Sample(textureSampler, position);
    }
    return sampled;
}
float2 depthOfFieldOffset(float2 uv, float radius, float2 offset) {
    precise float2 delta = radius * offset;
    return delta + uv;
}
float4 shadePixel(Fragment i) {
    uint mode = (uint)liveControl.w;
    float4 result = 0;
    if (mode == 19) {
        result = i.color;
        result.rgb = lerp(result.rgb, liveMix.rgb, liveMix.a);
        if (liveControl.z > 0.5) {
            result.rgb = lerp(liveFogColor.rgb, result.rgb, i.fog);
            result.a *= i.fog;
        }
    }
    else if (mode == 17) {
        uint w, h, dw, dh; baseTexture.GetDimensions(w, h); rampTexture.GetDimensions(dw, dh);
        float2 bias = (3.0 / 2048.0) / float2(w, h);
        float depth;
        if (liveFog.w > 0.5) depth = rampTexture.Sample(rampSampler, i.uv + (3.0 / 2048.0) / float2(dw, dh)).r;
        else depth = rampTexture.Sample(shadowSampler, i.uv + (3.0 / 2048.0) / float2(dw, dh)).r;
        float4 focus = characterPalette[0], fixedValues = characterPalette[1];
        float4 vertical = characterPalette[2], horizontal = characterPalette[3];
        precise float span = focus.y - focus.x, nearFar = focus.y + focus.x;
        precise float numerator = focus.y * fixedValues.z;
        numerator = numerator * focus.x;
        precise float denominator = span * depth;
        denominator = denominator - nearFar;
        precise float distance = numerator * rcp(denominator);
        distance = distance - focus.z;
        precise float radius = saturate(max(distance, fixedValues.y) * rcp(focus.w));
        precise float3 sum = imageSample(i.uv, bias).rgb + imageSample(depthOfFieldOffset(i.uv, radius, float2(horizontal.x, 0)), bias).rgb;
        sum = sum + imageSample(depthOfFieldOffset(i.uv, radius, float2(horizontal.y, 0)), bias).rgb;
        sum = sum + imageSample(depthOfFieldOffset(i.uv, radius, float2(0, vertical.x)), bias).rgb;
        sum = sum + imageSample(depthOfFieldOffset(i.uv, radius, float2(0, vertical.w)), bias).rgb;
        sum = sum + imageSample(depthOfFieldOffset(i.uv, radius, float2(horizontal.w, 0)), bias).rgb;
        sum = sum + imageSample(depthOfFieldOffset(i.uv, radius, float2(horizontal.z, 0)), bias).rgb;
        sum = sum + imageSample(depthOfFieldOffset(i.uv, radius, float2(0, vertical.z)), bias).rgb;
        sum = sum + imageSample(depthOfFieldOffset(i.uv, radius, float2(0, vertical.y)), bias).rgb;
        result = float4(sum * fixedValues.x, 1);
    }
    else if (mode == 15) {
        uint w, h; baseTexture.GetDimensions(w, h);
        result = imageSample(i.uv, (3.0 / 2048.0) / float2(w, h));
    }
    else if (mode == 16) {
        uint w, h; baseTexture.GetDimensions(w, h);
        float2 bias = (3.0 / 2048.0) / float2(w, h);
        precise float3 sum = 0;
        [unroll] for (int tap = 12; tap >= 0; --tap) {
            precise float2 uv = i.uv + characterPalette[tap].xy;
            precise float3 weighted = imageSample(uv, bias).rgb * characterPalette[tap].z;
            sum = weighted + sum;
        }
        result = float4(sum, 1);
    }
    else if (mode == 14) {
        uint w, h; baseTexture.GetDimensions(w, h);
        float2 bias = (3.0 / 2048.0) / float2(w, h), stepSize = liveMix.xy;
        float2 corner = i.n.xy;
        float nw = fxaaLuma(fxaaSample(corner, bias));
        float ne = fxaaLuma(fxaaSample(corner + float2(stepSize.x, 0), bias));
        float sw = fxaaLuma(fxaaSample(corner + float2(0, stepSize.y), bias));
        float se = fxaaLuma(fxaaSample(corner + stepSize, bias));
        float4 sums = float4(ne + se, sw + se, nw + sw, nw + ne);
        float dy = sums.z - sums.x, dx = -(sums.w - sums.y);
        float reduce = max((sums.y + sums.w) * 0.015625, 0.03125);
        float inverse = rcp(min(abs(dy), abs(dx)) + reduce);
        float2 direction = clamp(float2(dx, dy) * inverse, -8, 8);
        precise float3 nearSum = fxaaSample(fxaaOffset(i.uv, direction, -0.1666666716337204, stepSize), bias) + fxaaSample(fxaaOffset(i.uv, direction, 0.1666666716337204, stepSize), bias);
        precise float3 farSum = fxaaSample(fxaaOffset(i.uv, direction, -0.5, stepSize), bias) + fxaaSample(fxaaOffset(i.uv, direction, 0.5, stepSize), bias);
        precise float3 nearQuarter = nearSum * 0.25, farQuarter = farSum * 0.25;
        precise float3 rgbB = nearQuarter + farQuarter, rgbA = nearSum * 0.5;
        float center = fxaaLuma(fxaaSample(i.uv, bias)), lumaB = fxaaLuma(rgbB);
        float low = min(min(nw, ne), min(sw, se)), high = max(max(nw, ne), max(sw, se));
        result = float4((lumaB < min(low, center) || lumaB > max(high, center)) ? rgbA : rgbB, 1);
    }
    else if (mode == 13) {
        // Fixed semantics of the checked 046E7D32 video material: limited-range
        // BT.601. Luma and chroma retain their original independent resolutions.
        uint yw, yh, cw, ch; baseTexture.GetDimensions(yw, yh); rampTexture.GetDimensions(cw, ch);
        // Checked texel sample positioning resolves point-filter boundaries in
        // the same direction as the existing game presentation path.
        float2 lumaUV = i.uv + (3.0 / 2048.0) / float2(yw, yh);
        float2 chromaUV = i.uv + (3.0 / 2048.0) / float2(cw, ch);
        float y = liveFog.z > 0.5 ? baseTexture.Sample(rampSampler, lumaUV).r : baseTexture.Sample(textureSampler, lumaUV).r;
        float2 cbcr;
        if ((liveFog.x > 0.5) == (liveFog.y > 0.5)) cbcr = liveFog.x > 0.5 ? rampTexture.Sample(shadowSampler, chromaUV).rg : rampTexture.Sample(videoChromaSampler, chromaUV).rg;
        else {
            cbcr.x = liveFog.x > 0.5 ? rampTexture.Sample(shadowSampler, chromaUV).r : rampTexture.Sample(videoChromaSampler, chromaUV).r;
            cbcr.y = liveFog.y > 0.5 ? rampTexture.Sample(shadowSampler, chromaUV).g : rampTexture.Sample(videoChromaSampler, chromaUV).g;
        }
        precise float red = cbcr.y * 1.5960267782211304 - 0.874202311038971;
        precise float blue = cbcr.x * 2.0172321796417236 - 1.0856307744979858;
        red = y * 1.1643835306167603 + red;
        blue = y * 1.1643835306167603 + blue;
        precise float green = y * 1.1643835306167603 - 0.07305935770273209;
        green -= cbcr.x * 0.3916071653366089 - 0.1965714544057846;
        green -= cbcr.y * 0.8128125071525574 - 0.40800005197525024;
        result = float4(lerp(float3(red, green, blue) * i.color.rgb, liveMix.rgb, liveMix.a), i.color.a);
    }
    else if (mode == 11 || mode == 12) result = float4(liveTint.xxx, 1);
    else if (mode == 3 || mode == 7 || mode == 8) result = i.color;
    else if (mode == 4 || mode == 5 || mode == 6) {
        // The checked toon material normalizes the full homogeneous difference.
        // World position has w=1; some game lighting states use camera w=0.
        float cameraW = 1 - alphaTest.w;
        float normalSquared = dot(i.n, i.n), directionSquared = dot(i.cameraDirection, i.cameraDirection) + cameraW * cameraW;
        float3 n = normalSquared > 0 ? i.n * rsqrt(normalSquared) : float3(0, 0, 0);
        float3 direction = directionSquared > 0 ? i.cameraDirection * rsqrt(directionSquared) : float3(0, 0, 0);
        float edge = 1 - abs(dot(n, direction)), edge2 = edge * edge;
        float rim = i.color.a * (diffuse.x * edge2 + step(diffuse.x, edge2 * edge2 * edge2));
        float4 base = baseTexture.Sample(textureSampler, i.uv);
        float3 ramp = rampTexture.Sample(rampSampler, float2(diffuse.x * dot(n, liveTint.xyz) + diffuse.x, liveMix.w)).rgb;
        result = float4((base.a > diffuse.z ? base.rgb : base.rgb - ramp) + rim, diffuse.y + rim);
        if (liveControl.z > 0.5) {
            result.rgb = lerp(liveFogColor.rgb, result.rgb, i.fog);
            result.a *= i.fog;
        }
    } else if (liveControl.w > 0.5) {
        float4 texel = mode == 2 ? baseTexture.Sample(shadowSampler, i.uv) : baseTexture.Sample(textureSampler, i.uv);
        if (mode == 2) result = float4(liveMix.rgb * liveMix.a, texel.a * i.color.a);
        else {
            result = texel * i.color;
            result.rgb = lerp(result.rgb, liveMix.rgb, liveMix.a);
            if (liveControl.z > 0.5) {
                result.rgb = lerp(liveFogColor.rgb, result.rgb, i.fog);
                result.a *= i.fog;
            }
        }
    } else if (lightPass.w > 0.5) result = float4(0.018, 0.021, 0.03, 1);
    else {
        float normalLength = length(i.n);
        float3 n = normalLength > 0.0001 ? i.n / normalLength : float3(0, 0, 1);
        float4 base = diffuse.w > 0.5 ? baseTexture.Sample(textureSampler, i.uv) : float4(diffuse.rgb, 0);
        // The viewer's base alpha encodes the toon row, not transparency.
        float u = saturate(0.5 * dot(n, normalize(lightPass.xyz)) + 0.5);
        float3 ramp = rampTexture.Sample(rampSampler, float2(u, base.a)).rgb;
        float3 color = base.a > 0.99 ? base.rgb : saturate(base.rgb - ramp);
        float edge = 1 - abs(dot(n, normalize(i.cameraDirection)));
        if (normalLength > 0.0001) color += view.w * (0.5 * edge * edge + step(0.5, pow(edge, 6)));
        result = float4(saturate(color), 1);
    }
    return result;
}
float4 pixelMain(Fragment i) : SV_TARGET {
    float4 result = shadePixel(i);
    if (alphaTest.x > 0.5) {
        uint comparison = (uint)alphaTest.y;
        float a = result.a, reference = alphaTest.z;
        bool passed = comparison == 7 ||
            (comparison == 1 && a < reference) ||
            (comparison == 2 && a == reference) ||
            (comparison == 3 && a <= reference) ||
            (comparison == 4 && a > reference) ||
            (comparison == 5 && a != reference) ||
            (comparison == 6 && a >= reference);
        clip(passed ? 1 : -1);
    }
    return result;
}
float depthCopyPixel(Fragment i) : SV_Depth {
    uint w, h; baseTexture.GetDimensions(w, h);
    return imageSample(i.uv, (3.0 / 2048.0) / float2(w, h)).r;
}
float4 stencilCopyPixel(Fragment i) : SV_Target {
    uint w, h; baseTexture.GetDimensions(w, h);
    precise float2 uv = i.uv + (3.0 / 2048.0) / float2(w, h);
    int2 pixel = int2(floor(uv * float2(w, h))), size = int2(w, h);
    if (liveFog.y > 0.5) pixel = clamp(pixel, int2(0, 0), size - 1);
    else pixel = (pixel % size + size) % size;
    uint saved = stencilTexture.Load(int3(pixel, 0));
    if ((saved & (1u << (uint)alphaTest.w)) == 0) discard;
    return 0;
}
