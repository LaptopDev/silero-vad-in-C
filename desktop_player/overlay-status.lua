local status_file = "/tmp/player_status.txt"

local function format_time(sec)
    if not sec then sec = 0 end
    local total_ms = math.floor(sec * 1000)
    local ms = total_ms % 1000
    local s = math.floor(total_ms / 1000)
    local sec_ = s % 60
    local min_ = math.floor(s / 60) % 60
    local hr_ = math.floor(s / 3600)
    return string.format("%02d:%02d:%02d:%03d", hr_, min_, sec_, ms)
end

local function write_status()
    local pos = mp.get_property_number("time-pos")
    local dur = mp.get_property_number("duration")

    local f = io.open(status_file, "w")
    if not f then return end

    f:write(string.format("%s / %s\n",
        format_time(pos),
        format_time(dur)
    ))

    f:close()
end

mp.observe_property("time-pos", "number", write_status)
mp.observe_property("duration", "number", write_status)
mp.register_event("file-loaded", write_status)

write_status()
