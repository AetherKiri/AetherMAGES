root.SaveMenu = {
    Type = SaveMenuType.MO8,
    DrawType = DrawComponentType.SystemMenu,
    SaveMenuBackgroundSprite = "SaveMenuBackground",
    EmptyThumbnailSprite = "EmptyThumbnail",
    QuickLoadTextSprite = "QuickLoadText",
    LoadTextSprite = "LoadText",
    SaveTextSprite = "SaveText",
    MenuTitleTextPos = { X = 60, Y = 30 },
    NextButtonSprite = "SaveMenuNextButton",
    NextButtonHighlightedSprite = "SaveMenuNextButtonHighlighted",
    NextButtonPosition = { X = 1586, Y = 972 },
    BackButtonSprite = "SaveMenuBackButton",
    BackButtonHighlightedSprite = "SaveMenuBackButtonHighlighted",
    BackButtonPosition = { X = 30, Y = 972 },
    BackButtonPositionVoice = { X = 808, Y = 972 },
    -- Game.exe 0x42c8c0: column-major, four rows per column.
    EntryStartX = 180,
    EntryXPadding = 804,
    EntryStartY = 156,
    EntryYPadding = 192,
    EntrySprite = "SaveEntry",
    EntryHighlightedSprite = "SaveEntryHighlighted",
    EntryFocusBorderSprite = "SaveEntryFocusBorder",
    EntryLockSprite = "SaveEntryLock",
    ThumbnailOffset = { X = 73, Y = 17 },
    ThumbnailSize = { X = 260, Y = 146 },
    EntryNumberOffset = { X = 18, Y = 72 },
    EntryTitleOffset = { X = 348, Y = 24 },
    EntryPlayTimeOffset = { X = 492, Y = 108 },
    EntrySaveDateOffset = { X = 492, Y = 144 },
    EntryLockOffset = { X = 12, Y = 12 },
    EntryFontSize = 24,
    PageNumberPosition = { X = 960, Y = 996 },
    FadeInDuration = 0.2,
    FadeOutDuration = 0.2
};

root.Sprites["SaveMenuBackground"] = {
    Sheet = "Save",
    Bounds = { X = 0, Y = 0, Width = 1920, Height = 1080 }
};

root.Sprites["EmptyThumbnail"] = {
    Sheet = "Save",
    Bounds = { X = 74, Y = 1106, Width = 260, Height = 146 }
};

root.Sprites["SaveEntry"] = {
    Sheet = "Save",
    Bounds = { X = 0, Y = 1088, Width = 756, Height = 180 }
};
root.Sprites["SaveEntryHighlighted"] = {
    Sheet = "Save",
    Bounds = { X = 0, Y = 1276, Width = 756, Height = 180 }
};
root.Sprites["SaveEntryFocusBorder"] = {
    Sheet = "Save",
    Bounds = { X = 0, Y = 1464, Width = 752, Height = 180 }
};
root.Sprites["SaveEntryLock"] = {
    Sheet = "Save",
    Bounds = { X = 0, Y = 1946, Width = 28, Height = 30 }
};

root.Sprites["QuickLoadText"] = {
    Sheet = "Save",
    Bounds = { X = 0, Y = 1848, Width = 438, Height = 90 }
};
root.Sprites["SaveText"] = {
    Sheet = "Save",
    Bounds = { X = 0, Y = 1652, Width = 438, Height = 90 }
};
root.Sprites["LoadText"] = {
    Sheet = "Save",
    Bounds = { X = 0, Y = 1750, Width = 438, Height = 90 }
};

root.Sprites["SaveMenuBackButton"] = {
    Sheet = "Data",
    Bounds = { X = 0, Y = 722, Width = 304, Height = 80 }
};

root.Sprites["SaveMenuBackButtonHighlighted"] = {
    Sheet = "Data",
    Bounds = { X = 0, Y = 898, Width = 304, Height = 80 }
};

root.Sprites["SaveMenuNextButton"] = {
    Sheet = "Data",
    Bounds = { X = 0, Y = 810, Width = 304, Height = 80 }
};

root.Sprites["SaveMenuNextButtonHighlighted"] = {
    Sheet = "Data",
    Bounds = { X = 0, Y = 634, Width = 304, Height = 80 }
};
