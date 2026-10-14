local sv = root.ScriptVars;

sv.SW_BG1POSX_OFS = 2400;
sv.SW_BG1POSY_OFS = 2401;
sv.SW_BG1SX_OFS = 2402;
sv.SW_BG1SY_OFS = 2403;
sv.SW_BG1SIZE_OFS = 2404;
sv.SW_BG1LX_OFS = 2405;
sv.SW_BG1LY_OFS = 2406;
sv.SW_BG1ROTZ_OFS = 2407;
sv.SW_BG1ALPHA_OFS = 2408;
sv.SW_MASK1COLOR = 4400;
sv.SW_MASK1ALPHA = 4401;
sv.SW_MASK1PRI = 4402;
sv.SW_MASK1POSX = 4403;
sv.SW_MASK1POSY = 4404;
sv.SW_MASK1SIZEX = 4405;
sv.SW_MASK1SIZEY = 4406;
sv.SW_MESMODE0 = 4363;
-- Original MO8 reads the per-page window type from ScrWork byte offset
-- 0x4428 (Game.exe InstMes/MesMain) = index 4362 + page*10, the CCLCC-family
-- layout, NOT common/MO6TW's 4422 (reading 4422 picked up an unrelated
-- variable whose 0x20 bit masqueraded as DontDrawText and hid all text).
sv.SW_MESWIN0TYPE = 4362;

sv.SF_MOVIEPLAY = 1851;
