tiles = "tiles/castle"
pattern = "backgrounds/checker"
local map = { o="brick", u="brick_u", d="brick_d", l="brick_l", r="brick_r", ["~"]="watersurface", w="water" }
local function shell()
  ApplyTiles(map,0,0,[[
dddddddddddddddddddd
r..................l
r..................l
r..................l
r..................l
r..................l
r..................l
r..................l
r..................l
r..................l
r..................l
r..................l
r..................l
uuuuuuuuuuuuuuuuuuuu
oooooooooooooooooooo
]])
end
function start()
  shell()
  Spawn("player",5,12)
  Spawn("chest",5,12.5,"kept")
end
function kept()
  shell()
  Spawn("player",3,12)
  Spawn("yield",4.5,12)
  Spawn("box",8,12.5)
  Global("key",11,12.5)
  Spawn("chest",15,12.5,"inner")
  ApplyTiles(map,17,11,"~~\nww")
end
function inner()
  shell()
  Spawn("player",3,12)
  Spawn("chest",9,12.5,"kept")
end
function glitch()
  shell()
  Spawn("player",3,12)
  Spawn("crystal",12,12)
end
