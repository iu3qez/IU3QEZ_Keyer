-- RemoteCW Protocol Dissector for Wireshark
-- Save this file and load it in Wireshark: Analyze -> Reload Lua Plugins
-- Or place it in: ~/.local/lib/wireshark/plugins/ (Linux)

remotecw_proto = Proto("RemoteCW", "Remote CW Keyer Protocol")

-- Protocol fields
local f_command = ProtoField.uint8("remotecw.command", "Command", base.HEX)
local f_blocklen = ProtoField.uint8("remotecw.blocklen", "Block Length Type", base.HEX)
local f_length = ProtoField.uint16("remotecw.length", "Data Length", base.DEC)
local f_data = ProtoField.bytes("remotecw.data", "Data")

-- CW Stream fields
local f_cw_state = ProtoField.bool("remotecw.cw.state", "Key State")
local f_cw_delay = ProtoField.uint8("remotecw.cw.delay", "Delay (ms)", base.DEC)
local f_cw_delay_encoded = ProtoField.uint8("remotecw.cw.delay_encoded", "Delay Encoded (7-bit)", base.DEC)
local f_cw_byte = ProtoField.uint8("remotecw.cw.byte", "CW Event Byte", base.HEX)
local f_cw_description = ProtoField.string("remotecw.cw.description", "Event Description")

remotecw_proto.fields = {f_command, f_blocklen, f_length, f_data, f_cw_state, f_cw_delay, f_cw_delay_encoded, f_cw_byte, f_cw_description}

-- Command names
local cmd_names = {
    [0x00] = "NONE",
    [0x01] = "CONNECT",
    [0x02] = "DISCONNECT",
    [0x03] = "PING",
    [0x04] = "PRINT",
    [0x05] = "TX_INFO",
    [0x06] = "PTT",
    [0x10] = "MORSE",
    [0x11] = "AUDIO",
    [0x14] = "CI-V",
    [0x15] = "SPECTRUM",
    [0x16] = "FREQ_REPORT"
}

-- Block length type names
local blocklen_names = {
    [0x00] = "No Block",
    [0x40] = "Short Block",
    [0x80] = "Long Block",
    [0xC0] = "Reserved"
}

-- Decode 7-bit CW timestamp to milliseconds
function decode_cw_delay(timestamp)
    if timestamp <= 31 then
        return timestamp
    elseif timestamp <= 63 then
        return 32 + 4 * (timestamp - 32)
    else
        return 157 + 16 * (timestamp - 64)
    end
end

-- Dissector function
function remotecw_proto.dissector(buffer, pinfo, tree)
    local length = buffer:len()
    if length == 0 then return end

    pinfo.cols.protocol = remotecw_proto.name

    local subtree = tree:add(remotecw_proto, buffer(), "Remote CW Protocol Data")
    local offset = 0

    while offset < length do
        local cmd_byte = buffer(offset, 1):uint()
        local cmd = bit.band(cmd_byte, 0x3F)
        local blocklen_type = bit.band(cmd_byte, 0xC0)

        local cmd_name = cmd_names[cmd] or string.format("Unknown(0x%02X)", cmd)
        local item_tree = subtree:add(buffer(offset), string.format("%s Command", cmd_name))

        item_tree:add(f_command, buffer(offset, 1)):append_text(" (" .. cmd_name .. ")")
        item_tree:add(f_blocklen, buffer(offset, 1)):append_text(" (" .. (blocklen_names[blocklen_type] or "Unknown") .. ")")
        offset = offset + 1

        -- Parse data length
        local data_len = 0
        if blocklen_type == 0x40 then -- Short block (1-byte length)
            if offset < length then
                data_len = buffer(offset, 1):uint()
                item_tree:add(f_length, buffer(offset, 1))
                offset = offset + 1
            end
        elseif blocklen_type == 0x80 then -- Long block (2-byte length, little-endian)
            if offset + 1 < length then
                data_len = buffer(offset, 2):le_uint()
                item_tree:add(f_length, buffer(offset, 2))
                offset = offset + 2
            end
        end

        -- Parse data based on command type
        if data_len > 0 and offset + data_len <= length then
            local data_buf = buffer(offset, data_len)

            if cmd == 0x10 then -- MORSE command
                -- Decode CW stream
                local cw_tree = item_tree:add(buffer(offset, data_len), string.format("CW Events (%d bytes)", data_len))

                -- Build summary string for info column
                local summary_parts = {}

                for i = 0, data_len - 1 do
                    local cw_byte = buffer(offset + i, 1):uint()
                    local key_state = bit.band(cw_byte, 0x80) ~= 0
                    local timestamp = bit.band(cw_byte, 0x7F)
                    local delay_ms = decode_cw_delay(timestamp)

                    -- Determine encoding range for educational purposes
                    local range_info = ""
                    if timestamp <= 31 then
                        range_info = "1ms steps"
                    elseif timestamp <= 63 then
                        range_info = "4ms steps"
                    else
                        range_info = "16ms steps"
                    end

                    -- Create detailed event description
                    local state_str = key_state and "DOWN" or "UP"
                    local event_desc = string.format("%s after %d ms", state_str, delay_ms)
                    local event_str = string.format("[%d] 0x%02X = %s (timestamp=0x%02X=%d, %s)",
                                                    i, cw_byte, event_desc,
                                                    timestamp, timestamp, range_info)

                    -- Add to summary
                    table.insert(summary_parts, string.format("%s:%dms", state_str, delay_ms))

                    local event_tree = cw_tree:add(buffer(offset + i, 1), event_str)
                    event_tree:add(f_cw_byte, buffer(offset + i, 1)):append_text(string.format(" (binary: %s)",
                        string.format("%08d", tonumber(string.format("%d", cw_byte), 10))))
                    event_tree:add(f_cw_state, key_state):append_text(string.format(" (%s)", state_str))
                    event_tree:add(f_cw_delay_encoded, timestamp):append_text(string.format(" (0x%02X = %d)", timestamp, timestamp))
                    event_tree:add(f_cw_delay, delay_ms):append_text(string.format(" ms (%s)", range_info))

                    -- Add a human-readable description field
                    local full_desc = string.format("Event %d: Key %s after waiting %d ms (byte=0x%02X, bit7=%d, time_code=%d)",
                                                   i, state_str, delay_ms, cw_byte, key_state and 1 or 0, timestamp)
                    event_tree:add(f_cw_description, full_desc)
                end

                -- Add summary to packet info
                pinfo.cols.info = string.format("MORSE: %d events [%s]", data_len, table.concat(summary_parts, ", "))

            elseif cmd == 0x06 then -- PTT command
                local ptt_str = data_buf:string()
                item_tree:add(buffer(offset, data_len), "PTT Command: " .. ptt_str)
                pinfo.cols.info = "PTT: " .. ptt_str:gsub("%s+", " ")

            elseif cmd == 0x04 then -- PRINT command
                local print_str = data_buf:string()
                item_tree:add(buffer(offset, data_len), "Message: " .. print_str)
                pinfo.cols.info = "PRINT: " .. print_str

            elseif cmd == 0x01 then -- CONNECT command
                item_tree:add(f_data, data_buf)
                pinfo.cols.info = "CONNECT/LOGIN"

            else
                item_tree:add(f_data, data_buf)
                pinfo.cols.info = cmd_name
            end

            offset = offset + data_len
        else
            pinfo.cols.info = cmd_name
        end
    end
end

-- Register the dissector for TCP port 7355
local tcp_port = DissectorTable.get("tcp.port")
tcp_port:add(7355, remotecw_proto)

print("RemoteCW dissector loaded successfully!")
