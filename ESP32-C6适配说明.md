# EvilAppleJuice-ESP32 移植到 ESP32-C6 适配说明

本文档记录了把本项目（原本只支持 ESP32-C3 / S3 / 原版 ESP32）移植到 **ESP32-C6** 时的所有修改点、踩过的坑和解决办法，方便日后查阅、学习和维护。

> 时间：2026-08-24　目标芯片：ESP32-C6 (QFN40)　Flash：8MB　框架：Arduino（arduino-esp32 3.3.11）

---

## 目录

1. [背景与核心结论](#1-背景与核心结论)
2. [文件修改清单](#2-文件修改清单)
3. [关键问题与解决方案](#3-关键问题与解决方案)
4. [构建与烧录命令](#4-构建与烧录命令)
5. [注意事项与维护提示](#5-注意事项与维护提示)

---

## 1. 背景与核心结论

本项目是一个通过 BLE 广播伪造 Apple「连续互通（Continuity）」配对通知、向附近 iPhone 弹窗的恶作剧/测试程序。它使用 PlatformIO + Arduino 框架，依赖 ESP32 的 BLE 库。

核心结论只有一句话：

> **ESP32-C6 只支持 arduino-esp32 3.x（且默认蓝牙栈是 NimBLE 而非 Bluedroid），而官方 PlatformIO `espressif32` 平台默认仍停留在 arduino-esp32 2.0.17（Bluedroid），因此必须换用社区 `pioarduino` 平台，并把代码里的 BLE API 适配到 NimBLE。**

围绕这句话，共改了 3 个文件（其中 1 个是新增），解决了 4 个主要问题。

---

## 2. 文件修改清单

| 文件 | 类型 | 作用 |
|------|------|------|
| `platformio.ini` | 修改 | 新增 ESP32-C6 环境，使用 pioarduino 平台；挂载 `fix_map_path.py` 钩子 |
| `src/main.cpp` | 修改 | 用条件编译同时兼容 NimBLE（C6）和 Bluedroid（旧板）两套 BLE API |
| `fix_map_path.py` | 新增 | 把链接器 map 文件重定向到 ASCII 路径，规避中文路径问题 |

---

### 2.1 `platformio.ini`

在文件末尾新增了两个 ESP32-C6 环境：

```ini
[env:esp32-c6-devkitc-1]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
board = esp32-c6-devkitc-1
framework = arduino
monitor_speed = 115200
extra_scripts = post:fix_map_path.py

[env:esp32-c6-devkitm-1]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
board = esp32-c6-devkitm-1
framework = arduino
monitor_speed = 115200
extra_scripts = post:fix_map_path.py
```

要点：
- **`platform` 用 URL 而非 `espressif32`**：指向 `pioarduino` 平台的 release zip（内含 arduino-esp32 3.3.11 + ESP-IDF 5.5.5 预编译库）。
- **`board`**：`esp32-c6-devkitc-1`（8MB 参考板）或 `esp32-c6-devkitm-1`（4MB 小板），按你手上的板子选。
- **`extra_scripts = post:fix_map_path.py`**：挂载项目内的钩子脚本，解决中文路径下的链接失败（见 3.3）。

---

### 2.2 `src/main.cpp`

用 `#if defined(CONFIG_NIMBLE_ENABLED)` 做条件编译，共 3 处。这样旧板（C3/S3/原版 ESP32，仍走 Bluedroid）和 C6（走 NimBLE）都能编译。

#### 改动 1：setup() 里的地址初始化

```cpp
// 修改前
  esp_bd_addr_t null_addr = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
  pAdvertising->setDeviceAddress(null_addr, BLE_ADDR_TYPE_RANDOM);

// 修改后
#if defined(CONFIG_NIMBLE_ENABLED)
  // NimBLE: use a random static address, randomized per advertisement in loop()
  BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
#else
  esp_bd_addr_t null_addr = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
  pAdvertising->setDeviceAddress(null_addr, BLE_ADDR_TYPE_RANDOM);
#endif
```

#### 改动 2：loop() 里选择广播类型

```cpp
// 修改前
  int adv_type_choice = random(3);
  if (adv_type_choice == 0){
    pAdvertising->setAdvertisementType(ADV_TYPE_IND);
  } else if (adv_type_choice == 1){
    pAdvertising->setAdvertisementType(ADV_TYPE_SCAN_IND);
  } else {
    pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  }

// 修改后
  int adv_type_choice = random(3);
#if defined(CONFIG_NIMBLE_ENABLED)
  // NimBLE: map the three Bluedroid PDU types onto connectable/non-connectable modes
  // (scannable-undirected is approximated by undirected-connectable here)
  if (adv_type_choice == 0){
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_UND);   // ADV_TYPE_IND
  } else if (adv_type_choice == 1){
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_UND);   // ADV_TYPE_SCAN_IND
  } else {
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);   // ADV_TYPE_NONCONN_IND
  }
#else
  if (adv_type_choice == 0){
    pAdvertising->setAdvertisementType(ADV_TYPE_IND);
  } else if (adv_type_choice == 1){
    pAdvertising->setAdvertisementType(ADV_TYPE_SCAN_IND);
  } else {
    pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  }
#endif
```

#### 改动 3：loop() 里设置随机 MAC 地址

```cpp
// 修改前
  pAdvertising->setDeviceAddress(dummy_addr, BLE_ADDR_TYPE_RANDOM);
  pAdvertising->setAdvertisementData(oAdvertisementData);

// 修改后
#if defined(CONFIG_NIMBLE_ENABLED)
  BLEDevice::setOwnAddr(dummy_addr);
#else
  pAdvertising->setDeviceAddress(dummy_addr, BLE_ADDR_TYPE_RANDOM);
#endif
  pAdvertising->setAdvertisementData(oAdvertisementData);
```

#### 改动 4：随机 MAC 生成时修正地址类型标记位

```cpp
// 修改前
  for (int i = 0; i < 6; i++){
    dummy_addr[i] = random(256);
    if (i == 0){
      dummy_addr[i] |= 0xF0;   // 错误：设置的是最低有效字节(LSB)的高4位
    }
  }

// 修改后
  for (int i = 0; i < 6; i++){
    dummy_addr[i] = random(256);
  }
  // BLE 地址是小端序，index 5 是最高有效字节；地址类型标记在其最高两位。
  // NimBLE 要求这两位为 11(静态随机) 或 00(不可解析私有)，否则返回 BLE_HS_EINVAL。
  dummy_addr[5] |= 0xC0;
```

> 说明：`esp_ble_tx_power_set()`（设置 BLE 发射功率）**无需改动**——它定义在 `esp_bt.h` 的蓝牙控制器 VHCI API 里，与主机栈是 Bluedroid 还是 NimBLE 无关，两种栈都能用。

---

### 2.3 `fix_map_path.py`（新增，项目根目录）

```python
"""PlatformIO post-script: redirect the linker map file to an ASCII-only path."""

Import("env")

import os
import tempfile

_progname = env.subst("$PROGNAME") or "firmware"
_map_path = os.path.join(tempfile.gettempdir(), _progname + ".map")


def _redirect(flags):
    fixed = []
    for flag in flags:
        if flag.startswith("-Wl,-Map"):
            fixed.append('-Wl,-Map="%s"' % _map_path)
        else:
            fixed.append(flag)
    return fixed


_linkflags = env.get("LINKFLAGS")
if _linkflags:
    env.Replace(LINKFLAGS=_redirect(list(_linkflags)))
```

要点：
- 脚本里 `Import("env")` 拿到 SCons 构建环境，`env.Replace(LINKFLAGS=...)` 覆盖链接参数。
- 只替换以 `-Wl,-Map` 开头的参数，其余链接参数原样保留。
- 作为 `post:` 钩子运行（见 3.3），因此一定在框架脚本追加 `-Map` 之后执行，能成功覆盖。

---

## 3. 关键问题与解决方案

### 3.1 问题一：官方平台不支持 ESP32-C6 的 Arduino 框架

**现象**：用 `platform = espressif32` + `framework = arduino` 构建 C6 时报错：

```
Error: This board doesn't support arduino framework!
```

**根因**（两层）：
1. 官方 `espressif32` 平台（6.9.0）默认的 `framework-arduinoespressif32` 版本是 `3.20017.0`，即 **arduino-esp32 2.0.17**。而 arduino-esp32 从 **3.0.0** 才开始支持 ESP32-C6，2.x 根本不支持 C6。
2. 官方 C6 板卡定义 `esp32-c6-devkitc-1.json` 里 `"frameworks"` 只写了 `["espidf"]`，没写 `arduino`，所以 PlatformIO 直接拒绝。

**解决**：换用社区 **pioarduino** 平台（见 2.1）。它的板卡定义同时包含 `arduino` 与 `espidf`，并且打包了 arduino-esp32 3.3.11 + ESP-IDF 5.5.5 的预编译静态库（`framework-arduinoespressif32-libs`——这是 3.x 相对 2.x 在结构上的最大变化：3.x 把 ESP-IDF 预编译库拆成了独立包）。

### 3.2 问题二：Python 依赖安装失败

**现象**：第一次构建时，平台安装到一半报：

```
Error: Failed to install Python dependencies (exit code: 2)
Error: Failed to install Python dependencies into penv
```

**根因**：pioarduino 平台的构建脚本 `builder/penv_setup.py` 会用 `uv`（一个 Python 包管理器）往 PlatformIO 的虚拟环境 `penv` 里装一批依赖，但 `uv` 没走你本机 pip 配置的镜像（上交大 `mirror.sjtu.edu.cn`），导致下载/解析失败。

**解决**：绕过 `uv`，直接用 `pip`（会自动走镜像）把依赖装进 `penv`：

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m pip install `
  "pioarduino>=6.1.19" "littlefs-python>=0.16.0" "fatfs-ng>=0.1.14" `
  "pyyaml>=6.0.2" "rich-click>=1.8.6" "zopfli>=0.2.2" "intelhex>=2.3.0" `
  "rich>=14.0.0" "cryptography>=45.0.3" "certifi>=2025.8.3" "ecdsa>=0.19.1" `
  "bitstring>=4.3.1" "reedsolo>=1.5.3,<1.8" "esp-idf-size>=2.0.0" `
  "esp-coredump>=1.14.0" "pyelftools>=0.32"
```

> 依赖装进 `penv` 后，平台脚本会检测到它们已存在，就不再重复安装。**此操作只需做一次**（除非重装/更新 pioarduino 平台）。

### 3.3 问题三：中文路径导致链接失败

**现象**：代码能编译，但链接阶段报：

```
ld.exe: cannot open map file C:/Users/honey/Desktop/单片机/EvilAppleJuice-ESP32/.pio/build/.../firmware.map: Invalid argument
```

**根因**：本项目的路径里含中文「单片机」。新版 RISC-V 工具链（riscv32-esp-elf gcc **14.2.0**）的 `ld.exe` 无法在含非 ASCII 字符的路径下打开文件。map 文件参数来自 `framework-arduinoespressif32-libs/esp32c6/pioarduino-build.py` 第 217 行：

```python
'-Wl,-Map="%s"' % join("${BUILD_DIR}", "${PROGNAME}.map")
```

**解决**：新增 `fix_map_path.py`，作为 `post:` extra_scripts 在框架脚本**之后**运行，把 `-Wl,-Map` 重定向到系统临时目录（纯 ASCII），其余产物仍留在项目 `.pio/build/` 下。

> 备选方案（曾用过，后放弃）：把 `[platformio]` 的 `build_dir` 改成 ASCII 路径。虽然也能绕开问题，但会导致构建产物跑到项目外，不便管理，所以换成了现在的项目内脚本方案。

### 3.4 问题四：BLE 栈从 Bluedroid 变为 NimBLE

**现象**：换平台后编译 `main.cpp` 报错：

```
'class BLEAdvertising' has no member named 'setDeviceAddress'
'BLE_ADDR_TYPE_RANDOM' was not declared in this scope; did you mean 'BLE_ADDR_RANDOM'?
'ADV_TYPE_IND' / 'ADV_TYPE_SCAN_IND' / 'ADV_TYPE_NONCONN_IND' was not declared
```

**根因**：arduino-esp32 3.x 在 ESP32-C6 上默认启用 **NimBLE** 主机栈（`CONFIG_BT_NIMBLE_ENABLED = 1`），而不是旧版的 **Bluedroid**（`CONFIG_BLUEDROID_ENABLED` 未定义）。两套栈的 BLE 库 API 不同：

| 旧（Bluedroid） | 新（NimBLE） |
|-----------------|--------------|
| `pAdvertising->setDeviceAddress(addr, BLE_ADDR_TYPE_RANDOM)` | `BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)` + `BLEDevice::setOwnAddr(addr)` |
| `ADV_TYPE_IND`（可连接非定向） | `BLE_GAP_CONN_MODE_UND` (=2) |
| `ADV_TYPE_SCAN_IND`（可扫描） | 无直接对应，近似用 `BLE_GAP_CONN_MODE_UND` |
| `ADV_TYPE_NONCONN_IND`（不可连接） | `BLE_GAP_CONN_MODE_NON` (=0) |
| `esp_ble_tx_power_set()` | **无需改**（控制器 API，两栈通用） |

**解决**：见 2.2，用 `#if defined(CONFIG_NIMBLE_ENABLED)` 条件编译，一套代码同时兼容新旧板。

### 3.5 问题五：setOwnAddr() 报 rc=3（BLE_HS_EINVAL）

**现象**：运行中每几次广播就会打印：

```
[E][BLEDevice.cpp:1634] setOwnAddr(): Failed to set address, rc=3
```

**根因**：`rc=3` 是 NimBLE 错误码 `BLE_HS_EINVAL`。`ble_hs_id_set_rnd()` 会校验地址，要求它是合法的「静态随机地址」（MSB 最高两位 = `11`）或「不可解析私有地址」（MSB 最高两位 = `00`）。原代码生成随机 MAC 时写的是 `dummy_addr[0] |= 0xF0`——设置的是**最低有效字节（LSB）**的高 4 位；而地址类型标记位于**最高有效字节（MSB，即 `dummy_addr[5]`，因为地址是小端序）**的最高两位。由于 MSB 是纯随机值，其最高两位约有一半概率落在 `01`（可解析私有 RPA）/`10`（保留值），NimBLE 判定地址非法并返回 `BLE_HS_EINVAL`，于是表现为「某些」广播才报错。

**解决**：见 2.2 改动 4，把 `dummy_addr[0] |= 0xF0` 改为 `dummy_addr[5] |= 0xC0`（强制 MSB 最高两位 = `11`，即静态随机地址）。

---

## 4. 构建与烧录命令

> 注意：本机 `pio` 不在 PATH 里（是通过 VS Code PlatformIO 扩展安装的），命令行请用完整路径，或先把它加入 PATH。

```powershell
# 1. 构建（产物输出到项目 .pio\build\esp32-c6-devkitc-1\）
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32-c6-devkitc-1

# 2. 烧录（COM5 换成你的实际串口号）
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32-c6-devkitc-1 -t upload --upload-port COM5

# 3. 串口监视
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -e esp32-c6-devkitc-1
```

**构建产物**（都在项目目录 `.pio/build/esp32-c6-devkitc-1/` 下）：

| 文件 | 说明 |
|------|------|
| `firmware.bin` | 应用固件 |
| `firmware.factory.bin` | 合并镜像（bootloader + 分区表 + 应用），可单独用 esptool 一条命令烧录 |
| `bootloader.bin` / `partitions.bin` | 引导程序 / 分区表 |

资源占用（参考）：Flash 约 55.9%（732KB / 1310KB），RAM 约 5.9%（19KB / 320KB）。

---

## 5. 注意事项与维护提示

1. **板子选择**：默认按 `esp32-c6-devkitc-1`（8MB Flash）构建；若你用的是 4MB 的 DevKitM-1，改环境名为 `esp32-c6-devkitm-1`。

2. **`fix_map_path.py` 必须保留**：删掉它会导致中文路径下链接再次失败。它位于项目内、不依赖框架文件，pioarduino 平台升级后依然有效。

3. **pioarduino 平台重新安装/升级时**：可能需要重新执行 3.2 的 `pip install` 手动装 Python 依赖；如果仍报依赖错误，照 3.2 重跑一遍即可。

4. **旧板兼容性**：`main.cpp` 的改动全部用 `#if defined(CONFIG_NIMBLE_ENABLED)` 包裹，因此 C3/S3/原版 ESP32 等旧环境（仍用官方 espressif32 + Bluedroid）不受影响，`default_envs = airm2m_core_esp32c3` 保持不变。

5. **运行行为**：上电即开始向附近 iPhone 广播伪造的 Apple 配对通知；按下 **BOOT 键（GPIO9）** 可切换广播开/关。串口日志里会打印 `Starting ESP32 BLE`、`Sending Advertisement...` 等。

6. **合法合规**：本项目仅用于对你拥有/授权的设备做测试，请勿用于骚扰他人。

---

*文档由 AI 助手整理，覆盖了本次移植的全部改动与排查过程。*
