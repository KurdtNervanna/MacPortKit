"""Classic Mac OS A-line trap names, for the disassembly and audit tools.

The tables are a high-confidence subset collected while porting two games. Unknown
words print as "?"; look them up in MPW traps.h (github.com/ksherlock/mpw macos/traps.h)
or Inside Macintosh and ADD them here so the next person benefits.

name(word) -> e.g. 'GetResource' (or '?' if unknown).
"""
# --- OS traps: index = word & 0xFF (flag bits 8-11 ignored) ---
OS = {
 0x00:"Open",0x01:"Close",0x02:"Read",0x03:"Write",0x04:"Control",0x05:"Status",
 0x06:"KillIO",0x07:"GetVolInfo",0x08:"Create",0x09:"Delete",0x0A:"OpenRF",
 0x0B:"Rename",0x0C:"GetFileInfo",0x0D:"SetFileInfo",0x0E:"UnmountVol",
 0x0F:"MountVol",0x10:"Allocate",0x11:"GetEOF",0x12:"SetEOF",0x13:"FlushVol",
 0x14:"GetVol",0x15:"SetVol",0x16:"FInitQueue",0x17:"Eject",0x18:"GetFPos",
 0x19:"InitZone",0x1C:"FreeMem",0x1D:"MaxMem",0x1E:"NewPtr",0x1F:"DisposPtr",
 0x20:"SetPtrSize",0x21:"GetPtrSize",0x22:"NewHandle",0x23:"DisposHandle",
 0x24:"SetHandleSize",0x25:"GetHandleSize",0x26:"HandleZone",0x27:"ReallocHandle",
 0x28:"RecoverHandle",0x29:"HLock",0x2A:"HUnlock",0x2B:"EmptyHandle",
 0x2C:"InitApplZone",0x2E:"BlockMove",0x2F:"PostEvent",0x44:"SetFPos",
}
# --- Toolbox traps: full word 0xA8xx-0xABxx (high confidence subset) ---
TB = {
 0xA850:"InitCursor",0xA851:"SetCursor",0xA852:"HideCursor",0xA853:"ShowCursor",
 0xA856:"ObscureCursor",0xA858:"BitAnd",0xA860:"WaitNextEvent",0xA861:"Random",
 0xA865:"GetPixel",0xA866:"StuffHex",0xA86E:"InitGraf",0xA872:"GrafDevice",
 0xA873:"SetPort",0xA874:"GetPort",0xA875:"SetPortBits",0xA876:"PortSize",
 0xA877:"MovePortTo",0xA878:"SetOrigin",0xA879:"SetClip",0xA87A:"GetClip",
 0xA87B:"ClipRect",0xA87C:"BackPat",0xA883:"DrawChar",0xA884:"DrawString",
 0xA885:"DrawText",0xA887:"TextFont",0xA888:"TextFace",0xA889:"TextMode",
 0xA88A:"TextSize",0xA88B:"GetFontInfo",0xA88C:"StringWidth",0xA88D:"CharWidth",
 0xA88E:"TextWidth",0xA890:"InitFonts",0xA891:"LineTo",0xA892:"Line",
 0xA893:"MoveTo",0xA894:"Move",0xA896:"HidePen",0xA897:"ShowPen",0xA89A:"GetPen",
 0xA89B:"PenSize",0xA89C:"PenMode",0xA89D:"PenPat",0xA89E:"PenNormal",
 0xA8A1:"FrameRect",0xA8A2:"PaintRect",0xA8A3:"EraseRect",0xA8A4:"InverRect",
 0xA8A5:"FillRect",0xA8A6:"EqualRect",0xA8A7:"SetRect",0xA8A8:"OffsetRect",
 0xA8A9:"InsetRect",0xA8AA:"SectRect",0xA8AB:"UnionRect",0xA8AD:"PtInRect",
 0xA8AE:"EmptyRect",0xA8B0:"FrameRoundRect",0xA8B1:"PaintRoundRect",
 0xA8B7:"FrameOval",0xA8B8:"PaintOval",0xA8B9:"EraseOval",0xA8D8:"NewRgn",
 0xA8D9:"DisposeRgn",0xA8DA:"OpenRgn",0xA8DB:"CloseRgn",0xA8DD:"SetEmptyRgn",
 0xA8DE:"SetRectRgn",0xA8DF:"RectRgn",0xA8E0:"OffsetRgn",0xA8E4:"SectRgn",
 0xA8E5:"UnionRgn",0xA8E6:"DiffRgn",0xA8E8:"PtInRgn",0xA8EB:"ScalePt",
 0xA8EC:"CopyBits",0xA8EF:"ScrollRect",0xA8F6:"DrawPicture",0xA905:"DragGrayRgn",
 0xA910:"GetWMgrPort",0xA912:"InitWindows",0xA913:"NewWindow",0xA914:"DisposeWindow",
 0xA915:"ShowWindow",0xA916:"HideWindow",0xA917:"GetWRefCon",0xA918:"SetWRefCon",
 0xA919:"GetWTitle",0xA91A:"SetWTitle",0xA91B:"MoveWindow",0xA91D:"SizeWindow",
 0xA91E:"TrackGoAway",0xA91F:"SelectWindow",0xA920:"BringToFront",0xA921:"SendBehind",
 0xA922:"BeginUpdate",0xA923:"EndUpdate",0xA924:"FrontWindow",0xA925:"DragWindow",
 0xA927:"InvalRgn",0xA928:"InvalRect",0xA929:"ValidRgn",0xA92A:"ValidRect",
 0xA92B:"GrowWindow",0xA92C:"FindWindow",0xA92D:"CloseWindow",0xA930:"InitMenus",
 0xA931:"NewMenu",0xA932:"DisposeMenu",0xA933:"AppendMenu",0xA935:"InsertMenu",
 0xA937:"DrawMenuBar",0xA939:"EnableItem",0xA93A:"DisableItem",0xA93B:"GetMenuBar",
 0xA93C:"SetMenuBar",0xA93D:"MenuSelect",0xA93E:"MenuKey",0xA945:"CheckItem",
 0xA946:"GetMenuItemText",0xA947:"SetMenuItemText",0xA949:"GetMenuHandle",
 0xA94D:"AppendResMenu",0xA954:"NewControl",0xA955:"DisposeControl",
 0xA95D:"HiliteControl",0xA960:"GetCtlValue",0xA963:"SetCtlValue",0xA966:"TestControl",
 0xA968:"TrackControl",0xA969:"DrawControls",0xA96C:"FindControl",0xA970:"GetNextEvent",
 0xA971:"EventAvail",0xA972:"GetMouse",0xA973:"StillDown",0xA974:"Button",
 0xA975:"TickCount",0xA976:"GetKeys",0xA977:"WaitMouseUp",0xA97B:"InitDialogs",
 0xA97C:"GetNewDialog",0xA97D:"NewDialog",0xA97F:"IsDialogEvent",0xA980:"DialogSelect",
 0xA981:"DrawDialog",0xA982:"CloseDialog",0xA983:"DisposDialog",0xA985:"Alert",
 0xA986:"StopAlert",0xA987:"NoteAlert",0xA988:"CautionAlert",0xA98B:"ParamText",
 0xA98D:"GetDItem",0xA98E:"SetDItem",0xA98F:"SetIText",0xA990:"GetIText",
 0xA991:"ModalDialog",0xA992:"DetachResource",0xA994:"CurResFile",0xA997:"OpenResFile",
 0xA999:"UpdateResFile",0xA99A:"CloseResFile",0xA99E:"CountTypes",0xA99F:"GetIndType",
 0xA9A0:"GetResource",0xA9A1:"GetNamedResource",0xA9A2:"LoadResource",
 0xA9A3:"ReleaseResource",0xA9A4:"HomeResFile",0xA9A5:"SizeRsrc",0xA9A6:"GetResAttrs",
 0xA9A7:"SetResAttrs",0xA9A8:"GetResInfo",0xA9AA:"ChangedResource",0xA9AB:"AddResource",
 0xA9AD:"RmveResource",0xA9AF:"ResError",0xA9B0:"WriteResource",0xA9B3:"SystemClick",
 0xA9B4:"SystemTask",0xA9B6:"OpenDeskAcc",0xA9B7:"CloseDeskAcc",0xA9B9:"GetCursor",
 0xA9BB:"GetIcon",0xA9BC:"GetPicture",0xA9BD:"GetNewWindow",0xA9BE:"GetNewControl",
 0xA9C6:"Secs2Date",0xA9C7:"Date2Secs",0xA9C8:"SysBeep",0xA9CC:"TEInit",
 0xA9CD:"TEDispose",0xA9CF:"TESetText",0xA9D0:"TECalText",0xA9D2:"TENew",
 0xA9D3:"TEUpdate",0xA9D4:"TEClick",0xA9D8:"TEActivate",0xA9D9:"TEDeactivate",
 0xA9DA:"TEIdle",0xA9DC:"TEKey",0xA9F0:"LoadSeg",0xA9F1:"UnloadSeg",0xA9F2:"Launch",
 0xA9F4:"ExitToShell",0xAA14:"RGBForeColor",0xAA15:"RGBBackColor",0xAA91:"NewPalette",
 0xAB1A:"DisposeGWorld",0xAB1B:"SetGWorld",0xAB1C:"GetGWorld",0xAB1D:"NewGWorld",
 0xAB10:"LockPixels",0xAB11:"UnlockPixels",
 0xA805:"SndPlay",0xA807:"SndNewChannel",0xA801:"SndDisposeChannel",
 0xA803:"SndDoCommand",0xA804:"SndDoImmediate",0xA806:"SndControl",
}


EXTRA = {0xA862:'ForeColor',0xA863:'BackColor',0xA869:'FixRatio',0xA86F:'OpenPort',0xA870:'LocalToGlobal',0xA871:'GlobalToLocal',
 0xA87D:'ClosePort',0xA898:'GetPenState',0xA899:'SetPenState',0xA8BF:'PaintArc',0xA8E1:'InsetRgn',0xA8E2:'EmptyRgn',0xA8E9:'RectInRgn',
 0xA8FD:'PrGlue',0xA8FE:'InitFonts',0xA906:'NewString',0xA909:'CalcVis',0xA936:'DeleteMenu',0xA938:'HiliteMenu',0xA94C:'FlashMenuBar',
 0xA950:'CountMItems',0xA959:'MoveControl',0xA95C:'SizeControl',0xA961:'GetMinCtl',0xA962:'GetMaxCtl',0xA964:'SetMinCtl',0xA965:'SetMaxCtl',
 0xA97E:'SelIText',0xA998:'UseResFile',0xA9B1:'CreateResFile',0xA9B8:'GetPattern',0xA9BA:'GetString',0xA9BF:'GetMenu',0xA9E1:'HandToHand',
 0xA9E7:'Pack0',0xA9E8:'Pack1',0xA9E9:'Pack2',0xA9EA:'Pack3',0xA9EB:'Pack4',0xA9EC:'Pack5',0xA9ED:'Pack6',0xA9EE:'Pack7',0xA8BE:'FrameArc',0xAA3E:'ProtectEntry',0xAA3F:'SetEntries',0xAA45:'NewCWindow',0xAA46:'GetNewCWindow',0xAA48:'GetCWMgrPort'}
OSX = {0x1A:'GetZone',0x1B:'SetZone',0x2D:'SetApplLimit',0x30:'OSEventAvail',0x31:'GetOSEvent',0x33:'VInstall',0x34:'VRemove',
 0x39:'ReadDateTime',0x3A:'SetDateTime',0x3B:'Delay',0x3C:'CmpString',0x47:'SetTrapAddress',0x4C:'CompactMem',0x4D:'PurgeMem',
 0x57:'SetApplBase',0x58:'InsTime',0x59:'RmvTime',0x5A:'PrimeTime',0x62:'PurgeSpace',0x67:'HSetRBit',0x68:'HClrRBit',
 0x69:'HGetState',0x6A:'HSetState',0xAD:'Gestalt',0x32:'FlushEvents',0x36:'MoreMasters',0x49:'HPurge',0x4A:'HNoPurge',0x4B:'SetGrowZone',0x55:'StripAddress',0x60:'HFSDispatch',
 0x63:'MaxApplZone',0x64:'MoveHHi',0x90:'SysEnvirons',0x46:'GetTrapAddress'}

def name(w):
    if w < 0xA800:
        return OSX.get(w & 0xFF) or OS.get(w & 0xFF, '?')
    return EXTRA.get(w & 0xFBFF) or TB.get(w, TB.get(w & 0xFBFF, '?'))


if __name__ == '__main__':
    import sys
    for a in sys.argv[1:]:
        w = int(a, 16)
        print(f'{w:04X} {name(w)}')
