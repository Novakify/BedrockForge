# Porting feasibility: Impact frames

- **Loader:** fabric  |  **id:** impact-frames  |  **version:** 1.2.1.5
- **License:** ARR  (check before reusing any code or assets)
- **Environment:** client  |  **classes:** 44  |  **mixins:** 18
- **Score:** 0.14 → **Hard: needs systems Bedrock Forge doesn't have yet**

> Static analysis only. Labels: *Theoretical* until a port compiles and runs in game.

## Dependencies

- `fabricloader` >=0.19.2
- `minecraft` ~26.2
- `java` >=25
- `fabric-api` *

## What it touches → Bedrock Forge mapping

| Area | Refs | Bedrock path | Examples |
|---|---|---|---|
| Rendering (world/entity renderers) | 77 | native-only: RenderingAPI | `EntityRenderState`, `ItemStackRenderState`, `RenderSetup$RenderSetupBuilder`, `RenderType` |
| UI / HUD / screens | 47 | partial: bf::hud (v0) covers HUD text/boxes | `AbstractWidget`, `Button`, `Screen`, `OptionsSubScreen` |
| Rendering (Blaze3D/GL) | 34 | native-only: needs RenderingAPI (not built yet) | `BlendFunction`, `ColorTargetState`, `RenderPipeline`, `RenderPipeline$Builder` |
| Text components | 17 | supported: plain/formatted strings | `Component`, `MutableComponent`, `Style` |
| Entities | 16 | pack: custom entities (behavior + resource pack) | `Entity`, `EntityType`, `EntityTypes`, `Player` |
| Items | 13 | pack: custom items | `ItemStack`, `Items`, `MaceItem`, `BlocksAttacks` |
| Client core | 12 | native: LeviLamina ClientInstance | `Minecraft` |
| Audio | 9 | pack + native: AudioAPI (planned) | `SoundEngine`, `SoundManager`, `SoundEvent`, `SoundSource` |
| Fabric events | 4 | partial: map to ll::event / bf::events | `Event`, `AttackEntityCallback`, `UseBlockCallback` |
| Blocks | 3 | pack: custom blocks | `Block`, `Blocks`, `BlockState` |
| Keybinds | 2 | supported: bf::input::bindKey | `KeyMapping`, `KeyMapping$Category` |
| Networking | 1 | hard: Bedrock protocol differs; local worlds only | `ClientboundEntityEventPacket` |
| Camera | 1 | native: CameraAPI (planned) | `Camera` |

## Mixins (engine patches: each one needs a native hook or a redesign)

- [common] `LivingEntityMixin` → `BlockPos`, `Holder`, `Holder$Reference`, `Position`, `ResourceKey`, `SoundEvent`
- [common] `FlameFeatureRenderer` → `RenderPipeline`, `RenderPipeline$Builder`, `RenderPipeline$Snippet`, `RenderPipelines`, `FlameFeatureRenderer`, `RenderSetup`
- [client] `ClientPacketListenerMixin` → `Minecraft`, `ClientLevel`, `ClientPacketListener`, `ClientboundEntityEventPacket`, `Entity`, `LivingEntity`
- [client] `LocalPlayerMixin` → `GameProfile`, `Minecraft`, `LocalPlayer`, `SoundInstance`, `SoundInstance$Attenuation`, `SoundEngine`
- [client] `AccessibilityOptionsScreenMixin` → `Minecraft`, `OptionInstance`, `Options`, `AbstractWidget`, `Button`, `Button$Builder`
- [client] `OptionsMixin` → `Minecraft`, `OptionInstance`, `Options`, `Options$FieldAccess`, `CompoundTag`
- [client] `AnimationStateMixin` → `SpriteContents`, `SpriteContents$AnimationState`
- [client] `AvatarRendererMixin` → `AvatarRenderer`, `RenderType`, `RenderTypes`, `Identifier`
- [client] `EquipmentLayerRendererMixin` → `PoseStack`, `Model`, `SubmitNodeCollector`, `EquipmentLayerRenderer`, `EquipmentClientInfo$LayerType`, `DataComponentType`
- [client] `GameRendererMixin` → `ProjectionType`, `GpuBufferSlice`, `FrameGraphBuilder`, `RenderTarget`, `CrossFrameResourcePool`, `GraphicsResourceAllocator`
- [client] `ItemInHandRendererMixin` → `PoseStack`, `Axis`, `SpearAnimations`, `AbstractClientPlayer`, `LocalPlayer`, `ItemInHandRenderer`
- [client] `ItemFeatureRendererMixin` → `Sheets`, `ItemFeatureRenderer`, `ItemFeatureRenderer$Submit`, `ItemStackRenderState`, `ItemStackRenderState$FoilType`, `RenderType`
- [client] `LivingEntityEmissiveLayerMixin` → `PoseStack`, `SubmitNodeCollector`, `LivingEntityEmissiveLayer`, `LivingEntityRenderState`
- [client] `LivingEntityRendererMixin` → `LivingEntityRenderer`, `LivingEntityRenderState`, `RenderType`, `Identifier`, `LivingEntity`
- [client] `ModelPartMixin` → `VertexConsumer`, `ModelPart`, `ModelPart$Cube`, `OverlayTexture`
- [client] `NametagFeatureRendererMixin` → `NameTagFeatureRenderer`
- [client] `RenderTypesMixin` → `RenderType`, `RenderTypes`, `Identifier`
- [client] `ShieldSpecialRendererMixin` → `ShieldSpecialRenderer`

## Assets (candidates for AssetConverter, license permitting)

- assets/impact-frames/textures (.png): 7
- data/impact-frames/tags (.json): 6
- assets/impact-frames/shaders (.fsh): 3
- assets/impact-frames/lang (.json): 1
- assets/impact-frames/post_effect (.json): 1
- assets/impact-frames/sounds (.ogg): 1
