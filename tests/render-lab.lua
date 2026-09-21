-- Same arrangement in the live engine and in a self-referencing preview.
local t={o="brick",u="brick_u",d="brick_d",l="brick_l",r="brick_r",w="water",["~"]="watersurface"}
function start()
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
r..........~~~~~~~~l
r..........wwwwwwwwl
uuuuuuuuuuuuuuuuuuuu
oooooooooooooooooooo
]])
  Spawn("player",3,12)
  Spawn("chest",6,12.5,"start")
  Spawn("key",8,12.5)
  Spawn("lock",10,11.5)
  Spawn("box",14,12.5)
end
tiles="tiles/castle"
pattern="backgrounds/checker"
dark={0.17,0.19,0.48}
light={0.22,0.24,0.62}
