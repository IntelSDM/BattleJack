# BattleJack
Exploiting A Vulnerability In BattlEye's Launching Chain To Hijack Kernel Protections Onto Any Usermode Application.

<img width="500" height="281" alt="2026-06-24 18-09-37_1" src="https://github.com/user-attachments/assets/af923e80-340e-4164-bdaf-2a3d934e3d91" />


## Instructions:
* Download [BEService_arksa](https://github.com/IntelSDM/BattleJack/releases) (from releases) into your C:\Program Files (x86)\Common Files\BattlEye <br>
* Modify TargetGamePath in Main.c <br>
* Compile with MSVC x64(Not LLVM/GCC) <br>
* Run BattleJack <br>
* Enjoy BattlEye Protections <br>
BEDaisy.sys is also avaliable incase it gets patched, or you want to ensure stability.

## What is it?
BattleJack allows you to take the launcher commands for Battleye, send them to the service, and forge the loading process to poison the communication pipeline. Allowing you to secure your own process with BattlEye. <br>
When you start BE's game launcher it will actually start the service, communicate the target game to the service, invoke itself again with the parameter "6". <br>
The first launcher stays open, the secondary launcher stays open, verifies the cert of the game, opens the game and tells the service once it has done that. The second launcher then closes. <br>
While the original one stays open and waits for the game to close. Its a pretty simple process. The issue is that the driver does verify the parent chain, this is due to a project by [hypercall called FakeEye](https://github.com/Hypercall/FakeEye). 
They patched hypercall's mechanisms by checking the boot owner of the secondary launcher's uniqueprocess within the driver. <br>
Since they check the process chain, this shouldn't be possible should it? In classic BattlEye fashion they fucked that up. <br>
For the sake of securing our own application; we don't need the driver execution to proceed past its initilization of the protected process, its ability to communicate with BEService and BEClient aren't needed. <br>
In the state that we leave the application in, BEService has told BEDaisy that the game is open, to protect it, and to protect the secondary launcher. We keep the secondary launcher alive to maintain this state, as the next step in the launcher process is to close the secondary launcher, BEDaisy is waiting for it to close. <br>
OBRegistercallbacks is initialized on the target process, and LSASS.exe/CSRSS.exe are hooked to prevent handles being generated to the game. You are secure against usermode handles. <br>

## Use Case
Besides the clear anti debugging usecase; the intended usecase was to mitigate Vanguard's usermode VGM Module. <br>
This module scans other usermode applications. As it would scan through my process, it would spam pagefaults, upsetting my anti debugging. <br>
The simple solution is to hijack an anti cheat driver to prevent analysis. <br>
This can also be used on heavy anti tamper systems and usermode anti cheats that scan usermode applications memory. <br>
It served me well over the years; keeping intrusive anti cheats from setting off my loader protections.

## Credit
[Hypercall](https://github.com/Hypercall) | [His research into BELauncher powered this project](https://hypercall.net/posts/FakeEye/).
