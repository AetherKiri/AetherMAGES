-- MO8 PC (Steam 简中) layout: flat .mpk archives in the game root, movies via
-- CLS manifests. IDs are globally unique per pack family (bg1: 0-482,
-- bg2: 483-1104), so bg1+bg2 share the "bg" mountpoint. The ep packs
-- (bg_ep/voice_ep) are not part of the base mount table and stay unmounted.
root.Vfs = {
    Mounts = {
        ["script"] = {root.BasePaths.RootGamedataDir .. "/script.mpk"},
        ["system"] = {root.BasePaths.RootGamedataDir .. "/system.mpk"},
        ["bgm"] = {root.BasePaths.RootGamedataDir .. "/bgm.mpk"},
        ["se"] = {root.BasePaths.RootGamedataDir .. "/se.mpk"},
        ["sysse"] = {root.BasePaths.RootGamedataDir .. "/sysse.mpk"},
        ["voice"] = {root.BasePaths.RootGamedataDir .. "/voice.mpk"},
        ["bg"] = {
            root.BasePaths.RootGamedataDir .. "/bg1.mpk",
            root.BasePaths.RootGamedataDir .. "/bg2.mpk",
        },
        ["chara"] = {root.BasePaths.RootGamedataDir .. "/chara.mpk"},
        ["mask"] = {root.BasePaths.RootGamedataDir .. "/mask.mpk"},
        ["movie"] = {root.BasePaths.RootGamedataDir .. "/movie1080.cls"},
        ["face"] = {root.BasePaths.RootGamedataDir .. "/face.mpk"},
    }
};
