local vm = require("@lib/vm")

local counter = 0

return {
    setup = function()
        return 7
    end,
    [100] = function()
        counter += 1
        action.ProgSet(counter)
    end,
    [200] = function()
        action.ProgSet(1)
        vm.wait()
        action.ProgSet(2)
        vm.wait()
        action.ProgSet(3)
    end,
}
