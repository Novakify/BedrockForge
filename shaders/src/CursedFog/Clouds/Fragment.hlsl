// Clouds - Cursed Fog default CLOUDS 0: gbuffers_clouds writes alpha 0 (no clouds).
float4 main(PSIn i) : SV_Target {
    discard;
    return float4(0, 0, 0, 0);
}
