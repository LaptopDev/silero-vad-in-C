mp.msg.info("hardboundary+scrollseek: loaded")

-- shared EOF boundary
local boundary = nil

------------------------------------------------------------
-- boundary setup
------------------------------------------------------------
mp.register_event("file-loaded", function()
    local duration = mp.get_property_number("duration")
    if not duration then
        mp.msg.error("hardboundary: no duration")
        return
    end
    boundary = duration - 0.05
    mp.msg.info(string.format("hardboundary: duration=%f boundary=%f", duration, boundary))
end)

------------------------------------------------------------
-- fixed-step seeking (your old hardseek-relative)
------------------------------------------------------------
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

    -- boundary clamp
    if boundary and delta > 0 and target >= boundary then
        target = boundary
        mp.set_property_number("time-pos", target)
        mp.set_property_bool("pause", true)
        mp.msg.info("hardseek-relative: hit boundary, pausing")
        return
    end

    -- normal movement
    mp.set_property_number("time-pos", target)
    mp.set_property_bool("pause", false)
end)

------------------------------------------------------------
-- dynamic scroll wheel scrubbing (scrollseek)
------------------------------------------------------------

local last_time = 0
local velocity  = 0
local base_step = 1.0
local max_step  = 20.0
local decay     = 0.65

local function clamp(v, lo, hi)
    if v < lo then return lo end
    if v > hi then return hi end
    return v
end

mp.register_script_message("scrollseek", function(dir)
    local now = mp.get_time()
    local dt = now - last_time
    last_time = now

    -- velocity: very tight scroll → higher step
    if dt < 0.08 then
        velocity = velocity + (0.08 - dt) * 40
    else
        velocity = velocity * decay
    end

    local step = base_step + velocity
    step = clamp(step, base_step, max_step)

    if dir == "down" then
        step = -step
    end

    local cur = mp.get_property_number("time-pos")
    if not cur then return end

    local target = cur + step

    -- boundary clamp
    if boundary and step > 0 and target >= boundary then
        target = boundary
        mp.set_property_number("time-pos", target)
        mp.set_property_bool("pause", true)
        return
    end

    mp.set_property_number("time-pos", target)
    mp.set_property_bool("pause", false)
end)
