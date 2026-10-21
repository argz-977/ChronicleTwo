local vm = require("@lib/vm")

local script = {}

function script.setup()
    return 3
end

function script.init()
    action.ProgSet(100)
end

function script.loop()
    action.ProgSet(200)
    vm.wait()
    action.ProgSet(201)
end

-- Program 100 is named init, so this numeric key must never run.
script[100] = function()
    action.ProgSet(-1)
end

return script
