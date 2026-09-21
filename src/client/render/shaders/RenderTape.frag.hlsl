struct TapeVertexOutput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD2;
    float fogDistance : TEXCOORD3;
};

cbuffer RenderTapeFragmentConstants : register(b0, space3)
{
    float4 fogColor;
    float4 fogRange;
    float4 ambientLight;
    float4 mode;
    float4 textureTint;
};

Texture2D<float4> tapeTexture : register(t0, space2);
SamplerState tapeSampler : register(s0, space2);

float4 main(TapeVertexOutput input) : SV_Target0
{
    const float4 sampled = tapeTexture.Sample(tapeSampler, input.uv);
    float4 output = input.color;
    if (mode.x > 0.5F)
    {
        if (mode.y > 1.5F)
        {
            output *= float4(saturate(sampled.rgb * textureTint.x + textureTint.yzw), sampled.a);
        }
        else if (mode.y > 0.5F)
        {
            output.rgb += sampled.rgb;
            output.a *= sampled.a;
        }
        else
        {
            output *= sampled;
        }
    }

    if (ambientLight.w > 0.5F)
    {
        output.rgb *= 0.04F;
        output.a = 1.0F;
    }

    if (mode.z >= 1.0F && mode.z <= 5.0F)
    {
        bool alphaPass = false;
        switch ((uint)mode.z)
        {
        case 1: alphaPass = output.a < mode.w; break;
        case 2: alphaPass = output.a <= mode.w; break;
        case 3: alphaPass = output.a > mode.w; break;
        case 4: alphaPass = true; break;
        case 5: alphaPass = output.a == mode.w; break;
        default: break;
        }
        if (!alphaPass)
        {
            discard;
        }
    }

    if (fogRange.w > 0.5F)
    {
        const float fogSpan = max(fogRange.y - fogRange.x, 0.0001F);
        const float fogFactor = saturate(
            (input.fogDistance - fogRange.x) / fogSpan);
        output.rgb = lerp(output.rgb, fogColor.rgb, fogFactor);
    }
    return output;
}
