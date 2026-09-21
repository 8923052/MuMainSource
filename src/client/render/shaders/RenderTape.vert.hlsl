struct TapeVertexInput
{
    float4 position : TEXCOORD0;
    float4 color : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float2 uv : TEXCOORD3;
    uint4 bmdSource : TEXCOORD4;
};

struct TapeVertexOutput
{
    float4 position : SV_Position;
    float4 color : TEXCOORD0;
    float2 uv : TEXCOORD2;
    float fogDistance : TEXCOORD3;
};

cbuffer RenderTapeVertexConstants : register(b0, space1)
{
    row_major float4x4 modelView;
    row_major float4x4 projection;
    row_major float4x4 textureMatrix;
    float4 bmdScale;
    float4 bmdBodyOrigin;
    float4 bmdBodyLight;
    float4 bmdBaseColor;
    float4 bmdLightPosition;
    float4 bmdUvAnimation;
    float4 bmdChromeLight;
    float4 bmdLegacyLight;
    uint4 bmdMode;
    float4 rigidTransform0;
    float4 rigidTransform1;
    float4 rigidTransform2;
};

struct BmdBoneMatrix
{
    float4 row0;
    float4 row1;
    float4 row2;
};

StructuredBuffer<BmdBoneMatrix> bmdBones : register(t0, space0);

// Draw modes address a shared float4 stream: quad/rigid records use six rows,
// and compact particle records use four. Offsets are always float4 rows.
struct DrawInstanceData
{
    float4 rows[6];
};

StructuredBuffer<float4> drawInstances : register(t1, space0);

DrawInstanceData LoadSixRowInstance(uint row)
{
    DrawInstanceData instance;
    [unroll] for (uint index = 0; index < 6; ++index)
        instance.rows[index] = drawInstances[row + index];
    return instance;
}

struct TerrainCell
{
    float height;
    uint wall;
    float alpha;
    float padding;
};

StructuredBuffer<TerrainCell> terrainCells : register(t2, space0);
StructuredBuffer<float> terrainLights : register(t3, space0);

static const float TerrainScale = 100.0F;
static const uint TerrainSideLength = 256U;
static const uint TerrainCellCount = TerrainSideLength * TerrainSideLength;
static const uint TerrainIndexMask = TerrainSideLength - 1U;
static const uint TerrainHeightWall = 0x40U;
static const float ShadowHeightOffset = 5.0F;

float TerrainHeight(float2 worldPosition)
{
    if (worldPosition.x < 0.0F || worldPosition.y < 0.0F)
        return 0.0F;

    const float2 terrainPosition = worldPosition / TerrainScale;
    const int xi = (int)terrainPosition.x;
    const int yi = (int)terrainPosition.y;
    const uint index = (uint)(yi * (int)TerrainSideLength + xi);
    if (index >= TerrainCellCount)
        return bmdBodyLight.z;
    if ((terrainCells[index].wall & TerrainHeightWall) != 0)
        return bmdBodyLight.z;

    const float xd = terrainPosition.x - (float)xi;
    const float yd = terrainPosition.y - (float)yi;
    const uint x0 = (uint)xi & TerrainIndexMask;
    const uint x1 = (uint)(xi + 1) & TerrainIndexMask;
    const uint y0 = (uint)yi & TerrainIndexMask;
    const uint y1 = (uint)(yi + 1) & TerrainIndexMask;
    const float left0 = terrainCells[y0 * TerrainSideLength + x0].height;
    const float left1 = terrainCells[y1 * TerrainSideLength + x0].height;
    const float right0 = terrainCells[y0 * TerrainSideLength + x1].height;
    const float right1 = terrainCells[y1 * TerrainSideLength + x1].height;
    const float left = left0 + (left1 - left0) * yd;
    const float right = right0 + (right1 - right0) * yd;
    return left + (right - left) * xd;
}

uint TerrainCornerCell(uint tileIndex, uint corner)
{
    const uint x = tileIndex & TerrainIndexMask;
    const uint y = tileIndex / TerrainSideLength;
    const uint cornerX = (corner == 1U || corner == 2U) ? 1U : 0U;
    const uint cornerY = corner >= 2U ? 1U : 0U;
    return ((y + cornerY) & TerrainIndexMask) * TerrainSideLength
        + ((x + cornerX) & TerrainIndexMask);
}

float TerrainCornerHeight(uint cellIndex)
{
    return (terrainCells[cellIndex].wall & TerrainHeightWall) != 0U
        ? bmdBodyOrigin.w : terrainCells[cellIndex].height;
}

float3 RotateBmd(float3 value, BmdBoneMatrix bone)
{
    return float3(
        dot(value, bone.row0.xyz),
        dot(value, bone.row1.xyz),
        dot(value, bone.row2.xyz));
}

float3 TransformBmd(float3 value, BmdBoneMatrix bone)
{
    return RotateBmd(value, bone)
        + float3(bone.row0.w, bone.row1.w, bone.row2.w);
}

float2 BmdUv(float2 meshUv, float3 normal, float2 offset)
{
    const uint uvMode = bmdMode.z;
    const float wave = bmdUvAnimation.z;
    const float chromeWave = bmdUvAnimation.w;
    if (uvMode == 0)
    {
        return (bmdMode.w & 4U) != 0 ? meshUv + offset : meshUv;
    }
    if (uvMode == 1)
        return float2(normal.z * 0.5F + wave,
            normal.y * 0.5F + wave * 2.0F);
    if (uvMode == 2)
        return float2((normal.z + normal.x) * 0.8F + chromeWave * 2.0F,
            (normal.y + normal.x) + chromeWave * 3.0F);
    if (uvMode == 3)
    {
        const float light = dot(normal, bmdLegacyLight.xyz);
        return float2(light, 1.0F - light);
    }
    if (uvMode == 4)
    {
        const float light = dot(normal, bmdChromeLight.xyz);
        return float2(
            light + normal.y * 0.5F + bmdChromeLight.y * 3.0F,
            1.0F - light - normal.z * 0.5F - wave * 3.0F) + offset;
    }
    if (uvMode == 5)
    {
        const float light = dot(normal, bmdChromeLight.xyz);
        return float2(
            light + normal.y * 3.0F + bmdChromeLight.y * 5.0F,
            1.0F - light - normal.z * 2.5F - wave);
    }
    if (uvMode == 6)
    {
        const float value =
            (normal.z + normal.x) * 0.8F + chromeWave * 2.0F;
        return value.xx;
    }
    if (uvMode == 7)
    {
        const float value =
            (normal.z + normal.x) * 0.8F + bmdChromeLight.w;
        return value.xx;
    }
    if (uvMode == 8)
        return float2(normal.z * 0.5F + 0.2F, normal.y * 0.5F + 0.5F);
    return normal.xy * meshUv + offset;
}

TapeVertexOutput main(
    TapeVertexInput input, uint instanceId : SV_InstanceID)
{
    TapeVertexOutput output;
    float4 position = input.position;
    float4 color = input.color;
    float2 uv = input.uv;
    if (bmdMode.x == 2)
    {
        const uint cornerIndex = input.bmdSource.w;
        const DrawInstanceData instance =
            LoadSixRowInstance(bmdMode.y + instanceId * 6U);
        const float4 corner = instance.rows[cornerIndex];
        position = float4(corner.xyz, 1.0F);
        color = instance.rows[5];
        uv = float2(corner.w, instance.rows[4][cornerIndex]);
    }
    else if (bmdMode.x == 3)
    {
        const uint cornerIndex = input.bmdSource.w;
        const DrawInstanceData instance =
            LoadSixRowInstance(bmdMode.y + instanceId * 6U);
        const float2 signs = float2(
            (cornerIndex == 0 || cornerIndex == 3) ? -1.0F : 1.0F,
            cornerIndex < 2 ? -1.0F : 1.0F);
        const float2 local = signs * float2(
            instance.rows[0].w, instance.rows[1].x);
        const float sine = instance.rows[1].y;
        const float cosine = instance.rows[1].z;
        const float2 rotated = float2(
            local.x * cosine - local.y * sine,
            local.x * sine + local.y * cosine);
        position = float4(instance.rows[0].xyz
            + float3(rotated, 0.0F), 1.0F);
        const float2 uvCorner = float2(
            (cornerIndex == 1 || cornerIndex == 2) ? 1.0F : 0.0F,
            cornerIndex < 2 ? 1.0F : 0.0F);
        uv = instance.rows[2].xy
            + instance.rows[2].zw * uvCorner;
        color = instance.rows[5];
    }
    else if (bmdMode.x == 8U || bmdMode.x == 9U)
    {
        const uint faceCount = bmdMode.w == 3U ? 2U : 1U;
        const uint face = instanceId % faceCount;
        const uint row = bmdMode.y + (instanceId / faceCount) * 2U;
        const float4 parameters = drawInstances[row];
        const float4 colorSample = drawInstances[row + 1U];
        const uint sampleIndex = asuint(parameters.w);
        const uint corner = input.bmdSource.w;
        const uint endpoint = corner < 2U ? 0U : 1U;
        const uint sampleRow = bmdMode.z + ((sampleIndex + endpoint) * faceCount + face) * 2U;
        const float4 edges0 = drawInstances[sampleRow];
        const float4 edges1 = drawInstances[sampleRow + 1U];
        const bool secondEdge = corner == 1U || corner == 2U;
        position = float4(secondEdge ? float3(edges0.w, edges1.xy) : edges0.xyz, 1.0F);
        color = colorSample;
        if (bmdMode.x == 9U)
        {
            const float logicalIndex = parameters.z + float(endpoint);
            precise float u = logicalIndex / parameters.x;
            precise float fade = parameters.y != 0.0F ? (parameters.x - logicalIndex) / parameters.x : 1.0F;
            color.rgb *= fade;
            uv = float2(u, secondEdge ? 0.0F : 1.0F);
        }
        else
        {
            const bool secondFace = bmdMode.w == 2U || face == 1U;
            const float u = (endpoint == 0U ? parameters.x : parameters.y)
                + (secondFace ? bmdUvAnimation.x : 0.0F);
            const bool useFirstV = secondEdge != secondFace;
            uv = float2(u, useFirstV ? parameters.z : 1.0F - parameters.z);
        }
    }
    else if (bmdMode.x == 7U)
    {
        const uint row = bmdMode.y + instanceId * 4U;
        const float4 centerWidth = drawInstances[row];
        const float4 heightRotation = drawInstances[row + 1U];
        const float4 uvRect = drawInstances[row + 2U];
        const uint cornerIndex = input.bmdSource.w;
        precise float3 center;
        center.x = centerWidth.x * rigidTransform0.x + centerWidth.y * rigidTransform0.y
            + centerWidth.z * rigidTransform0.z + rigidTransform0.w;
        center.y = centerWidth.x * rigidTransform1.x + centerWidth.y * rigidTransform1.y
            + centerWidth.z * rigidTransform1.z + rigidTransform1.w;
        center.z = centerWidth.x * rigidTransform2.x + centerWidth.y * rigidTransform2.y
            + centerWidth.z * rigidTransform2.z + rigidTransform2.w;
        const float2 signs = float2(
            (cornerIndex == 0 || cornerIndex == 3) ? -1.0F : 1.0F,
            cornerIndex < 2 ? -1.0F : 1.0F);
        const float2 local = signs * float2(centerWidth.w, heightRotation.x);
        float sine = 0.0F, cosine = 1.0F;
        if (heightRotation.y != 0.0F) sincos(heightRotation.y, sine, cosine);
        const float2 rotated = float2(local.x * cosine - local.y * sine,
            local.x * sine + local.y * cosine);
        position = float4(center + float3(rotated, 0.0F), 1.0F);
        const float2 uvCorner = float2(
            (cornerIndex == 1 || cornerIndex == 2) ? 1.0F : 0.0F,
            cornerIndex < 2 ? 1.0F : 0.0F);
        uv = uvRect.xy + uvRect.zw * uvCorner;
        color = drawInstances[row + 3U];
    }
    else if (bmdMode.x == 4U)
    {
        const uint corner = input.bmdSource.w;
        const float4 instance = drawInstances[bmdMode.y + instanceId];
        const uint tileIndex = asuint(instance.x);
        const uint x = tileIndex & TerrainIndexMask;
        const uint y = tileIndex / TerrainSideLength;
        const bool top = corner < 2U;
        const bool diagonal = corner == 1U || corner == 2U;
        const uint heightCellIndex = TerrainCornerCell(tileIndex, diagonal ? 2U : 0U);
        position = float4((float)(x + (diagonal ? 1U : 0U)) * TerrainScale
                - (top ? 50.0F : 0.0F),
            (float)(y + (diagonal ? 1U : 0U)) * TerrainScale,
            TerrainCornerHeight(heightCellIndex) + (top ? instance.z : 0.0F), 1.0F);
        if (top)
            position.y += sin(bmdScale.x + (float)(x + (corner == 1U ? 1U : 0U))
                * bmdScale.z) * bmdScale.y;
        uv = float2(instance.y + ((corner == 1U || corner == 2U) ? 0.25F : 0.0F),
            top ? 0.0F : 1.0F);
        const uint lightIndex = TerrainCornerCell(tileIndex, corner);
        color = float4(terrainLights[lightIndex * 3U], terrainLights[lightIndex * 3U + 1U],
            terrainLights[lightIndex * 3U + 2U], 1.0F);
    }
    else if (bmdMode.x == 10U)
    {
        const uint corner = input.bmdSource.w;
        const float4 instance = drawInstances[bmdMode.y + instanceId];
        const uint tileIndex = asuint(instance.x);
        const uint x = tileIndex & TerrainIndexMask;
        const uint y = tileIndex / TerrainSideLength;
        const uint cellIndex = TerrainCornerCell(tileIndex, corner);
        const uint cornerX = (corner == 1U || corner == 2U) ? 1U : 0U;
        const uint cornerY = corner >= 2U ? 1U : 0U;
        position = float4((float)(x + cornerX) * TerrainScale,
            (float)(y + cornerY) * TerrainScale, TerrainCornerHeight(cellIndex), 1.0F);
        uv = float2((float)(x + cornerX) * bmdScale.x + bmdScale.z,
            (float)(y + cornerY) * bmdScale.y);
        if ((bmdMode.z & 4U) != 0U)
        {
            const float windX = (float)(x + cornerX);
            uv.y += sin(bmdBodyOrigin.x + windX * bmdBodyOrigin.z)
                * bmdBodyOrigin.y * bmdScale.w;
        }
        const float alpha = terrainCells[cellIndex].alpha;
        if ((bmdMode.z & 2U) != 0U)
            color = float4(alpha, alpha, alpha, 1.0F);
        else
            color = float4(terrainLights[cellIndex * 3U], terrainLights[cellIndex * 3U + 1U],
                terrainLights[cellIndex * 3U + 2U], (bmdMode.z & 1U) != 0U ? alpha : 1.0F);
    }
    else if (bmdMode.x != 0)
    {
        BmdBoneMatrix positionBone;
        BmdBoneMatrix normalBone;
        float4 bodyLight = bmdBodyLight;
        float4 baseColor = bmdBaseColor;
        float alpha = bmdBodyOrigin.w;
        float2 uvOffset = bmdUvAnimation.xy;
        if (bmdMode.x == 6U)
        {
            const DrawInstanceData instance = LoadSixRowInstance(bmdMode.y + instanceId * 6U);
            positionBone.row0 = instance.rows[0];
            positionBone.row1 = instance.rows[1];
            positionBone.row2 = instance.rows[2];
            normalBone = positionBone;
            bodyLight = instance.rows[3];
            alpha = bodyLight.w;
            baseColor = instance.rows[4];
            uvOffset = instance.rows[5].xy;
        }
        else if ((bmdMode.w & 64U) != 0)
        {
            positionBone.row0 = rigidTransform0;
            positionBone.row1 = rigidTransform1;
            positionBone.row2 = rigidTransform2;
            normalBone = positionBone;
        }
        else
        {
            positionBone = bmdBones[bmdMode.y + input.bmdSource.x];
            normalBone = bmdBones[bmdMode.y + input.bmdSource.y];
        }
        float3 skinnedPosition;
        if ((bmdMode.w & 16U) != 0)
        {
            skinnedPosition = RotateBmd(input.position.xyz, positionBone)
                * bmdScale.y
                + float3(positionBone.row0.w,
                    positionBone.row1.w, positionBone.row2.w);
        }
        else
        {
            skinnedPosition = TransformBmd(
                input.position.xyz * bmdScale.x, positionBone);
        }
        if ((bmdMode.w & 1U) != 0)
        {
            skinnedPosition = skinnedPosition * bmdScale.z
                + bmdBodyOrigin.xyz;
        }
        const float3 normal = RotateBmd(input.normal, normalBone);
        if ((bmdMode.w & 8U) != 0)
        {
            const float displacement = sin((bmdScale.w
                + (float)input.bmdSource.z * 931.0F) * 0.007F) * 28.0F;
            skinnedPosition += normal * displacement;
        }
        if (bmdMode.x == 5U)
        {
            // The projection origin may be below an airborne mesh's origin.
            float3 relative = skinnedPosition - bmdLightPosition.xyz;
            relative.x += relative.z * (relative.x + bmdBodyLight.x)
                / (relative.z - bmdBodyLight.y);
            skinnedPosition = relative + bmdLightPosition.xyz;
            skinnedPosition.z = TerrainHeight(skinnedPosition.xy)
                + ShadowHeightOffset;
            color = baseColor;
        }
        else if ((bmdMode.w & 2U) != 0)
        {
            const float luminosity = max(
                dot(normal, bmdLightPosition.xyz) * 0.8F + 0.4F, 0.2F);
            color = float4(bodyLight.xyz * luminosity, alpha);
        }
        else
        {
            color = baseColor;
        }
        position = float4(skinnedPosition, 1.0F);
        uv = BmdUv(input.uv, normal, uvOffset);
    }
    if ((bmdMode.w & 32U) != 0)
    {
        const uint lightIndex = input.bmdSource.x * 3U;
        color.rgb = float3(terrainLights[lightIndex], terrainLights[lightIndex + 1U],
            terrainLights[lightIndex + 2U]);
    }
    const float4 viewPosition = mul(position, modelView);
    output.position = mul(viewPosition, projection);
    output.color = saturate(color);
    output.uv = mul(float4(uv, 0.0F, 1.0F), textureMatrix).xy;
    output.fogDistance = abs(viewPosition.z);
    return output;
}
