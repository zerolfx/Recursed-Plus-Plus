local t = { o="brick", u="brick_u", d="brick_d", l="brick_l", r="brick_r", ["~"]="watersurface", w="water" }
local function shell()
  ApplyTiles(t,0,0,[[
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
  ApplyTiles(t,12,10,"~~~~~~~\nwwwwwww\nwwwwwww")
  Spawn("player",6,12)
  Spawn("chest",6,12.5,"vault")
  Spawn("chest",15,12.5,"vault")
end
function vault(wet)
  shell()
  if wet then
    ApplyTiles(t,1,1,"~~~~~~~~~~~~~~~~~~\n"..string.rep("wwwwwwwwwwwwwwwwww\n",11))
  end
  Spawn("player",3,12)
  Global("key",4,3.5)
  Global("box",15,3.5)
  Spawn("chest",6,12.5,"vault")
end
tiles="tiles/castle"
pattern="backgrounds/checker"
dark={0.17,0.19,0.48}
light={0.22,0.24,0.62}
