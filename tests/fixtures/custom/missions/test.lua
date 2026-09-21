-- Authored test data; no game resources are needed by CI.
tiles = "tiles/test"
local mapping = { x = "floor", w = "water" }
function start(wet)
  ApplyTiles(mapping, 2, 3, "xxx")
  if wet then ApplyTiles(mapping, 2, 2, "www") end
  Spawn("player", 1, 2)
  Spawn("chest", 5, 2, "start")
  Global("key", 6, 2)
end
function forbidden()
  Spawn("box", 1, 1)
  io.open("outside", "w")
end
function endless()
  while true do end
end
function overflow()
  for i = 1, 2049 do Spawn("box", 1, 1) end
end
