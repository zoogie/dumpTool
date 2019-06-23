Directions:
Just boot the dumpTool.nds app and press A.
It will dump a nand.bin with nocash footer to your dsi at 
DT010203040A0B0C0D/nand.bin
The folder will be next to wherever you put dumpTool.nds. And obviously, your foldername will have different characters. It's console-unique.

General Info:
This tool should create a nand.bin identical to fwTool.nds 2.0. The same holds true for its nand.bin.sha1 file as well.
If this isn't the case in your neck of the woods, please make an issue :)
(note that identical nands can only occur if both app's output are compared in the same hbmenu session; booting to dsi home menu in between will undoubtably change NAND contents)

Features:
- Completely open source.
- Lots of checks, including verifying the nocash footer will decrypt the outputed NAND. Low battery and insufficient SD space are also checked.
- Project has a permissive license, and more importantly, its dev is permissive. I don't care what's done with this post-release as long as credit is given.
- Simple operation. Just press A and watch it go.
- You can cancel the dump in progress. The incomplete nand will be cleaned up.
- A little bit faster than fwTool. Should complete in about 7 minutes.

Thanks:
Martin Korth (nocash) - Documenting the consoleID dumping method on GBAtek.
Tinivi - Borrowed his 3ds mode (arm9) aes function from https://github.com/TiniVi/AHPCFW/
WulfyStylez - Loosely followed his method for nocash footer verification in twlTool:
https://gbatemp.net/threads/release-twltool-dsi-downgrading-save-injection-etc-multitool.393488/ (attachment has source code)
neimod - Taddy dsi aes functions (dsi.c/h) https://github.com/neimod/dsi/tree/master/taddy (this inlcludes some polarssl files (aes.c/h) and that is GPL v2, info in aes.h)


