// LyricsLight terrain vertex: the game's own maths + wind in leaves and plants (Extreme sways more)
// @only RenderAsBillboards=Off
// @only Instancing=Off
#define WAVE_STYLE 1
#if LL_TIER >= 2
#define WAVE_STRENGTH 1.0
#else
#define WAVE_STRENGTH 0.55
#endif
// @include ../../_shared/RenderChunkWave.vertex.hlsli
