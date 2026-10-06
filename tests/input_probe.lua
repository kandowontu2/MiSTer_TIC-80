-- title: MiSTer input probe
-- script: lua
-- saveid: mister-input-probe-v2
-- Run with a fresh save and tools/input_probe.c on the selected TIC-80 core.
local frames=0
function BOOT()
 local x,y=mouse()
 pmem(10,x); pmem(11,y)
 pmem(12,pmem(12)+1)
end
function TIC()
 frames=frames+1
 local x,y,left,middle,right,sx,sy=mouse()
 if key(1) then pmem(0,1) end -- A
 if key(63) then pmem(1,pmem(1)+1) end -- Ctrl (either side)
 if keyp(78) then pmem(2,pmem(2)+1) end -- F12 reaches the cartridge
 if keyp(1) then pmem(3,pmem(3)+1) end
 pmem(4,x); pmem(5,y)
 local buttons=(left and 1 or 0)+(middle and 2 or 0)+(right and 4 or 0)
 pmem(6,pmem(6)|buttons)
 pmem(7,buttons)
 pmem(8,(pmem(8)+sy)&0xffffffff)
 pmem(9,key() and 1 or 0)
 pmem(13,pmem(13)|btn())
 cls(0)
 print('MiSTer keyboard / mouse',10,12,12)
 print('A seen '..pmem(0)..'  Ctrl ticks '..pmem(1),10,35,12)
 print('F12 presses '..pmem(2)..'  A presses '..pmem(3),10,48,12)
 print('Mouse '..x..','..y..'  wheel '..sy,10,66,12)
 print('Buttons seen '..pmem(6)..'  held '..buttons,10,79,12)
 print('Win+F12: menu',10,105,11)
 rect(x,y,3,3,15)
end
