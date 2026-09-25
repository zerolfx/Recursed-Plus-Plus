-- Cauldron Lab. A cauldron takes the player into the timeline it names: back to the room that
-- timeline was left in, or, the first time, to its first room, built fresh and dry and alone, in
-- the timeline's own colours. The first room's cauldron leads into two, and two's leads home.
-- lab holds a global cauldron into two, so in two's own lab it leads nowhere else.
-- For the paradox: go into lab, take its cauldron into two, go into two's lab, which gets the
-- cauldron back, carry it out into two and put it down. Two's cauldron home then leads into
-- threadless, because the lab left through that cauldron has lost it.
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
  Spawn("player",3,12)
  Spawn("cauldron",8,12.5,"two")
  Spawn("chest",14,12.5,"lab")
end
function lab()
  shell()
  Spawn("player",3,12)
  Global("cauldron",10,12.5,"two")
  Spawn("box",15,12.5)
end
function two()
  shell()
  Spawn("player",3,12)
  Spawn("chest",7,12.5,"lab")
  Spawn("cauldron",12,12.5,"start")
  Spawn("key",15,12.5)
  ApplyTiles(map,17,11,"~~\nww")
end
function threadless()
  shell()
  Spawn("player",10,12)
  Spawn("crystal",14,12)
end
tiles="tiles/castle"
pattern="backgrounds/checker"
dark={start={0.17,0.19,0.48},two={0.36,0.14,0.06},threadless={0.03,0.03,0.03}}
light={start={0.22,0.24,0.62},two={0.62,0.26,0.10},threadless={0.12,0.10,0.14}}
