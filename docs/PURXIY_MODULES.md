# Purxiy module settings (reference spec)

Read from the readable purxiy-loader.json the buddies shared (Reina0613, OwlStorm / Ashe Kirk, 1j01). Their code is locked; this is only the settings schema we match 1:1 where we build the same feature.

## Active Modules (`active_modules`, HUD)
Every module you have switched on, as a list

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 1.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `sort` Order: Mode default Length [Length / Alphabetical]
- `gradient` Gradient: Boolean default True - Colour the list with the Purxiy gradient
- `hud` HUD elements: Boolean default False - List enabled HUD elements as well

## Afterglow (`afterglow`, RENDER)
The fallen dissolve into drifting light

- `style` Style: Mode default Motes [Motes / Stardust / Petals] - The light they dissolve into
- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the light
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `who` Who: Mode default Everyone [Everyone / Players & monsters / Players] - Whose fall turns to light
- `yours` Only your kills: Boolean default False - Only what you defeat yourself dissolves
- `speed` Speed: Number default 1.0 0.5..2.0 - How fast they dissolve
- `amount` Light: Number default 1.0 0.0..2.0 - How much light they release
- `glow` Glow: Number default 0.7 0.0..1.0 - The burning edge and the light it sheds
- `fall` Fall over: Boolean default False - Topple over as in vanilla while they dissolve
- `sound` Sound: Boolean default True - A soft shimmer where they dissolve

## Ambience (`ambience`, RENDER)
Glowing particles drifting through the world around you

- `style` Style: Mode default Motes [Motes / Stardust / Petals / Snow] - What drifts through the air
- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the light
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `amount` Amount: Number default 1.0 0.1..2.0 - How many drift around you
- `size` Size: Number default 1.0 0.5..2.0
- `glow` Glow: Number default 0.6 0.0..1.0 - The soft light around each one
- `speed` Speed: Number default 1.0 0.0..3.0 - How fast they drift; at 0 they hang still
- `twinkle` Twinkle: Number default 0.5 0.0..1.0 - How much they sparkle and breathe
- `range` Range: Number default 24.0 8.0..40.0 - How far around you they appear, in blocks
- `depth_of_field` Depth of field: Boolean default True - The closest ones melt into soft bokeh
- `react` React to you: Boolean default True - They swirl aside as you move through them
- `curious` Curious: Boolean default False - Stand still and a few drift over to circle around you
- `daylight` Daylight: Number default 0.5 0.0..1.0 - How many stay out in the open by day

## Armor Status (`armor_status`, HUD)
Durability of everything you wear

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 1.0 0.0..1.0
- `y` Y: Number default 0.72 0.0..1.0
- `placed` Placed: Boolean default False
- `layout` Layout: Mode default Vertical [Vertical / Horizontal]
- `durability` Durability: Mode default Value [Value / Percent / Bar only]
- `held` Held items: Boolean default True - Main and off hand as well

## Aura (`aura`, COSMETICS)
Soft light that lives around you

- `style` Style: Mode default Orbit [Orbit / Fireflies / Spiral / Constellation] - How your aura looks
- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the aura
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `size` Size: Number default 1.0 0.6..1.6 - How far the aura reaches around you
- `speed` Speed: Number default 1.0 0.25..2.5 - How lively it moves
- `glow` Glow: Number default 0.65 0.0..1.0 - A soft halo of light around every light
- `first_person` In first person: Boolean default False - See your aura from your own eyes too
- `others` Other players: Boolean default False - Auras around other players too

## Block Outline (`block_outline`, RENDER)
A cleaner outline for the block you target

- `style` Style: Mode default Outline [Outline / Corners] - Every edge, or only the corners
- `color` Colour: Color default accent
- `gradient` Gradient: Boolean default True - Blend into a second colour towards the top
- `color2` Second colour: Color default accent
- `width` Line width: Number default 2.0 0.5..5.0
- `glow` Glow: Number default 0.7 0.0..1.0 - A soft halo of light around the lines
- `fill` Fill: Number default 0.0 0.0..1.0 - Tint the faces as well
- `aim_light` Aim light: Number default 0.6 0.0..1.0 - A soft light that follows your crosshair over the block
- `glide` Glide: Boolean default True - Slide smoothly from block to block
- `speed` Animation speed: Number default 1.0 0.25..3.0 - How fast the outline glides, fades and shimmers
- `shimmer` Shimmer: Number default 0.4 0.0..1.0 - Now and then, a sheen of light sweeps over the edges
- `mining` Mining progress: Boolean default True - The outline draws itself in while you break a block
- `feedback` Feedback: Boolean default True - A flash when you place a block, a ripple when one breaks
- `hidden_edges` Hidden edges: Boolean default False - Faintly dashed edges behind the block, like a blueprint

## Bokeh (`bokeh`, RENDER)
Sharp where you look, creamy light behind

- `focus` Focus: Mode default Auto [Auto / Distance / Tilt-shift] - What the lens keeps sharp
- `speed` Focus speed: Number default 1.0 0.25..3.0 - How quickly the focus follows your look
- `near` Near blur: Boolean default True - What is closer than the focus melts away too
- `distance` Distance: Number default 24.0 4.0..96.0 - Everything nearer than this stays sharp
- `band` Sharp band: Number default 0.26 0.06..0.6 - How much of the picture stays sharp
- `shape` Bokeh: Mode default Round [Round / Oval / Blades / Star / Heart] - The shape lights take when they blur
- `blur` Blur: Number default 0.5 0.1..1.0 - How soft the out-of-focus world becomes
- `highlights` Highlights: Number default 0.6 0.0..1.0 - How brightly lights bloom into discs
- `fringe` Fringe: Number default 0.45 0.0..1.0 - A soft lilac edge around every disc
- `swirl` Swirl: Boolean default False - Discs near the edges turn, like an old lens

## Cape (`cape`, COSMETICS)
Wear an animated Purxiy cape

- `design` Cape: Mode default Purxiy [Purxiy / Midnight / Pearl / Nebula / Aurora / Eclipse / Amethyst / The End / Cherry] - Which cape you wear
- `glow` Glow: Number default 0.7 0.0..1.0 - How brightly it shines in the dark
- `speed` Animation: Number default 1.0 0.0..2.0 - How fast it moves; at 0 it holds still
- `motion` Movement: Mode default Flowing [Flowing / Vanilla] - Flowing bends like cloth; Vanilla is the game's own
- `elytra` On your elytra: Boolean default True - Your elytra wears the design too

## Chroma (`chroma`, RENDER)
The world waits in grey, and colour blooms wherever you walk

- `tone` Tone: Mode default Lilac [Lilac / Cream / Noir] - The shade the world waits in
- `hint` Hint of colour: Number default 0.06 0.0..0.5 - How much colour the grey world keeps
- `living` Living colour: Boolean default True - Players, mobs and items keep their colour
- `brush` Brush size: Number default 6.0 3.0..12.0 - How far colour blooms around you
- `memory` Memory: Mode default 2 min [30 s / 2 min / 10 min / Forever] - How long colour stays where you have been
- `splashes` Splashes: Boolean default True - Landings, hits and blasts splash colour
- `others` Other players: Boolean default False - Colour blooms where they walk too

## Clock (`clock`, HUD)
Real-world time

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `format` Format: Mode default 24h [24h / 12h]
- `seconds` Seconds: Boolean default False
- `date` Date: Boolean default False - Today's date next to the time

## Coordinates (`coordinates`, HUD)
Where you are, always in view

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `layout` Layout: Mode default Stacked [Stacked / Single line]
- `precision` Precision: Mode default Blocks [Blocks / Decimals]
- `facing` Facing: Boolean default True - The direction you are looking, like North (-Z)
- `biome` Biome: Boolean default False
- `other` Other dimension: Boolean default False - Matching Overworld or Nether position

## CPS (`cps`, HUD)
Clicks per second, left and right

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `right` Right clicks: Boolean default True - Show right clicks next to left clicks

## Creamy Keys (`creamy_keys`, CLIENT)
Your keyboard, with a sound of its own

- `style` Sound style: Mode default Creamy [Creamy / Clacky / Thocky / Honey Creamy] - Click a key to select and listen
- `keyboard` Keyboard sounds: Boolean default True - Hear each press and release
- `keyboard_volume` Keyboard volume: Number default 0.7 0.0..1.0
- `release_volume` Release volume: Number default 0.85 0.0..1.0 - Relative to the key press
- `mouse` Mouse clicks: Boolean default True - Distinct left, right and middle clicks
- `mouse_volume` Click volume: Number default 0.65 0.0..1.0
- `scroll` Scroll wheel: Boolean default True - A soft notch in either direction
- `scroll_volume` Scroll volume: Number default 0.5 0.0..1.0
- `pitch` Pitch: Number default 1.0 0.88..1.12 - Deep to bright
- `variation` Natural variation: Number default 0.02 0.0..0.08 - Alternate recorded takes with subtle touch variation

## Custom Crosshair (`custom_crosshair`, RENDER)
Your aim, in Purxiy colours. Make it yours.

- `style` Style: Mode default Vesper [Vesper / Cross / Halo / Diamond / Brackets / Dot] - Six signatures. One perfect centre.
- `size` Size: Number default 1.0 0.6..2.0 - Independent of HUD and GUI scale
- `thickness` Thickness: Number default 2.4 1.0..5.0 - Bold strokes with a precise centre
- `palette` Palette: Mode default Purxiy [Purxiy / Frost / Rose / Custom] - Colours from the Purxiy night sky
- `animation` Idle animation: Mode default Breathe [Off / Breathe / Orbit] - The centre always stays still
- `advanced` Advanced settings: Boolean default False - Shape, light, reactions and sound
- `gap` Centre gap: Number default 3.5 1.0..10.0 - Clear space around your aim
- `length` Arm length: Number default 4.5 2.0..12.0 - Also changes the radius of closed shapes
- `dot` Centre dot: Boolean default True - A fixed, bright aiming point
- `outline` Outline: Number default 1.0 0.0..3.0 - Dark edge for bright backgrounds
- `glow` Glow: Number default 0.25 0.0..1.0 - A soft violet halo around the strokes
- `opacity` Opacity: Number default 1.0 0.25..1.0 - Keep the crosshair clearly visible
- `primary` Primary colour: Color default accent - Inner stroke
- `secondary` Secondary colour: Color default accent - Outer stroke and orbit
- `speed` Animation speed: Number default 1.0 0.0..3.0 - Zero freezes the decorative animation
- `breathing` Breathing amount: Number default 0.3 0.0..1.0 - Gentle movement of the outer strokes
- `spread` Movement spread: Number default 0.0 0.0..6.0 - Visual response to walking; not weapon accuracy
- `kick` Attack pulse: Number default 1.5 0.0..6.0 - Outer strokes open as you swing
- `target_tint` Target highlight: Boolean default True - Tint when aiming at a visible living entity
- `target_color` Target colour: Color default #FFFFAACF - Soft rose when a target is in your sights
- `hit_marker` Hit confirmation: Boolean default True - A brief sparkle on server-confirmed damage you deal
- `cooldown` Attack indicator: Mode default Auto [Auto / Ring / Bar / Off] - Auto follows Minecraft's crosshair indicator option
- `sound` Hit sound: Mode default Off [Off / Soft / Chime] - Only for confirmed hits; respects Client > Sound
- `volume` Hit volume: Number default 0.45 0.0..1.0 - Keep combat feedback subtle
- `third_person` Third person: Boolean default False - Also show the reticle in third person
- `hide_using` Hide while using items: Boolean default False - Fade while eating, blocking or drawing a bow

## Cute World (`cute_world`, RENDER)
Sparkling eyes, rosy cheeks and a world full of little smiles

- `players` Player faces: Boolean default True - Give each player's own skin a cute expression
- `mobs` Mob faces: Boolean default True - Cute creatures, from creepers to cows
- `expression` Expression: Mode default Sparkle [Sparkle / Happy / Sleepy] - Choose their little personality
- `blush` Rosy cheeks: Boolean default True - A soft touch of pink beneath the eyes
- `adapt` Read skin colours: Boolean default True - Use the face's own skin tone and eye colour; keep hair and clothes
- `softness` Plush colours: Number default 0.3 0.0..0.7 - Soften mob textures into pastels
- `scenery` Pastel scenery: Boolean default True - Candy-coloured leaves, soft grass, water, sky and clouds
- `palette` Palette: Mode default Sakura [Sakura / Peach / Lavender] - The colours of your little world
- `hearts` Hearts & sparkles: Boolean default True - Occasional hearts over creatures and sparkles in the air
- `amount` Sprinkles: Number default 0.5 0.1..1.0 - How often little hearts and sparkles appear

## Cyberspace (`cyberspace`, RENDER)
Step into a digital dimension of light, wires and data

- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the light
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `theme` Theme: Mode default Midnight [Midnight / Porcelain] - A midnight network, or a bright one of porcelain
- `reality` Reality: Number default 0.15 0.0..1.0 - How much of the real world shows through
- `glow` Glow: Number default 0.7 0.0..1.0 - How brightly the lines shine
- `circuits` Circuits: Boolean default True - Wiring on every block, with data running through it
- `flight` Flying data: Boolean default True - Packets of light race through the air, bits drift up from the ground
- `scan` Scan wave: Boolean default True - A wave of light sweeps out from you every few seconds
- `interval` Scan every: Number default 8.0 3.0..20.0
- `sky` Digital sky: Boolean default True - An endless grid below the horizon, stars of data above
- `glitch` Glitch when hurt: Boolean default True - The simulation stutters for a moment when you take damage

## Direction (`direction`, HUD)
A compass strip that glides with your view

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.5 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `degrees` Degrees: Mode default Heading [Heading / Yaw / Off] - Heading counts clockwise from north, yaw matches the debug screen

## FakePay (`fake_pay`, UTILITY)
A /pay only you see, with the server's message and sounds. For clips

- `command` Command: Text default pay - The pay command to answer, without the slash
- `feedback` Show in: Mode default Both [Both / Action bar / Chat] - Where the message appears
- `currency` Currency: Text default $ - The symbol before the amount
- `currency_color` Currency colour: Color default #FF008EFF
- `sounds` Sounds: Boolean default True - A level-up when it works, a villager's no when it does not
- `check_balance` Check balance: Boolean default True - With FakeStats on, refuse payments larger than the fake balance
- `self_guard` No paying yourself: Boolean default True - Refuse paying your own name, like the real command

## FakeStats (`fake_stats`, UTILITY)
Your sidebar shows the money, shards and kills you type. Only on your screen

- `match` Find lines: Mode default By position [By position / By name] - By position counts lines from the top (DonutSMP has no labels); By name looks for words such as Money or Kills
- `money` Money: Text default 1m - Your fake balance, such as 1m, 250k or 1500. Empty leaves it alone
- `money_line` Money line: Number default 1.0 1.0..15.0 - Which sidebar line, counted from the top
- `shards` Shards: Text default  - Empty leaves the shards alone
- `shards_line` Shards line: Number default 2.0 1.0..15.0 - Which sidebar line, counted from the top
- `kills` Kills: Text default  - Empty leaves the kills alone
- `kills_line` Kills line: Number default 3.0 1.0..15.0 - Which sidebar line, counted from the top
- `deaths` Deaths: Text default  - Empty leaves the deaths alone
- `deaths_line` Deaths line: Number default 4.0 1.0..15.0 - Which sidebar line, counted from the top
- `playtime` Playtime: Text default  - Any text, such as 365d 12h. Empty leaves it alone
- `playtime_line` Playtime line: Number default 5.0 1.0..15.0 - Which sidebar line, counted from the top
- `balance_command` Answer /bal: Boolean default True - /bal, /balance and /money show your fake balance
- `deduct_on_pay` Pay from balance: Boolean default True - FakePay takes what you pay off the balance

## FPS (`fps`, HUD)
Frames per second, with a live frame graph

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `graph` Frame graph: Boolean default True - A tiny live graph of recent frame times

## Freelook (`freelook`, RENDER)
Look around without turning

- `activation` Activation: Mode default Hold [Hold / Toggle] - Hold the key or click to toggle
- `animation` Animation: Boolean default True - Glide from close to far and back. Off, it jumps at once
- `animation_speed` Animation speed: Number default 1.0 0.25..3.0 - How fast the camera glides out and back
- `distance` Distance: Number default 4.0 1.5..12.0 - How far away it floats. Scroll to change it
- `through_walls` Through walls: Boolean default False - See yourself through the wall behind you

## Glass (`glass`, RENDER)
Players and items made of glass

- `color` Colour: Color default #FFFFFFFF - White glass, or tinted any colour you like
- `frost` Frosted: Number default 0.35 0.0..1.0 - Clear as a window, or soft and milky
- `glow` Glow: Number default 0.2 0.0..1.0 - How much light the glass gives off
- `shimmer` Shimmer: Boolean default True - Now and then a soft sheen glides over the glass
- `self` You: Boolean default True - Your body in third person, your arm in first person
- `players` Other players: Boolean default True - Everyone else around you
- `items` Items: Boolean default True - Held, dropped and in item frames
- `armour` Armour: Boolean default True - Worn armour too (off: it keeps its own look)

## Halo (`halo`, COSMETICS)
A gently glowing halo

- `design` Halo: Mode default Seraph [Seraph / Sigil / Crown / Stardust / Eclipse / Fallen] - Which halo you wear
- `skin_colour` Match your skin: Boolean default False - Its colour comes from the skin you wear
- `tint` Colour: Color default accent - The colour of its light
- `size` Size: Number default 1.0 0.7..1.4 - How wide the ring is
- `glow` Glow: Number default 0.7 0.0..1.0 - How brightly it shines
- `height` Height: Number default 1.0 0.6..1.8 - How far it floats above your head
- `follow` Follow your head: Number default 0.4 0.0..1.0 - How far it tilts as you look up and down
- `float` Float: Boolean default True - It bobs and sways softly over your head

## Hand Movement (`hand_movement`, PLAYER)
Place, swing and move your hand your way

- `preset` Placement: Mode default Vanilla [Vanilla / Low / Compact / Wide / Centred / Custom] - A starting point: the studio fine-tunes it
- `editing` Editing: Mode default Main hand [Main hand / Off hand] - Which hand the sliders move
- `mirror` Mirror off hand: Boolean default True - Off hand follows, mirrored
- `main_x` Sideways: Number default 0.0 -0.6..0.6 - Towards the middle, or out
- `main_y` Height: Number default 0.0 -0.6..0.6 - Up, or down out of the way
- `main_z` Depth: Number default 0.0 -0.6..0.6 - Closer, or further away
- `main_pitch` Tilt: Number default 0.0 -90.0..90.0 - Tipped forwards or back
- `main_yaw` Turn: Number default 0.0 -90.0..90.0 - Turned in or out
- `main_roll` Roll: Number default 0.0 -90.0..90.0 - Rolled around your view
- `main_size` Size: Number default 1.0 0.4..1.6 - Smaller or larger
- `off_x` Sideways: Number default 0.0 -0.6..0.6 - Towards the middle, or out
- `off_y` Height: Number default 0.0 -0.6..0.6 - Up, or down out of the way
- `off_z` Depth: Number default 0.0 -0.6..0.6 - Closer, or further away
- `off_pitch` Tilt: Number default 0.0 -90.0..90.0 - Tipped forwards or back
- `off_yaw` Turn: Number default 0.0 -90.0..90.0 - Turned in or out
- `off_roll` Roll: Number default 0.0 -90.0..90.0 - Rolled around your view
- `off_size` Size: Number default 1.0 0.4..1.6 - Smaller or larger
- `swing` Style: Mode default Smooth [Vanilla / Smooth / Chop / Slash / Punch] - How you hit, chop and mine
- `swing_speed` Speed: Number default 1.0 0.5..2.0 - How quick it looks (only to you)
- `swing_amount` Intensity: Number default 1.0 0.0..1.5 - How far your hand swings
- `blur` Swing blur: Boolean default True - A clean trail as you chop or hit
- `blur_length` Length: Number default 0.35 0.1..1.0 - How far back the trail reaches
- `blur_strength` Strength: Number default 0.6 0.1..1.0 - How clearly the trail shows
- `blur_radiance` Radiance light: Boolean default True - With Radiance, the trail glows too
- `sway` Sway: Number default 1.0 0.0..2.0 - Your hand trails as you look around
- `bobbing` Bobbing: Number default 1.0 0.0..1.5 - As you walk (with View Bobbing on)
- `momentum` Momentum: Number default 0.5 0.0..1.0 - Leans as you strafe, dips as you land
- `breathing` Breathing: Number default 0.4 0.0..1.0 - A slow float while you stand still
- `equip` Switching: Mode default Smooth [Vanilla / Smooth / Instant] - How a new item comes up
- `cooldown_dip` Cooldown dip: Boolean default True - Lowered while your attack recharges
- `empty_hand` Empty hand: Boolean default True - Show your arm when you hold nothing
- `studio_key` Studio key: Key default -1 - Opens the Hand Studio in game

## Haze (`haze`, RENDER)
A soft haze of colour over the world around you

- `palette` Colour: Mode default Rose [Rose / Lilac / Peach / Sunset / Mint / Ocean / Aurora / Custom] - The colour of the haze
- `color` Your colour: Color default accent - The rest of the haze is drawn from it
- `strength` Strength: Number default 0.45 0.1..1.0 - How strongly the world takes on the colour
- `height` Height: Number default 24.0 6.0..64.0 - How high the haze rises above the ground
- `clear` Clear around you: Number default 8.0 0.0..32.0 - How far around you the air stays clear
- `drift` Drift: Number default 1.0 0.0..3.0 - How fast the colour drifts by
- `glow` Sun glow: Number default 0.6 0.0..1.0 - A soft glow towards the sun and moon
- `creatures` See players: Boolean default True - Players and mobs keep their colours

## Hit Splash (`hit_splash`, RENDER)
Glowing splashes where your hits land

- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the ink
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `size` Size: Number default 1.0 0.5..2.0 - How big the splashes are
- `duration` Duration: Number default 6.0 2.0..20.0 - How long a splash stays on the ground
- `glow` Glow: Number default 0.7 0.0..1.0 - Its light, brightest where hits pile up
- `others` Other players: Boolean default False - Splashes where other players land hits too

## HUD (`hud`, CLIENT)
Scale, glass and the HUD editor

- `scale` HUD scale: Number default 1.0 0.75..1.5 - Size of every HUD element together
- `glass` Frosted glass: Boolean default True - Blur the world behind HUD panels
- `chat_editor` Edit in chat: Boolean default True - Open the chat (T) to move, resize and arrange your HUD
- `snap` Snap to guides: Boolean default True - Magnetic centre lines and alignment with other elements
- `no_overlap` Prevent overlap: Boolean default True - Elements push each other aside instead of stacking
- `group1_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `group1_background` Background: Boolean default True - A frosted panel behind the element
- `group1_opacity` Background opacity: Number default 0.6 0.0..1.0
- `group1_x` X: Number default 0.0 0.0..1.0
- `group1_y` Y: Number default 0.0 0.0..1.0
- `group1_placed` Placed: Boolean default False
- `group1_members` Members: Text default 
- `group1_axis` Direction: Mode default Row [Row / Column]
- `group2_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `group2_background` Background: Boolean default True - A frosted panel behind the element
- `group2_opacity` Background opacity: Number default 0.6 0.0..1.0
- `group2_x` X: Number default 0.0 0.0..1.0
- `group2_y` Y: Number default 0.0 0.0..1.0
- `group2_placed` Placed: Boolean default False
- `group2_members` Members: Text default 
- `group2_axis` Direction: Mode default Row [Row / Column]
- `group3_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `group3_background` Background: Boolean default True - A frosted panel behind the element
- `group3_opacity` Background opacity: Number default 0.6 0.0..1.0
- `group3_x` X: Number default 0.0 0.0..1.0
- `group3_y` Y: Number default 0.0 0.0..1.0
- `group3_placed` Placed: Boolean default False
- `group3_members` Members: Text default 
- `group3_axis` Direction: Mode default Row [Row / Column]
- `group4_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `group4_background` Background: Boolean default True - A frosted panel behind the element
- `group4_opacity` Background opacity: Number default 0.6 0.0..1.0
- `group4_x` X: Number default 0.0 0.0..1.0
- `group4_y` Y: Number default 0.0 0.0..1.0
- `group4_placed` Placed: Boolean default False
- `group4_members` Members: Text default 
- `group4_axis` Direction: Mode default Row [Row / Column]
- `group5_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `group5_background` Background: Boolean default True - A frosted panel behind the element
- `group5_opacity` Background opacity: Number default 0.6 0.0..1.0
- `group5_x` X: Number default 0.0 0.0..1.0
- `group5_y` Y: Number default 0.0 0.0..1.0
- `group5_placed` Placed: Boolean default False
- `group5_members` Members: Text default 
- `group5_axis` Direction: Mode default Row [Row / Column]
- `group6_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `group6_background` Background: Boolean default True - A frosted panel behind the element
- `group6_opacity` Background opacity: Number default 0.6 0.0..1.0
- `group6_x` X: Number default 0.0 0.0..1.0
- `group6_y` Y: Number default 0.0 0.0..1.0
- `group6_placed` Placed: Boolean default False
- `group6_members` Members: Text default 
- `group6_axis` Direction: Mode default Row [Row / Column]
- `group7_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `group7_background` Background: Boolean default True - A frosted panel behind the element
- `group7_opacity` Background opacity: Number default 0.6 0.0..1.0
- `group7_x` X: Number default 0.0 0.0..1.0
- `group7_y` Y: Number default 0.0 0.0..1.0
- `group7_placed` Placed: Boolean default False
- `group7_members` Members: Text default 
- `group7_axis` Direction: Mode default Row [Row / Column]
- `group8_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `group8_background` Background: Boolean default True - A frosted panel behind the element
- `group8_opacity` Background opacity: Number default 0.6 0.0..1.0
- `group8_x` X: Number default 0.0 0.0..1.0
- `group8_y` Y: Number default 0.0 0.0..1.0
- `group8_placed` Placed: Boolean default False
- `group8_members` Members: Text default 
- `group8_axis` Direction: Mode default Row [Row / Column]

## HUD Hider (`hud_hider`, HUD)
Choose which Minecraft HUD elements to hide

- `scoreboard` Hide scoreboard: Boolean default True - The sidebar leaderboard and server information
- `titles` Hide titles: Boolean default True - Large messages such as You won, including their subtitles
- `action_bar` Hide action bar: Boolean default False - Short messages above the hotbar
- `boss_bars` Hide boss bars: Boolean default False - Boss health and server bars at the top of the screen
- `player_list` Hide player list: Boolean default False - The list of players shown when holding Tab
- `hotbar` Hide hotbar: Boolean default False - Item slots, offhand slot and the spectator hotbar
- `health` Hide health: Boolean default False - Your hearts, including absorption hearts
- `armor` Hide armor bar: Boolean default False - The armor points above the hotbar
- `hunger` Hide hunger: Boolean default False - The food bar above the hotbar
- `air` Hide air bubbles: Boolean default False - Your remaining breath underwater
- `mount_health` Hide mount health: Boolean default False - The hearts of the animal you are riding
- `info_bar` Hide experience / info bar: Boolean default False - The XP bar and level, locator bar and mount jump bar
- `item_name` Hide held item name: Boolean default False - The name shown when selecting an item
- `crosshair` Hide crosshair: Boolean default False - The aiming crosshair, including Custom Crosshair
- `status_effects` Hide effect icons: Boolean default False - Minecraft's potion effect icons in the top right
- `chat` Hide chat: Boolean default False - Hide chat while playing; opening chat still shows its history
- `sound_subtitles` Hide sound subtitles: Boolean default False - The accessibility captions for nearby sounds

## Interface (`interface`, CLIENT)
Your colour, language, scale and the look of the menu

- `accent` Accent colour: Color default #FFB48CFF - One colour for the menu, the HUD and your effects
- `logo` Logo colour: Mode default Original [Original / Accent / Custom] - Its own purple, your accent, or one you pick
- `logo_color` Colour: Color default #FFB48CFF - The colour the dog takes on
- `language` Language: Mode default Auto [Auto / English / Русский] - Auto follows the language of Minecraft
- `scale` Interface scale: Number default 1.0 0.8..1.25 - Size of everything in the Purxiy menu
- `blur` Background blur: Boolean default True - Frosted glass over the game behind the menu
- `dim` Background dim: Number default 0.45 0.2..0.85 - How much the game darkens behind the menu
- `stars` Starfield: Boolean default True - Twinkling stars around the orbit
- `pause` Pause singleplayer: Boolean default False - Pause the game while the menu is open

## Jump Circles (`jump_circles`, COSMETICS)
A ring of light where you jump

- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the rings
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `size` Size: Number default 1.2 0.5..3.0 - How far a ring spreads
- `duration` Duration: Number default 1.1 0.4..3.0 - How long a ring lasts
- `glow` Glow: Number default 0.6 0.0..1.0 - A soft halo of light around the ring
- `others` Other players: Boolean default False - Rings where other players jump too

## Keybinds (`keybinds`, CLIENT)
Every key you have bound, in one place


## Keystrokes (`keystrokes`, HUD)
Your movement keys and clicks, live

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.45 0.0..1.0
- `placed` Placed: Boolean default False
- `mouse` Mouse buttons: Boolean default True
- `space` Space bar: Boolean default True
- `cps` Show CPS: Boolean default True - Clicks per second on the mouse buttons
- `accent` Accent: Color default accent - Colour of pressed keys

## Knives (`knives`, COSMETICS)
Five iconic knives and three blades for both hands

- `model` Knife: Mode default Karambit [Karambit / Butterfly / M9 Bayonet / Tactical / Crystal Fang] - Five sculpted blades, each with tricks of its own
- `finish` Finish: Mode default Signature [Signature / Obsidian / Doppler / Gold] - The blade's metal and handle accents
- `inspect_key` Inspect key: Key default 73 - Play a trick; press again to chain the next
- `slash_key` Slash key: Key default -1 - Slash the air; it hits nothing
- `auto_inspect` Idle tricks: Boolean default True - Plays with your blade while you stand still
- `interval` Trick interval: Number default 12.0 5.0..30.0 - Seconds of idle time between tricks
- `walk_tricks` Walking tricks: Boolean default True - Keeps playing with your blade as you move
- `walk_pause` Pause between: Number default 0.0 0.0..6.0 - None: each trick flows straight into the next
- `flow_tempo` Tempo on the move: Number default 1.25 1.0..1.6 - How fast the tricks play as you move
- `air_slashes` Air slashes: Boolean default True - Tricks may slash the air in front of you
- `speed` Animation speed: Number default 1.0 0.65..1.5 - Tempo of every trick and the draw
- `draw` Draw animation: Boolean default True - A flourish when you equip a sword
- `two_handed` Two-handed tricks: Boolean default True - Your other hand joins in while it holds nothing
- `sounds` Trick sounds: Boolean default True - Clean steel sounds for every flip, spin and catch
- `attack_sounds` Attack sounds: Mode default Knife [Knife / Knife and sword / Sword] - A swish on every attack, a cut when it hits
- `volume` Volume: Number default 0.7 0.0..1.0 - Of the knife's sounds
- `offhand` Off-hand knives: Boolean default True - Replace swords in your other hand too
- `size` Knife size: Number default 1.0 0.7..1.3 - Scale around the grip
- `weapon` Weapon: Mode default Knife [Knife / Katana / Greatsword / Scythe] - A knife, or a blade held in both hands
- `arms_fov` Arms field of view: Number default 100.0 70.0..120.0 - How wide your arms and the blade are drawn: wider shows more of your arms
- `slash_arcs` Slash arcs: Boolean default True - The blade leaves an arc of light as it swings
- `hit_effects` Hit effects: Boolean default True - A cut of light and sparks on every hit
- `walk_effects` Walking effects: Boolean default True - Petals, embers or wisps drift off the blade as you move
- `karambit_camo` Karambit camo: Mode default Original [Original / Fade / Doppler / Crimson Web / Tiger Tooth] - Remembered for this knife
- `butterfly_camo` Butterfly camo: Mode default Original [Original / Marble Fade / Sapphire / Slaughter / Black Laminate] - Remembered for this knife
- `bayonet_camo` M9 Bayonet camo: Mode default Original [Original / Case Hardened / Lore / Damascus Steel / Autotronic] - Remembered for this knife
- `tactical_camo` Tactical camo: Mode default Original [Original / Gold / Damascus / Orion / Interstellar] - Remembered for this knife
- `crystal_camo` Crystal Fang camo: Mode default Original [Original / Chroma / Ice Shard / Gemstone / Candy] - Remembered for this knife

## Low Fire (`low_fire`, PLAYER)
Low, soft flames in the Purxiy colours

- `style` Style: Mode default Flames [Flames / Pixel / Glow] - Soft flames, pixel fire, or only a glow
- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the fire
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `height` Height: Number default 0.3 0.0..1.0 - How far up the screen the flames reach
- `opacity` Opacity: Number default 0.85 0.2..1.0 - How much of the world shows through them
- `speed` Animation speed: Number default 1.0 0.0..3.0 - How fast the flames flicker and rise (0 = still)
- `embers` Embers: Number default 1.0 0.0..2.0 - Sparks that drift up from the flames
- `haze` Heat haze: Boolean default True - The air above the flames shimmers
- `flare` Flare: Boolean default True - The flames flare up each time the fire hurts you
- `sway` Sway: Boolean default True - They lean and trail as you move and turn
- `resistance` Fire Resistance: Mode default Calm [Normal / Calm / Hide] - While the fire cannot hurt you
- `ground` Ground fire: Boolean default True - Fire on the ground burns low, in the palette
- `ground_height` Ground height: Number default 0.3 0.15..1.0 - How high fire on the ground burns
- `world` Burning mobs: Boolean default True - Players and mobs burn in the palette too

## Main Menu (`main_menu`, CLIENT)
The world behind the menus, the loading screen and the look of Minecraft's menus

- `menus` Purxiy main menu: Boolean default True - A living world behind Minecraft's menus, and the menus in the Purxiy style
- `loading` Loading screen: Boolean default True - The Purxiy logo while the game loads, flying to its place in the menu
- `time` Time of day: Mode default Day [Morning / Day / Sunset / Night / Real time] - Real time follows the clock of your computer
- `trees` Trees: Mode default Mixed [Oak / Birch / Cherry / Spruce / Mixed] - What grows around you
- `camera` Camera: Mode default Orbit [Orbit / Drift / Still] - Orbit turns slowly round, Drift sways, Still stands
- `grow_on_start` Grow on start: Boolean default True - The trees grow up from saplings as the game starts
- `splash` Splash text: Boolean default True - Minecraft's yellow line beside the logo
- `forest` Forest: Text default 

## Memory (`memory`, HUD)
How much memory the game is using

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `amount` Show amount: Boolean default False - Used and maximum memory next to the percentage

## Mist (`mist`, RENDER)
Soft autumn mist that hangs low in the world

- `palette` Colour: Mode default Autumn [Autumn / Lilac / Pearl / Rose] - The colour of the mist
- `density` Density: Number default 0.5 0.1..1.0 - How thick the mist hangs; less lets you see further
- `height` Height: Number default 10.0 3.0..32.0 - How high the mist rises above the ground
- `clear` Clear around you: Number default 6.0 0.0..24.0 - How far around you the air stays clear
- `drift` Drift: Number default 1.0 0.0..3.0 - How fast the banks of mist roll by; at 0 they hang still
- `rays` Sun rays: Number default 0.6 0.0..1.0 - Warm light through the mist, in beams between the trees
- `lanterns` Lantern glow: Number default 0.7 0.0..1.0 - Torches, lanterns and fire glow softly in the mist at night
- `creatures` See players: Boolean default True - Players and mobs show clearly through the mist

## Motion (`motion`, CLIENT)
Animation speeds for every kind of animation

- `global` Overall speed: Number default 1.0 0.5..2.0 - Scales every animation in Purxiy
- `menu` Menu transitions: Number default 1.0 0.5..2.0 - Opening, closing and moving between views
- `orbit` Orbit: Number default 1.0 0.5..2.0 - How quickly the ring turns to a category
- `hover` Hover: Number default 1.0 0.5..2.0 - Highlights that follow your cursor
- `toggles` Switches: Number default 1.0 0.5..2.0 - Switches, sliders and button presses
- `panels` Panels: Number default 1.0 0.5..2.0 - Settings panels opening and closing
- `cascade` Cascade: Number default 1.0 0.5..2.0 - Modules flowing in one after another
- `reduce` Reduce motion: Boolean default False - Calmer: no parallax, floating or cascades

## Name Tags (`name_tags`, RENDER)
Clean tags for players, pets and the items around you

- `style` Style: Mode default Glass [Glass / Solid / Minimal] - Frosted glass, a solid pill or only the name
- `faces` Faces: Boolean default True - Their face next to the name (mobs get an icon)
- `health` Health: Mode default Bar [Off / Bar / Hearts] - A line along the tag, or hearts as a number
- `below_name` Below name: Boolean default True - The line a server shows under names, like a score
- `own` Your own tag: Boolean default True - See your own tag in third person
- `mobs` Named mobs: Boolean default True - Pets and every other mob with a name
- `items` Dropped items: Mode default Nearby [Off / Looking at / Nearby] - Name and amount above items on the ground
- `item_range` Item range: Number default 12.0 4.0..128.0 - How far away loaded item tags show, including through walls
- `piles` Group piles: Boolean default True - Items lying together share one card
- `size` Size: Number default 1.0 0.5..2.0
- `shrink` Shrink with distance: Number default 0.6 0.0..1.0 - At 0% every tag keeps the same size
- `opacity` Background opacity: Number default 0.8 0.0..1.0
- `font` Font: Mode default Purxiy [Purxiy / Minecraft] - Purxiy's Inter or Minecraft's own letters
- `team_colours` Team colours: Boolean default True - Names in the colours the server gives them
- `walls` Through walls: Mode default Dim [Hide / Dim / Show] - Players, mobs and dropped items behind blocks; sneaking tags stay hidden
- `entity_range` Player & mob range: Number default 128.0 4.0..512.0 - Maximum distance for player and mob tags

## Nick Hider (`nick_hider`, PLAYER)
Hide your name in screenshots and streams

- `hide_own` Hide your name: Boolean default True - Chat, tab, name tags, books, signs and more
- `nickname` Nickname: Text default Purxiy - Shown instead of your name · &d colours work
- `also_hide` Also hide: Text default  - Server nicks, alts… separated by commas
- `hide_own_skin` Hide your skin: Boolean default False - Wear a default skin, on your head items too
- `custom_names` Custom names: NameList default [] - Give chosen players a name, like Henk → Pieter
- `hide_others` Hide other names: Boolean default False - Everyone else gets a numbered stand-in
- `others_name` Their name: Text default Player - Numbered per player, like Player 3
- `hide_others_skins` Hide their skins: Boolean default False - Default skins for everyone else

## Notifications (`notifications`, HUD)
A quiet message when a keybind toggles a module

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 1.0 0.0..1.0
- `y` Y: Number default 1.0 0.0..1.0
- `placed` Placed: Boolean default False
- `duration` Duration: Number default 2.5 1.5..6.0 - How long a notification stays

## Ping (`ping`, HUD)
Your connection to the server

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False

## Place Animation (`place_animation`, RENDER)
Blocks grow into place as you build

- `style` Style: Mode default Pop [Pop / Grow / Twist / Drop] - How a block comes into place
- `duration` Duration: Number default 0.34 0.12..1.0 - How long a block takes to settle
- `bounce` Bounce: Number default 0.5 0.0..1.0 - How far it springs past its full size before it settles
- `glow` Glow: Number default 0.6 0.0..1.0 - Soft light on the block as it appears
- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the glow
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `breaking` Broken blocks: Boolean default True - Blocks you break shrink away
- `others` Other players: Boolean default False - Blocks other players place grow in too

## Potion Effects (`potion_effects`, HUD)
Active effects and their timers

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 1.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `hide_vanilla` Hide vanilla icons: Boolean default True - Remove the effect icons in the top right corner
- `names` Names: Boolean default True - Effect names next to the icons

## Radiance (`radiance`, RENDER)
Glowing outlines on hands, items and mobs

- `style` Style: Mode default Glow [Glow / Aura / Outline] - A glowing line, rising light, or only the line
- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the light
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `hand` Your hand: Boolean default True - Your arm in first person
- `items` Held items: Boolean default True - What you hold, in first person
- `players` Players: Boolean default True - Other players, and you in third person
- `mobs` Mobs: Mode default All [Off / Monsters / All] - Only monsters, or every creature
- `drops` Dropped items: Boolean default True - Items lying on the ground
- `width` Line width: Number default 2.0 0.5..4.0 - How thick the line around them is
- `glow` Glow: Number default 0.6 0.05..1.0 - How far the soft light reaches
- `fill` Fill: Number default 0.2 0.0..1.0 - A soft tint over them, lit at the edges
- `speed` Animation speed: Number default 1.0 0.0..3.0 - How fast the colours flow (0 = still)
- `range` Range: Number default 48.0 8.0..128.0 - How far away things still glow
- `sparkles` Sparkles: Boolean default True - Tiny stars twinkle along the edge
- `hits` Hit flash: Boolean default True - A flash and a ripple when something is hit
- `health` Health colours: Boolean default False - The hurt glow warmer, towards rose
- `rarity` Rarity colours: Boolean default False - Rare items glow in their rarity's colour

## Region Map (`region_map`, HUD)
Donut SMP's regions, and which one you are in

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.36 0.0..1.0
- `placed` Placed: Boolean default False
- `numbers` Region numbers: Boolean default True - The number of every region on the map
- `legend` Legend: Boolean default True - Which colour is which server location
- `next` Next region: Boolean default True - The region you are heading for and how far away it is

## Ripple (`ripple`, RENDER)
A wave of light where every hit lands

- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the light
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `glow` Glow: Number default 0.8 0.2..1.0 - How brightly a hit lights them up
- `duration` Duration: Number default 0.8 0.4..2.0 - How long the light lingers
- `who` Whose hits: Mode default Everyone [Everyone / Your hits] - Every hit you see, or only the ones you land
- `crits` Critical stars: Boolean default True - A burst of stars where a critical hit lands
- `stars` Stars: Number default 1.0 0.25..2.0 - How many stars a critical hit throws
- `kills` Light pillar: Boolean default True - A pillar of light where your kills fall
- `ring` Shockwave: Boolean default True - A ring of light that rolls over the ground

## Roles (`roles`, PLAYER)
DonutSMP staff roles before your name. Only you see them

- `staff_star` Staff star: Boolean default True - A green ★, as DonutSMP's staff wear it
- `media` Media: Boolean default False - The pink camera with its plus
- `donut_level` Donut level: Mode default None [None / + / ++ / +++] - One, two or three light blue plusses
- `custom_prefix` Custom prefix: Text default  - Any text before your name · &c colours work
- `apply_to` Worn by: Mode default Only you [Only you / Everyone] - Only your name, or every player's
- `tab_list` Tab list: Boolean default True - The roles in the player list too

## Server (`server_info`, HUD)
The address of the server you are on

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False

## Session Time (`session_time`, HUD)
How long you have been playing

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False

## Shockwave (`shockwave`, RENDER)
Land, and a wave runs through the ground

- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the light
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `strength` Strength: Number default 1.0 0.4..2.0 - How deep the blocks sink
- `size` Size: Number default 1.0 0.5..2.0 - How far the wave runs
- `glow` Glow: Number default 0.7 0.0..1.0 - The light that wells up between the blocks
- `steps` Footsteps: Boolean default True - Every step softly presses the ground
- `blasts` Explosions: Boolean default True - Blasts send a wave through the ground
- `others` Other players: Boolean default False - Their landings and steps move the ground too

## 3D Skins (`skins_3d`, COSMETICS)
Real 3D skins, and a wardrobe of new ones

- `skin` Skin: Mode default Your skin [Your skin / E-Girl / Stargazer / Shinobi / Peter Griffin / Spider-Man / Mario / Pikachu / SpongeBob / E-Girl 2] - What you wear
- `size` Own size: Boolean default True - Each skin its own height and build, only for you
- `hide_armor` Hide armor: Boolean default False - Your armor is not drawn over your skin in third person
- `depth` Depth: Number default 1.0 0.5..1.5 - How far the outer layer stands out
- `others` Other players: Boolean default True - Everyone's skin in 3D, not only yours
- `first_person` First person: Boolean default True - Your sleeves in 3D on your hands
- `uwu_voice` UwU voice: Boolean default True - Occasionally says uwu, more often at three hunger icons or less
- `uwu_volume` Voice volume: Number default 0.55 0.0..1.0 - Also follows the game's Players volume
- `mask_key` Mask key: Key default -1 - Take off and throw the mask; press again nearby to pick it up and wear it

## Sky (`sky`, RENDER)
Eight living skies, from cosmic light to snow clouds and fire

- `style` Sky style: Mode default Galaxy [Galaxy / Aether / Nocturne / Amethyst / Opaline / Rain / Snow / Fire] - Eight living skies, including rain clouds, winter white and fire
- `palette` Palette: Mode default Purxiy [Purxiy / Nebula / Eclipse / Dream] - The colours of your dimension
- `galaxy_colors` Colours: Mode default Original [Original / Accent / Custom] - Its own colours, your accent, or one you pick
- `galaxy_color` Colour: Color default #FFB48CFF - The colour this sky takes on
- `aether_colors` Colours: Mode default Original [Original / Accent / Custom] - Its own colours, your accent, or one you pick
- `aether_color` Colour: Color default #FFC3C9D2 - The colour this sky takes on
- `nocturne_colors` Colours: Mode default Original [Original / Accent / Custom] - Its own colours, your accent, or one you pick
- `nocturne_color` Colour: Color default #FF805C9C - The colour this sky takes on
- `amethyst_colors` Colours: Mode default Original [Original / Accent / Custom] - Its own colours, your accent, or one you pick
- `amethyst_color` Colour: Color default #FFC49EFF - The colour this sky takes on
- `opaline_colors` Colours: Mode default Original [Original / Accent / Custom] - Its own colours, your accent, or one you pick
- `opaline_color` Colour: Color default #FF754A94 - The colour this sky takes on
- `rain_colors` Colours: Mode default Original [Original / Accent / Custom] - Its own colours, your accent, or one you pick
- `rain_color` Colour: Color default #FF3D4A59 - The colour this sky takes on
- `snow_colors` Colours: Mode default Original [Original / Accent / Custom] - Its own colours, your accent, or one you pick
- `snow_color` Colour: Color default #FFC4D2E2 - The colour this sky takes on
- `fire_colors` Colours: Mode default Original [Original / Accent / Custom] - Its own colours, your accent, or one you pick
- `fire_color` Colour: Color default #FFFF4A07 - The colour this sky takes on
- `intensity` Pattern intensity: Number default 0.8 0.0..1.0 - The depth and glow of the flowing patterns
- `speed` Animation speed: Number default 1.0 0.0..3.0 - How fast everything drifts and flows
- `liveliness` Liveliness: Number default 0.6 0.0..1.0 - How much it swirls, twinkles and breathes
- `breathing` Breathing speed: Number default 1.0 0.0..3.0 - Tempo of the glow that pulses through it
- `nebula` Nebula: Number default 0.8 0.0..1.0 - Glowing clouds of cosmic dust
- `stars` Stars: Number default 0.75 0.0..1.0 - How many stars fill the sky
- `shooting_stars` Shooting stars: Boolean default True - Streaks of light, and now and then a shower
- `comets` Comets: Boolean default True - Now and then a comet crosses the sky
- `aurora` Aurora: Number default 0.55 0.0..1.0 - Curtains of light drifting overhead
- `planet` Planet: Mode default World [Off / Giant / World] - A detailed 3D world with cloud layers, rings and orbiting moons
- `singularity` Singularity: Boolean default True - A black hole that bends the light around it
- `sun_moon` Sun & moon: Boolean default True - Keep track of the time of day
- `daylight` Daylight: Number default 0.6 0.0..1.0 - How much the day brightens the sky
- `brightness` Brightness: Number default 1.0 0.5..1.5
- `clouds` Tint clouds: Boolean default True - Clouds take on the colours of the sky
- `steady` Steady sky: Boolean default True - The sky stays still while you walk
- `quality` Quality: Mode default High [Low / Medium / High] - Detail versus frame rate

## Sound (`sound`, CLIENT)
Volume and which sounds you hear

- `enabled` Interface sounds: Boolean default True - Soft sounds for every interaction
- `volume` Volume: Number default 0.6 0.0..1.0
- `hover` Hover ticks: Boolean default True - A faint tick when your cursor finds something
- `ticks` Slider & typing ticks: Boolean default True - Tiny detents while dragging and typing
- `transitions` Transitions: Boolean default True - Opening, closing and panel sounds

## Speed (`speed`, HUD)
How fast you are moving

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `unit` Unit: Mode default Blocks/s [Blocks/s / km/h]

## Spotify Player (`spotify_player`, HUD)
Your music on screen, with lyrics to sing along

- `layout` Layout: Mode default Split [Split / Combined / Lyrics only / Player only] - Where the lyrics and the player go
- `lyrics_place` Lyrics: Mode default On screen [On screen / In the world] - On your screen, or out in the world: on the ground or across the sky
- `world_where` Where: Mode default On the ground [On the ground / In the sky / Ground and sky] - On the ground in front of you, across the sky, or both: on the ground, and in the sky while you fly or look up
- `world_words` Words: Mode default As sung [As sung / Whole line] - Every word appears as it is sung (on the ground one left, one right of your way), or the whole line at once
- `world_distance` Distance: Number default 6.0 3.0..16.0 - How far ahead of you the words are written on the ground
- `world_size` Size: Number default 1.0 0.6..1.6 - How large the words are written
- `world_open_sky` Only in open sky: Boolean default True - Blocks hide the words: they only show where you can see the sky
- `lyrics_menus` In the menus: Boolean default True - On the ground of the main menu's world, and beside Minecraft's other menus
- `player_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `player_background` Background: Boolean default True - A frosted panel behind the element
- `player_opacity` Background opacity: Number default 0.6 0.0..1.0
- `player_x` X: Number default 1.0 0.0..1.0
- `player_y` Y: Number default 1.0 0.0..1.0
- `player_placed` Placed: Boolean default False
- `controls` Controls: Mode default Always [Always / In chat / Hidden] - The buttons below the song
- `art` Album art: Boolean default True
- `visualizer` Visualizer: Boolean default True - Little bars that move with the music
- `colors` Colours: Mode default Purxiy [Purxiy / Album] - Purxiy's purple, or the colour of the album cover
- `lyrics_size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `lyrics_background` Background: Boolean default True - A frosted panel behind the element
- `lyrics_opacity` Background opacity: Number default 0.6 0.0..1.0
- `lyrics_x` X: Number default 0.5 0.0..1.0
- `lyrics_y` Y: Number default 0.0 0.0..1.0
- `lyrics_placed` Placed: Boolean default False
- `lines` Lines: Number default 2.0 1.0..5.0 - How many lines you see at once
- `width` Width: Mode default Normal [Narrow / Normal / Wide]
- `align` Alignment: Mode default Auto [Auto / Left / Centre / Right] - Auto follows where the lyrics are on screen
- `sweep` Word sweep: Boolean default True - The words light up as they are sung
- `glow` Glow: Boolean default True - A soft glow around the line being sung
- `lyrics_timing` Timing: Number default 0.0 -2.0..2.0 - For a song whose lyrics run early or late
- `source` Follow: Mode default Spotify [Spotify / Any player] - Spotify, or whichever app plays music
- `paused` When paused: Mode default Dim [Show / Dim / Hide]
- `volume_step` Volume step: Number default 0.05 0.01..0.2 - For the volume keys and scrolling
- `key_play` Play / pause: Key default -1
- `key_next` Next track: Key default -1
- `key_previous` Previous track: Key default -1
- `key_volume_up` Volume up: Key default -1
- `key_volume_down` Volume down: Key default -1

## Target HUD (`target_hud`, HUD)
Health and fight stats of whoever you aim at

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.62 0.0..1.0
- `y` Y: Number default 0.56 0.0..1.0
- `placed` Placed: Boolean default False
- `placement` Placement: Mode default Screen [Screen / Beside target] - On your screen, or in the world right beside whoever you fight
- `range` Range: Number default 12.0 3.0..64.0 - How far away someone can be when you aim at them
- `hold` Hold time: Number default 3.0 0.0..10.0 - How long it stays after you look away
- `targets` Targets: Mode default Players [Players / Players & mobs]
- `fights` Lock on in fights: Boolean default True - Also show whoever you hit or who hits you
- `auto_detect` Auto-detect PvP: Boolean default True - Recognise the mode from gear, items and the fight
- `pvp` PvP mode: Mode default Crystal [General / Crystal / Sword / Axe / Mace / UHC / Pot / NethOP / SMP / Spear] - Which stats to show
- `detect_crystal` Crystal PvP: Boolean default True - Totem pops, totem in hand and armour wear
- `detect_sword` Sword: Boolean default True - Combo, hits traded and who is ahead
- `detect_axe` Axe & shield: Boolean default True - Shield up, down or disabled, hits and armour
- `detect_mace` Mace: Boolean default True - Height in the air, totem pops and wind charges
- `detect_uhc` UHC: Boolean default True - Golden apples eaten, arrow hits and armour
- `detect_pot` Pot: Boolean default True - Potions thrown, active effects and armour
- `detect_nethop` NethOP: Boolean default True - Potions, experience bottles and totem pops
- `detect_smp` SMP: Boolean default True - Totem pops, ender pearls and golden apples
- `detect_spear` Spear: Boolean default True - Speed, hits traded and who is ahead
- `health` Health: Mode default Hearts [Hearts / Points / Percent]
- `damage_numbers` Damage numbers: Boolean default True - Hits and heals float up from the health bar
- `sounds` Sounds: Mode default All [All / Events / Off] - Lock-on, totem pops and eliminations

## Time (`time_changer`, RENDER)
Set your own time and daylight, only for your view

- `preset` Time of day: Mode default Custom [Morning / Day / Sunset / Night / Custom] - Choose the daylight you want to see
- `time` Custom time: Number default 6000.0 0.0..24000.0 - 06:00 sunrise, 12:00 noon, 00:00 midnight

## Trail (`trail`, COSMETICS)
Leave a trail of light behind you

- `style` Style: Mode default Line [Line / Path / Helix / Stardust] - How your trail looks
- `palette` Palette: Mode default Accent [Accent / Purxiy / Dream / Custom] - The colours of the trail
- `color` Colour: Color default accent - The rest of the palette is derived from it
- `duration` Length: Number default 1.2 0.4..3.0 - How long the light stays behind you
- `size` Size: Number default 1.0 0.5..2.0 - How wide the trail is
- `glow` Glow: Number default 0.6 0.0..1.0 - A soft halo of light around the trail
- `others` Other players: Boolean default False - Trails behind other players too

## Watermark (`watermark`, HUD)
The Purxiy mark in your corner

- `size` Size: Number default 1.0 0.5..2.0 - Or scroll on it while the HUD editor is open
- `background` Background: Boolean default True - A frosted panel behind the element
- `opacity` Background opacity: Number default 0.6 0.0..1.0
- `x` X: Number default 0.0 0.0..1.0
- `y` Y: Number default 0.0 0.0..1.0
- `placed` Placed: Boolean default False
- `style` Style: Mode default Mark & name [Mark & name / Mark only]
- `fps` Show FPS: Boolean default False - Frames per second after the name

## Weather (`weather`, RENDER)
Sculpt the weather: glassy rain, soft snowfall and ember showers

- `mode` Weather: Mode default Rain [Rain / Snow / Fire] - Choose the atmosphere around you
- `intensity` Intensity: Number default 1.0 0.1..2.0 - From a gentle shower to heavy precipitation
- `wind` Wind: Number default 0.45 0.0..2.0 - The strength of the moving gusts
- `size` Drop size: Number default 1.0 0.5..2.0 - The size of droplets, crystals and embers
- `range` Range: Number default 24.0 8.0..40.0 - How far around you the drops fall, in blocks
- `ground` Ground effects: Boolean default True - Glassy wet ground, snow or embers, far and wide
- `coverage` Coverage: Number default 0.85 0.0..1.0 - How much of the open ground is covered
- `reflections` Reflections: Boolean default True - Mirror the scenery, not only the sky
- `storm` Thunderstorm: Boolean default False - Branching lightning with distant rolling thunder
- `lightning` Lightning: Number default 0.65 0.0..1.0 - Brightness of the lightning and its glow; zero turns flashes off
- `sound` Weather sounds: Boolean default True - Original soundscapes that follow the weather
- `volume` Volume: Number default 0.65 0.0..1.0 - Also follows Minecraft's Weather and Master volume

## Wings (`wings`, COSMETICS)
A pair of softly animated wings

- `design` Wings: Mode default Seraph [Seraph / Nocturne / Lumen / Phoenix / Fairy] - Which wings you wear
- `tint` Colour: Color default accent - The colour in your wings
- `size` Size: Number default 0.9 0.5..1.3 - How large they are
- `glow` Glow: Number default 0.7 0.0..1.0 - How brightly they shine in the dark
- `rest` At rest: Mode default Folded [Folded / Unfolded] - How they rest while you stand
- `jump` Beat as you jump: Boolean default True - Two strong beats lift you off the ground
- `elytra` In place of your elytra: Boolean default True - Gliding, your wings carry you instead

## Zoom (`zoom`, RENDER)
A smooth lens, one key away

- `activation` Activation: Mode default Hold [Hold / Toggle] - Hold the key or click to toggle
- `factor` Zoom amount: Number default 4.0 1.25..32.0 - Starting magnification
- `smooth` Smooth zoom: Boolean default True - Ease gently into and out of focus
- `duration` Zoom in: Number default 240.0 80.0..800.0 - Time to settle into focus
- `release_duration` Zoom out: Number default 180.0 80.0..800.0 - Time to return to your normal view
- `scroll` Scroll to adjust: Boolean default True - Fine-tune zoom without changing slots
- `scroll_speed` Scroll step: Number default 0.15 0.02..0.5 - Magnification change per wheel notch
- `invert_scroll` Reverse scroll: Boolean default False - Scroll down to move closer
- `remember` Remember zoom: Boolean default False - Keep your scrolled amount for next time
- `auto_sensitivity` Adaptive sensitivity: Boolean default True - Keep aiming precise at every distance
- `sensitivity` Aim speed: Number default 1.0 0.1..2.0 - Fine-tune mouse speed while zoomed
- `hide_hand` Hide hand: Boolean default True - Keep held items out of the view
- `indicator` Zoom indicator: Boolean default True - A small magnification readout

## Profile (`profile`, CLIENT)
What your profile keeps, and whether purxiy.com shows it

- `name` Display name: Text default  - Shown instead of your Minecraft name
- `portrait` Portrait: Mode default Photo [Photo / Skin] - Your own photo, or your skin as a 3D model
- `track` Keep statistics: Boolean default True - Play time, distance, blocks, fights and your records
- `idle` Count idle time: Boolean default False - Keep counting while you stand still for a while
- `journey` Record your route: Boolean default True - Draws the map of where you went in each session
- `middle_click` Middle-click friends: Boolean default True - Middle-click a player to add them as a friend, or to remove them
- `sync` Show on purxiy.com: Boolean default False - Your statistics in your dashboard on the website. Never your route or what you type

## Purxiy Garage (`mounts`, RENDER)
Real cars and bikes that follow your own movement

- `board_key` Board / leave key: Key default 86 - The key shown beside your ride
- `style` Ride: Mode default Porsche 911 [Porsche 911 / BMW M2 / Porsche GT4 / Lamborghini Urus / Lamborghini Veneno / Dracorus / BMW S1000RR / Quad Bike / Kids Bike] - Nine rides, each with its own sound and way of moving
- `custom_finish` Custom paint: Boolean default False - Pick your own colour beyond the collection's finishes
- `paint` Paint: Color default #FFB6BCC0 - The bodywork's colour
- `cabin_light` Cabin light: Color default #FFAC93E4 - The soft light inside the cabin after dark
- `lights` Lights: Boolean default True - Headlights come on in the dark; brakes, indicators and reverse lights work on their own
- `particles` Exhaust & effects: Boolean default True - Exhaust smoke and flames on a gear change
- `quality` Render quality: Mode default Quality [Quality / Performance] - Quality draws the ride at double resolution for perfectly smooth edges
- `smooth_throttle` Progressive throttle: Boolean default True - Rides pull away gently and build up to your normal walking and sprinting speed
- `acceleration` Take-off time: Number default 2.0 0.6..4.0 - Time holding W to reach full throttle after ignition
- `gearbox` Gearbox: Mode default Auto [Auto / Manual] - Automatic shifts or manual gears using the Controls-menu bindings
- `auto_camera` First person on entry: Boolean default True - Get in and look out from the driver's seat; your previous view returns when you get out
- `camera_distance` Camera distance: Number default 6.0 4.0..8.0 - A wider view of your ride; respects walls
- `screen_focus` Screen focus: Boolean default True - Look at the in-car screen and a wide field of view narrows so it reads easily
- `hud` Boarding prompt: Boolean default True - Your board key beside the parked ride, in the style of the HUD
- `sounds` Ride sounds: Boolean default True - Ignition, engine, gear changes and horn, different for every ride
- `volume` Ride volume: Number default 0.4 0.0..1.0 - Follows Minecraft's master volume
