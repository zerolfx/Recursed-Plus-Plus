local wip = { o="brick", u="brick_u", d="brick_d", l="brick_l", r="brick_r", ["~"]="watersurface", ["-"]="glitchledge", w="water" }
local function shell()
  ApplyTiles(wip,0,0,[[
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
  Spawn("chest",7,12.5,"keyroom")
  Spawn("chest",12,12.5,"pool")
  Spawn("box",15,12.5)
end
function keyroom(wet)
  shell()
  ApplyTiles(wip,10,9,"uuuuu")
  Spawn("player",3,12)
  Spawn("key",12,8.5)
  Spawn("lock",16,11.5)
  Spawn("chest",7,12.5,"pool")
end
function pool(wet)
  shell()
  ApplyTiles(wip,9,11,"~~~~~~~~~~\nwwwwwwwwww")
  Spawn("player",3,12)
  Spawn("chest",7,12.5,"keyroom")
end
tiles="tiles/castle"
pattern="backgrounds/checker"
dark={0.17,0.19,0.48}
light={0.22,0.24,0.62}
