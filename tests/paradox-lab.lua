-- Paradox Lab. The chest in the first room is global, so another copy of that room hands it over:
-- go into it, take the chest in the attic back to a second first room, pick the chest up there and
-- carry it out into the attic. Holding it, the attic's flame leads home. Put it down and the way
-- back is gone: the flame leads into reject, which is built alone on a new stack and so has no
-- flame of its own, like the first room.
local t = { o="brick", u="brick_u", d="brick_d", l="brick_l", r="brick_r" }
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
  Spawn("player",3,12)
  Global("chest",8,12.5,"attic")
end
function attic()
  shell()
  Spawn("player",3,12)
  Spawn("chest",12,12.5,"start")
end
function reject()
  shell()
  Spawn("player",10,12)
  Spawn("box",14,12.5)
end
tiles="tiles/castle"
pattern="backgrounds/checker"
dark={start={0.17,0.19,0.48},reject={0.02,0.02,0.03}}
light={start={0.22,0.24,0.62},reject={0.10,0.04,0.12}}
