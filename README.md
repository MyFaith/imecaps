### IMECaps
使用CapsLock切换输入法，需要**管理员权限运行**或勾选**以管理员身份运行此程序**

本程序使用AI编写，其中风险请自行评估。

### 编译
```bash
g++ IMECaps.cpp IMECaps.res -o IMECaps.exe -mwindows -luser32 -lshell32 -ladvapi32 -lgdi32
```
