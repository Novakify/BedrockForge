local R = {}
do local ok, o = pcall(function() return action_wheel:newPage("cov2"):newAction() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Action: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__newindex","color","getColor","getHoverColor","getTitle","getToggleColor","getToggleTitle","hoverColor","hoverItem","hoverTexture","isToggled","item","onLeftClick","onRightClick","onScroll","onToggle","onUntoggle","setColor","setHoverColor","setHoverItem","setHoverTexture","setItem","setOnLeftClick","setOnRightClick","setOnScroll","setOnToggle","setOnUntoggle","setTexture","setTitle","setToggleColor","setToggleItem","setToggleTexture","setToggleTitle","setToggled","texture","title","toggleColor","toggleItem","toggleTexture","toggleTitle","toggled"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Action: " .. #miss .. "/42 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return action_wheel end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "ActionWheelAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__newindex","execute","getCurrentPage","getPage","getSelected","getSelectedAction","isEnabled","newAction","newPage","setPage"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "ActionWheelAPI: " .. #miss .. "/11 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return (function() for _,a in ipairs(animations:getAnimations()) do return a end end)() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Animation: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","blend","code","getBlend","getLength","getLoop","getLoopDelay","getName","getOffset","getOverridePos","getOverrideRot","getOverrideScale","getPlayState","getPriority","getSpeed","getStartDelay","getTime","isHolding","isPaused","isPlaying","isStopped","length","loop","loopDelay","newCode","offset","override","overridePos","overrideRot","overrideScale","pause","play","playing","priority","restart","setBlend","setLength","setLoop","setLoopDelay","setOffset","setOverride","setOverridePos","setOverrideRot","setOverrideScale","setPlaying","setPriority","setSpeed","setStartDelay","setTime","speed","startDelay","stop","time"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Animation: " .. #miss .. "/53 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return animations end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "AnimationAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","getAnimations","getPlaying","stopAll"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "AnimationAPI: " .. #miss .. "/4 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return avatar end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "AvatarAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"badges","canEditNameplate","canEditVanillaModel","canHaveCustomSkull","canRenderOffscreen","canUseCustomSounds","color","getAnimationComplexity","getAnimationCount","getAuthors","getBadges","getBuffersCount","getColor","getComplexity","getCurrentInstructions","getEntityInitCount","getEntityName","getInitCount","getMaxAnimationComplexity","getMaxAnimationCount","getMaxBufferSize","getMaxBuffersCount","getMaxComplexity","getMaxInitCount","getMaxParticles","getMaxRenderCount","getMaxSounds","getMaxTextureSize","getMaxTickCount","getMaxWorldRenderCount","getMaxWorldTickCount","getNBT","getName","getPermissionLevel","getRemainingParticles","getRemainingSounds","getRenderCount","getSize","getTickCount","getUUID","getVersion","getVolume","getWorldRenderCount","getWorldTickCount","hasScriptError","hasTexture","setColor","store"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "AvatarAPI: " .. #miss .. "/48 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return world.getBiome(vec(0,0,0)) end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Biome: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__eq","__index","getDownfall","getFogColor","getFoliageColor","getGrassColor","getPos","getPrecipitation","getSkyColor","getTags","getTemperature","getWaterColor","getWaterFogColor","isCold","isHot","pos","setPos"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Biome: " .. #miss .. "/17 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return world.getBlockState(vec(0,0,0)) end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "BlockState: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__eq","__index","asItem","emitsRedstonePower","getBlastResistance","getCollisionShape","getComparatorOutput","getEntityData","getFluidTags","getFriction","getHardness","getID","getJumpVelocityMultiplier","getLuminance","getMapColor","getOpacity","getOutlineShape","getPos","getProperties","getSounds","getTags","getTextures","getVelocityMultiplier","hasBlockEntity","hasCollision","hasEmissiveLighting","isAir","isFullCube","isOpaque","isSolidBlock","isTranslucent","pos","setPos","toStateString"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "BlockState: " .. #miss .. "/34 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return models:newBlock("covb") end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "BlockTask: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"block","setBlock"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "BlockTask: " .. #miss .. "/2 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return client end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "ClientAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"compareVersions","generateUUID","getActionbar","getActiveLang","getActiveResourcePacks","getAllocatedMemory","getAtlas","getCameraDir","getCameraEntity","getCameraPos","getCameraRot","getChunkStatistics","getClientBrand","getCurrentEffect","getDate","getEntityCount","getEntityStatistics","getEnum","getFOV","getFPS","getFPSString","getFiguraVersion","getFrameTime","getGuiScale","getJavaVersion","getMaxMemory","getMousePos","getParticleCount","getRegistry","getScaledWindowSize","getScoreboard","getServerBrand","getServerData","getShaderPackName","getSoundStatistics","getSubtitle","getSystemTime","getTabList","getTextDimensions","getTextHeight","getTextWidth","getTitle","getTranslatedString","getUsedMemory","getVersion","getVersionName","getViewer","getWindowSize","hasResource","hasShaderPack","hasShaderPackMod","intUUIDToString","isDebugOverlayEnabled","isHudEnabled","isIntegratedServer","isModLoaded","isPaused","isSnapshot","isWindowFocused","listAtlases","uuidToIntArray"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "ClientAPI: " .. #miss .. "/61 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return config end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "ConfigAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getName","load","name","save","setName"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "ConfigAPI: " .. #miss .. "/5 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return data end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "DataAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"createBuffer"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "DataAPI: " .. #miss .. "/1 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return player end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "EntityAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__eq","__tostring","getBoundingBox","getControlledVehicle","getControllingPassenger","getDimensionName","getEyeHeight","getEyeY","getFrozenTicks","getItem","getLookDir","getMaxAir","getName","getNbt","getNearestEntity","getPassengers","getPermissionLevel","getPos","getPose","getRot","getTargetedBlock","getTargetedEntity","getType","getUUID","getVariable","getVehicle","getVelocity","hasAvatar","hasContainer","hasInventory","isAlive","isCrouching","isFalling","isGlowing","isInLava","isInRain","isInWater","isInvisible","isLiving","isLoaded","isMoving","isOnFire","isOnGround","isPlayer","isSilent","isSneaking","isSprinting","isUnderwater","isWet"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "EntityAPI: " .. #miss .. "/49 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nameplate.ENTITY end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "EntityNameplateCustomization: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"backgroundColor","getBackgroundColor","getLight","getPivot","getPos","getScale","hasOutline","hasShadow","isVisible","light","outline","outlineColor","pivot","pos","scale","setBackgroundColor","setLight","setOutline","setOutlineColor","setPivot","setPos","setScale","setShadow","setVisible","shadow","visible"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "EntityNameplateCustomization: " .. #miss .. "/26 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return models.newEntity and models:newEntity("cove") end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "EntityTask: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"asEntity","setHeadRotation","setNbt","updateWalkingDistance"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "EntityTask: " .. #miss .. "/4 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return events.TICK end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Event: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__len","clear","getRegisteredCount","register","remove"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Event: " .. #miss .. "/5 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return events end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "EventsAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__newindex","getEvents"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "EventsAPI: " .. #miss .. "/3 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return file end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "FileAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"allowed","delete","exists","isDirectory","isFile","isPathAllowed","list","mkdir","mkdirs","openReadStream","openWriteStream","readString","writeString"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "FileAPI: " .. #miss .. "/13 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return host end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "HostAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__newindex","actionbar","appendChatHistory","badge","chatColor","chatText","clearTitle","clipboard","getAir","getAttackCharge","getChatColor","getChatMessage","getChatText","getClipboard","getPickBlock","getPickEntity","getReachDistance","getScreen","getScreenSlot","getScreenSlotCount","getSlot","getStatusEffects","isAvatarUploaded","isChatOpen","isChatVerified","isContainerOpen","isCursorUnlocked","isFlying","isHost","isJumping","screenshot","sendChatCommand","sendChatMessage","setActionbar","setBadge","setChatColor","setChatMessage","setChatText","setClipboard","setSlot","setSubtitle","setTitle","setTitleTimes","setUnlockCursor","subtitle","swingArm","title","titleTimes","warnToLog","writeToLog"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "HostAPI: " .. #miss .. "/50 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return net and net.http end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "HttpAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"body","getBody","getData","getHeaders","getMethod","getResponseCode","getUri","header","method","request","send","uri"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "HttpAPI: " .. #miss .. "/12 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nil end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "InputStream: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"available","close","isAsyncOnly","mark","markSupported","read","readAsync","reset","skip","transferTo"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "InputStream: " .. #miss .. "/10 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return player:getHeldItem() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "ItemStack: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__eq","__index","copy","getBlockstate","getCount","getDamage","getEquipmentSlot","getID","getMaxCount","getMaxDamage","getName","getPopTime","getRarity","getRepairCost","getTag","getTags","getUseAction","getUseDuration","hasGlint","isArmor","isBlockItem","isDamageable","isEnchantable","isFood","isStackable","isTool","toStackString"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "ItemStack: " .. #miss .. "/27 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return models:newItem("covi") end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "ItemTask: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"displayMode","getDisplayMode","item","setDisplayMode","setItem"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "ItemTask: " .. #miss .. "/5 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return json end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "JsonAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"isSerializable","newArray","newBuilder","newObject"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "JsonAPI: " .. #miss .. "/4 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return json and json.newArray and json.newArray() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "JsonArray: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__ipairs","__len","__newindex","__pairs","add","clear","contains","get","indexOf","insert","lastIndexOf","remove","removeAt","set","size"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "JsonArray: " .. #miss .. "/16 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return json and json.newBuilder and json.newBuilder() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "JsonBuilder: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__newindex","build"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "JsonBuilder: " .. #miss .. "/3 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return json and json.newObject and json.newObject() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "JsonObject: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__ipairs","__newindex","__pairs","clear","containsKey","containsValue","get","put","remove","size"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "JsonObject: " .. #miss .. "/11 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nil end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "JsonSerializer: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"deserialize","serialize"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "JsonSerializer: " .. #miss .. "/2 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return keybinds:newKeybind("cov","key.keyboard.k") end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Keybind: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__newindex","enabled","getID","getKey","getKeyName","getName","gui","isDefault","isEnabled","isGuiEnabled","isPressed","key","onPress","onRelease","setEnabled","setGUI","setKey","setOnPress","setOnRelease"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Keybind: " .. #miss .. "/20 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return keybinds end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "KeybindAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"fromVanilla","getKeybinds","getVanillaKey","newKeybind","of"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "KeybindAPI: " .. #miss .. "/5 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return player end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "LivingEntityAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getAbsorptionAmount","getActiveHand","getActiveItem","getActiveItemTime","getArmor","getArrowCount","getBodyYaw","getDeathTime","getEntityCategory","getHealth","getHeldItem","getMaxHealth","getStingerCount","getSwingArm","getSwingDuration","getSwingTime","isBlocking","isClimbing","isGliding","isLeftHanded","isSensitiveToWater","isSwingingArm","isUsingItem","isVisuallySwimming","riptideSpinning"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "LivingEntityAPI: " .. #miss .. "/25 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return matrices end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "MatricesAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"mat2","mat3","mat4","rotation2","rotation3","rotation4","scale2","scale3","scale4","translate3","translate4","xRotation3","xRotation4","yRotation3","yRotation4","zRotation3","zRotation4"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "MatricesAPI: " .. #miss .. "/17 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return matrices.mat2 and matrices.mat2() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Matrix2: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__add","__eq","__index","__len","__mul","__newindex","__sub","__tostring","add","apply","applyDir","augmented","copy","det","getColumn","getRow","invert","inverted","multiply","reset","rightMultiply","rotate","scale","set","sub","transpose","transposed"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Matrix2: " .. #miss .. "/27 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return matrices.mat3 and matrices.mat3() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Matrix3: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__add","__eq","__index","__len","__mul","__newindex","__sub","__tostring","add","apply","applyDir","augmented","copy","deaugmented","det","getColumn","getRow","invert","inverted","multiply","reset","rightMultiply","rotate","rotateX","rotateY","rotateZ","scale","set","sub","translate","transpose","transposed"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Matrix3: " .. #miss .. "/32 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return matrices.mat4() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Matrix4: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__add","__eq","__index","__len","__mul","__newindex","__sub","__tostring","add","apply","applyDir","copy","deaugmented","det","getColumn","getRow","invert","inverted","multiply","reset","rightMultiply","rotate","rotateX","rotateY","rotateZ","scale","set","sub","translate","transpose","transposed"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Matrix4: " .. #miss .. "/31 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return models end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "ModelPart: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__newindex","addChild","addTask","color","copy","getAllVertices","getAnimPos","getAnimRot","getAnimScale","getChildren","getColor","getLight","getName","getNormalMatrix","getNormalMatrixRaw","getOffsetPivot","getOffsetRot","getOffsetScale","getOpacity","getOverlay","getParent","getParentType","getPivot","getPos","getPositionMatrix","getPositionMatrixRaw","getPrimaryColor","getPrimaryDefinedTextures","getPrimaryRenderType","getPrimaryTexture","getRot","getScale","getSecondaryColor","getSecondaryDefinedTextures","getSecondaryRenderType","getSecondaryTexture","getTask","getTextureSize","getTextures","getTruePivot","getTruePos","getTrueRot","getTrueScale","getType","getUV","getUVMatrix","getUVPixels","getVertices","getVisible","isChildOf","light","matrix","moveTo","newBlock","newEntity","newItem","newPart","newSprite","newText","offsetPivot","offsetRot","offsetScale","opacity","overlay","overrideVanillaPos","overrideVanillaRot","overrideVanillaScale","parentType","partToWorldMatrix","pivot","pos","primaryColor","primaryRenderType","primaryTexture","remove","removeChild","removeTask","rot","scale","secondaryColor","secondaryRenderType","secondaryTexture","setColor","setLight","setMatrix","setMidRender","setOffsetPivot","setOffsetRot","setOffsetScale","setOpacity","setOverlay","setParentType","setPivot","setPos","setPostRender","setPreRender","setPrimaryColor","setPrimaryRenderType","setPrimaryTexture","setRot","setScale","setSecondaryColor","setSecondaryRenderType","setSecondaryTexture","setUV","setUVMatrix","setUVPixels","setVisible","uv","uvMatrix","uvPixels","visible"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "ModelPart: " .. #miss .. "/113 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nameplate end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "NameplateAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "NameplateAPI: " .. #miss .. "/1 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nameplate.CHAT end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "NameplateCustomization: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getText","setText"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "NameplateCustomization: " .. #miss .. "/2 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nameplate.ALL end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "NameplateCustomizationGroup: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"setText"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "NameplateCustomizationGroup: " .. #miss .. "/1 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return net end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "NetworkingAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","isLinkAllowed","isNetworkingAllowed"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "NetworkingAPI: " .. #miss .. "/3 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nil end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "NullEntity: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","isLoaded"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "NullEntity: " .. #miss .. "/2 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nil end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "OutputStream: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"close","flush","write"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "OutputStream: " .. #miss .. "/3 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return action_wheel:newPage("cov") end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Page: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__newindex","action","getAction","getActions","getSlotsShift","getTitle","newAction","setAction","setKeepSlots","setSlotsShift","shouldKeepSlots","slotsShift"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Page: " .. #miss .. "/13 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return particles["minecraft:flame"] end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Particle: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"color","getColor","getGravity","getLifetime","getPos","getPower","getScale","getSize","getVelocity","gravity","hasPhysics","isAlive","lifetime","physics","pos","power","remove","scale","setColor","setGravity","setLifetime","setPhysics","setPos","setPower","setScale","setSize","setVelocity","size","spawn","velocity"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Particle: " .. #miss .. "/30 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return particles end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "ParticleAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","isPresent","newParticle","removeParticles"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "ParticleAPI: " .. #miss .. "/4 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return pings end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "PingAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__newindex"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "PingAPI: " .. #miss .. "/2 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nil end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "PingFunction: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "PingFunction: " .. #miss .. "/0 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return player end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "PlayerAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getChargedAttackDelay","getCooldownPercent","getExhaustion","getExperienceLevel","getExperienceProgress","getFood","getGamemode","getModelType","getSaturation","getShoulderEntity","getTeamInfo","hasCape","hasSkin","isFishing","isSkinLayerVisible"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "PlayerAPI: " .. #miss .. "/15 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return raycast end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "RaycastAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"aabb","block","entity"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "RaycastAPI: " .. #miss .. "/3 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return models:newText("covt") end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "RenderTask: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getLight","getName","getNormalMatrix","getNormalMatrixRaw","getOverlay","getPos","getPositionMatrix","getPositionMatrixRaw","getRot","getScale","isVisible","light","matrix","overlay","pos","remove","rot","scale","setLight","setMatrix","setOverlay","setPos","setRot","setScale","setVisible","visible"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "RenderTask: " .. #miss .. "/26 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return renderer end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "RendererAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","__newindex","blockOutlineColor","cameraMatrix","cameraNormal","cameraPivot","cameraPos","cameraRot","crosshairOffset","eyeOffset","fov","getBlockOutlineColor","getCameraMatrix","getCameraNormal","getCameraOffsetPivot","getCameraOffsetRot","getCameraPivot","getCameraPos","getCameraRot","getCrosshairOffset","getEyeOffset","getFOV","getOutlineColor","getPrimaryFireTexture","getRenderLeftArm","getRenderRightArm","getRootRotationAllowed","getSecondaryFireTexture","getShadowRadius","isCameraBackwards","isFirstPerson","isUpsideDown","offsetCameraPivot","offsetCameraRot","outlineColor","postEffect","primaryFireTexture","renderLeftArm","renderRightArm","rootRotationAllowed","secondaryFireTexture","setBlockOutlineColor","setCameraMatrix","setCameraNormal","setCameraPivot","setCameraPos","setCameraRot","setCrosshairOffset","setEyeOffset","setFOV","setForcePaperdoll","setOffsetCameraPivot","setOffsetCameraRot","setOutlineColor","setPostEffect","setPrimaryFireTexture","setRenderCrosshair","setRenderFire","setRenderHUD","setRenderLeftArm","setRenderRightArm","setRenderVehicle","setRootRotationAllowed","setSecondaryFireTexture","setShadowRadius","setUpsideDown","shadowRadius","shouldForcePaperdoll","shouldRenderCrosshair","shouldRenderFire","shouldRenderHUD","shouldRenderVehicle","upsideDown"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "RendererAPI: " .. #miss .. "/73 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return resources end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "ResourcesAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","get","getPaths"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "ResourcesAPI: " .. #miss .. "/3 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return sounds["x"] end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Sound: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"attenuation","getAttenuation","getPitch","getPos","getSubtitle","getVolume","isLooping","isPlaying","loop","pause","pitch","play","pos","setAttenuation","setLoop","setPitch","setPos","setSubtitle","setVolume","stop","subtitle","volume"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Sound: " .. #miss .. "/22 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return sounds end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "SoundAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","getCustomSounds","isPresent","newSound","playSound","stopSound"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "SoundAPI: " .. #miss .. "/6 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return models:newSprite("covs") end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "SpriteTask: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"color","dimensions","getColor","getDimensions","getRegion","getRenderType","getSize","getTexture","getUV","getUVPixels","getVertices","region","renderType","setColor","setDimensions","setRegion","setRenderType","setSize","setTexture","setUV","setUVPixels","size","texture","uv","uvPixels"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "SpriteTask: " .. #miss .. "/25 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return models:newText("covt2") end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "TextTask: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"alignment","background","backgroundColor","getAlignment","getBackgroundColor","getOpacity","getOutlineColor","getText","getWidth","hasBackground","hasOutline","hasShadow","hasWrap","isSeeThrough","opacity","outline","outlineColor","seeThrough","setAlignment","setBackground","setBackgroundColor","setOpacity","setOutline","setOutlineColor","setSeeThrough","setShadow","setText","setWidth","setWrap","shadow","text","width","wrap"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "TextTask: " .. #miss .. "/33 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return textures:getTextures()[1] end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Texture: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","applyFunc","applyMatrix","fill","getDimensions","getName","getPath","getPixel","pixel","restore","save","setPixel","update"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Texture: " .. #miss .. "/13 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return textures end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "TextureAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","copy","fromVanilla","get","getTextures","newTexture","read"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "TextureAPI: " .. #miss .. "/7 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return client.getAtlas and client.getAtlas("minecraft:textures/atlas/blocks.png") end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "TextureAtlas: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getHeight","getSpriteUV","getWidth","listSprites"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "TextureAtlas: " .. #miss .. "/4 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return vanilla_model end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "VanillaModelAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "VanillaModelAPI: " .. #miss .. "/1 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return vanilla_model.PLAYER end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "VanillaModelGroup: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__index","offsetRot","offsetScale","pos","rot","scale","setOffsetRot","setOffsetScale","setPos","setRot","setScale","setVisible","visible"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "VanillaModelGroup: " .. #miss .. "/13 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return vanilla_model.HEAD end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "VanillaModelPart: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getOriginPos","getOriginRot","getOriginScale","getOriginVisible"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "VanillaModelPart: " .. #miss .. "/4 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return vanilla_model.HEAD end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "VanillaPart: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getOffsetRot","getOffsetScale","getPos","getRot","getScale","getVisible","offsetRot","offsetScale","pos","rot","scale","setOffsetRot","setOffsetScale","setPos","setRot","setScale","setVisible","visible"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "VanillaPart: " .. #miss .. "/18 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return vec(1,2) end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Vector2: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__add","__div","__eq","__index","__le","__len","__lt","__mod","__mul","__newindex","__sub","__tostring","__unm","add","applyFunc","augmented","ceil","clampLength","clamped","copy","div","dot","floor","length","lengthSquared","mul","normalize","normalized","offset","reduce","reset","scale","set","sub","toDeg","toRad","toString","transform","unpack"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Vector2: " .. #miss .. "/39 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return vec(1,2,3) end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Vector3: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__add","__div","__eq","__index","__le","__len","__lt","__mod","__mul","__newindex","__sub","__tostring","__unm","add","applyFunc","augmented","ceil","clampLength","clamped","copy","cross","crossed","div","dot","floor","length","lengthSquared","mul","normalize","normalized","offset","reduce","reset","scale","set","sub","toDeg","toRad","toString","transform","unpack"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Vector3: " .. #miss .. "/41 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return vec(1,2,3,4) end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Vector4: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"__add","__div","__eq","__index","__le","__len","__lt","__mod","__mul","__newindex","__sub","__tostring","__unm","add","applyFunc","ceil","clampLength","clamped","copy","div","dot","floor","length","lengthSquared","mul","normalize","normalized","offset","reduce","reset","scale","set","sub","toDeg","toRad","toString","transform","unpack"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Vector4: " .. #miss .. "/38 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return vectors end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "VectorsAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"angleToDir","hexToRGB","hsvToRGB","intToRGB","rgbToHSV","rgbToHex","rgbToInt","rotateAroundAxis","toCameraSpace","vec","vec2","vec3","vec4","worldToScreenSpace"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "VectorsAPI: " .. #miss .. "/14 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return nil end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "Vertex: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getNormal","getPos","getUV","normal","pos","setNormal","setPos","setUV","uv"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "Vertex: " .. #miss .. "/9 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return client.getViewer and client.getViewer() end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "ViewerAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"getAir","getAttackCharge","getReachDistance","getStatusEffects","isFlying","isJumping"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "ViewerAPI: " .. #miss .. "/6 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return world end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "WorldAPI: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"avatarVars","exists","getBiome","getBlockLightLevel","getBlockState","getBlocks","getBuildHeight","getDay","getDayTime","getDimension","getEntities","getEntity","getHeight","getLightLevel","getMapData","getMoonPhase","getPlayers","getRainGradient","getRedstonePower","getSkyLightLevel","getSpawnPoint","getStrongRedstonePower","getTime","getTimeOfDay","isChunkLoaded","isOpenSky","isThundering","newBlock","newItem"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "WorldAPI: " .. #miss .. "/29 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return _G end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "globals: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({"log","logJson","logTable","parseJson","toJson"}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "globals: " .. #miss .. "/5 missing: " .. table.concat(miss, " ")
  end end
do local ok, o = pcall(function() return math end)
  local miss = {}
  if not ok or o == nil then R[#R+1] = "math: NO INSTANCE (" .. tostring(o) .. ")" else
    for _, m in ipairs({}) do
      local ok2, v = pcall(function() return o[m] end)
      if not ok2 or v == nil then miss[#miss+1] = m end
    end
    R[#R+1] = "math: " .. #miss .. "/0 missing: " .. table.concat(miss, " ")
  end end
for _, l in ipairs(R) do print(l) end