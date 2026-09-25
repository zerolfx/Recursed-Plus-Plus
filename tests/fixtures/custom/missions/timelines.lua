-- Authored test data: colours kept per timeline, as the levels with paradox rooms keep them.
tiles = "tiles/test"
local mapping = { x = "floor" }
function start()
  ApplyTiles(mapping, 0, 14, "xxxxx")
  Spawn("player", 1, 13)
end
function reject()
  Spawn("player", 4, 13)
  Global("chest", 6, 13, "start")
end
dark = { start = {0.1, 0.2, 0.3}, reject = {0.4, 0.5, 0.6} }
light = { start = {0.2, 0.3, 0.4} }
light.reject = {0.7, 0.8, 0.9}
dark.threadless = {1, 0, 0}
