-- MO8 (PC) keyboard bindings for the common (CCLCC-layout) PADtoKBcustom
-- table: PAD1A -> KBcustom[4], PAD1B -> [5], directions -> [0..3].
-- Physical defaults follow the MAGES PC family convention
-- (Enter/Z confirm, Esc/X cancel, arrows move, Ctrl skip).
local SC = KeyboardScanCode;

root.Input.KBcustom = {
    [0] = {SC._UP},
    [1] = {SC._DOWN},
    [2] = {SC._LEFT},
    [3] = {SC._RIGHT},
    [4] = {SC._RETURN, SC._Z},   -- confirm
    [5] = {SC._ESCAPE, SC._X},   -- cancel
    [6] = {SC._C},               -- X
    [7] = {SC._V},               -- Y
    [8] = {SC._LCTRL, SC._Q},    -- L1 (skip)
    [9] = {SC._W},               -- L2
    [10] = {},
    [11] = {SC._E},              -- R1
    [12] = {},
    [13] = {},
    [14] = {SC._F1},             -- START (system menu)
    [15] = {SC._F2},             -- SELECT
    [16] = {},
    [17] = {},
    [18] = {},
    [19] = {},
    [20] = {},
    [21] = {},
    [22] = {},
    [23] = {},
};
