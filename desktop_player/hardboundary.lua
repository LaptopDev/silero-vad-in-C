mp.msg.info("hardboundary: loaded")

-- last-frame-ish boundary (set per file)
local boundary = nil

mp.register_event("file-loaded", function()
    local duration = mp.get_property_number("duration")
    if not duration then
        mp.msg.error("hardboundary: no duration")
        return
    end
    -- stay just before true EOF so mpv never actually reaches it
    boundary = duration - 0.05
    mp.msg.info(string.format("hardboundary: duration=%f boundary=%f", duration, boundary))
end)

mp.register_script_message("hardseek-relative", function(val)
    mp.msg.info("hardseek-relative: called with "..tostring(val))

    local delta = tonumber(val)
    if not delta then
        mp.msg.error("hardseek-relative: invalid number")
        return
    end

    local cur = mp.get_property_number("time-pos")
    if not cur then
        mp.msg.error("hardseek-relative: time-pos not available")
        return
    end

    local target = cur + delta
    mp.msg.info(string.format("hardseek-relative: cur=%f target=%f", cur, target))

    if boundary then
        -- forward movement: respect boundary
        if delta > 0 and target >= boundary then
            target = boundary
            mp.set_property_number("time-pos", target)
            -- hit the wall -> pause at boundary
            mp.set_property_bool("pause", true)
            mp.msg.info("hardseek-relative: hit boundary, pausing")
            return
        end
    end

    -- normal seek (no boundary hit)
    mp.set_property_number("time-pos", target)

    -- always resume after seek (back or forward that didn't hit boundary)
    mp.set_property_bool("pause", false)
end)
