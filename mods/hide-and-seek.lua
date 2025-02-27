-- name: Hide and Seek
-- incompatible: gamemode
-- description: A simple hide-and-seek gamemode for\nCo-op.\n\nThe game is split into two teams:\n\nHiders and Seekers. The goal is for all\n\Hiders to be converted into a Seeker within a certain timeframe.\n\nAll Seekers appear as a metal character, and are given boosted speed\n\and jump height.\n\nHiders are given no enhancements, and\n\become a Seeker upon dying.\n\nEnjoy! :D\n\nConcept by: Super Keeberghrh

-- globally sync enabled state
gGlobalSyncTable.hideAndSeek = true

-- keep track of round info
ROUND_STATE_WAIT        = 0
ROUND_STATE_ACTIVE      = 1
ROUND_STATE_SEEKERS_WIN = 2
ROUND_STATE_HIDERS_WIN  = 3
ROUND_STATE_UNKNOWN_END = 4
gGlobalSyncTable.campingTimer = true -- enable/disable camping timer
gGlobalSyncTable.roundState   = ROUND_STATE_WAIT -- current round state
gGlobalSyncTable.displayTimer = 0 -- the displayed timer
sRoundTimer        = 0            -- the server's round timer
sRoundStartTimeout = 15 * 30      -- fifteen seconds
sRoundEndTimeout   = 3 * 60 * 30  -- three minutes

-- server keeps track of last player turned seeker
sLastSeekerIndex = 0

-- keep track of distance moved recently (camping detection)
sLastPos = {}
sLastPos.x = 0
sLastPos.y = 0
sLastPos.z = 0
sDistanceMoved = 0
sDistanceTimer = 0
sDistanceTimeout = 10 * 30 -- ten seconds

-- flashing 'keep moving' index
sFlashingIndex = 0

function server_update(m)
    -- increment timer
    sRoundTimer = sRoundTimer + 1
    gGlobalSyncTable.displayTimer = math.floor(sRoundTimer / 30)

    -- figure out state of the game
    local hasSeeker = false
    local hasHider = false
    local activePlayers = {}
    local connectedCount = 0
    for i=0,(MAX_PLAYERS-1) do
        if gNetworkPlayers[i].connected then
            connectedCount = connectedCount + 1
            table.insert(activePlayers, gPlayerSyncTable[i])
            if gPlayerSyncTable[i].seeking then
                hasSeeker = true
            else
                hasHider = true
            end
        end
    end

    -- only change state if there are 2+ players
    if connectedCount < 2 then
        gGlobalSyncTable.roundState = ROUND_STATE_WAIT
        return
    elseif gGlobalSyncTable.roundState == ROUND_STATE_WAIT then
        gGlobalSyncTable.roundState = ROUND_STATE_UNKNOWN_END
        sRoundTimer = 0
        gGlobalSyncTable.displayTimer = 0
    end

    -- check to see if the round should end
    if gGlobalSyncTable.roundState == ROUND_STATE_ACTIVE then
        if not hasHider or not hasSeeker or sRoundTimer > sRoundEndTimeout then
            if not hasHider then
                gGlobalSyncTable.roundState = ROUND_STATE_SEEKERS_WIN
            elseif sRoundTimer > sRoundEndTimeout then
                gGlobalSyncTable.roundState = ROUND_STATE_HIDERS_WIN
            else
                gGlobalSyncTable.roundState = ROUND_STATE_UNKNOWN_END
            end
            sRoundTimer = 0
            gGlobalSyncTable.displayTimer = 0
        else
            return
        end
    end

    -- start round
    if sRoundTimer >= sRoundStartTimeout then
        -- reset seekers
        for i=0,(MAX_PLAYERS-1) do
            gPlayerSyncTable[i].seeking = false
        end
        hasSeeker = false

        -- set seeker to last one turned into seeker
        local np = gNetworkPlayers[sLastSeekerIndex]
        if np.connected then
            local s = gPlayerSyncTable[sLastSeekerIndex]
            s.seeking = true
            hasSeeker = true
        end

        -- pick random seeker if last turned to seeker is invalid
        if not hasSeeker then
            local s = activePlayers[math.random(#activePlayers)]
            s.seeking = true
        end

        -- set round state
        gGlobalSyncTable.roundState = ROUND_STATE_ACTIVE
        sRoundTimer = 0
        gGlobalSyncTable.displayTimer = 0
    end
end

function camping_detection(m)
    -- this code only runs for the local player
    local s = gPlayerSyncTable[m.playerIndex]

    -- track how far the local player has moved recently
    sDistanceMoved = sDistanceMoved - 0.25 + vec3f_dist(sLastPos, m.pos) * 0.02
    vec3f_copy(sLastPos, m.pos)

    -- clamp between 0 to 100
    if sDistanceMoved < 0   then sDistanceMoved = 0   end
    if sDistanceMoved > 100 then sDistanceMoved = 100 end

    -- if player hasn't moved enough, start a timer
    if sDistanceMoved < 10 and not s.seeking then
        sDistanceTimer = sDistanceTimer + 1
    end

    -- if the player has moved enough, reset the timer
    if sDistanceMoved > 25 then
        sDistanceTimer = 0
    end

    -- inform the player that they need to move, or make them a seeker
    if sDistanceTimer > sDistanceTimeout then
        s.seeking = true
    end

    -- make sound
    if sDistanceTimer > 0 and sDistanceTimer % 30 == 1 then
        play_sound(SOUND_MENU_CAMERA_BUZZ, m.marioObj.header.gfx.cameraToObject)
    end
end

function update()
    -- check gamemode enabled state
    if not gGlobalSyncTable.hideAndSeek then
        return
    end

    -- only allow the server to figure out the seeker
    if network_is_server() then
        server_update(gMarioStates[0])
    end

    -- check if local player is camping
    if gGlobalSyncTable.roundState == ROUND_STATE_ACTIVE then
        if gGlobalSyncTable.campingTimer then
            camping_detection(gMarioStates[0])
        else
            sDistanceTimer = 0
        end
    else
        sDistanceTimer = 0
    end
end

function mario_update(m)
    -- check gamemode enabled state
    if not gGlobalSyncTable.hideAndSeek then
        return
    end

    -- this code runs for all players
    local s = gPlayerSyncTable[m.playerIndex]

    -- if the local player died, make them a seeker
    if m.playerIndex == 0 and m.health <= 0x110 then
        s.seeking = true
    end

    -- display all seekers as metal
    if s.seeking then
        m.marioBodyState.modelState = MODEL_STATE_METAL
        m.health = 0x880
    end
end

function mario_before_phys_step(m)
    -- prevent physics from being altered when bubbled
    if m.action == ACT_BUBBLED then
        return
    end

    -- check gamemode enabled state
    if not gGlobalSyncTable.hideAndSeek then
        return
    end

    local s = gPlayerSyncTable[m.playerIndex]

    -- only make seekers faster
    if not s.seeking then
        return
    end

    local hScale = 1.0
    local vScale = 1.0

    -- make swimming seekers 5% faster
    if (m.action & ACT_FLAG_SWIMMING) ~= 0 then
        hScale = hScale * 1.05
        if m.action ~= ACT_WATER_PLUNGE then
            vScale = vScale * 1.05
        end
    end

    -- faster ground movement
    if (m.action & ACT_FLAG_MOVING) ~= 0 then
        hScale = hScale * 1.19
    end

    m.vel.x = m.vel.x * hScale
    m.vel.y = m.vel.y * vScale
    m.vel.z = m.vel.z * hScale
end

function on_pvp_attack(attacker, victim)
    -- check gamemode enabled state
    if not gGlobalSyncTable.hideAndSeek then
        return
    end

    -- this code runs when a player attacks another player
    local sAttacker = gPlayerSyncTable[attacker.playerIndex]
    local sVictim = gPlayerSyncTable[victim.playerIndex]

    -- only consider local player
    if victim.playerIndex ~= 0 then
        return
    end

    -- make victim a seeker
    if sAttacker.seeking and not sVictim.seeking then
        sVictim.seeking = true
    end
end

function on_player_connected(m)
    -- start out as a non-seeker
    local s = gPlayerSyncTable[m.playerIndex]
    s.seeking = true
    network_player_set_description(gNetworkPlayers[m.playerIndex], "seeker", 255, 64, 64, 255)
end

function hud_top_render()
    -- check gamemode enabled state
    if not gGlobalSyncTable.hideAndSeek then
        return
    end

    local seconds = 0
    local text = ''

    if gGlobalSyncTable.roundState == ROUND_STATE_WAIT then
        seconds = 60
        text = 'waiting for players'
    elseif gGlobalSyncTable.roundState == ROUND_STATE_ACTIVE then
        seconds = math.floor(sRoundEndTimeout / 30 - gGlobalSyncTable.displayTimer)
        if seconds < 0 then seconds = 0 end
        text = 'seekers have ' .. seconds .. ' seconds'
    else
        seconds = math.floor(sRoundStartTimeout / 30 - gGlobalSyncTable.displayTimer)
        if seconds < 0 then seconds = 0 end
        text = 'next round in ' .. seconds .. ' seconds'
    end

    local scale = 0.50

    -- get width of screen and text
    local screenWidth = djui_hud_get_screen_width()
    local width = djui_hud_measure_text(text) * scale

    local x = (screenWidth - width) / 2.0
    local y = 0

    local background = 0.0
    if seconds < 60 and gGlobalSyncTable.roundState == ROUND_STATE_ACTIVE then
        background = (math.sin(sFlashingIndex / 10.0) * 0.5 + 0.5) * 1.0
        background = background * background
        background = background * background
    end

    -- render top
    djui_hud_set_color(255 * background, 0, 0, 128);
    djui_hud_render_rect(x - 6, y, width + 12, 16);

    djui_hud_set_color(255, 255, 255, 255);
    djui_hud_print_text(text, x, y, scale);
end

function hud_bottom_render()
    local seconds = math.floor((sDistanceTimeout - sDistanceTimer) / 30)
    if seconds < 0 then seconds = 0 end
    if sDistanceTimer < 1 then return end

    local text = 'Keep moving! (' .. seconds .. ')'
    local scale = 0.50

    -- get width of screen and text
    local screenWidth = djui_hud_get_screen_width()
    local screenHeight = djui_hud_get_screen_height()
    local width = djui_hud_measure_text(text) * scale

    local x = (screenWidth - width) / 2.0
    local y = screenHeight - 16

    local background = (math.sin(sFlashingIndex / 10.0) * 0.5 + 0.5) * 1.0
    background = background * background
    background = background * background

    -- render top
    djui_hud_set_color(255 * background, 0, 0, 128);
    djui_hud_render_rect(x - 6, y, width + 12, 16);

    djui_hud_set_color(255, 255, 255, 255);
    djui_hud_print_text(text, x, y, scale);
end

function hud_center_render()
    if gGlobalSyncTable.displayTimer > 3 then return end

    -- set text
    local text = ''
    if gGlobalSyncTable.roundState == ROUND_STATE_SEEKERS_WIN then
        text = 'Seekers Win!'
    elseif gGlobalSyncTable.roundState == ROUND_STATE_HIDERS_WIN then
        text = 'Hiders Win!'
    elseif gGlobalSyncTable.roundState == ROUND_STATE_ACTIVE then
        text = 'Go!'
    else
        return
    end

    -- set scale
    local scale = 1

    -- get width of screen and text
    local screenWidth = djui_hud_get_screen_width()
    local screenHeight = djui_hud_get_screen_height()
    local width = djui_hud_measure_text(text) * scale
    local height = 32 * scale

    local x = (screenWidth - width) / 2.0
    local y = (screenHeight - height) / 2.0

    -- render
    djui_hud_set_color(0, 0, 0, 128);
    djui_hud_render_rect(x - 6 * scale, y, width + 12 * scale, height);

    djui_hud_set_color(255, 255, 255, 255);
    djui_hud_print_text(text, x, y, scale);
end

function on_hud_render()
    -- render to N64 screen space, with the HUD font
    djui_hud_set_resolution(RESOLUTION_N64)
    djui_hud_set_font(FONT_NORMAL)

    hud_top_render()
    hud_bottom_render()
    hud_center_render()

    sFlashingIndex = sFlashingIndex + 1
end

function on_hide_and_seek_command(msg)
    if not network_is_server() then
        djui_chat_message_create('Only the server can change this setting!')
        return true
    end
    if msg == 'on' then
        djui_chat_message_create('Hide-and-seek mod: enabled')
        gGlobalSyncTable.hideAndSeek = true
        return true
    elseif msg == 'off' then
        djui_chat_message_create('Hide-and-seek mod: disabled')
        gGlobalSyncTable.hideAndSeek = false
        return true
    end
    return false
end

function on_anti_camp_command(msg)
    if not network_is_server() then
        djui_chat_message_create('Only the server can change this setting!')
        return true
    end
    if msg == 'on' then
        djui_chat_message_create('Anti-camping timer: enabled')
        gGlobalSyncTable.campingTimer = true
        return true
    elseif msg == 'off' then
        djui_chat_message_create('Anti-camping timer: disabled')
        gGlobalSyncTable.campingTimer = false
        return true
    end
    return false
end

function on_pause_exit(exitToCastle)
    local s = gPlayerSyncTable[0]
    if not s.seeking then
        s.seeking = true
        network_player_set_description(gNetworkPlayers[0], "seeker", 255, 64, 64, 255)
    end
    return true
end

function allow_pvp_attack(m1, m2)
    local s1 = gPlayerSyncTable[m1.playerIndex]
    local s2 = gPlayerSyncTable[m2.playerIndex]
    if s1.seeking == s2.seeking then
        return false
    end
    return true
end

-----------------------
-- network callbacks --
-----------------------

function on_round_state_changed(tag, oldVal, newVal)
    local rs = gGlobalSyncTable.roundState

    if     rs == ROUND_STATE_WAIT        then
        -- nothing
    elseif rs == ROUND_STATE_ACTIVE      then
        play_character_sound(gMarioStates[0], CHAR_SOUND_HERE_WE_GO)
    elseif rs == ROUND_STATE_SEEKERS_WIN then
        play_sound(SOUND_MENU_CLICK_CHANGE_VIEW, gMarioStates[0].marioObj.header.gfx.cameraToObject)
    elseif rs == ROUND_STATE_HIDERS_WIN  then
        play_sound(SOUND_MENU_CLICK_CHANGE_VIEW, gMarioStates[0].marioObj.header.gfx.cameraToObject)
    elseif rs == ROUND_STATE_UNKNOWN_END then
        -- nothing
    end
end


function on_seeking_changed(tag, oldVal, newVal)
    local m = gMarioStates[tag]
    local np = gNetworkPlayers[tag]

    -- play sound and create popup if became a seeker
    if newVal and not oldVal then
        play_sound(SOUND_OBJ_BOWSER_LAUGH, m.marioObj.header.gfx.cameraToObject)
        playerColor = network_get_player_text_color_string(m.playerIndex)
        djui_popup_create(playerColor .. np.name .. '\\#ffa0a0\\ is now a seeker', 2)
        if gGlobalSyncTable.roundState == ROUND_STATE_ACTIVE then
            sLastSeekerIndex = m.playerIndex
        end
        sRoundTimer = 0
    end

    if newVal then
        network_player_set_description(np, "seeker", 255, 64, 64, 255)
    else
        network_player_set_description(np, "hider", 128, 128, 128, 255)
    end
end

-----------
-- hooks --
-----------

hook_event(HOOK_UPDATE, update)
hook_event(HOOK_MARIO_UPDATE, mario_update)
hook_event(HOOK_BEFORE_PHYS_STEP, mario_before_phys_step)
hook_event(HOOK_ON_PVP_ATTACK, on_pvp_attack)
hook_event(HOOK_ON_PLAYER_CONNECTED, on_player_connected)
hook_event(HOOK_ON_HUD_RENDER, on_hud_render)
hook_event(HOOK_ON_PAUSE_EXIT, on_pause_exit)
hook_event(HOOK_ALLOW_PVP_ATTACK, allow_pvp_attack)

hook_chat_command('hide-and-seek', "[on|off] turn hide-and-seek on or off", on_hide_and_seek_command)
hook_chat_command('anti-camp', "[on|off] turn the anti-camp timer on or off", on_anti_camp_command)

-- call functions when certain sync table values change
hook_on_sync_table_change(gGlobalSyncTable, 'roundState', 0, on_round_state_changed)
for i=0,(MAX_PLAYERS-1) do
    gPlayerSyncTable[i].seeking = true
    hook_on_sync_table_change(gPlayerSyncTable[i], 'seeking', i, on_seeking_changed)
    network_player_set_description(gNetworkPlayers[i], "seeker", 255, 64, 64, 255)
end

